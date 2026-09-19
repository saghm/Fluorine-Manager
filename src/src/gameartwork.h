#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QQueue>
#include <QSet>

// Shared by game selection and setup details. Artwork never affects game detection.
class GameArtwork : public QObject
{
  Q_OBJECT
public:
  explicit GameArtwork(QObject* parent = nullptr);
  QString identify(const QString& gameShortName, const QString& gameName,
                   const QString& steamId);
  QPixmap localCover(const QString& key) const;
  void request(const QString& key);
  void setDownloadsEnabled(bool enabled);

signals:
  void coverReady(const QString& key, const QPixmap& cover);

private:
  QNetworkAccessManager m_network;
  QHash<QString, QUrl> m_artworkUrls;
  QSet<QString> m_requested;
  QQueue<QString> m_pending;
  int m_active = 0;
  bool m_downloadsEnabled = true;

  QString cacheFile(const QString& key) const;
  void fetchNext();
};
