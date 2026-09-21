#include "../src/clf3collectiondialog.h"
#include "../src/clf3installertabs.h"
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QtEndian>
#include <gtest/gtest.h>

namespace
{
bool
spin(const std::function<bool()>& predicate, int timeout = 15000)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout)
  {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }
  return predicate();
}
void
write(const QString& path, const QByteArray& data)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(data), data.size());
}
QByteArray
json(const QJsonObject& value)
{
  return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
QByteArray
md5(const QByteArray& bytes)
{
  return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
}
QByteArray
artwork(const QSize& size = QSize(480, 270))
{
  QImage image(size, QImage::Format_RGB32);
  image.fill(QColor(45, 92, 170));
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}
class Reply : public QNetworkReply
{
public:
  Reply(QNetworkRequest request, QByteArray bytes, int status, QString redirect, QObject* parent)
    : QNetworkReply(parent)
    , m_bytes(std::move(bytes))
  {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    if (!redirect.isEmpty())
      setAttribute(QNetworkRequest::RedirectionTargetAttribute, QUrl(redirect));
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0,
                       this,
                       [this]
                       {
                         if (!isFinished())
                         {
                           emit readyRead();
                           setFinished(true);
                           emit finished();
                         }
                       });
  }
  void abort() override
  {
    if (isFinished())
      return;
    setError(OperationCanceledError, "secret-in-network-error");
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override
  {
    return m_bytes.size() - m_offset + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char* data, qint64 max) override
  {
    const auto count = qMin(max, qint64(m_bytes.size() - m_offset));
    if (!count)
      return -1;
    memcpy(data, m_bytes.constData() + m_offset, count);
    m_offset += count;
    return count;
  }

private:
  QByteArray m_bytes;
  qsizetype m_offset{ 0 };
};
class Network : public QNetworkAccessManager
{
public:
  struct Response
  {
    QByteArray bytes;
    int status{ 200 };
    QString redirect;
  };
  QList<QNetworkRequest> requests;
  QList<QByteArray> bodies;
  QList<Response> responses;

protected:
  QNetworkReply* createRequest(Operation,
                               const QNetworkRequest& request,
                               QIODevice* outgoing) override
  {
    requests << request;
    bodies << (outgoing ? outgoing->readAll() : QByteArray{});
    const auto response = responses.isEmpty() ? Response{ "{}", 500, {} } : responses.takeFirst();
    return new Reply(request, response.bytes, response.status, response.redirect, this);
  }
};
QJsonObject
artifact(const QByteArray& bytes)
{
  return { { "id", "exact" },
           { "source_type", "nexus" },
           { "domain", "skyrimspecialedition" },
           { "mod_id", 123 },
           { "file_id", 456 },
           { "expected_md5", QString::fromLatin1(md5(bytes)) },
           { "expected_size", bytes.size() } };
}
}

TEST(CollectionHost, PinsRevisionAndRejectsCredentialBearingSources)
{
  const QString base = "https://www.nexusmods.com/games/skyrimspecialedition/collections/test";
  EXPECT_EQ(Clf3CollectionHost::locator(base + "/revisions/12").value("revision").toInt(), 12);
  for (const auto& source :
       { base + "?token=secret",
         base + "#secret",
         base + "/revisions/0",
         QString("https://user:secret@www.nexusmods.com/games/skyrim/collections/test") })
    EXPECT_TRUE(Clf3CollectionHost::locator(source).isEmpty());
  EXPECT_FALSE(Clf3CollectionHost::apiPathAllowed("https://evil.test/v2/graphql"));
  EXPECT_FALSE(
    Clf3CollectionHost::apiPathAllowed("/v2/collections/1/revisions/2/download_link?key=secret"));
  EXPECT_FALSE(
    Clf3CollectionHost::archiveUrlAllowed(QUrl("https://user:secret@example.test/file")));
  const auto body =
    Clf3CollectionHost::searchBody("fallout3", "quoted \"text\"", 2, "updatedAt", true);
  const auto vars = body.value("variables").toObject();
  EXPECT_EQ(vars.value("offset").toInt(), 48);
  EXPECT_EQ(vars.value("filter")
              .toObject()
              .value("gameDomain")
              .toArray()[0]
              .toObject()
              .value("value")
              .toString(),
            "fallout3");
}

TEST(CollectionHost, CredentialsAndSignedUrlsRemainInAcquisitionHost)
{
  QTemporaryDir root;
  Network api, downloads;
  const QByteArray bytes = "exact pinned archive";
  api.responses << Network::Response{
    R"([{"URI":"https://cdn.example.test/archive?token=signed-secret"}])"
  };
  downloads.responses << Network::Response{ bytes };
  Clf3CollectionHost host(
    [&](const QUrl& url, const QByteArray& body)
    {
      QNetworkRequest request(url);
      request.setRawHeader("APIKEY", "account-secret");
      return body.isEmpty() ? api.get(request) : api.post(request, body);
    },
    nullptr,
    &downloads);
  QSignalSpy errors(&host, &Clf3CollectionHost::failed);
  QString path;
  host.artifact(artifact(bytes), {}, root.path(), {}, [&](QString value) { path = value; });
  ASSERT_TRUE(spin([&] { return !path.isEmpty() || !errors.empty(); }));
  ASSERT_TRUE(errors.empty());
  ASSERT_EQ(api.requests.size(), 1);
  ASSERT_EQ(downloads.requests.size(), 1);
  EXPECT_EQ(api.requests[0].url().path(),
            "/v1/games/skyrimspecialedition/mods/123/files/456/download_link.json");
  EXPECT_EQ(api.requests[0].rawHeader("APIKEY"), "account-secret");
  EXPECT_FALSE(downloads.requests[0].hasRawHeader("APIKEY"));
  EXPECT_FALSE(downloads.requests[0].hasRawHeader("Authorization"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), bytes);
  EXPECT_FALSE(path.contains("secret"));
  host.artifact(artifact(bytes), {}, root.path(), {}, [&](QString value) { path = value; });
  QTest::qWait(100);
  EXPECT_EQ(api.requests.size(), 1); // verified cache resumes without account I/O
}

TEST(CollectionHost, WrongHashAndCrossOriginMetadataCannotProduceFilesOrLeakErrors)
{
  QTemporaryDir root;
  Network api, downloads;
  downloads.responses << Network::Response{ "wrong version" };
  Clf3CollectionHost host({}, nullptr, &downloads);
  QSignalSpy errors(&host, &Clf3CollectionHost::failed);
  auto a = artifact("expected bytes");
  a.insert("source_type", "direct");
  bool completed = false;
  host.artifact(a,
                QUrl("https://cdn.example.test/file?token=private"),
                root.path(),
                {},
                [&](QString) { completed = true; });
  ASSERT_TRUE(spin([&] { return !errors.empty(); }));
  EXPECT_FALSE(completed);
  EXPECT_FALSE(errors[0][0].toString().contains("private"));
  EXPECT_TRUE(QDir(root.path()).entryList(QDir::Files).isEmpty());
  api.responses << Network::Response{
    R"({"data":{"collectionRevision":{"revisionNumber":12,"collectionSchemaId":1,"downloadLink":"https://evil.test/steal"}}})"
  };
  Clf3CollectionHost packageHost([&](const QUrl& url, const QByteArray& body)
                                 { return api.post(QNetworkRequest(url), body); },
                                 nullptr,
                                 &downloads);
  QSignalSpy packageErrors(&packageHost, &Clf3CollectionHost::failed);
  packageHost.package({ { "domain", "skyrim" }, { "slug", "test" }, { "revision", 12 } },
                      root.path(),
                      [&](QJsonObject) { completed = true; });
  ASSERT_TRUE(spin([&] { return !packageErrors.empty(); }));
  EXPECT_EQ(api.requests.size(), 1);
  EXPECT_FALSE(completed);
}

TEST(CollectionHost, CancellationDiscardsLateCallbacksAndResumesVerifiedCache)
{
  QTemporaryDir root;
  Network network;
  network.responses << Network::Response{ "valid bytes" };
  Clf3CollectionHost host({}, nullptr, &network);
  const auto bytes = QByteArray("valid bytes");
  const auto target = root.filePath(QString::fromLatin1(md5(bytes)) + ".archive");
  write(target, bytes);
  bool completed = false;
  auto a = artifact(bytes);
  host.artifact(a, {}, root.path(), {}, [&](QString) { completed = true; });
  host.cancel();
  QTest::qWait(100);
  EXPECT_FALSE(completed);
  host.artifact(a, {}, root.path(), {}, [&](QString) { completed = true; });
  ASSERT_TRUE(spin([&] { return completed; }));
  EXPECT_TRUE(network.requests.empty());
}

TEST(CollectionGui, ActualEngineReviewOptionalBlockersPublicationAndResume)
{
  if (!Clf3CollectionDialog::InstallationEnabled)
    GTEST_SKIP() << "Collections installation is temporarily disabled while WIP";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE for actual-engine GUI fixture validation";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  QTemporaryDir root;
  ASSERT_TRUE(root.isValid());
  qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
  qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
  const auto package = root.filePath("package");
  const auto game = root.filePath("game");
  const auto output = root.filePath("installed");
  write(
    package + "/collection.json",
    R"({"info":{"name":"GUI fixture","domainName":"skyrimspecialedition"},"mods":[{"name":"Fixture mod","version":"1.2.3","optional":true,"source":{"type":"bundle","fileExpression":"fixture","tag":"fixture"}}]})");
  write(package + "/bundled/fixture/textures/fixture.txt", "fixture payload");
  write(game + "/SkyrimSE.exe", "fixture executable");
  write(game + "/Skyrim_Default.ini", "[General]\n");
  QByteArray plugin(24, '\0');
  plugin.replace(0, 4, "TES4");
  plugin[8] = 1;
  write(game + "/Data/Skyrim.esm", plugin);
  const auto master = root.filePath("masterlist.yaml");
  write(master, "plugins: []\n");
  int authCalls = 0;
  auto makeDialog = [&]
  {
    return std::make_unique<Clf3CollectionDialog>(
      [&](const QUrl&, const QByteArray&) -> QNetworkReply*
      {
        ++authCalls;
        return nullptr;
      },
      nullptr,
      false);
  };
  auto dialog = makeDialog();
  dialog->show();
  auto button = [&](const char* name) { return dialog->findChild<QPushButton*>(name); };
  auto edit = [&](const char* name, const QString& value)
  { dialog->findChild<QLineEdit*>(name)->setText(value); };
  ASSERT_TRUE(spin([&] { return button("collectionPlan")->isEnabled(); }));
  edit("collectionSource",
       "https://www.nexusmods.com/games/skyrimspecialedition/collections/fixture/revisions/12");
  edit("collectionPackage", package);
  edit("collectionGame", game + "/");
  edit("collectionOutput", output + "/");
  edit("collectionCache", root.filePath("archives"));
  edit("collectionMasterlist", master);
  button("collectionPlan")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  auto* members = dialog->findChild<QListWidget*>("collectionMembers");
  ASSERT_EQ(members->count(), 1);
  EXPECT_EQ(members->item(0)->checkState(), Qt::Unchecked);
  EXPECT_TRUE(dialog->findChild<QWidget*>("collectionReviewPanel")->isVisible());
  EXPECT_TRUE(button("collectionInstall")->isVisible());
  EXPECT_FALSE(button("collectionAdd")->isVisible());
  EXPECT_FALSE(button("collectionOpen")->isVisible());
  members->item(0)->setCheckState(Qt::Checked);
  EXPECT_FALSE(button("collectionInstall")->isEnabled());
  button("collectionPlan")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  EXPECT_EQ(members->item(0)->checkState(), Qt::Checked);
  const auto existing = root.filePath("existing-installation");
  write(existing + "/preserve.txt", "existing work");
  edit("collectionOutput", existing);
  button("collectionPlan")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  button("collectionInstall")->click();
  EXPECT_TRUE(dialog->findChild<QLabel*>("collectionStatus")->text().contains("already exists"));
  QFile preserved(existing + "/preserve.txt");
  ASSERT_TRUE(preserved.open(QIODevice::ReadOnly));
  EXPECT_EQ(preserved.readAll(), "existing work");
  edit("collectionOutput", output);
  button("collectionPlan")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  EXPECT_FALSE(button("collectionAdd")->isEnabled());
  EXPECT_TRUE(dialog->createdInstanceDir().isEmpty());
  button("collectionInstall")->click();
  button("collectionCancel")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  EXPECT_FALSE(button("collectionAdd")->isEnabled());
  EXPECT_FALSE(QFileInfo::exists(output));
  button("collectionInstall")->click();
  ASSERT_TRUE(spin(
    [&]
    { return button("collectionAdd")->isEnabled() || !button("collectionCancel")->isEnabled(); },
    30000));
  ASSERT_TRUE(button("collectionAdd")->isEnabled())
    << dialog->findChild<QLabel*>("collectionStatus")->text().toStdString();
  EXPECT_TRUE(QFileInfo::exists(output + "/ModOrganizer.ini"));
  EXPECT_TRUE(button("collectionAdd")->isVisible());
  EXPECT_TRUE(button("collectionOpen")->isVisible());
  EXPECT_FALSE(button("collectionPlan")->isVisible());
  EXPECT_FALSE(button("collectionInstall")->isVisible());
  EXPECT_EQ(authCalls, 0);
  EXPECT_TRUE(
    dialog->findChild<QLabel*>("collectionStatus")->text().contains("launch has not been tested"));
  EXPECT_NE(dialog->result(),
            QDialog::Accepted); // Publication alone never accepts/registers/opens.
  dialog->grab().save("/tmp/fluorine-collections-published.png");
  dialog->reject();
  dialog.reset();
  dialog = makeDialog();
  ASSERT_TRUE(spin([&] { return button("collectionResume")->isEnabled(); }));
  button("collectionResume")->click();
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }))
    << dialog->findChild<QLabel*>("collectionStatus")->text().toStdString();
  button("collectionInstall")->click();
  ASSERT_TRUE(spin([&] { return button("collectionAdd")->isEnabled(); }, 30000));
  const auto profilePath = output + "/profiles/Default/plugins.txt";
  QFile profile(profilePath);
  ASSERT_TRUE(profile.open(QIODevice::ReadOnly));
  const auto originalProfile = profile.readAll();
  profile.close();
  write(profilePath, "changed after publication\n");
  button("collectionResume")->click();
  EXPECT_FALSE(button("collectionAdd")->isEnabled());
  ASSERT_TRUE(spin([&] { return button("collectionInstall")->isEnabled(); }));
  button("collectionInstall")->click();
  ASSERT_TRUE(spin([&] { return !dialog->isBusy(); }, 30000));
  EXPECT_FALSE(button("collectionAdd")->isEnabled());
  EXPECT_FALSE(button("collectionOpen")->isEnabled());
  write(profilePath, originalProfile);
  button("collectionInstall")->click();
  ASSERT_TRUE(spin([&] { return button("collectionAdd")->isEnabled(); }, 30000));
  button("collectionOpen")->click();
  EXPECT_EQ(dialog->result(), QDialog::Accepted);
  EXPECT_TRUE(dialog->shouldOpen());
  EXPECT_EQ(dialog->createdInstanceDir(), output);
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionGui, CatalogGameFilteringRevisionSelectionAndUnsupportedAdapters)
{
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  Network catalog;
  const QByteArray results =
    R"({"data":{"collectionsV2":{"totalCount":1,"nodes":[{"slug":"exact","name":"Catalog fixture","summary":"Reviewed revision","tileImage":{"thumbnailUrl":"https://staticdelivery.nexusmods.com/collections/fixture.png"},"game":{"name":"Skyrim Special Edition","domainName":"skyrimspecialedition"},"user":{"name":"Author"},"latestPublishedRevision":{"revisionNumber":9,"modCount":8,"collectionSchemaId":1}},{"slug":"unsupported","name":"Unsupported collection","game":{"name":"Cyberpunk","domainName":"cyberpunk2077"}}]}}})";
  catalog.responses << Network::Response{ results } << Network::Response{ artwork() };
  Clf3CollectionDialog dialog({}, nullptr, true, &catalog);
  dialog.setDetectedGames({ { "fallout3", "/detected/Fallout 3" } });
  dialog.show();
  auto* list = dialog.findChild<QListWidget*>("collectionCatalog");
  ASSERT_TRUE(spin(
    [&]
    {
      return list->count() == 1 && list->item(0)->data(Qt::UserRole + 2).toBool() &&
             dialog.findChild<QComboBox*>("collectionGameFilter")->count() == 6;
    }));
  EXPECT_EQ(list->viewMode(), QListView::IconMode);
  EXPECT_EQ(list->iconSize(), QSize(240, 135));
  EXPECT_FALSE(list->item(0)->icon().isNull());
  EXPECT_TRUE(list->item(0)->text().startsWith("Catalog fixture\nAuthor"));
  ASSERT_EQ(catalog.requests.size(), 2);
  EXPECT_TRUE(catalog.bodies[0].contains("tileImage{thumbnailUrl(size:med)}"));
  EXPECT_FALSE(catalog.requests[1].hasRawHeader("Authorization"));
  EXPECT_FALSE(catalog.requests[1].hasRawHeader("APIKEY"));
  auto queriedGame = [&]
  {
    return QJsonDocument::fromJson(catalog.bodies.last())
      .object()
      .value("variables")
      .toObject()
      .value("filter")
      .toObject()
      .value("gameDomain")
      .toArray()[0]
      .toObject()
      .value("value")
      .toString();
  };
  const auto initialQuery = QJsonDocument::fromJson(catalog.bodies.first()).object();
  EXPECT_EQ(initialQuery.value("variables")
              .toObject()
              .value("filter")
              .toObject()
              .value("gameDomain")
              .toArray()[0]
              .toObject()
              .value("value")
              .toString(),
            "skyrimspecialedition");
  auto* games = dialog.findChild<QComboBox*>("collectionGameFilter");
  ASSERT_EQ(games->count(), 6);
  EXPECT_EQ(games->findData("cyberpunk2077"), -1);
  EXPECT_EQ(games->findData(""), -1); // Never offer an unfiltered Nexus-wide search.
  EXPECT_EQ(games->itemText(games->findData("skyrimspecialedition")), "Skyrim Special Edition");
  for (const auto* domain : { "fallout4", "newvegas", "fallout3", "oblivion", "skyrim" })
  {
    ASSERT_GE(games->findData(domain), 0);
    EXPECT_TRUE(games->itemText(games->findData(domain)).endsWith("(Experimental)"));
  }
  auto* source = dialog.findChild<QLineEdit*>("collectionSource");
  auto* selection = dialog.findChild<QLabel*>("collectionSelection");
  auto* output = dialog.findChild<QLineEdit*>("collectionOutput");
  EXPECT_TRUE(source->text().endsWith("/revisions/9"));
  dialog.findChild<QSpinBox*>("collectionRevision")->setValue(4);
  EXPECT_TRUE(source->text().endsWith("/revisions/4"));
  EXPECT_TRUE(selection->text().contains("Catalog fixture · Revision 4"));
  EXPECT_FALSE(selection->text().contains("https://"));
  EXPECT_TRUE(output->text().isEmpty());
  EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionCache")->text().isEmpty());
  QByteArray falloutResults = results;
  falloutResults.replace("skyrimspecialedition", "fallout3");
  falloutResults.replace("Skyrim Special Edition", "Fallout 3");
  falloutResults.replace("Catalog fixture", "Fallout collection");
  catalog.responses << Network::Response{ falloutResults };
  games->setCurrentIndex(games->findData("fallout3"));
  ASSERT_TRUE(spin(
    [&] { return list->count() == 1 && list->item(0)->text().contains("Fallout collection"); }));
  ASSERT_EQ(catalog.requests.size(), 3); // Artwork reused from the public image cache.
  EXPECT_EQ(queriedGame(), "fallout3");
  EXPECT_TRUE(list->item(0)->text().contains("Fallout 3 (Experimental)"));
  EXPECT_TRUE(selection->text().contains("Fallout 3 (Experimental)"));
  EXPECT_TRUE(output->text().isEmpty());
  EXPECT_EQ(dialog.findChild<QLineEdit*>("collectionGame")->text(), "/detected/Fallout 3");
  if (Clf3CollectionDialog::InstallationEnabled)
  {
    dialog.findChild<QPushButton*>("collectionConfigure")->click();
    EXPECT_TRUE(output->isVisible());
    EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionCache")->isVisible());
    EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionGame")->isVisible());
    EXPECT_FALSE(dialog.findChild<QLineEdit*>("collectionPackage")->isVisible());
    EXPECT_FALSE(dialog.findChild<QLineEdit*>("collectionIni")->isVisible());
    EXPECT_FALSE(dialog.findChild<QLineEdit*>("collectionMasterlist")->isVisible());
    EXPECT_FALSE(dialog.findChild<QWidget*>("collectionReviewPanel")->isVisible());
    for (const auto* name :
         { "collectionInstall", "collectionAdd", "collectionOpen", "collectionCancel" })
      EXPECT_FALSE(dialog.findChild<QPushButton*>(name)->isVisible());
    dialog.grab().save("/tmp/fluorine-collections-setup.png");
    auto* advanced = dialog.findChild<QToolButton*>("collectionAdvancedToggle");
    advanced->click();
    EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionPackage")->isVisible());
    EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionIni")->isVisible());
    EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionMasterlist")->isVisible());
    dialog.findChild<QLineEdit*>("collectionPackage")->setText("/chosen/package");
    advanced->click();
    EXPECT_EQ(dialog.findChild<QLineEdit*>("collectionPackage")->text(), "/chosen/package");
  }
  else
  {
    EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionConfigure")->isEnabled());
    dialog.findChild<QPushButton*>("collectionConfigure")->click();
    EXPECT_EQ(dialog.findChild<QStackedWidget*>("collectionPages")->currentIndex(), 0);
    EXPECT_TRUE(dialog.findChild<QLabel*>("collectionWipNotice")->isVisible());
  }
  output->setText("/custom/instance");
  dialog.findChild<QPushButton*>("collectionBack")->click();
  catalog.responses << Network::Response{ results };
  games->setCurrentIndex(games->findData("skyrimspecialedition"));
  ASSERT_TRUE(
    spin([&] { return list->count() == 1 && list->item(0)->text().contains("Catalog fixture"); }));
  EXPECT_EQ(output->text(), "/custom/instance");
  dialog.findChild<QLineEdit*>("collectionSource")
    ->setText("https://www.nexusmods.com/games/cyberpunk2077/collections/fixture/revisions/1");
  dialog.findChild<QPushButton*>("collectionPlan")->click();
  if (Clf3CollectionDialog::InstallationEnabled)
    EXPECT_TRUE(dialog.findChild<QLabel*>("collectionStatus")
                  ->text()
                  .contains("no reviewed installation adapter"));
  else
    EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionPlan")->isEnabled());
  EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionInstall")->isEnabled());
  dialog.findChild<QPushButton*>("collectionBack")->click();
  dialog.grab().save("/tmp/fluorine-collections-browser.png");
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionGui, ActualEngineBlockerReviewPreventsInstallation)
{
  if (!Clf3CollectionDialog::InstallationEnabled)
    GTEST_SKIP() << "Collections installation is temporarily disabled while WIP";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  QTemporaryDir root;
  qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
  const auto package = root.filePath("package");
  const auto game = root.filePath("game");
  write(
    package + "/collection.json",
    R"({"info":{"name":"Blocked fixture","domainName":"skyrimspecialedition","gameVersions":["1.2.3"]},"mods":[]})");
  write(game + "/SkyrimSE.exe", "fixture executable without version");
  Clf3InstallerTabs tabs(new QLabel("Wabbajack"),
                         [](QWidget* parent)
                         { return new Clf3CollectionDialog({}, parent, false); });
  tabs.show();
  tabs.setCurrentIndex(1);
  auto& dialog = *tabs.collections();
  auto* review = dialog.findChild<QPushButton*>("collectionPlan");
  ASSERT_TRUE(spin([&] { return review->isEnabled(); }));
  dialog.findChild<QLineEdit*>("collectionSource")
    ->setText(
      "https://www.nexusmods.com/games/skyrimspecialedition/collections/blocked/revisions/1");
  dialog.findChild<QLineEdit*>("collectionPackage")->setText(package);
  dialog.findChild<QLineEdit*>("collectionGame")->setText(game);
  dialog.findChild<QLineEdit*>("collectionOutput")->setText(root.filePath("output"));
  dialog.findChild<QLineEdit*>("collectionCache")->setText(root.filePath("archives"));
  review->click();
  EXPECT_FALSE(tabs.isTabEnabled(0));
  auto* details = dialog.findChild<QPlainTextEdit*>("collectionReview");
  ASSERT_TRUE(spin([&] { return details->toPlainText().contains("BLOCKER"); }));
  EXPECT_TRUE(details->toPlainText().contains("game_version_required"));
  EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionInstall")->isEnabled());
  EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionAdd")->isEnabled());
  EXPECT_FALSE(QFileInfo::exists(root.filePath("output")));
  EXPECT_TRUE(tabs.isTabEnabled(0));
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionHost, ThumbnailsArePublicBoundedAndCannotRedirectCredentials)
{
  Network images;
  int authCalls = 0;
  Clf3CollectionHost host(
    [&](const QUrl&, const QByteArray&) -> QNetworkReply*
    {
      ++authCalls;
      return nullptr;
    },
    nullptr,
    &images);
  QSignalSpy failures(&host, &Clf3CollectionHost::failed);
  images.responses << Network::Response{ artwork() };
  QImage received;
  host.thumbnail(QUrl("https://staticdelivery.nexusmods.com/test.png"),
                 [&](QImage image) { received = image; });
  ASSERT_TRUE(spin([&] { return !received.isNull(); }));
  EXPECT_EQ(received.size(), QSize(480, 270));
  EXPECT_EQ(received.pixelColor(0, 0), QColor(45, 92, 170));
  EXPECT_EQ(authCalls, 0);
  EXPECT_FALSE(images.requests.first().hasRawHeader("APIKEY"));
  EXPECT_FALSE(images.requests.first().hasRawHeader("Authorization"));
  for (const auto& url : { "https://evil.test/image.png",
                           "https://nexusmods.com.evil.test/a",
                           "http://media.nexusmods.com/a",
                           "https://user:secret@media.nexusmods.com/a" })
    EXPECT_FALSE(Clf3CollectionHost::thumbnailUrlAllowed(QUrl(url)));
  bool completed = false;
  images.responses << Network::Response{ {}, 302, "https://evil.test/redirect" };
  host.thumbnail(QUrl("https://staticdelivery.nexusmods.com/redirect.png"),
                 [&](QImage image)
                 {
                   EXPECT_TRUE(image.isNull());
                   completed = true;
                 });
  ASSERT_TRUE(spin([&] { return completed; }));
  EXPECT_EQ(images.requests.size(), 2);
  images.responses << Network::Response{ artwork(QSize(9000, 1)) };
  completed = false;
  host.thumbnail(QUrl("https://staticdelivery.nexusmods.com/oversized.png"),
                 [&](QImage image)
                 {
                   EXPECT_TRUE(image.isNull());
                   completed = true;
                 });
  ASSERT_TRUE(spin([&] { return completed; }));
  EXPECT_TRUE(failures.isEmpty()); // Broken artwork must not fail an installation.
}

TEST(CollectionGui, TabsEmbedBothBrowsersAndPreserveStateAcrossSwitches)
{
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  auto* wabbajack = new QLineEdit("Wabbajack selection");
  int created = 0;
  Clf3InstallerTabs tabs(wabbajack,
                         [&](QWidget* parent)
                         {
                           ++created;
                           return new Clf3CollectionDialog({}, parent, false);
                         });
  tabs.resize(1280, 820);
  tabs.show();
  ASSERT_EQ(tabs.count(), 2);
  EXPECT_EQ(tabs.tabText(0), "Wabbajack");
  EXPECT_EQ(tabs.tabText(1),
            Clf3CollectionDialog::InstallationEnabled ? "Nexus Collections"
                                                      : "Nexus Collections (WIP)");
  EXPECT_EQ(created, 0);
  tabs.setCurrentIndex(1);
  ASSERT_EQ(created, 1);
  auto* panel = tabs.collections();
  ASSERT_NE(panel, nullptr);
  EXPECT_FALSE(panel->isWindow());
  EXPECT_EQ(panel->window(), tabs.window());
  ASSERT_TRUE(
    spin([&] { return panel->findChild<QComboBox*>("collectionGameFilter")->count() == 6; }));
  auto* source = panel->findChild<QLineEdit*>("collectionSource");
  source->setText("https://www.nexusmods.com/games/skyrim/collections/fixture/revisions/4");
  auto* pages = panel->findChild<QStackedWidget*>("collectionPages");
  EXPECT_EQ(pages->currentIndex(), 0);
  panel->findChild<QPushButton*>("collectionConfigure")->click();
  EXPECT_EQ(pages->currentIndex(), Clf3CollectionDialog::InstallationEnabled ? 1 : 0);
  panel->findChild<QPushButton*>("collectionBack")->click();
  EXPECT_EQ(pages->currentIndex(), 0);
  tabs.setCurrentIndex(0);
  EXPECT_TRUE(wabbajack->isVisible());
  EXPECT_EQ(wabbajack->text(), "Wabbajack selection");
  tabs.setCurrentIndex(1);
  EXPECT_EQ(created, 1);
  EXPECT_TRUE(source->text().endsWith("/revisions/4"));
  QSignalSpy closed(&tabs, &Clf3InstallerTabs::collectionsFinished);
  panel->reject();
  ASSERT_EQ(closed.size(), 1);
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionGui, WipBlocksDownloadsAndResumeEvenIfControlsAreActivated)
{
  if (Clf3CollectionDialog::InstallationEnabled)
    GTEST_SKIP() << "Collections WIP lock has been lifted";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  QTemporaryDir root;
  qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
  const auto job = root.filePath("saved-job");
  const auto pending = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                       "/collections/pending.json";
  const auto pendingBytes = json({ { "job", job } });
  const auto source =
    QString("https://www.nexusmods.com/games/fallout4/collections/fixture/revisions/2");
  const auto savedBytes =
    json({ { "version", 1 }, { "job", job }, { "source", source }, { "started", true } });
  write(pending, pendingBytes);
  write(job + "/saved.json", savedBytes);
  const auto game = root.filePath("game");
  write(game + "/Fallout4.exe", "fixture executable");
  Network network;
  int authenticated = 0;
  Clf3CollectionDialog dialog(
    [&](const QUrl&, const QByteArray&) -> QNetworkReply*
    {
      ++authenticated;
      return nullptr;
    },
    nullptr,
    false,
    &network);
  dialog.show();
  auto button = [&](const char* name) { return dialog.findChild<QPushButton*>(name); };
  EXPECT_FALSE(button("collectionConfigure")->isEnabled());
  EXPECT_FALSE(button("collectionResume")->isEnabled());
  ASSERT_TRUE(
    spin([&] { return dialog.findChild<QComboBox*>("collectionGameFilter")->count() == 6; }));
  EXPECT_TRUE(dialog.windowTitle().contains("WIP"));
  EXPECT_TRUE(dialog.findChild<QLabel*>("collectionWipNotice")->isVisible());
  dialog.findChild<QLineEdit*>("collectionSource")->setText(source);
  dialog.findChild<QLineEdit*>("collectionGame")->setText(game);
  dialog.findChild<QLineEdit*>("collectionOutput")->setText(root.filePath("output"));
  dialog.findChild<QLineEdit*>("collectionCache")->setText(root.filePath("archives"));
  for (const auto* name : { "collectionConfigure",
                            "collectionPlan",
                            "collectionResume",
                            "collectionInstall",
                            "collectionManual",
                            "collectionBrowser",
                            "collectionAdd",
                            "collectionOpen" })
  {
    EXPECT_FALSE(button(name)->isEnabled()) << name;
    button(name)->setEnabled(true); // Exercise the handler, not just the disabled appearance.
    button(name)->click();
  }
  QTest::qWait(100);
  EXPECT_EQ(dialog.findChild<QStackedWidget*>("collectionPages")->currentIndex(), 0);
  EXPECT_EQ(authenticated, 0);
  EXPECT_TRUE(network.requests.isEmpty());
  EXPECT_FALSE(dialog.isBusy());
  EXPECT_FALSE(QFileInfo::exists(root.filePath("output")));
  EXPECT_FALSE(QFileInfo::exists(root.filePath("archives")));
  EXPECT_TRUE(dialog.createdInstanceDir().isEmpty());
  EXPECT_NE(dialog.result(), QDialog::Accepted);
  QFile saved(job + "/saved.json");
  ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
  EXPECT_EQ(saved.readAll(), savedBytes);
  QFile pendingFile(pending);
  ASSERT_TRUE(pendingFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(pendingFile.readAll(), pendingBytes);
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionCompatibility, ReleasedPackageReaderPinsRuntimeAndKeepsDirectSourcesPrivate)
{
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  const auto extractor = QFileInfo(engine).absolutePath() + "/7zz";
  if (engine.isEmpty() || !QFileInfo(extractor).isExecutable())
    GTEST_SKIP() << "Requires a packaged CLF3 release";
  QTemporaryDir root;
  const auto package = root.filePath("package");
  const auto game = root.filePath("game");
  const auto archive = root.filePath("collection.7z");
  write(
    package + "/Collection.json",
    R"({"info":{"domainName":"fallout4"},"mods":[{"source":{"type":"direct","url":"https://example.test/exact.zip?token=private-source"}}]})");
  write(game + "/Fallout4.exe",
        "fixture" + QByteArray::fromHex("bd04effe000001000a0001000000d801"));
  QProcess pack;
  pack.setProcessEnvironment(Clf3ProcessController::engineEnvironment());
  pack.setWorkingDirectory(package);
  pack.start(extractor, { "a", archive, "Collection.json" });
  ASSERT_TRUE(pack.waitForFinished(10000));
  ASSERT_EQ(pack.exitCode(), 0);
  Clf3CollectionCompat compatibility;
  QSignalSpy prepared(&compatibility, &Clf3CollectionCompat::prepared);
  QSignalSpy failed(&compatibility, &Clf3CollectionCompat::failed);
  const QJsonObject caps{ { "engine_version", "0.2.6" },
                          { "installation_available", true },
                          { "standalone_worker", "collection_local_worker_v1" },
                          { "protocol_version", 1 },
                          { "plan_schema_version", 1 } };
  auto support = Clf3CollectionCompat::gameSupport(caps, { { "domain", "fallout4" } });
  EXPECT_EQ(support.value("executable"), "Fallout4.exe");
  EXPECT_EQ(support.value("masterlist_sha256").toString().size(), 64);
  auto unsupported = caps;
  unsupported.insert("standalone_worker", "collection_local_worker_v2");
  EXPECT_FALSE(Clf3CollectionCompat::supports(unsupported));
  compatibility.prepare(archive, game, support, engine);
  ASSERT_TRUE(spin([&] { return !prepared.isEmpty() || !failed.isEmpty(); }));
  ASSERT_EQ(prepared.size(), 1) << (failed.isEmpty()
                                      ? ""
                                      : failed.first().first().toString().toStdString());
  EXPECT_EQ(prepared.first()[0].toString(), "1.10.472.0");
  EXPECT_EQ(prepared.first()[1].toJsonObject().value("0"),
            "https://example.test/exact.zip?token=private-source");
  compatibility.prepare(archive, game, support, engine);
  compatibility.cancel();
  QTest::qWait(100);
  EXPECT_EQ(prepared.size(), 1);
  EXPECT_TRUE(failed.isEmpty());
}

TEST(CollectionGui, RequiresChosenFoldersBeforeAcquisition)
{
  if (!Clf3CollectionDialog::InstallationEnabled)
    GTEST_SKIP() << "Collections installation is temporarily disabled while WIP";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  QTemporaryDir root;
  int authCalls = 0;
  Clf3CollectionDialog dialog(
    [&](const QUrl&, const QByteArray&) -> QNetworkReply*
    {
      ++authCalls;
      return nullptr;
    },
    nullptr,
    false);
  ASSERT_TRUE(spin([&] { return dialog.findChild<QPushButton*>("collectionPlan")->isEnabled(); }));
  dialog.findChild<QLineEdit*>("collectionSource")
    ->setText("https://www.nexusmods.com/games/fallout4/collections/fixture/revisions/2");
  dialog.findChild<QLineEdit*>("collectionGame")->setText(root.path());
  EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionOutput")->text().isEmpty());
  EXPECT_TRUE(dialog.findChild<QLineEdit*>("collectionCache")->text().isEmpty());
  dialog.findChild<QPushButton*>("collectionPlan")->click();
  EXPECT_TRUE(dialog.findChild<QLabel*>("collectionStatus")
                ->text()
                .contains("Choose an instance folder and a downloads folder"));
  EXPECT_EQ(authCalls, 0);
  EXPECT_FALSE(dialog.findChild<QPushButton*>("collectionInstall")->isEnabled());
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionGui, ActualEngineSixGamePublicationAndTamperDetection)
{
  if (!Clf3CollectionDialog::InstallationEnabled)
    GTEST_SKIP() << "Collections installation is temporarily disabled while WIP";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  if (engine.isEmpty())
    GTEST_SKIP() << "Set CLF3_COLLECTION_TEST_ENGINE";
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  struct Game
  {
    const char *domain, *exe, *base, *ini, *defaultIni, *extender;
    int header;
    bool timestamps, asterisks;
  };
  const Game games[]{ { "skyrimspecialedition",
                        "SkyrimSE.exe",
                        "Skyrim.esm",
                        "Skyrim.ini",
                        "Skyrim_Default.ini",
                        "skse64_loader.exe",
                        24,
                        false,
                        true },
                      { "fallout4",
                        "Fallout4.exe",
                        "Fallout4.esm",
                        "Fallout4.ini",
                        "Fallout4_Default.ini",
                        "f4se_loader.exe",
                        24,
                        false,
                        true },
                      { "newvegas",
                        "FalloutNV.exe",
                        "FalloutNV.esm",
                        "Fallout.ini",
                        "Fallout_default.ini",
                        "nvse_loader.exe",
                        24,
                        true,
                        false },
                      { "fallout3",
                        "Fallout3.exe",
                        "Fallout3.esm",
                        "Fallout.ini",
                        "Fallout_default.ini",
                        "fose_loader.exe",
                        24,
                        true,
                        false },
                      { "oblivion",
                        "Oblivion.exe",
                        "Oblivion.esm",
                        "Oblivion.ini",
                        "Oblivion_default.ini",
                        "obse_loader.exe",
                        20,
                        true,
                        false },
                      { "skyrim",
                        "TESV.exe",
                        "Skyrim.esm",
                        "Skyrim.ini",
                        "Skyrim_default.ini",
                        "skse_loader.exe",
                        24,
                        false,
                        false } };
  auto plugin = [](int header, const QByteArray& master, bool esm)
  {
    QByteArray body = QByteArray::fromHex("484544520c000000803f0000000000080000");
    if (!master.isEmpty())
    {
      body += "MAST";
      const auto length = qToLittleEndian<quint16>(master.size() + 1);
      body.append(reinterpret_cast<const char*>(&length), 2);
      body += master;
      body += '\0';
      body += QByteArray::fromHex("4441544108000000000000000000");
    }
    QByteArray bytes(header, '\0');
    bytes.replace(0, 4, "TES4");
    qToLittleEndian<quint32>(body.size(), bytes.data() + 4);
    qToLittleEndian<quint32>(esm ? 1 : 0, bytes.data() + 8);
    return bytes + body;
  };
  for (const auto& game : games)
  {
    SCOPED_TRACE(game.domain);
    QTemporaryDir root;
    qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
    const auto package = root.filePath("package");
    const auto source = root.filePath("game");
    const auto output = root.filePath("output");
    const auto masterlist = root.filePath("masterlist.yaml");
    write(masterlist, "plugins: []\n");
    write(source + '/' + game.exe,
          "synthetic executable" + QByteArray::fromHex("bd04effe000001000200010004000300"));
    write(source + '/' + game.defaultIni, "[General]\noriginal=1\n");
    write(source + "/Data/" + game.base, plugin(game.header, {}, true));
    write(package + "/collection.json",
          json({ { "info",
                   QJsonObject{ { "name", "Cross-game GUI fixture" },
                                { "domainName", game.domain },
                                { "gameVersions", QJsonArray{ "1.2.3.4" } } } },
                 { "mods",
                   QJsonArray{ QJsonObject{ { "name", "Exact payload" },
                                            { "source",
                                              QJsonObject{ { "type", "bundle" },
                                                           { "fileExpression", "A" },
                                                           { "tag", "a" } } } } } },
                 { "plugins",
                   QJsonArray{ QJsonObject{ { "name", "A.esp" }, { "enabled", true } },
                               QJsonObject{ { "name", "B.esp" }, { "enabled", true } },
                               QJsonObject{ { "name", "Off.esp" }, { "enabled", false } } } } }));
    write(package + "/bundled/A/" + game.extender, "fixture extender");
    write(package + "/bundled/A/Data/A.esp", plugin(game.header, "B.esp", false));
    write(package + "/bundled/A/Data/B.esp", plugin(game.header, game.base, false));
    write(package + "/bundled/A/Data/Off.esp", plugin(game.header, "MissingOptional.esm", false));
    const auto stem = QString(game.ini).chopped(4);
    write(package + "/INI Tweaks/Profile [" + stem + "].ini", "[General]\nbAlwaysActive=1\n");
    Clf3CollectionDialog dialog({}, nullptr, false);
    auto button = [&](const char* name) { return dialog.findChild<QPushButton*>(name); };
    ASSERT_TRUE(spin([&] { return button("collectionPlan")->isEnabled(); }));
    auto edit = [&](const char* name, const QString& value)
    { dialog.findChild<QLineEdit*>(name)->setText(value); };
    edit("collectionSource",
         QString("https://www.nexusmods.com/games/%1/collections/fixture/revisions/2")
           .arg(game.domain));
    edit("collectionPackage", package);
    edit("collectionGame", source);
    edit("collectionOutput", output);
    edit("collectionCache", root.filePath("archives"));
    edit("collectionMasterlist", masterlist);
    button("collectionPlan")->click();
    ASSERT_TRUE(spin([&] { return !dialog.isBusy(); }));
    ASSERT_TRUE(button("collectionInstall")->isEnabled())
      << dialog.findChild<QLabel*>("collectionStatus")->text().toStdString();
    button("collectionInstall")->click();
    ASSERT_TRUE(spin([&] { return !dialog.isBusy(); }, 30000));
    ASSERT_TRUE(button("collectionAdd")->isEnabled())
      << dialog.findChild<QLabel*>("collectionStatus")->text().toStdString();
    auto read = [](const QString& path)
    {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly))
        return QByteArray{};
      return file.readAll();
    };
    EXPECT_EQ(read(output + "/Stock Game/" + game.extender), "fixture extender");
    EXPECT_TRUE(read(output + "/profiles/Default/" + game.ini).contains("bAlwaysActive=1"));
    const auto plugins = read(output + "/profiles/Default/plugins.txt");
    EXPECT_TRUE(plugins.contains(game.asterisks ? "*A.esp\n" : "\nA.esp\n"));
    if (!game.asterisks)
      EXPECT_FALSE(plugins.contains("Off.esp"));
    const auto pending =
      QJsonDocument::fromJson(
        read(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
             "/collections/pending.json"))
        .object();
    const auto saved =
      QJsonDocument::fromJson(read(pending.value("job").toString() + "/saved.json")).object();
    Clf3CollectionCompat compat;
    QSignalSpy verified(&compat, &Clf3CollectionCompat::verified);
    QSignalSpy failed(&compat, &Clf3CollectionCompat::failed);
    const auto support =
      Clf3CollectionCompat::gameSupport({ { "engine_version", "0.2.6" },
                                          { "installation_available", true },
                                          { "standalone_worker", "collection_local_worker_v1" },
                                          { "protocol_version", 1 },
                                          { "plan_schema_version", 1 } },
                                        { { "domain", game.domain } });
    const QJsonObject request{ { "output", output },
                               { "plan", pending.value("job").toString() + "/plan.json" },
                               { "job_identity", saved.value("identity") } };
    compat.verifyPublication(request, support);
    ASSERT_TRUE(spin([&] { return !verified.isEmpty() || !failed.isEmpty(); }));
    ASSERT_EQ(verified.size(), 1) << (failed.isEmpty()
                                        ? ""
                                        : failed.first().first().toString().toStdString());
    const auto profile = output + "/profiles/Default/plugins.txt";
    write(profile, "tampered\n");
    compat.verifyPublication(request, support);
    ASSERT_TRUE(spin([&] { return !failed.isEmpty(); }));
    EXPECT_EQ(verified.size(), 1);
    write(profile, plugins);
    failed.clear();
    write(output + "/Stock Game/" + game.extender, "changed extender");
    compat.verifyPublication(request, support);
    ASSERT_TRUE(spin([&] { return !failed.isEmpty(); }));
    EXPECT_EQ(verified.size(), 1);
    write(output + "/Stock Game/" + game.extender, "fixture extender");
    if (game.timestamps)
    {
      failed.clear();
      const auto timestamps =
        QJsonDocument::fromJson(read(output + "/.collection/plugin-timestamps.json")).object();
      QString pluginPath;
      for (auto it = timestamps.begin(); it != timestamps.end(); ++it)
        if (it.key().endsWith("/A.esp", Qt::CaseInsensitive))
          pluginPath = output + '/' + it.key();
      ASSERT_FALSE(pluginPath.isEmpty());
      QFile file(pluginPath);
      ASSERT_TRUE(file.open(QIODevice::ReadOnly));
      ASSERT_TRUE(file.setFileTime(QDateTime::fromSecsSinceEpoch(1700000000),
                                   QFileDevice::FileModificationTime));
      file.close();
      compat.verifyPublication(request, support);
      ASSERT_TRUE(spin([&] { return !failed.isEmpty(); }));
      EXPECT_EQ(verified.size(), 1);
    }
  }
  qunsetenv("FLUORINE_CLF3_PATH");
}

TEST(CollectionGui, LivePublicCatalogArtwork)
{
  if (qEnvironmentVariableIsEmpty("FLUORINE_TEST_LIVE_COLLECTIONS"))
    GTEST_SKIP() << "Optional live public catalog smoke test";
  const auto engine = qEnvironmentVariable("CLF3_COLLECTION_TEST_ENGINE");
  ASSERT_FALSE(engine.isEmpty());
  qputenv("FLUORINE_CLF3_PATH", engine.toUtf8());
  Clf3InstallerTabs tabs(new QLabel("Wabbajack"),
                         [](QWidget* parent) { return new Clf3CollectionDialog({}, parent); });
  tabs.resize(1280, 820);
  tabs.show();
  tabs.setCurrentIndex(1);
  auto* catalog = tabs.collections()->findChild<QListWidget*>("collectionCatalog");
  ASSERT_TRUE(spin([&] { return catalog->count() > 0; }, 45000))
    << tabs.collections()->findChild<QLabel*>("collectionStatus")->text().toStdString();
  ASSERT_TRUE(spin([&] { return catalog->item(0)->data(Qt::UserRole + 2).toBool(); }, 45000));
  // Allow remaining visible cards to fill without requiring every optional image.
  spin(
    [&]
    {
      for (int row = 0; row < qMin(9, catalog->count()); ++row)
      {
        auto* item = catalog->item(row);
        if (!item->data(Qt::UserRole + 1).toString().isEmpty() &&
            !item->data(Qt::UserRole + 2).toBool())
          return false;
      }
      return true;
    },
    10000);
  tabs.grab().save("/tmp/fluorine-collections-tabs-live.png");
  qunsetenv("FLUORINE_CLF3_PATH");
}

int
main(int argc, char** argv)
{
  QApplication app(argc, argv);
  app.setOrganizationName("FluorineTests");
  app.setApplicationName("CollectionsTests");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
