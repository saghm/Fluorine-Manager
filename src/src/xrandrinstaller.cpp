#include "xrandrinstaller.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

namespace {
constexpr qint64 maxPayload = 16 * 1024 * 1024;

// Debian packages use the simple ar format: an eight-byte signature followed
// by fixed-width headers and even-byte-aligned members. Read just data.tar.*;
// filenames from the package are never used as extraction destinations.
QString readPayload(QFile& package, QByteArray& payload)
{
  if (!package.open(QIODevice::ReadOnly))
    return QStringLiteral("Cannot open xrandr package: %1").arg(package.errorString());
  if (package.read(8) != "!<arch>\n")
    return QStringLiteral("The xrandr download is not a Debian package");
  while (!package.atEnd()) {
    const auto header = package.read(60);
    bool validSize = false;
    const auto size = header.mid(48, 10).trimmed().toLongLong(&validSize);
    if (header.size() != 60 || header.right(2) != "`\n" || !validSize ||
        size < 0 || size > package.size() - package.pos())
      return QStringLiteral("The xrandr package is truncated or invalid");
    auto name = header.left(16).trimmed();
    if (name.endsWith('/')) name.chop(1);
    if (name == "data.tar" || name == "data.tar.xz" || name == "data.tar.gz" ||
        name == "data.tar.zst" || name == "data.tar.bz2") {
      if (!payload.isEmpty() || size <= 0 || size > maxPayload)
        return QStringLiteral("The xrandr package has an invalid data archive");
      payload = package.read(size);
      if (payload.size() != size)
        return QStringLiteral("Cannot read the xrandr data archive");
    } else if (!package.seek(package.pos() + size)) {
      return QStringLiteral("Cannot read the xrandr package");
    }
    if (size % 2 && package.read(1).size() != 1)
      return QStringLiteral("The xrandr package is truncated");
  }
  return payload.isEmpty() ? QStringLiteral("The xrandr package has no data archive")
                           : QString{};
}
}

QString installXrandrFromDeb(const QString& packagePath, const QString& destination,
                            const int* cancelFlag)
{
  const auto cancelled = [&] { return cancelFlag && *cancelFlag != 0; };
  if (cancelled()) return QStringLiteral("xrandr installation cancelled");
  QFile package(packagePath);
  QByteArray payload;
  if (const auto error = readPayload(package, payload); !error.isEmpty()) return error;

  QTemporaryDir staging;
  if (!staging.isValid()) return QStringLiteral("Cannot create xrandr staging directory");
  QFile archive(staging.filePath("data.tar"));
  if (!archive.open(QIODevice::WriteOnly) || archive.write(payload) != payload.size() ||
      !archive.flush())
    return QStringLiteral("Cannot stage the xrandr data archive: %1").arg(archive.errorString());
  archive.close();

  // tar detects compression by its signature, including xz/gzip/zstd. It is
  // already required for SLR itself. Stream one exact member to a temporary
  // file, so archive paths and links cannot write outside our staging area.
  QProcess tar;
  tar.setStandardOutputFile(staging.filePath("xrandr"));
  tar.start(QStringLiteral("tar"), {"-xOf", archive.fileName(), "./usr/bin/xrandr"});
  if (!tar.waitForStarted())
    return QStringLiteral("Cannot start tar to extract xrandr: %1").arg(tar.errorString());
  QElapsedTimer timer;
  timer.start();
  while (!tar.waitForFinished(100)) {
    if (cancelled() || timer.elapsed() >= 30000 ||
        QFileInfo(staging.filePath("xrandr")).size() > maxPayload) {
      tar.kill();
      tar.waitForFinished();
      return cancelled() ? QStringLiteral("xrandr installation cancelled")
                         : QStringLiteral("xrandr extraction exceeded its time or size limit");
    }
  }
  if (cancelled()) return QStringLiteral("xrandr installation cancelled");
  if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0)
    return QStringLiteral("xrandr extraction failed (tar exit %1): %2")
        .arg(tar.exitCode()).arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed().left(1500));

  QFile executable(staging.filePath("xrandr"));
  if (!executable.open(QIODevice::ReadOnly) || executable.size() > maxPayload ||
      executable.read(4) != QByteArray("\x7f" "ELF", 4))
    return QStringLiteral("The xrandr package did not contain a valid executable");
  if (!executable.seek(0) || !QDir().mkpath(QFileInfo(destination).absolutePath()))
    return QStringLiteral("Cannot prepare the xrandr installation directory");
  QSaveFile output(destination);
  if (QFileInfo(destination).isSymLink())
    return QStringLiteral("Refusing to replace a symbolic link with the xrandr helper");
  const auto bytes = executable.readAll();
  if (executable.error() != QFileDevice::NoError || !output.open(QIODevice::WriteOnly) ||
      output.write(bytes) != bytes.size() ||
      !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                             QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther) ||
      !output.commit())
    return QStringLiteral("Cannot install xrandr: %1").arg(output.errorString());
  return {};
}
