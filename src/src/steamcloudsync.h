#pragma once

#include <QJsonObject>
#include <QString>
#include <QProcessEnvironment>
#include <memory>

class QWidget;
class QLockFile;
class QProgressDialog;

namespace SteamCloud {
inline constexpr auto setting = "fluorine/steam_cloud_enabled";
bool supported(const QString& appId, const QString& executable);
bool validAccount(const QString& steamId);
QProcessEnvironment clientEnvironment(QProcessEnvironment environment);

// Recognizes only this app's new log records, not a stale UI "synced" label.
class JobLog {
public:
  explicit JobLog(bool upload) : m_upload(upload) {}
  void consume(const QByteArray& data);
  bool started() const { return m_started; }
  bool complete() const { return m_complete && m_error.isEmpty(); }
  QString error() const { return m_error; }
private:
  bool m_upload;
  bool m_started{false};
  bool m_complete{false};
  QByteArray m_buffer;
  QString m_error;
};

// Does not follow/replace an unrelated symlink. Existing folders are renamed,
// never deleted. Both endpoints must be the Cyberpunk save-folder leaf.
QString mapSaveFolder(const QString& source, const QString& target);
QString snapshotSaves(const QString& source, const QString& backupRoot);
bool cacheMatchesSaves(const QString& source, const QByteArray& cache);
bool saveCollectionsMatch(const QString& first, const QString& second);
QString replaceSaveCollection(const QString& source, const QString& destination);
bool matchingJournal(const QJsonObject& journal, const QString& account,
                     const QString& source);
}

// Per-launch session held until the full game process tree and VFS cleanup finish.
// Native Steam + Cyberpunk only; unsupported configurations fail closed.
class SteamCloudSync
{
public:
  SteamCloudSync(QWidget* parent, QString settingsFile, QString gameDirectory,
                  QString saveDirectory);
  ~SteamCloudSync();
  bool prepare();
  bool markLaunching();
  bool finish(bool cleanExit);
  static bool optIn(QWidget* parent, const QString& settingsFile);

private:
  QWidget* m_parent;
  QString m_settingsFile, m_gameDirectory, m_source, m_target;
  QString m_root, m_account, m_journalPath;
  qint64 m_launchLogOffset{-1};
  std::unique_ptr<QLockFile> m_lock;
  bool m_launched{false};
  QJsonObject evaluate(const QString& expression);
  void ensureClient();
  QString currentAccount();
  QJsonObject cloudState();
  void checkAccountAndCloud(bool requireSynchronized = false);
  void sync(bool upload, QProgressDialog* progress = nullptr, qint64 logStart = -1);
  QString cachePath() const;
  QJsonObject journal() const;
  void saveJournal(bool pending, const QString& status);
  void displayFailure(const QString& message);
};
