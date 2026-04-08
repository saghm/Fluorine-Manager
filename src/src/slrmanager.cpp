#include "slrmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QTimer>
#include <QEventLoop>
#include <uibase/log.h>

namespace {

const char* BASE_URL     = "https://repo.steampowered.com/steamrt3/images/latest-public-beta";
const char* ARCHIVE_NAME = "SteamLinuxRuntime_sniper.tar.xz";
const char* EXTRACTED_DIR = "SteamLinuxRuntime_sniper";

QString slrInstallDir()
{
  return QDir::homePath() + "/.local/share/fluorine/steamrt";
}

QString slrRunScriptPath()
{
  return slrInstallDir() + "/" + EXTRACTED_DIR + "/run";
}

QString localBuildIdPath()
{
  return slrInstallDir() + "/BUILD_ID.txt";
}

/// Blocking HTTP GET that returns the response body as QByteArray.
QByteArray httpGet(const QString& url, const int* cancelFlag,
                   const std::function<void(float)>& progressCb = nullptr,
                   const QString& destFile = {})
{
  QNetworkAccessManager mgr;
  QNetworkReply* reply = mgr.get(QNetworkRequest(QUrl(url)));
  QEventLoop loop;

  QFile outFile;
  if (!destFile.isEmpty()) {
    outFile.setFileName(destFile);
    if (!outFile.open(QIODevice::WriteOnly))
      return {};
  }

  qint64 totalBytes = -1;
  qint64 received   = 0;

  QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
    if (totalBytes < 0)
      totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    QByteArray chunk = reply->readAll();
    received += chunk.size();
    if (outFile.isOpen())
      outFile.write(chunk);
    if (progressCb && totalBytes > 0)
      progressCb(static_cast<float>(received) / static_cast<float>(totalBytes));
  });

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  // Poll cancel flag via a timer.
  QTimer cancelTimer;
  if (cancelFlag) {
    QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
      if (*cancelFlag != 0) {
        reply->abort();
        loop.quit();
      }
    });
    cancelTimer.start(200);
  }

  loop.exec();

  if (outFile.isOpen())
    outFile.close();

  if (reply->error() != QNetworkReply::NoError) {
    reply->deleteLater();
    if (!destFile.isEmpty())
      QFile::remove(destFile);
    return {};
  }

  QByteArray body;
  if (destFile.isEmpty())
    body = reply->readAll();  // small responses fully buffered
  reply->deleteLater();
  return body;
}

}  // namespace

bool isSlrInstalled()
{
  const QString script = slrRunScriptPath();
  QFileInfo fi(script);
  return fi.exists() && fi.isExecutable();
}

QString getSlrRunScript()
{
  return isSlrInstalled() ? slrRunScriptPath() : QString();
}

QString downloadSlr(const std::function<void(float)>& progressCb,
                    const std::function<void(const QString&)>& statusCb,
                    const int* cancelFlag)
{
  auto status = [&](const QString& msg) { if (statusCb) statusCb(msg); };
  auto progress = [&](float p) { if (progressCb) progressCb(p); };

  // 1. Check for updates.
  status(QStringLiteral("Checking Steam Linux Runtime version..."));

  const QByteArray remoteBuildIdRaw = httpGet(
      QStringLiteral("%1/BUILD_ID.txt").arg(QLatin1String(BASE_URL)), cancelFlag);
  if (remoteBuildIdRaw.isEmpty())
    return QStringLiteral("Failed to fetch SLR BUILD_ID");

  const QString remoteBuildId = QString::fromUtf8(remoteBuildIdRaw).trimmed();

  // Read local BUILD_ID.
  QString localBuildId;
  {
    QFile f(localBuildIdPath());
    if (f.open(QIODevice::ReadOnly))
      localBuildId = QString::fromUtf8(f.readAll()).trimmed();
  }

  if (localBuildId == remoteBuildId && isSlrInstalled()) {
    MOBase::log::info("Steam Linux Runtime is already up to date");
    status(QStringLiteral("Steam Linux Runtime is already up to date"));
    progress(1.0f);
    return {};
  }

  MOBase::log::info("Downloading Steam Linux Runtime (BUILD_ID: {})", remoteBuildId);

  const QString installDir = slrInstallDir();
  QDir().mkpath(installDir);
  const QString archivePath = installDir + "/" + ARCHIVE_NAME;

  // 2. Download.
  status(QStringLiteral("Downloading Steam Linux Runtime (sniper, ~180 MB)..."));
  httpGet(QStringLiteral("%1/%2").arg(QLatin1String(BASE_URL), QLatin1String(ARCHIVE_NAME)),
          cancelFlag, progress, archivePath);
  progress(1.0f);

  if (!QFileInfo::exists(archivePath))
    return QStringLiteral("Download failed or was cancelled");

  // 3. Extract.
  status(QStringLiteral("Extracting Steam Linux Runtime..."));
  const QString extractedDir = installDir + "/" + EXTRACTED_DIR;
  if (QFileInfo::exists(extractedDir))
    QDir(extractedDir).removeRecursively();

  QProcess tar;
  tar.setWorkingDirectory(installDir);
  tar.start(QStringLiteral("tar"), {QStringLiteral("xJf"), archivePath});
  tar.waitForFinished(600000);
  QFile::remove(archivePath);

  if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0)
    return QStringLiteral("tar extraction failed (exit code %1)").arg(tar.exitCode());

  if (!QFileInfo::exists(slrRunScriptPath()))
    return QStringLiteral("Extraction succeeded but run script not found");

  // 4. Save BUILD_ID.
  {
    QFile f(localBuildIdPath());
    if (f.open(QIODevice::WriteOnly))
      f.write(remoteBuildId.toUtf8());
  }

  MOBase::log::info("Steam Linux Runtime installed successfully");
  status(QStringLiteral("Steam Linux Runtime ready"));
  return {};
}
