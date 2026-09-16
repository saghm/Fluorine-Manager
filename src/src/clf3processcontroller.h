#pragma once

#include "clf3enginemanager.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

class Clf3ProcessController : public QObject
{
  Q_OBJECT

public:
  static constexpr int ProtocolVersion = 1;

  explicit Clf3ProcessController(QObject* parent = nullptr);

  QString enginePath() const;
  bool isRunning() const;
  void startInstall(const QString& source, const QString& downloads,
                    const QString& output, const QString& game,
                    const QString& machineName = {});
  void sendNexusUrls(const QString& requestId, const QStringList& urls);
  void sendManualFile(const QString& requestId, const QString& path);
  void startCollectionPlan(const QString& sourceUrl, const QString& gameVersion = {},
                           bool allOptional = false);
  void sendCollectionPackage(const QString& jobId, const QString& requestId,
                             const QJsonObject& locator, int schemaId,
                             const QString& packagePath);
  void rejectCollectionRequest(const QString& jobId, const QString& requestId);
  static QProcessEnvironment engineEnvironment();
  void rejectRequest(const QString& requestId, const QString& reason);
  void cancel();

signals:
  void engineReady(QString version);
  void phaseChanged(QString phase);
  void statusChanged(QString status);
  void itemMetadata(QString name, QString displayName, QString subtitle,
                    QString imageUrl);
  void artifactProgress(QString name, qint64 received, qint64 total,
                        double bytesPerSecond);
  void itemStarted(QString itemId, QString name, QString displayName,
                   QString subtitle, QString stage, QString imageUrl,
                   qint64 total, QString unit);
  void itemProgress(QString itemId, qint64 completed, qint64 total,
                    double bytesPerSecond, QString unit);
  void itemMessage(QString itemId, QString message);
  void itemCompleted(QString itemId);
  void itemFailed(QString itemId, QString message);
  void overallProgress(int completed, int total);
  void nexusAuthorizationRequired(QString requestId, QString archiveName,
                                   QString domain, int modId, int fileId,
                                   qint64 expectedSize);
  void manualDownloadRequired(QString requestId, QString archiveName,
                              QString url, QString prompt,
                              qint64 expectedSize, QString expectedHash);
  void logLine(QString line);
  void completed(QJsonObject stats);
  void failed(QString reason);
  void cancelled();
  void collectionRevisionRequired(QString jobId, QString requestId, QJsonObject locator);
  void collectionPlanReady(QJsonObject plan);

private:
  Clf3EngineManager m_engineManager;
  bool m_preparing{false};
  QString m_managedEnginePath;
  QStringList m_arguments;
  QProcess m_process;
  QTimer m_cancelTimer;
  QTimer m_killTimer;
  QTimer m_handshakeTimer;
  QByteArray m_stdoutBuffer;
  QByteArray m_stderrBuffer;
  QJsonObject m_result;
  QString m_failure;
  bool m_completed{false};
  bool m_cancelRequested{false};
  bool m_collectionPlanning{false};
  QString m_collectionJob;
  QString m_collectionRequest;
  bool m_collectionPackageSent{false};

  void consumeStdout();
  void consumeStderr();
  void handleEvent(const QJsonObject& event);
  void send(const QJsonObject& command);
  void begin(const QStringList& arguments, bool collectionPlanning);
};
