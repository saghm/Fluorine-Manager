#include "gameartwork.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <steamutility.h>

namespace
{
constexpr qint64 MaxArtworkBytes = 4 * 1024 * 1024;

QPixmap readCover(QIODevice* device)
{
  QImageReader reader(device);
  const QSize size = reader.size();
  if (!size.isValid() || size.width() > 4096 || size.height() > 4096) {
    return {};
  }
  reader.setScaledSize(size.scaled(300, 450, Qt::KeepAspectRatio));
  return QPixmap::fromImage(reader.read());
}

} // namespace

GameArtwork::GameArtwork(QObject* parent) : QObject(parent) {}

QString GameArtwork::identify(const QString& gameShortName, const QString& gameName,
                              const QString& steamId)
{
  QString artworkKey;
  // Keep artwork metadata separate from the plugin's installation/launch ID.
  static const QJsonArray catalog = [] {
    QFile file(":/Fluorine/gameartwork.json");
    if (!file.open(QIODevice::ReadOnly)) {
      return QJsonArray{};
    }
    return QJsonDocument::fromJson(file.readAll()).object()["entries"].toArray();
  }();
  QString artworkSteamId = steamId;
  QString curatedKey;
  QUrl curatedUrl;
  const QString identity = gameName;
  for (const auto& entry : catalog) {
    const auto record = entry.toObject();
    if (record["shortName"].toString() != gameShortName ||
        (record.contains("gameName") && record["gameName"].toString() != identity)) {
      continue;
    }
    if (record.contains("steamId")) {
      artworkSteamId = record["steamId"].toString();
    } else {
      curatedKey = record["key"].toString();
      curatedUrl = QUrl(record["url"].toString());
    }
    break;
  }
  // Only exact numeric app IDs can become filenames or network requests.
  static const QRegularExpression validId(QStringLiteral("^[1-9][0-9]{0,9}$"));
  if (validId.match(artworkSteamId).hasMatch()) {
    const QString key = "steam-" + artworkSteamId;
    artworkKey = key;
    m_artworkUrls.insert(key, QUrl("https://cdn.cloudflare.steamstatic.com/steam/apps/" +
                                   artworkSteamId + "/library_600x900.jpg"));
  }
  static const QRegularExpression validKey(QStringLiteral("^sgdb-[1-9][0-9]*$"));
  if (validKey.match(curatedKey).hasMatch() && curatedUrl.scheme() == "https" &&
      curatedUrl.host() == "cdn2.steamgriddb.com") {
    artworkKey = curatedKey;
    m_artworkUrls.insert(curatedKey, curatedUrl);
  }
  return artworkKey;
}

QString GameArtwork::cacheFile(const QString& key) const
{
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
         "/game-artwork/" + key + ".png";
}

QPixmap GameArtwork::localCover(const QString& key) const
{
  QStringList paths{cacheFile(key)};
  const QString id = key.mid(6);
  const QString steam = MOBase::findSteam();
  if (key.startsWith("steam-") && !steam.isEmpty()) {
    paths << steam + "/appcache/librarycache/" + id + "/library_600x900.jpg"
          << steam + "/appcache/librarycache/" + id + "_library_600x900.jpg";
  }
  for (const auto& path : paths) {
    QFile file(path);
    if (file.size() <= MaxArtworkBytes && file.open(QIODevice::ReadOnly)) {
      const auto cover = readCover(&file);
      if (!cover.isNull()) {
        return cover;
      }
    }
  }
  return {};
}

void GameArtwork::request(const QString& key)
{
  if (key.isEmpty() || !m_artworkUrls.contains(key) || m_requested.contains(key)) {
    return;
  }
  m_requested.insert(key);
  const auto cover = localCover(key);
  if (!cover.isNull()) {
    emit coverReady(key, cover);
  } else {
    m_pending.enqueue(key);
    fetchNext();
  }
}

void GameArtwork::fetchNext()
{
  while (m_downloadsEnabled && m_active < 4 && !m_pending.isEmpty()) {
    const QString id = m_pending.dequeue();
    QNetworkRequest request(m_artworkUrls.value(id));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Fluorine-Manager");
    request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_network.get(request);
    ++m_active;
    // A wall-clock deadline also bounds servers that keep trickling bytes.
    QTimer::singleShot(10000, reply, [reply] {
      if (!reply->isFinished()) {
        reply->abort();
      }
    });
    connect(reply, &QNetworkReply::readyRead, reply, [reply] {
      if (reply->bytesAvailable() > MaxArtworkBytes) {
        reply->abort();
      }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
      if (reply->error() == QNetworkReply::NoError && reply->isOpen() &&
          reply->bytesAvailable() <= MaxArtworkBytes) {
        const QByteArray data = reply->readAll();
        QBuffer buffer;
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        const auto cover = readCover(&buffer);
        if (!cover.isNull()) {
          emit coverReady(id, cover);
          const QString path = cacheFile(id);
          QDir().mkpath(QFileInfo(path).absolutePath());
          QSaveFile file(path);
          if (file.open(QIODevice::WriteOnly) && cover.save(&file, "PNG")) {
            file.commit();
          }
        }
      }
      reply->deleteLater();
      --m_active;
      fetchNext();
    });
  }
}

void GameArtwork::setDownloadsEnabled(bool enabled)
{
  m_downloadsEnabled = enabled;
  if (enabled) {
    fetchNext();
  }
}
