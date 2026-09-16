#include "clf3galleryloader.h"
#include "clf3processenvironment.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

Clf3GalleryLoader::Clf3GalleryLoader(QObject* parent,
                                   QNetworkAccessManager* network,
                                   const QString& cacheRoot)
    : QObject(parent), m_engine(nullptr, network, cacheRoot)
{
  connect(&m_engine, &Clf3EngineManager::ready, this, &Clf3GalleryLoader::start);
  connect(&m_engine, &Clf3EngineManager::statusChanged,
          this, &Clf3GalleryLoader::statusChanged);
  connect(&m_engine, &Clf3EngineManager::failed, this, &Clf3GalleryLoader::fail);
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
    m_output += m_process.readAllStandardOutput();
  });
  // Do not retain an unbounded stderr buffer or expose command output containing URLs.
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
    m_process.readAllStandardError();
  });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      fail(tr("CLF3 could not start: %1").arg(m_process.errorString()));
  });
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int code, QProcess::ExitStatus status) {
    if (!m_busy) return;
    m_output += m_process.readAllStandardOutput();
    if (status != QProcess::NormalExit || code != 0) {
      fail(tr("CLF3 gallery command failed (exit code %1). Please retry.").arg(code));
      return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(m_output, &error);
    if (error.error != QJsonParseError::NoError
        || (!document.isArray()
            && !(document.isObject() && document.object().value("modlists").isArray()))) {
      fail(tr("CLF3 returned invalid gallery data. Please refresh or update CLF3."));
      return;
    }
    m_timeout.stop();
    m_busy = false;
    emit loaded(document);
  });
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    fail(tr("Loading the Wabbajack gallery timed out. Please retry."));
    m_process.kill();
  });
}

Clf3GalleryLoader::~Clf3GalleryLoader()
{
  cancel();
}

void Clf3GalleryLoader::load(bool refresh)
{
  if (m_busy || m_process.state() != QProcess::NotRunning) return;
  m_busy = true;
  m_refresh = refresh;
  m_output.clear();
  const QString override = qEnvironmentVariable("FLUORINE_CLF3_PATH");
  if (!override.isEmpty()) start(QFileInfo(override).absoluteFilePath());
  else m_engine.prepare();
}

void Clf3GalleryLoader::start(const QString& path)
{
  if (!m_busy) return;
  emit statusChanged(tr("Loading the Wabbajack gallery…"));
  QStringList arguments{"gallery", "--host-metadata"};
  if (m_refresh) arguments << "--refresh";
  m_timeout.start(180000);
  m_process.setProcessEnvironment(clf3EngineEnvironment());
  m_process.start(path, arguments);
}

void Clf3GalleryLoader::fail(const QString& message)
{
  if (!m_busy) return;
  m_timeout.stop();
  m_busy = false;
  emit failed(message);
}

void Clf3GalleryLoader::cancel()
{
  m_busy = false;
  m_timeout.stop();
  m_engine.cancel();
  if (m_process.state() != QProcess::NotRunning) m_process.kill();
}
