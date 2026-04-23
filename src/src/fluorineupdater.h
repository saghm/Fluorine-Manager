#ifndef FLUORINE_UPDATER_H
#define FLUORINE_UPDATER_H

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Lightweight self-update checker. Queries the GitHub Releases API for
// Fluorine Manager and notifies when a newer build is available. Does not
// perform auto-install — the user is given a download URL to apply manually,
// matching the existing MO2 updater UX.
//
// Two channels:
//   stable: fetches the latest tagged `v*` release and compares against the
//           build's FLUORINE_VERSION_STRING as a semver-ish triple.
//   beta:   fetches the rolling `beta` tag's release and compares the
//           fluorine-meta timestamp/commit block against the installed build.
class FluorineUpdater : public QObject
{
  Q_OBJECT

public:
  enum class Channel
  {
    Stable,
    Beta,
  };

  struct ReleaseInfo
  {
    Channel channel       = Channel::Stable;
    QString tagName;       // "v0.1.5" or "beta"
    QString name;          // release title
    QString htmlUrl;       // release HTML page
    QString downloadUrl;   // first .tar.gz asset URL (may be empty)
    QString timestamp;     // beta only: "YYYYMMDDHHMM"
    QString commit;        // beta only: full commit SHA (from fluorine-meta)
    QString versionString; // stable only: tag minus leading 'v'
  };

  explicit FluorineUpdater(QObject* parent = nullptr);
  ~FluorineUpdater() override;

  // Kick off an async check. Emits updateAvailable()/upToDate()/checkFailed()
  // exactly once per call.
  void checkForUpdates(Channel channel);

  // Build channel that was baked into this binary at compile time. The
  // Settings toggle defaults to this value.
  static Channel buildChannel();

  static QString channelToString(Channel c);
  static Channel channelFromString(const QString& s, Channel fallback);

signals:
  void updateAvailable(const FluorineUpdater::ReleaseInfo& info);
  void upToDate(const FluorineUpdater::ReleaseInfo& info);
  void checkFailed(const QString& reason);

private slots:
  void onReplyFinished();

private:
  bool parseBetaRelease(const QJsonObject& obj, ReleaseInfo& out) const;
  bool parseStableRelease(const QJsonObject& obj, ReleaseInfo& out) const;

  QNetworkAccessManager* m_net;
  QNetworkReply* m_reply = nullptr;
  Channel m_pendingChannel = Channel::Stable;
};

#endif  // FLUORINE_UPDATER_H
