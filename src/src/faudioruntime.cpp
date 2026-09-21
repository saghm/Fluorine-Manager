#include "faudioruntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
QString sha256(const QString& path)
{
  QFile file(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return QString::fromLatin1(hash.result().toHex());
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QStringList moduleNames()
{
  QStringList result;
  const auto append = [&](const QString& family, int first, int last) {
    for (int n = first; n <= last; ++n) result.append(family + QString::number(n) + ".dll");
  };
  append("xaudio2_", 0, 9);
  append("x3daudio1_", 0, 7);
  append("xapofx1_", 1, 5);
  append("xactengine3_", 0, 7);
  for (int n : {0, 4, 7, 9}) result.append("xactengine2_" + QString::number(n) + ".dll");
  result.sort();
  return result;
}
}

bool installFAudioPayload(const QString& prefix, const QString& bundle,
                         FAudioPayload& payload, QString& error)
{
  payload = {};
  error.clear();
  const QString versionPath = bundle + "/version.txt";
  QFile versionFile(versionPath);
  if (!versionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    error = QStringLiteral(
        "Bundled FAudio is missing at %1; rebuild Fluorine with build.sh").arg(bundle);
    return false;
  }
  QString faudioVersion;
  bool nativePack = false;
  for (const QByteArray& line : versionFile.readAll().split('\n')) {
    if (line.startsWith("faudio="))
      faudioVersion = QString::fromLatin1(line.mid(7)).trimmed();
    if (line.trimmed() == "override=native")
      nativePack = true;
  }
  if (!nativePack || !QRegularExpression(QStringLiteral(R"(^[0-9]{2}\.[0-9]{2}$)"))
           .match(faudioVersion).hasMatch()) {
    error = "Invalid bundled FAudio version metadata";
    return false;
  }

  const QStringList dlls32 = QDir(bundle + "/i386-windows")
                                 .entryList({"*.dll"}, QDir::Files, QDir::Name);
  const QStringList dlls64 = QDir(bundle + "/x86_64-windows")
                                 .entryList({"*.dll"}, QDir::Files, QDir::Name);
  if (dlls32 != moduleNames() || dlls32 != dlls64) {
    error = "Bundled FAudio x86/x64 DLL sets are incomplete";
    return false;
  }

  const QString checksumPath = bundle + "/sha256sums.txt";
  QFile checksumFile(checksumPath);
  if (!checksumFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    error = "Bundled FAudio checksums are missing";
    return false;
  }
  const QStringList checksumLines =
      QString::fromUtf8(checksumFile.readAll()).split('\n', Qt::SkipEmptyParts);
  if (checksumLines.size() != dlls32.size() * 2) {
    error = "Bundled FAudio checksum list is incomplete";
    return false;
  }
  QSet<QString> expectedFiles;
  for (const QString& name : dlls32) {
    expectedFiles.insert("i386-windows/" + name);
    expectedFiles.insert("x86_64-windows/" + name);
  }
  for (const QString& line : checksumLines) {
    const QString hash = line.left(64);
    const QString relative = line.mid(66);
    const QString absolute = QDir(bundle).filePath(relative);
    if (line.mid(64, 2) != QLatin1String("  ") ||
        !expectedFiles.remove(relative) || sha256(absolute) != hash) {
      error =
          QStringLiteral("Bundled FAudio checksum failed: %1").arg(relative);
      return false;
    }
    QFile module(absolute);
    if (!module.open(QIODevice::ReadOnly) ||
        module.read(96).contains("Wine builtin DLL")) {
      error =
          QStringLiteral("FAudio DLL is not packaged for prefix loading: %1")
              .arg(relative);
      return false;
    }
  }

  // Never follow a directory link into another prefix or a shared runner.
  for (const QString& path : {QStringLiteral("drive_c"), QStringLiteral("drive_c/windows"),
                             QStringLiteral("drive_c/windows/system32"),
                             QStringLiteral("drive_c/windows/syswow64"),
                             QStringLiteral(".fluorine-faudio")}) {
    const QFileInfo info(prefix + "/" + path);
    if (info.isSymLink() || (path != ".fluorine-faudio" && !info.isDir())) {
      error = QStringLiteral("Unsafe or missing FAudio destination: %1").arg(info.filePath());
      return false;
    }
  }
  const QString installedDir = prefix + "/.fluorine-faudio";
  if (!QDir().mkpath(installedDir)) {
    error = "Could not create FAudio installation metadata";
    return false;
  }
  QLockFile lock(installedDir + "/install.lock");
  if (!lock.tryLock()) {
    error = "Another process is updating the prefix's FAudio runtime";
    return false;
  }

  // Hash the actual files too: a Proton update or manual repair can replace a
  // DLL without changing our recorded version. QSaveFile follows symlinks, so
  // unlink prefix DLL links before writing, even when their contents match.
  for (const QString& line : checksumLines) {
    const QString relative = line.mid(66);
    const QString name = QFileInfo(relative).fileName();
    const QString windowsDir = relative.startsWith("i386-windows/") ? "syswow64" : "system32";
    const QString targetDir = prefix + "/drive_c/windows/" + windowsDir;
    const QString target = targetDir + "/" + name;
    if (!QFileInfo(target).isSymLink() && sha256(target) == line.left(64)) continue;

    // Remove case variants as well, since Wine uses case-insensitive DLL names.
    for (const QString& existing : QDir(targetDir).entryList(
             QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
      if (existing.compare(name, Qt::CaseInsensitive) == 0 &&
          (existing != name || QFileInfo(targetDir + "/" + existing).isSymLink()) &&
          !QFile::remove(targetDir + "/" + existing)) {
        error = QStringLiteral("Could not replace audio DLL: %1").arg(existing);
        return false;
      }
    }
    QFile input(bundle + "/" + relative);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly) ||
        output.write(input.readAll()) != input.size() || !output.commit()) {
      error = QStringLiteral("Could not install FAudio DLL into prefix: %1").arg(target);
      return false;
    }
    payload.changed = true;
  }
  // Commit metadata after the payload. A failed update remains repairable on
  // the next launch, and the caller must not launch after any failure.
  for (const QString& name : {QStringLiteral("version.txt"), QStringLiteral("sha256sums.txt")}) {
    const QByteArray contents = readFile(bundle + "/" + name);
    const QString target = installedDir + "/" + name;
    if (!QFileInfo(target).isSymLink() && readFile(target) == contents) continue;
    if (QFileInfo(target).isSymLink() && !QFile::remove(target)) {
      error = "Could not replace FAudio metadata link";
      return false;
    }
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() ||
        !output.commit()) {
      error = "Could not save FAudio installation metadata";
      return false;
    }
    payload.changed = true;
  }
  payload.version = faudioVersion;
  payload.dlls = dlls32;
  return true;
}

bool refreshFAudioPayload(const QString& prefix, const QString& bundleRoot,
                         const QString& requestedVariant, FAudioPayload& payload,
                         QString& error)
{
  payload = {};
  error.clear();
  const QString marker = prefix + "/.fluorine-faudio/version.txt";
  if (!QFileInfo::exists(marker)) return true;
  QString variant = requestedVariant.trimmed();
  if (variant.isEmpty()) {
    for (const QByteArray& line : readFile(marker).split('\n')) {
      if (line.startsWith("variant=")) variant = QString::fromUtf8(line.mid(8)).trimmed();
    }
    if (variant.isEmpty()) variant = "latest";
  }
  if (variant != "safe" && variant != "latest") {
    error = QStringLiteral("Unknown FAudio variant '%1' (use safe or latest)").arg(variant);
    return false;
  }
  return installFAudioPayload(prefix, QDir(bundleRoot).filePath(variant), payload, error);
}
