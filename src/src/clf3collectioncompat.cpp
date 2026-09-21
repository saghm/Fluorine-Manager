#include "clf3collectioncompat.h"
#include "clf3processenvironment.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QtConcurrent>
#include <QtEndian>
#include <stdexcept>

namespace
{
using Token = std::shared_ptr<std::atomic_bool>;
// Acquisition/runtime metadata from CLF3 0.2.6's game registry, commit
// 801d85e189290698a54776fd7c60da2bad85e62b, src/collection/games.rs.
// The released capabilities omit these fields. Never add games to the engine's
// advertised list; newer hosted engines supply their own registry metadata.
const auto releaseProfiles =
  QJsonDocument::fromJson(
    R"profiles({"skyrimspecialedition":{"executable":"SkyrimSE.exe","masterlist_url":"https://raw.githubusercontent.com/loot/skyrimse/e3c591ba9c041f23f407a0a0f87f72cc6325aa43/masterlist.yaml","masterlist_sha256":"95caf8492923b77386fc725150d38c635a581a16bf0e546c3a44eec85afbe484","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Skyrim.ini","SkyrimPrefs.ini","SkyrimCustom.ini"],"timestamp_order":false},"fallout4":{"executable":"Fallout4.exe","masterlist_url":"https://raw.githubusercontent.com/loot/fallout4/22dcffee55f148f8b41981c8119cd6ed36869f56/masterlist.yaml","masterlist_sha256":"b018c8c92de4fe0c4cb19476ee41a7cc0b37d40e1922e3810b0cc33526d65c06","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Fallout4.ini","Fallout4Prefs.ini","Fallout4Custom.ini"],"timestamp_order":false},"newvegas":{"executable":"FalloutNV.exe","masterlist_url":"https://raw.githubusercontent.com/loot/falloutnv/79b2bb6db4ce560e8ecd80f66b375618d3e33405/masterlist.yaml","masterlist_sha256":"ec2d9f340f1ad308aef4fa8e4190408a9ddf0a2148131945fdd95ffd38064ad4","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Fallout.ini","FalloutPrefs.ini","FalloutCustom.ini"],"timestamp_order":true},"fallout3":{"executable":"Fallout3.exe","masterlist_url":"https://raw.githubusercontent.com/loot/fallout3/76e62d5479eedf24975895fe2019000c6df679c3/masterlist.yaml","masterlist_sha256":"4947d03d6e5dbe1e933d597eee467e7554fc5f744a207736c3964df12feb1f29","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Fallout.ini","FalloutPrefs.ini","FalloutCustom.ini"],"timestamp_order":true},"oblivion":{"executable":"Oblivion.exe","masterlist_url":"https://raw.githubusercontent.com/loot/oblivion/5e80ff2298d6dc433021bdc0e6be3ca044566d1c/masterlist.yaml","masterlist_sha256":"643e36cb730cabbe78849ebfa2a484ad60550b3a826bd3c76de400a3bb56c214","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Oblivion.ini"],"timestamp_order":true},"skyrim":{"executable":"TESV.exe","masterlist_url":"https://raw.githubusercontent.com/loot/skyrim/2d65f851150a155998765446e8c7dc607eb4bd73/masterlist.yaml","masterlist_sha256":"6d1fed6da0f598e94d9b415e2e455987b1db43beab95d25f47d4c63adbbfa6e9","snapshot_files":["modlist.txt","plugins.txt","loadorder.txt","settings.ini","Skyrim.ini","SkyrimPrefs.ini","SkyrimCustom.ini"],"timestamp_order":false}})profiles")
    .object();
void
require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void
check(const Token& token)
{
  require(!token->load(), "Cancelled");
}
QString
resolve(const QString& root, const QString& relative)
{
  require(QDir::isAbsolutePath(root) && !QFileInfo(root).isSymLink(), "Invalid collection root");
  require(!relative.isEmpty() && !QDir::isAbsolutePath(relative) && !relative.contains('\\'),
          "Invalid collection path");
  QString path = root;
  for (const auto& part : relative.split('/'))
  {
    require(!part.isEmpty() && part != "." && part != "..", "Invalid collection path");
    QString match;
    for (const auto& name : QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                                                 QDir::NoDotAndDotDot))
    {
      if (name.compare(part, Qt::CaseInsensitive) == 0)
      {
        require(match.isEmpty(), "Case collision in collection files");
        match = name;
      }
    }
    require(!match.isEmpty(), "A required collection file is missing");
    path += '/' + match;
    require(!QFileInfo(path).isSymLink(), "A collection file is a symbolic link");
  }
  return path;
}
QByteArray
read(const QString& path, qint64 limit)
{
  QFile file(path);
  require(!QFileInfo(path).isSymLink() && QFileInfo(path).isFile() &&
            file.open(QIODevice::ReadOnly) && file.size() <= limit,
          "A collection file is missing, invalid or oversized");
  const auto bytes = file.read(limit + 1);
  require(bytes.size() <= limit && file.error() == QFileDevice::NoError,
          "Cannot read collection file");
  return bytes;
}
QJsonObject
object(const QString& path, qint64 limit = 32 * 1024 * 1024)
{
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(read(path, limit), &error);
  require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid collection job JSON");
  return doc.object();
}
QString
digest(const QString& path, QCryptographicHash::Algorithm algorithm, const Token& token)
{
  QFile file(path);
  require(!QFileInfo(path).isSymLink() && QFileInfo(path).isFile() &&
            file.open(QIODevice::ReadOnly),
          "Cannot verify collection file");
  QCryptographicHash hash(algorithm);
  while (!file.atEnd())
  {
    check(token);
    const auto bytes = file.read(128 * 1024);
    require(file.error() == QFileDevice::NoError, "Cannot read collection file");
    hash.addData(bytes);
  }
  return QString::fromLatin1(hash.result().toHex());
}
QJsonObject
manifest(const QString& package, const QString& engine, const Token& token)
{
  if (QFileInfo(package).isDir())
    return object(resolve(package, "collection.json"), 16 * 1024 * 1024);
  const auto extractor = QFileInfo(engine).absolutePath() + "/7zz";
  require(QFileInfo(extractor).isExecutable(), "The CLF3 release is missing its archive reader");
  QProcess process;
  process.setProcessEnvironment(clf3EngineEnvironment());
  process.setStandardErrorFile(QProcess::nullDevice());
  process.start(extractor, { "x", "-so", "-spd", "-ssc-", package, "collection.json" });
  require(process.waitForStarted(5000), "Cannot read the collection archive");
  QByteArray bytes;
  QElapsedTimer time;
  time.start();
  while (process.state() != QProcess::NotRunning)
  {
    process.waitForReadyRead(50);
    bytes += process.readAllStandardOutput();
    if (token->load() || bytes.size() > 16 * 1024 * 1024 || time.elapsed() > 60000)
    {
      process.kill();
      process.waitForFinished(5000);
      throw std::runtime_error("Collection archive read cancelled or exceeded its limit");
    }
  }
  bytes += process.readAllStandardOutput();
  require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
            bytes.size() <= 16 * 1024 * 1024,
          "Cannot read the full collection package");
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(bytes, &error);
  require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid collection manifest");
  return doc.object();
}
QString
runtime(const QString& game, const QJsonObject& support)
{
  const auto bytes = read(resolve(game, support.value("executable").toString()), 128 * 1024 * 1024);
  // Same fixed-version resource used by CLF3's executable_version().
  const auto offset = bytes.indexOf(QByteArray::fromHex("bd04effe00000100"));
  if (offset < 0)
    return {};
  require(offset + 16 <= bytes.size(), "Truncated game runtime version");
  const auto ms = qFromLittleEndian<quint32>(bytes.constData() + offset + 8);
  const auto ls = qFromLittleEndian<quint32>(bytes.constData() + offset + 12);
  return QString("%1.%2.%3.%4").arg(ms >> 16).arg(ms & 65535).arg(ls >> 16).arg(ls & 65535);
}
} // namespace

Clf3CollectionCompat::Clf3CollectionCompat(QObject* parent)
  : QObject(parent)
  , m_cancelled(std::make_shared<std::atomic_bool>(false))
{
}
Clf3CollectionCompat::~Clf3CollectionCompat()
{
  cancel();
}
bool
Clf3CollectionCompat::supports(const QJsonObject& caps)
{
  return caps.value("protocol_version").toInt() == 1 &&
         caps.value("plan_schema_version").toInt() == 1 &&
         caps.value("engine_version") == "0.2.6" && caps.value("installation_available").toBool() &&
         caps.value("standalone_worker") == "collection_local_worker_v1";
}
QJsonObject
Clf3CollectionCompat::gameSupport(const QJsonObject& caps, const QJsonObject& game)
{
  if (!supports(caps))
    return game;
  auto result = releaseProfiles.value(game.value("domain").toString()).toObject();
  for (auto i = game.begin(); i != game.end(); ++i)
    result.insert(i.key(), i.value());
  return result;
}
void
Clf3CollectionCompat::cancel()
{
  m_cancelled->store(true);
}
void
Clf3CollectionCompat::run(std::function<QJsonObject(Token)> work,
                          std::function<void(QJsonObject)> complete)
{
  cancel();
  const auto token = m_cancelled = std::make_shared<std::atomic_bool>(false);
  auto* watcher = new QFutureWatcher<QJsonObject>(this);
  connect(watcher,
          &QFutureWatcher<QJsonObject>::finished,
          this,
          [this, watcher, token, complete]
          {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (token->load())
              return;
            if (result.contains("error"))
              emit failed(result.value("error").toString());
            else
              complete(result);
          });
  watcher->setFuture(QtConcurrent::run(
    [work, token]
    {
      try
      {
        return work(token);
      }
      catch (const std::exception& error)
      {
        return QJsonObject{ { "error", QString::fromUtf8(error.what()) } };
      }
    }));
}
void
Clf3CollectionCompat::prepare(const QString& package,
                              const QString& game,
                              const QJsonObject& support,
                              const QString& engine)
{
  run(
    [=](Token token)
    {
      const auto version = runtime(game, support);
      check(token);
      const auto mods = manifest(package, engine, token).value("mods").toArray();
      QJsonObject sources;
      for (qsizetype i = 0; i < mods.size(); ++i)
      {
        const auto source = mods[i].toObject().value("source").toObject();
        if (source.value("type") == "direct")
          sources.insert(QString::number(i), source.value("url"));
      }
      return QJsonObject{ { "runtime", version }, { "sources", sources } };
    },
    [this](QJsonObject result)
    { emit prepared(result.value("runtime").toString(), result.value("sources").toObject()); });
}

void
Clf3CollectionCompat::verifyPublication(const QJsonObject& request, const QJsonObject& support)
{
  run(
    [=](Token token)
    {
      const auto output = request.value("output").toString();
      const auto plan = object(request.value("plan").toString());
      const auto journal =
        object(resolve(output, ".collection/installation.json"), 512 * 1024 * 1024);
      const auto marker = object(resolve(output, ".collection/gui-job.json"), 4096);
      const auto reportPath = resolve(output, ".collection/report.json");
      const auto report = object(reportPath, 1024 * 1024);
      require(!request.value("job_identity").toString().isEmpty() &&
                marker.value("job_identity") == request.value("job_identity") &&
                marker.value("plan_sha256").toString().size() == 64 &&
                marker.value("plan_sha256") == journal.value("plan_sha256") &&
                journal.value("journal_schema").toInt() == 1 &&
                journal.value("status") == "published" &&
                journal.value("plan").toObject() == plan &&
                journal.value("package_sha256") == plan.value("package_sha256") &&
                report.value("package_sha256") == plan.value("package_sha256") &&
                report.value("output").toString() == output &&
                report.value("installed_members").toInt(-1) ==
                  plan.value("installation_order").toArray().size(),
              "The published destination does not match this exact job and reviewed plan");
      resolve(output, "ModOrganizer.ini");
      resolve(output, "Stock Game/" + support.value("executable").toString());
      const auto members = journal.value("members").toObject();
      qint64 verified = 0;
      for (auto it = members.begin(); it != members.end(); ++it)
      {
        check(token);
        const auto member = it.value().toObject();
        const auto relative = member.value("directory").toString();
        require(relative.startsWith("mods/") && relative.split('/').size() == 2,
                "Invalid published member directory");
        const auto directory = resolve(output, relative);
        QSet<QString> expected{ "meta.ini" };
        for (const auto& value : member.value("files").toArray())
        {
          const auto file = value.toObject();
          const auto path = file.value("staged_path").toString() +
                            (file.value("excluded").toBool() ? ".mohidden" : "");
          require(!expected.contains(path.toLower()), "Duplicate published file");
          expected.insert(path.toLower());
          const auto actual = resolve(directory, path);
          require(QFileInfo(actual).size() == file.value("size").toInteger(-1) &&
                    digest(actual, QCryptographicHash::Sha256, token) ==
                      file.value("sha256").toString(),
                  "A published mod file changed or is missing");
          ++verified;
        }
        QDirIterator files(directory,
                           QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
        while (files.hasNext())
        {
          check(token);
          files.next();
          const auto info = files.fileInfo();
          require(!info.isSymLink() && (info.isFile() || info.isDir()),
                  "Published member contains a link or special file");
          if (info.isFile())
            require(expected.remove(QDir(directory).relativeFilePath(info.filePath()).toLower()),
                    "Unexpected published member file");
        }
        require(expected.isEmpty(), "A published member is missing files");
      }
      require(verified == report.value("verified_mod_files").toInteger(-1),
              "Published file count changed");
      for (const auto& value : support.value("snapshot_files").toArray())
      {
        check(token);
        const auto name = value.toString();
        if (QFileInfo::exists(output + "/.collection/profile-snapshot/" + name))
        {
          require(read(resolve(output, ".collection/profile-snapshot/" + name), 64 * 1024 * 1024) ==
                    read(resolve(output, "profiles/Default/" + name), 64 * 1024 * 1024),
                  "The published profile changed since installation");
        }
      }
      if (support.value("timestamp_order").toBool())
      {
        const auto timestamps =
          object(resolve(output, ".collection/plugin-timestamps.json"), 4 * 1024 * 1024);
        const auto lines = QString::fromUtf8(read(resolve(output, "profiles/Default/loadorder.txt"),
                                                  4 * 1024 * 1024))
                             .split('\n');
        QHash<QString, qint64> expected;
        for (auto line : lines)
        {
          if (line.endsWith('\r'))
            line.chop(1);
          if (!line.isEmpty() && !line.startsWith('#'))
            expected.insert(line.toLower(), 1577836800 + expected.size() * 60);
        }
        require(timestamps.size() == expected.size(), "Plugin timestamp coverage changed");
        for (auto it = timestamps.begin(); it != timestamps.end(); ++it)
        {
          check(token);
          const auto name = QFileInfo(it.key()).fileName().toLower();
          require(expected.contains(name) && expected.take(name) == it.value().toInteger(-1) &&
                    QFileInfo(resolve(output, it.key())).lastModified().toSecsSinceEpoch() ==
                      it.value().toInteger(-1),
                  "Published plugin timestamps changed");
        }
      }
      QHash<QString, QString> roots;
      for (const auto& id : journal.value("asset_order").toArray())
      {
        require(members.contains(id.toString()), "Published asset order changed");
        for (const auto& value : members.value(id.toString()).toObject().value("files").toArray())
        {
          const auto file = value.toObject();
          if (file.value("deployment_root") == "game" && !file.value("excluded").toBool())
          {
            const auto path = file.value("staged_path").toString();
            require(path.startsWith("Root/"), "Invalid published root payload");
            roots.insert(path.mid(5).toLower(), file.value("sha256").toString());
          }
        }
      }
      for (auto it = roots.begin(); it != roots.end(); ++it)
        require(digest(resolve(output, "Stock Game/" + it.key()),
                       QCryptographicHash::Sha256,
                       token) == it.value(),
                "Published game-root payload changed");
      const auto archives = object(resolve(output, ".collection/artifacts.json"), 16 * 1024 * 1024);
      for (const auto& value : plan.value("artifacts").toArray())
      {
        const auto artifact = value.toObject();
        if (artifact.value("source_type") == "bundle")
          continue;
        const auto path = archives.value(artifact.value("id").toString()).toString();
        require(path.startsWith(output + '/'), "A retained archive is outside this installation");
        const auto file = resolve(output, path.mid(output.size() + 1));
        require((!artifact.value("expected_size").isDouble() ||
                 QFileInfo(file).size() == artifact.value("expected_size").toInteger()) &&
                  digest(file, QCryptographicHash::Md5, token) ==
                    artifact.value("expected_md5").toString(),
                "A retained archive differs from the pinned file");
      }
      check(token);
      return QJsonObject{ { "report", reportPath } };
    },
    [this](QJsonObject result) { emit verified(result.value("report").toString()); });
}
