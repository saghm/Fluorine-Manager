#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>

class QNetworkReply;

// All authenticated API access and resolved download URLs live in this host.
// Workers receive only pinned plans and verified local file paths.
class Clf3CollectionHost : public QObject
{
  Q_OBJECT
public:
  using JsonResult = std::function<void(QJsonObject)>;
  using FileResult = std::function<void(QString)>;
  using AuthRequest = std::function<QNetworkReply*(const QUrl&, const QByteArray&)>;
  explicit Clf3CollectionHost(AuthRequest auth,
                              QObject* parent = nullptr,
                              QNetworkAccessManager* publicNetwork = nullptr);
  ~Clf3CollectionHost() override;
  void cancel();
  void games(JsonResult result);
  void thumbnail(const QUrl& url, std::function<void(QImage)> result, int redirects = 0);
  static bool thumbnailUrlAllowed(const QUrl& url);
  void search(const QString& game,
              const QString& text,
              int page,
              const QString& sort,
              bool hideAdult,
              JsonResult result);
  void package(const QJsonObject& locator, const QString& job, JsonResult result);
  void artifact(const QJsonObject& artifact,
                const QUrl& direct,
                const QString& cache,
                const QString& nxm,
                FileResult result);
  void masterlist(const QJsonObject& game, const QString& target, FileResult result);
  void verify(const QString& path,
              const QByteArray& hash,
              qint64 size,
              bool sha256,
              std::function<void(bool)> result);
  static QJsonObject locator(const QString& source);
  static QString sourceUrl(const QJsonObject& locator);
  static bool apiPathAllowed(const QString& path);
  static bool archiveUrlAllowed(const QUrl& url);
  static QJsonObject searchBody(const QString& game,
                                const QString& text,
                                int page,
                                const QString& sort,
                                bool hideAdult);

signals:
  void failed(QString message);
  void progress(qint64 received, qint64 total);

private:
  AuthRequest m_auth;
  QNetworkAccessManager* m_public;
  QSet<QNetworkReply*> m_replies;
  std::shared_ptr<std::atomic_bool> m_cancelled;
  void json(const QString& path, const QJsonObject& body, bool authenticated, JsonResult result);
  void transfer(const QStringList& urls,
                const QString& path,
                const QByteArray& hash,
                qint64 size,
                bool sha256,
                FileResult result,
                int redirectCount = 0);
};
