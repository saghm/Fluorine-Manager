#include "clf3processcontroller.h"
#include "clf3processenvironment.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

Clf3ProcessController::Clf3ProcessController(QObject* parent)
    : QObject(parent)
{
  connect(&m_engineManager, &Clf3EngineManager::ready, this, [this](const QString& path) {
    m_preparing = false;
    m_managedEnginePath = path;
    m_process.start(path, m_arguments, QIODevice::ReadWrite);
  });
  connect(&m_engineManager, &Clf3EngineManager::statusChanged, this, [this](const QString& status) {
    emit statusChanged(status);
    emit logLine(status);
  });
  connect(&m_engineManager, &Clf3EngineManager::failed, this, [this](const QString& reason) {
    m_preparing = false;
    emit failed(reason);
  });
  connect(&m_engineManager, &Clf3EngineManager::cancelled, this, [this] {
    m_preparing = false;
    emit cancelled();
  });
  m_process.setProcessChannelMode(QProcess::SeparateChannels);
  connect(&m_process, &QProcess::readyReadStandardOutput, this,
          &Clf3ProcessController::consumeStdout);
  connect(&m_process, &QProcess::readyReadStandardError, this,
          &Clf3ProcessController::consumeStderr);
  m_cancelTimer.setSingleShot(true);
  m_killTimer.setSingleShot(true);
  m_handshakeTimer.setSingleShot(true);
  connect(&m_cancelTimer, &QTimer::timeout, this, [this] {
    if (isRunning() && m_cancelRequested) {
      m_process.terminate();
      m_killTimer.start(2000);
    }
  });
  connect(&m_killTimer, &QTimer::timeout, this, [this] {
    if (isRunning() && m_cancelRequested) m_process.kill();
  });
  connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
    m_failure = tr("CLF3 did not respond to the startup handshake within 30 seconds.");
    m_completed = true;
    m_process.kill();
  });
  connect(&m_process, &QProcess::started, this, [this] {
    if (m_cancelRequested) {
      if (!m_collectionPlanning) send({{"type", "cancel"}});
    } else {
      m_handshakeTimer.start(30000);
    }
  });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            // Crashes also emit finished(); report one terminal result, after exit.
            if (error != QProcess::FailedToStart) return;
            m_cancelTimer.stop();
            m_killTimer.stop();
            m_handshakeTimer.stop();
            m_completed = true;
            if (m_cancelRequested) emit cancelled();
            else emit failed(tr("CLF3 could not be started: %1").arg(m_process.errorString()));
          });
  connect(&m_process,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            consumeStdout();
            consumeStderr();
            m_cancelTimer.stop();
            m_killTimer.stop();
            m_handshakeTimer.stop();
            if (m_cancelRequested) {
              emit cancelled();
            } else if (!m_failure.isEmpty()) {
              emit failed(m_failure);
            } else if (status != QProcess::NormalExit || code != 0 || !m_completed) {
              emit failed(status == QProcess::CrashExit
                              ? tr("CLF3 crashed during installation.")
                              : tr("CLF3 exited with code %1 without a successful installation.").arg(code));
            } else {
              if (m_collectionPlanning) emit collectionPlanReady(m_result);
              else emit completed(m_result);
            }
          });
}

QString Clf3ProcessController::enginePath() const
{
  const QString override = qEnvironmentVariable("FLUORINE_CLF3_PATH");
  if (!override.isEmpty()) return QFileInfo(override).absoluteFilePath();
  if (!m_managedEnginePath.isEmpty()) return m_managedEnginePath;
  return m_engineManager.cachedEnginePath();
}

bool Clf3ProcessController::isRunning() const
{
  return m_preparing || m_process.state() != QProcess::NotRunning;
}

QStringList Clf3ProcessController::buildInstallArguments(
    const QString& source, const QString& downloads, const QString& output,
    const QString& game, const QString& machineName, const Clf3Tuning& tuning)
{
  QStringList arguments{QStringLiteral("install"), source, downloads, output};
  if (!game.isEmpty()) arguments << QStringLiteral("--game") << game;
  arguments << QStringLiteral("--jackify") << QStringLiteral("--hosted");
  if (!machineName.isEmpty())
    arguments << QStringLiteral("--machine-name") << machineName;
  if (tuning.concurrentDownloads && *tuning.concurrentDownloads >= 1)
    arguments << QStringLiteral("--concurrent")
              << QString::number(*tuning.concurrentDownloads);
  if (tuning.installWorkers && *tuning.installWorkers >= 1)
    arguments << QStringLiteral("--install-workers")
              << QString::number(*tuning.installWorkers);
  if (tuning.bsaWorkers && *tuning.bsaWorkers >= 1)
    arguments << QStringLiteral("--bsa-workers")
              << QString::number(*tuning.bsaWorkers);
  if (tuning.sevenzipWorkers && *tuning.sevenzipWorkers >= 1)
    arguments << QStringLiteral("--sevenzip-workers")
              << QString::number(*tuning.sevenzipWorkers);
  if (tuning.extractStrategy
      && (*tuning.extractStrategy == QStringLiteral("streaming")
          || *tuning.extractStrategy == QStringLiteral("phased")))
    arguments << QStringLiteral("--extract") << *tuning.extractStrategy;
  return arguments;
}

void Clf3ProcessController::startInstall(const QString& source,
                                         const QString& downloads,
                                         const QString& output,
                                         const QString& game,
                                         const QString& machineName,
                                         const Clf3Tuning& tuning)
{
  begin(buildInstallArguments(source, downloads, output, game, machineName,
                              tuning),
        false);
}

QProcessEnvironment Clf3ProcessController::engineEnvironment()
{
  return clf3EngineEnvironment();
}

void Clf3ProcessController::startCollectionPlan(const QString& sourceUrl,
                                               const QString& gameVersion,
                                               bool allOptional)
{
  QStringList arguments{"collection", "hosted-plan", sourceUrl};
  if (!gameVersion.isEmpty()) arguments << "--game-version" << gameVersion;
  if (allOptional) arguments << "--all-optional";
  begin(arguments, true);
}

void Clf3ProcessController::begin(const QStringList& arguments, bool collectionPlanning)
{
  if (isRunning()) return;
  m_stdoutBuffer.clear();
  m_stderrBuffer.clear();
  m_result = {};
  m_failure.clear();
  m_cancelTimer.stop();
  m_killTimer.stop();
  m_handshakeTimer.stop();
  m_completed       = false;
  m_cancelRequested = false;
  m_collectionPlanning = collectionPlanning;
  m_collectionJob.clear();
  m_collectionRequest.clear();
  m_collectionPackageSent = false;
  m_process.setProcessEnvironment(engineEnvironment());
  m_arguments = arguments;
  if (!qEnvironmentVariableIsEmpty("FLUORINE_CLF3_PATH")) {
    m_process.start(enginePath(), m_arguments, QIODevice::ReadWrite);
  } else {
    m_preparing = true;
    m_engineManager.prepare();
  }
}

void Clf3ProcessController::sendCollectionPackage(const QString& jobId,
                                                   const QString& requestId,
                                                   const QJsonObject& locator,
                                                   int schemaId,
                                                   const QString& packagePath)
{
  if (!m_collectionPlanning || m_cancelRequested || m_completed || jobId != m_collectionJob
      || requestId != m_collectionRequest || requestId.isEmpty() || m_collectionPackageSent) return;
  send({{"type", "collection_package_result"}, {"job_id", jobId},
        {"request_id", requestId}, {"locator", locator}, {"schema_id", schemaId},
        {"package_path", packagePath}});
  m_collectionPackageSent = true;
}

void Clf3ProcessController::rejectCollectionRequest(const QString& jobId,
                                                    const QString& requestId)
{
  if (!m_collectionPlanning || m_cancelRequested || m_completed || jobId != m_collectionJob
      || requestId != m_collectionRequest || requestId.isEmpty() || m_collectionPackageSent) return;
  send({{"type", "collection_request_failed"}, {"job_id", jobId}, {"request_id", requestId}});
  m_collectionPackageSent = true;
}

void Clf3ProcessController::sendNexusUrls(const QString& requestId,
                                          const QStringList& urls)
{
  QJsonArray jsonUrls;
  for (const auto& url : urls) jsonUrls.push_back(url);
  send({{"type", "download_authorization_result"},
        {"request_id", requestId},
        {"urls", jsonUrls}});
}

void Clf3ProcessController::sendManualFile(const QString& requestId,
                                           const QString& path)
{
  send({{"type", "manual_download_result"},
        {"request_id", requestId},
        {"path", path}});
}

void Clf3ProcessController::rejectRequest(const QString& requestId,
                                          const QString& reason)
{
  send({{"type", "authorization_failed"},
        {"request_id", requestId},
        {"error", reason}});
}

void Clf3ProcessController::cancel()
{
  if (!isRunning() || m_cancelRequested) return;
  m_cancelRequested = true;
  if (m_preparing) {
    m_engineManager.cancel();
    return;
  }
  m_handshakeTimer.stop();
  if (m_collectionPlanning) {
    if (!m_collectionJob.isEmpty()) send({{"type", "cancel"}, {"job_id", m_collectionJob}});
  } else send({{"type", "cancel"}});
  m_cancelTimer.start(5000);
}

void Clf3ProcessController::consumeStdout()
{
  m_stdoutBuffer += m_process.readAllStandardOutput();
  // An engine may exit without a trailing newline on its final event.
  if (m_process.state() == QProcess::NotRunning && !m_stdoutBuffer.isEmpty()
      && !m_stdoutBuffer.endsWith('\n')) m_stdoutBuffer += '\n';
  qsizetype newline = -1;
  while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
    const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
    m_stdoutBuffer.remove(0, newline + 1);
    if (line.isEmpty()) continue;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      emit logLine(tr("Invalid CLF3 protocol line: %1").arg(QString::fromUtf8(line)));
      continue;
    }
    handleEvent(document.object());
  }
}

void Clf3ProcessController::consumeStderr()
{
  m_stderrBuffer += m_process.readAllStandardError();
  if (m_process.state() == QProcess::NotRunning && !m_stderrBuffer.isEmpty()
      && !m_stderrBuffer.endsWith('\n')) m_stderrBuffer += '\n';
  qsizetype newline;
  while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
    const QByteArray line = m_stderrBuffer.left(newline);
    m_stderrBuffer.remove(0, newline + 1);
    if (!line.isEmpty()) emit logLine(QString::fromUtf8(line));
  }
}

void Clf3ProcessController::handleEvent(const QJsonObject& event)
{
  if (m_completed) return;
  const QString type = event.value("type").toString();
  if (m_cancelRequested) {
    // A cancellation during process startup must still unblock the host handshake.
    if (type == "hello") {
      if (m_collectionPlanning) {
        m_collectionJob = event.value("job_id").toString();
        send({{"type", "cancel"}, {"job_id", m_collectionJob}});
      } else {
        send({{"type", "hello_ack"}, {"protocol_version", ProtocolVersion}});
        send({{"type", "cancel"}});
      }
    }
    return;
  }
  if (m_collectionPlanning && type != "hello" && !type.startsWith("collection_")) {
    m_failure = tr("CLF3 returned an event outside the negotiated Collections protocol.");
    m_completed = true;
    m_process.kill();
    return;
  }
  if (type == "hello") {
    if (m_collectionPlanning && !m_collectionJob.isEmpty()) {
      m_failure = tr("CLF3 repeated the Collections handshake.");
      m_completed = true;
      m_process.kill();
      return;
    }
    m_handshakeTimer.stop();
    const int protocol = event.value("protocol_version").toInt();
    if (protocol != ProtocolVersion) {
      m_failure = tr("CLF3 protocol %1 is incompatible with Fluorine protocol %2.")
                      .arg(protocol).arg(ProtocolVersion);
      m_completed = true;
      m_process.kill();
      return;
    }
    if (m_collectionPlanning) {
      m_collectionJob = event.value("job_id").toString();
      if (m_collectionJob.isEmpty() || !event.value("capabilities").toArray().contains("collection_plan_v1")) {
        m_failure = tr("This CLF3 engine does not support collection planning.");
        m_completed = true;
        m_process.kill();
        return;
      }
      send({{"type", "hello_ack"}, {"protocol_version", ProtocolVersion},
            {"capabilities", QJsonArray{"collection_plan_v1"}}});
    } else send({{"type", "hello_ack"}, {"protocol_version", ProtocolVersion}});
    emit engineReady(event.value("engine_version").toString());
  } else if (m_collectionPlanning && type.startsWith("collection_")) {
    if (event.value("job_id").toString() != m_collectionJob || m_collectionJob.isEmpty()) {
      m_failure = tr("CLF3 returned a collection event for a different job.");
      m_completed = true;
      m_process.kill();
    } else if (type == "collection_revision_required") {
      if (!m_collectionRequest.isEmpty()) {
        m_failure = tr("CLF3 sent an overlapping collection request.");
        m_completed = true;
        m_process.kill();
        return;
      }
      m_collectionRequest = event.value("request_id").toString();
      if (m_collectionRequest.isEmpty()) {
        m_failure = tr("CLF3 returned a collection request without an identity.");
        m_completed = true;
        m_process.kill();
        return;
      }
      emit collectionRevisionRequired(m_collectionJob, m_collectionRequest,
                                      event.value("locator").toObject());
    } else if (type == "collection_plan_ready") {
      if (!m_collectionPackageSent || event.value("request_id").toString() != m_collectionRequest
          || !event.value("plan").isObject()
          || event.value("plan").toObject().value("plan_schema_version").toInt() != 1) {
        m_failure = tr("CLF3 returned an invalid or unsolicited collection plan.");
        m_completed = true;
        m_process.kill();
        return;
      }
      m_completed = true;
      m_result = event.value("plan").toObject();
    } else if (type == "collection_failed") {
      m_failure = event.value("message").toString(tr("Collection planning failed."));
      m_completed = true;
    } else if (type == "collection_cancelled") {
      m_cancelRequested = true;
      m_completed = true;
    }
  } else if (type == "PhaseChange" || type == "phase_changed") {
    emit phaseChanged(event.value("phase").toString());
  } else if (type == "plan_ready") {
    emit statusChanged(tr("Installing %1 · %2 archives")
                           .arg(event.value("name").toString())
                           .arg(event.value("archive_count").toInt()));
    for (const auto& value : event.value("artifacts").toArray()) {
      const auto artifact = value.toObject();
      emit itemMetadata(artifact.value("name").toString(),
                        artifact.value("display_name").toString(),
                        artifact.value("subtitle").toString(),
                        artifact.value("image_url").toString());
    }
  } else if (type == "Status" || type == "status") {
    emit statusChanged(event.value("message").toString());
  } else if (type == "DownloadProgress" || type == "artifact_progress") {
    emit artifactProgress(event.value("name").toString(),
                          event.value("downloaded").toVariant().toLongLong(),
                          event.value("total").toVariant().toLongLong(),
                          event.value("speed").toDouble());
  } else if (type == "item_started") {
    emit itemStarted(event.value("item_id").toString(),
                     event.value("name").toString(),
                     event.value("display_name").toString(),
                     event.value("subtitle").toString(),
                     event.value("stage").toString(),
                     event.value("image_url").toString(),
                     event.value("total").toVariant().toLongLong(),
                     event.value("unit").toString());
  } else if (type == "item_progress") {
    emit itemProgress(event.value("item_id").toString(),
                      event.value("completed").toVariant().toLongLong(),
                      event.value("total").toVariant().toLongLong(),
                      event.value("speed").toDouble(),
                      event.value("unit").toString());
  } else if (type == "item_message") {
    emit itemMessage(event.value("item_id").toString(),
                     event.value("message").toString());
  } else if (type == "item_completed") {
    emit itemCompleted(event.value("item_id").toString());
  } else if (type == "item_failed") {
    emit itemFailed(event.value("item_id").toString(),
                    event.value("message").toString());
  } else if (type == "ArchiveComplete" || type == "overall_progress") {
    emit overallProgress(event.value("index").toInt(), event.value("total").toInt());
  } else if (type == "download_authorization_required") {
    emit nexusAuthorizationRequired(
        event.value("request_id").toString(), event.value("archive_name").toString(),
        event.value("domain").toString(), event.value("mod_id").toInt(),
        event.value("file_id").toInt(),
        event.value("expected_size").toVariant().toLongLong());
  } else if (type == "manual_download_required") {
    emit manualDownloadRequired(
        event.value("request_id").toString(), event.value("archive_name").toString(),
        event.value("url").toString(), event.value("prompt").toString(),
        event.value("expected_size").toVariant().toLongLong(),
        event.value("expected_hash").toString());
  } else if (type == "install_completed") {
    m_completed = true;
    QJsonObject stats = event.value("stats").toObject();
    const QString gamePath = event.value("game_path").toString();
    if (!gamePath.isEmpty()) stats.insert(QStringLiteral("game_path"), gamePath);
    m_result = stats;
  } else if (type == "install_failed") {
    m_completed = true;
    m_failure = event.value("message").toString(tr("CLF3 installation failed."));
    if (m_failure.isEmpty()) m_failure = tr("CLF3 installation failed.");
  } else if (type == "protocol_warning") {
    emit logLine(event.value("message").toString());
  }
}

void Clf3ProcessController::send(const QJsonObject& command)
{
  if (!isRunning()) return;
  m_process.write(QJsonDocument(command).toJson(QJsonDocument::Compact));
  m_process.write("\n");
}
