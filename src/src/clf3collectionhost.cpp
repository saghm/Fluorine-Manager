#include "clf3collectionhost.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtConcurrent>

namespace
{
bool
id(const QString& value)
{
  static const QRegularExpression re("^[a-z0-9][a-z0-9_-]{0,99}$");
  return re.match(value).hasMatch();
}
QStringList
mirrors(const QJsonArray& links)
{
  QStringList result;
  for (const auto& link : links)
  {
    const auto url = link.toObject().value("URI").toString();
    if (Clf3CollectionHost::archiveUrlAllowed(QUrl(url)))
      result << url;
  }
  return result;
}
}

Clf3CollectionHost::Clf3CollectionHost(AuthRequest auth,
                                       QObject* parent,
                                       QNetworkAccessManager* publicNetwork)
  : QObject(parent)
  , m_auth(std::move(auth))
  , m_public(publicNetwork ? publicNetwork : new QNetworkAccessManager(this))
  , m_cancelled(std::make_shared<std::atomic_bool>(false))
{
}
Clf3CollectionHost::~Clf3CollectionHost()
{
  cancel();
}
void
Clf3CollectionHost::cancel()
{
  m_cancelled->store(true);
  m_cancelled = std::make_shared<std::atomic_bool>(false);
  const auto replies = m_replies;
  m_replies.clear();
  for (auto* reply : replies)
  {
    reply->abort();
    reply->deleteLater();
  }
}

QJsonObject
Clf3CollectionHost::locator(const QString& source)
{
  const QUrl url(source);
  if (url.scheme() != "https" ||
      (url.host() != "www.nexusmods.com" && url.host() != "nexusmods.com") ||
      !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || url.port(-1) != -1)
    return {};
  static const QRegularExpression re(
    "^/games/([a-z0-9_-]+)/collections/([a-z0-9_-]+)(?:/revisions/([1-9][0-9]*))?/?$");
  const auto match = re.match(url.path());
  if (!match.hasMatch() || !id(match.captured(1)) || !id(match.captured(2)))
    return {};
  QJsonObject result{ { "domain", match.captured(1) }, { "slug", match.captured(2) } };
  if (!match.captured(3).isEmpty())
  {
    bool ok = false;
    const auto revision = match.captured(3).toInt(&ok);
    if (!ok || revision <= 0)
      return {};
    result.insert("revision", revision);
  }
  return result;
}
QString
Clf3CollectionHost::sourceUrl(const QJsonObject& value)
{
  if (!id(value.value("domain").toString()) || !id(value.value("slug").toString()))
    return {};
  QString url = QString("https://www.nexusmods.com/games/%1/collections/%2")
                  .arg(value.value("domain").toString(), value.value("slug").toString());
  if (value.value("revision").toInt() > 0)
    url += "/revisions/" + QString::number(value.value("revision").toInt());
  return url;
}
bool
Clf3CollectionHost::apiPathAllowed(const QString& path)
{
  static const QRegularExpression download(
    "^/v2/collections/[0-9]+/revisions/[0-9]+/download_link$");
  static const QRegularExpression file(
    "^/v1/games/[a-z0-9_-]+/mods/[1-9][0-9]*/files/[1-9][0-9]*/download_link.json$");
  return path == "/v2/graphql" || download.match(path).hasMatch() || file.match(path).hasMatch();
}
bool
Clf3CollectionHost::archiveUrlAllowed(const QUrl& url)
{
  return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() &&
         url.userInfo().isEmpty();
}
void
Clf3CollectionHost::json(const QString& path,
                         const QJsonObject& body,
                         bool authenticated,
                         JsonResult result)
{
  // Only internally constructed Nexus file authorization queries may carry NXM keys.
  const QUrl relative(path);
  if (!apiPathAllowed(relative.path()) || !relative.isRelative() || path.startsWith("//") ||
      relative.hasFragment())
  {
    emit failed(tr("Invalid Nexus API location."));
    return;
  }
  const QUrl url("https://api.nexusmods.com" + path);
  const QByteArray bytes =
    body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact);
  QNetworkReply* reply = nullptr;
  if (authenticated && m_auth)
    reply = m_auth(url, bytes);
  if (!reply)
  {
    if (authenticated)
    {
      emit failed(tr("Connect to Nexus before acquiring this file."));
      return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);
    reply = bytes.isEmpty() ? m_public->get(request) : m_public->post(request, bytes);
  }
  const auto token = m_cancelled;
  m_replies.insert(reply);
  connect(reply,
          &QNetworkReply::readyRead,
          this,
          [reply]
          {
            if (reply->bytesAvailable() > 4 * 1024 * 1024)
              reply->abort();
          });
  connect(
    reply,
    &QNetworkReply::finished,
    this,
    [this, reply, token, result]
    {
      m_replies.remove(reply);
      reply->deleteLater();
      if (token->load())
        return;
      const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300 ||
          reply->bytesAvailable() > 4 * 1024 * 1024)
      {
        emit failed(tr("Nexus request failed. Check your connection and account, then retry."));
        return;
      }
      const auto doc = QJsonDocument::fromJson(reply->readAll());
      QJsonObject value = doc.isArray() ? QJsonObject{ { "links", doc.array() } } : doc.object();
      const auto errors = value.value("errors");
      if (value.isEmpty() || (!errors.isUndefined() && !errors.isNull() &&
                              (!errors.isArray() || !errors.toArray().isEmpty())))
      {
        emit failed(tr("Nexus returned invalid or incomplete collection data."));
        return;
      }
      result(value);
    });
}
QJsonObject
Clf3CollectionHost::searchBody(const QString& game,
                               const QString& text,
                               int page,
                               const QString& sort,
                               bool hideAdult)
{
  if ((!game.isEmpty() && !id(game)) || text.size() > 200 || page < 0 || page > 1000000)
    return {};
  auto equal = [](QJsonValue value, const QString& op = "EQUALS")
  { return QJsonArray{ QJsonObject{ { "value", value }, { "op", op } } }; };
  QJsonObject filter{ { "collectionStatus", equal("listed") },
                      { "hasPublishedRevision", equal(true) } };
  if (!game.isEmpty())
    filter.insert("gameDomain", equal(game));
  if (!text.trimmed().isEmpty())
    filter.insert("name", equal(text.trimmed(), "WILDCARD"));
  if (hideAdult)
    filter.insert("adultContent", equal(false));
  const QString key = sort == "updatedAt" || sort == "createdAt" ? sort : "downloads";
  return {
    { "query",
      "query($filter:CollectionsSearchFilter,$sort:[CollectionsSearchSort!],$offset:Int!,$count:"
      "Int!){collectionsV2(filter:$filter,sort:$sort,offset:$offset,count:$count){totalCount "
      "nodes{slug name summary totalDownloads game{name domainName} user{name} "
      "tileImage{thumbnailUrl(size:med)} "
      "latestPublishedRevision{revisionNumber modCount collectionSchemaId adultContent}}}}" },
    { "variables",
      QJsonObject{
        { "filter", filter },
        { "sort", QJsonArray{ QJsonObject{ { key, QJsonObject{ { "direction", "DESC" } } } } } },
        { "offset", page * 24 },
        { "count", 24 } } }
  };
}
void
Clf3CollectionHost::games(JsonResult result)
{
  json("/v2/graphql", { { "query", "{collectionGames{name domainName}}" } }, false, result);
}
void
Clf3CollectionHost::search(const QString& game,
                           const QString& text,
                           int page,
                           const QString& sort,
                           bool hideAdult,
                           JsonResult result)
{
  const auto body = searchBody(game, text, page, sort, hideAdult);
  if (body.isEmpty())
  {
    emit failed(tr("Invalid collection search."));
    return;
  }
  json("/v2/graphql", body, false, result);
}
void
Clf3CollectionHost::package(const QJsonObject& locator, const QString& job, JsonResult result)
{
  if (sourceUrl(locator).isEmpty())
  {
    emit failed(tr("Invalid collection URL."));
    return;
  }
  const bool pinned = locator.value("revision").toInt() > 0;
  const QString fields = "revisionNumber collectionSchemaId downloadLink";
  const QString query =
    pinned ? "query($slug:String!,$domain:String!,$revision:Int!){collectionRevision(slug:$slug,"
             "domainName:$domain,revision:$revision){" +
               fields + "}}"
           : "query($slug:String!,$domain:String!){collection(slug:$slug,domainName:$domain){"
             "latestPublishedRevision{" +
               fields + "}}}";
  QJsonObject variables{ { "slug", locator.value("slug") }, { "domain", locator.value("domain") } };
  if (pinned)
    variables.insert("revision", locator.value("revision"));
  json("/v2/graphql",
       { { "query", query }, { "variables", variables } },
       true,
       [=, this](QJsonObject value)
       {
         const auto data = value.value("data").toObject();
         const auto revision =
           pinned ? data.value("collectionRevision").toObject()
                  : data.value("collection").toObject().value("latestPublishedRevision").toObject();
         const int number = revision.value("revisionNumber").toInt();
         if (number <= 0 || (pinned && number != locator.value("revision").toInt()) ||
             revision.value("collectionSchemaId").toInt() != 1)
         {
           emit failed(tr("Nexus did not return the requested revision with supported schema 1."));
           return;
         }
         const auto path = revision.value("downloadLink").toString();
         if (!apiPathAllowed(path) || !path.startsWith("/v2/collections/"))
         {
           emit failed(tr("Invalid package download location."));
           return;
         }
         auto resolved = locator;
         resolved.insert("revision", number);
         json(path,
              {},
              true,
              [=, this](QJsonObject value)
              {
                transfer(mirrors(value.value("download_links").toArray()),
                         job + "/collection.7z",
                         {},
                         -1,
                         false,
                         [=](QString path)
                         {
                           result({ { "locator", resolved },
                                    { "schema_id", 1 },
                                    { "package_path", path } });
                         });
              });
       });
}
void
Clf3CollectionHost::verify(const QString& path,
                           const QByteArray& hash,
                           qint64 size,
                           bool sha256,
                           std::function<void(bool)> result)
{
  auto* watcher = new QFutureWatcher<bool>(this);
  const auto token = m_cancelled;
  connect(watcher,
          &QFutureWatcher<bool>::finished,
          this,
          [watcher, token, result]
          {
            const bool valid = watcher->result();
            watcher->deleteLater();
            if (!token->load())
              result(valid);
          });
  watcher->setFuture(QtConcurrent::run(
    [=]
    {
      QFile file(path);
      if (hash.isEmpty() || QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) ||
          (size >= 0 && file.size() != size))
        return false;
      QCryptographicHash digest(sha256 ? QCryptographicHash::Sha256 : QCryptographicHash::Md5);
      while (!file.atEnd())
      {
        if (token->load())
          return false;
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
          return false;
        digest.addData(bytes);
      }
      return digest.result().toHex() == hash.toLower();
    }));
}
void
Clf3CollectionHost::transfer(const QStringList& urls,
                             const QString& path,
                             const QByteArray& hash,
                             qint64 size,
                             bool sha256,
                             FileResult result,
                             int redirectCount)
{
  if (urls.isEmpty() || redirectCount > 5)
  {
    emit failed(tr("No verified download was available. Retry or select the exact local archive."));
    return;
  }
  const QUrl url(urls.first());
  if (!archiveUrlAllowed(url) || !QDir().mkpath(QFileInfo(path).absolutePath()))
  {
    emit failed(tr("Invalid download or cache location."));
    return;
  }
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(120000);
  auto* reply = m_public->get(request); // Deliberately separate from the account manager.
  auto* output = new QSaveFile(path, reply);
  if (!output->open(QIODevice::WriteOnly))
  {
    reply->abort();
    reply->deleteLater();
    emit failed(tr("Cannot write the download cache."));
    return;
  }
  auto digest = std::make_shared<QCryptographicHash>(sha256 ? QCryptographicHash::Sha256
                                                            : QCryptographicHash::Md5);
  auto received = std::make_shared<qint64>(0);
  const auto token = m_cancelled;
  auto read = [=]
  {
    const auto bytes = reply->readAll();
    *received += bytes.size();
    const qint64 limit = size >= 0 ? size : 4LL * 1024 * 1024 * 1024;
    if (*received > limit || output->write(bytes) != bytes.size())
    {
      output->cancelWriting();
      reply->abort();
      return;
    }
    digest->addData(bytes);
  };
  m_replies.insert(reply);
  connect(reply, &QNetworkReply::readyRead, this, read);
  connect(reply, &QNetworkReply::downloadProgress, this, &Clf3CollectionHost::progress);
  connect(reply,
          &QNetworkReply::finished,
          this,
          [=, this]
          {
            read();
            m_replies.remove(reply);
            reply->deleteLater();
            if (token->load())
              return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400)
            {
              const QUrl redirected =
                url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
              output->cancelWriting();
              if (!archiveUrlAllowed(redirected))
              {
                emit failed(tr("Unsafe download redirect rejected."));
                return;
              }
              auto next = urls;
              next[0] = redirected.toString();
              transfer(next, path, hash, size, sha256, result, redirectCount + 1);
              return;
            }
            if (reply->error() != QNetworkReply::NoError || status != 200 ||
                (size >= 0 && *received != size) ||
                (!hash.isEmpty() && digest->result().toHex() != hash.toLower()) ||
                !output->commit())
            {
              output->cancelWriting();
              auto next = urls;
              next.removeFirst();
              transfer(next, path, hash, size, sha256, result);
              return;
            }
            result(path);
          });
}
void
Clf3CollectionHost::artifact(const QJsonObject& artifact,
                             const QUrl& direct,
                             const QString& cache,
                             const QString& nxm,
                             FileResult result)
{
  const auto hash = artifact.value("expected_md5").toString().toLatin1();
  static const QRegularExpression md5("^[0-9a-fA-F]{32}$");
  if (!md5.match(QString::fromLatin1(hash)).hasMatch())
  {
    emit failed(tr("The exact archive hash is missing."));
    return;
  }
  const auto size = artifact.value("expected_size").isDouble()
                      ? artifact.value("expected_size").toVariant().toLongLong()
                      : -1;
  const auto target = cache + "/" + QString::fromLatin1(hash.toLower()) + ".archive";
  verify(
    target,
    hash,
    size,
    false,
    [=, this](bool valid)
    {
      if (valid)
      {
        result(target);
        return;
      }
      if (artifact.value("source_type") == "direct" && archiveUrlAllowed(direct))
      {
        transfer({ direct.toString() }, target, hash, size, false, result);
        return;
      }
      if (artifact.value("source_type") != "nexus" || !id(artifact.value("domain").toString()))
      {
        emit failed(tr("Select the exact local archive for this member."));
        return;
      }
      QString path = QString("/v1/games/%1/mods/%2/files/%3/download_link.json")
                       .arg(artifact.value("domain").toString())
                       .arg(artifact.value("mod_id").toInteger())
                       .arg(artifact.value("file_id").toInteger());
      if (!nxm.isEmpty())
      {
        const QUrl auth(nxm);
        const auto pieces = auth.path().split('/', Qt::SkipEmptyParts);
        if (auth.scheme() != "nxm" || auth.host() != artifact.value("domain").toString() ||
            pieces.size() != 4 || pieces[0] != "mods" || pieces[2] != "files" ||
            pieces[1].toLongLong() != artifact.value("mod_id").toInteger() ||
            pieces[3].toLongLong() != artifact.value("file_id").toInteger())
        {
          emit failed(tr("The authorization belongs to a different Nexus file."));
          return;
        }
        const QUrlQuery input(auth);
        QUrlQuery query;
        query.addQueryItem("key", input.queryItemValue("key"));
        query.addQueryItem("expires", input.queryItemValue("expires"));
        path += "?" + query.toString(QUrl::FullyEncoded);
      }
      json(
        path,
        {},
        true,
        [=, this](QJsonObject value)
        { transfer(mirrors(value.value("links").toArray()), target, hash, size, false, result); });
    });
}
void
Clf3CollectionHost::masterlist(const QJsonObject& game, const QString& target, FileResult result)
{
  const auto hash = game.value("masterlist_sha256").toString().toLatin1();
  verify(target,
         hash,
         -1,
         true,
         [=, this](bool valid)
         {
           if (valid)
             result(target);
           else
             transfer({ game.value("masterlist_url").toString() }, target, hash, -1, true, result);
         });
}

bool
Clf3CollectionHost::thumbnailUrlAllowed(const QUrl& url)
{
  return archiveUrlAllowed(url) && url.port(443) == 443 && url.host().endsWith(".nexusmods.com");
}

void
Clf3CollectionHost::thumbnail(const QUrl& url, std::function<void(QImage)> result, int redirects)
{
  // Artwork is public and never passes through the account-bearing API manager.
  if (!thumbnailUrlAllowed(url) || redirects > 3)
  {
    result({});
    return;
  }
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(25000);
  auto* reply = m_public->get(request);
  const auto token = m_cancelled;
  m_replies.insert(reply);
  connect(reply,
          &QNetworkReply::readyRead,
          this,
          [reply]
          {
            if (reply->bytesAvailable() > 8 * 1024 * 1024)
              reply->abort();
          });
  connect(reply,
          &QNetworkReply::finished,
          this,
          [=, this]
          {
            m_replies.remove(reply);
            reply->deleteLater();
            if (token->load())
              return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 300 && status < 400)
            {
              const auto redirect =
                url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
              thumbnail(redirect, result, redirects + 1);
              return;
            }
            if (reply->error() != QNetworkReply::NoError || status != 200 ||
                reply->bytesAvailable() > 8 * 1024 * 1024)
            {
              result({});
              return;
            }
            const auto bytes = reply->readAll();
            auto* watcher = new QFutureWatcher<QImage>(this);
            connect(watcher,
                    &QFutureWatcher<QImage>::finished,
                    this,
                    [watcher, token, result]
                    {
                      const auto image = watcher->result();
                      watcher->deleteLater();
                      if (!token->load())
                        result(image);
                    });
            watcher->setFuture(QtConcurrent::run(
              [bytes, token]
              {
                if (token->load())
                  return QImage{};
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto size = reader.size();
                if (!size.isValid() || size.width() > 8192 || size.height() > 8192)
                  return QImage{};
                reader.setScaledSize(size.scaled(QSize(480, 270), Qt::KeepAspectRatio));
                reader.setAutoTransform(true);
                return reader.read();
              }));
          });
}
