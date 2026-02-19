#include "wineprefix.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <log.h>
#include <uibase/filesystemutilities.h>

namespace
{
constexpr const char* BackupIniSuffix  = ".mo2linux_backup";

bool copyFileWithParents(const QString& source, const QString& destination)
{
  const QFileInfo destinationInfo(destination);
  if (!QDir().mkpath(destinationInfo.dir().absolutePath())) {
    return false;
  }

  if (QFile::exists(destination) && !QFile::remove(destination)) {
    return false;
  }

  return QFile::copy(source, destination);
}

bool copyTreeContents(const QString& sourceRoot, const QString& destinationRoot)
{
  QDirIterator it(sourceRoot, QDir::Files, QDirIterator::Subdirectories);

  while (it.hasNext()) {
    const QString source = it.next();
    const QString relativePath = QDir(sourceRoot).relativeFilePath(source);
    const QString destination = QDir(destinationRoot).filePath(relativePath);

    if (!copyFileWithParents(source, destination)) {
      return false;
    }
  }

  return true;
}

bool restoreBackedUpSaves(const QString& liveUpper, const QString& liveLower,
                          const QString& backupUpper, const QString& backupLower)
{
  if (QDir(liveUpper).exists() && !QDir(liveUpper).removeRecursively()) {
    return false;
  }
  if (QDir(liveLower).exists() && !QDir(liveLower).removeRecursively()) {
    return false;
  }

  if (QDir(backupUpper).exists() && !QDir().rename(backupUpper, liveUpper)) {
    return false;
  }
  if (QDir(backupLower).exists() && !QDir().rename(backupLower, liveLower)) {
    return false;
  }

  return true;
}

bool restoreBackedUpIni(const QString& liveIni, const QString& backupIni)
{
  if (!QFile::exists(backupIni)) {
    return true;
  }

  if (QFile::exists(liveIni) && !QFile::remove(liveIni)) {
    return false;
  }

  return QFile::rename(backupIni, liveIni);
}

// Find all files in the same directory that match the filename case-insensitively.
// E.g. for "skyrimprefs.ini" returns {"skyrimprefs.ini", "SkyrimPrefs.ini"} if both exist.
QStringList findCaseVariants(const QString& path)
{
  QFileInfo info(path);
  QDir dir(info.path());
  if (!dir.exists()) {
    return {};
  }

  QStringList result;
  const QString target = info.fileName();
  for (const QString& entry :
       dir.entryList(QDir::Files | QDir::Hidden | QDir::System)) {
    if (entry.compare(target, Qt::CaseInsensitive) == 0) {
      result.append(dir.absoluteFilePath(entry));
    }
  }
  return result;
}
}  // namespace

WinePrefix::WinePrefix(const QString& prefixPath)
    : m_prefixPath(QDir::cleanPath(prefixPath))
{
  MOBase::log::debug("WinePrefix: initialized with path '{}'", m_prefixPath);
}

bool WinePrefix::isValid() const
{
  return QDir(driveC()).exists();
}

QString WinePrefix::driveC() const
{
  return QDir(m_prefixPath).filePath("drive_c");
}

QString WinePrefix::documentsPath() const
{
  return QDir(driveC()).filePath("users/steamuser/Documents");
}

QString WinePrefix::myGamesPath() const
{
  return QDir(documentsPath()).filePath("My Games");
}

QString WinePrefix::appdataLocal() const
{
  return QDir(driveC()).filePath("users/steamuser/AppData/Local");
}

QString WinePrefix::userProfilePath() const
{
  return QDir(driveC()).filePath("users/steamuser");
}

bool WinePrefix::deployPlugins(const QStringList& plugins, const QString& dataDir) const
{
  if (!isValid()) {
    MOBase::log::error("deployPlugins: prefix '{}' is not valid (drive_c not found)",
                       m_prefixPath);
    return false;
  }

  const QString pluginsDir = QDir(appdataLocal()).filePath(dataDir);
  MOBase::log::info("deployPlugins: target dir='{}', count={}", pluginsDir,
                    plugins.size());

  if (!QDir().mkpath(pluginsDir)) {
    MOBase::log::error("deployPlugins: failed to create directory '{}'", pluginsDir);
    return false;
  }

  // Remove all case variants of plugins.txt and loadorder.txt before writing.
  // Linux is case-sensitive, so a stale "plugins.txt" can coexist with
  // "Plugins.txt" and the game may read the wrong one (e.g. FalloutNV reads
  // lowercase "plugins.txt").
  const QString pluginsPath  = QDir(pluginsDir).filePath("Plugins.txt");
  const QString loadOrderPath = QDir(pluginsDir).filePath("loadorder.txt");
  for (const QString& variant : findCaseVariants(pluginsPath)) {
    MOBase::log::debug("deployPlugins: removing stale plugins variant '{}'", variant);
    QFile::remove(variant);
  }
  for (const QString& variant : findCaseVariants(loadOrderPath)) {
    MOBase::log::debug("deployPlugins: removing stale loadorder variant '{}'", variant);
    QFile::remove(variant);
  }

  QFile pluginsFile(pluginsPath);
  if (!pluginsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    MOBase::log::error("deployPlugins: failed to open '{}' for writing", pluginsPath);
    return false;
  }

  QTextStream pluginsStream(&pluginsFile);
  for (const QString& plugin : plugins) {
    pluginsStream << plugin << "\r\n";
  }
  pluginsFile.close();
  MOBase::log::info("deployPlugins: wrote {} plugins to '{}'", plugins.size(),
                    pluginsPath);

  // Also write lowercase "plugins.txt" for games that expect it (e.g. FalloutNV).
  const QString pluginsLower = QDir(pluginsDir).filePath("plugins.txt");
  if (pluginsLower != pluginsPath) {
    QFile::remove(pluginsLower);
    if (!QFile::copy(pluginsPath, pluginsLower)) {
      MOBase::log::warn("deployPlugins: failed to create lowercase copy '{}'",
                        pluginsLower);
    }
  }

  QFile loadOrderFile(loadOrderPath);
  if (!loadOrderFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    MOBase::log::error("deployPlugins: failed to open '{}' for writing",
                       loadOrderPath);
    return false;
  }

  QTextStream loadOrderStream(&loadOrderFile);
  for (const QString& plugin : plugins) {
    QString line = plugin;
    if (line.startsWith('*')) {
      line.remove(0, 1);
    }

    loadOrderStream << line << "\r\n";
  }
  MOBase::log::info("deployPlugins: wrote loadorder.txt to '{}'", loadOrderPath);

  return true;
}

bool WinePrefix::deployProfileIni(const QString& sourceIniPath,
                                  const QString& targetIniPath) const
{
  const QFileInfo iniInfo(sourceIniPath);
  if (!iniInfo.exists() || !iniInfo.isFile()) {
    MOBase::log::warn("deployProfileIni: source '{}' does not exist or is not a file",
                      sourceIniPath);
    return false;
  }

  MOBase::log::debug("deployProfileIni: '{}' -> '{}'", sourceIniPath, targetIniPath);
  const QString destination = QDir::cleanPath(targetIniPath);

  // Back up ALL case-insensitive variants (e.g. both skyrimprefs.ini and
  // SkyrimPrefs.ini). Linux is case-sensitive, so the game may create a
  // different-case file alongside ours. Backing up all variants ensures
  // a clean deploy and correct restore later.
  const QStringList variants = findCaseVariants(destination);
  for (const QString& variant : variants) {
    const QString backup = variant + BackupIniSuffix;
    if (!restoreBackedUpIni(variant, backup)) {
      return false;
    }
    if (QFile::exists(variant) && !QFile::rename(variant, backup)) {
      return false;
    }
  }

  // If the exact-case file wasn't among the variants (didn't exist yet),
  // still restore any stale backup for it.
  if (!variants.contains(destination)) {
    const QString backup = destination + BackupIniSuffix;
    if (!restoreBackedUpIni(destination, backup)) {
      return false;
    }
  }

  if (!copyFileWithParents(iniInfo.absoluteFilePath(), destination)) {
    return false;
  }

  // Create a lowercase alias so the game can find the INI regardless of
  // which casing it uses (e.g. FalloutNV reads "fallout.ini" but we deploy
  // "Fallout.ini").
  const QFileInfo destInfo(destination);
  const QString lowerName = destInfo.fileName().toLower();
  if (lowerName != destInfo.fileName()) {
    const QString lowerPath = QDir(destInfo.path()).filePath(lowerName);
    QFile::remove(lowerPath);  // remove stale copy/symlink if any
    QFile::link(destInfo.fileName(), lowerPath);
  }

  return true;
}

bool WinePrefix::deployProfileSaves(const QString& profileSaveDir,
                                    const QString& absoluteSaveDir,
                                    bool clearDestination) const
{
  if (!isValid()) {
    MOBase::log::error("deployProfileSaves: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("deployProfileSaves: profileSaveDir='{}', absoluteSaveDir='{}', "
                     "clearDestination={}",
                     profileSaveDir, absoluteSaveDir, clearDestination);

  const QFileInfo saveDirInfo(absoluteSaveDir);
  const QString parentDir = saveDirInfo.dir().absolutePath();
  const QString leafName = saveDirInfo.fileName();
  const QString backupUpper =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName);
  const QString backupLower =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName.toLower());
  const QString lowerSaveDir =
      QDir(parentDir).filePath(leafName.toLower());

  if (clearDestination) {
    // Recover from any stale backup left by an interrupted run.
    if ((QDir(backupUpper).exists() || QDir(backupLower).exists()) &&
        !restoreBackedUpSaves(absoluteSaveDir, lowerSaveDir,
                              backupUpper, backupLower)) {
      return false;
    }

    if (QDir(absoluteSaveDir).exists() &&
        !QDir().rename(absoluteSaveDir, backupUpper)) {
      return false;
    }
    if (absoluteSaveDir != lowerSaveDir &&
        QDir(lowerSaveDir).exists() &&
        !QDir().rename(lowerSaveDir, backupLower)) {
      return false;
    }
  }

  if (!QDir().mkpath(absoluteSaveDir)) {
    return false;
  }

  if (!QDir(profileSaveDir).exists()) {
    return true;
  }

  return copyTreeContents(profileSaveDir, absoluteSaveDir);
}

bool WinePrefix::syncSavesBack(const QString& profileSaveDir,
                               const QString& absoluteSaveDir) const
{
  if (!isValid()) {
    MOBase::log::error("syncSavesBack: prefix '{}' is not valid", m_prefixPath);
    return false;
  }

  MOBase::log::debug("syncSavesBack: profileSaveDir='{}', absoluteSaveDir='{}'",
                     profileSaveDir, absoluteSaveDir);

  const QFileInfo saveDirInfo(absoluteSaveDir);
  const QString parentDir = saveDirInfo.dir().absolutePath();
  const QString leafName = saveDirInfo.fileName();
  const QString lowerSaveDir =
      QDir(parentDir).filePath(leafName.toLower());

  QString sourceSavesDir;
  if (QDir(absoluteSaveDir).exists()) {
    sourceSavesDir = absoluteSaveDir;
  } else if (absoluteSaveDir != lowerSaveDir && QDir(lowerSaveDir).exists()) {
    sourceSavesDir = lowerSaveDir;
  } else {
    return true;
  }

  if (!QDir().mkpath(profileSaveDir)) {
    return false;
  }

  const bool copied = copyTreeContents(sourceSavesDir, profileSaveDir);
  if (!copied) {
    MOBase::log::warn("Failed syncing saves from '{}' to '{}'", sourceSavesDir,
                      profileSaveDir);
  }

  const QString backupUpper =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName);
  const QString backupLower =
      QDir(parentDir).filePath(".mo2linux_backup_" + leafName.toLower());
  if (!restoreBackedUpSaves(absoluteSaveDir, lowerSaveDir,
                            backupUpper, backupLower)) {
    MOBase::log::warn("Failed restoring backed up global saves in '{}'", parentDir);
    return false;
  }

  return copied;
}

void WinePrefix::restoreStaleBackups() const
{
  if (!isValid()) {
    return;
  }

  // Scan the entire prefix for stale .mo2linux_backup INI files.
  // These are left behind when MO2 crashes after deploying profile INIs.
  QDirIterator it(driveC(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (!it.fileName().endsWith(BackupIniSuffix)) {
      continue;
    }

    const QString backupPath = it.filePath();
    const QString livePath =
        backupPath.left(backupPath.length() - QString(BackupIniSuffix).length());

    MOBase::log::info("Restoring stale INI backup '{}' -> '{}'", backupPath, livePath);
    if (!restoreBackedUpIni(livePath, backupPath)) {
      MOBase::log::warn("Failed to restore stale INI backup '{}'", backupPath);
    }
  }

  // Also restore stale save backups — scan entire prefix for .mo2linux_backup_*
  // directories (saves may be in Documents/My Games/.../Saves, Saved Games/..., etc.)
  QSet<QString> processedBackups;
  QDirIterator saveIt(driveC(), QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
  while (saveIt.hasNext()) {
    saveIt.next();
    const QString dirName = saveIt.fileName();
    if (!dirName.startsWith(".mo2linux_backup_")) {
      continue;
    }

    const QString backupPath = saveIt.filePath();
    const QString parentDir = QFileInfo(backupPath).dir().absolutePath();
    const QString originalLeaf = dirName.mid(QString(".mo2linux_backup_").length());
    if (originalLeaf.isEmpty()) {
      continue;
    }

    // Use lowercase key to deduplicate upper/lower variants in same parent
    const QString dedupeKey = parentDir + "/" + originalLeaf.toLower();
    if (processedBackups.contains(dedupeKey)) {
      continue;
    }
    processedBackups.insert(dedupeKey);

    const QString liveUpper = QDir(parentDir).filePath(originalLeaf);
    const QString liveLower = QDir(parentDir).filePath(originalLeaf.toLower());
    const QString backupUpper =
        QDir(parentDir).filePath(".mo2linux_backup_" + originalLeaf);
    const QString backupLower =
        QDir(parentDir).filePath(".mo2linux_backup_" + originalLeaf.toLower());

    MOBase::log::info("Restoring stale save backups: '{}' in '{}'",
                      originalLeaf, parentDir);
    if (!restoreBackedUpSaves(liveUpper, liveLower, backupUpper, backupLower)) {
      MOBase::log::warn("Failed to restore stale save backups in '{}'", parentDir);
    }
  }
}

bool WinePrefix::syncProfileInisBack(
    const QList<QPair<QString, QString>>& iniMappings) const
{
  MOBase::log::debug("syncProfileInisBack: {} INI mappings to sync back",
                     iniMappings.size());
  bool allCopied = true;
  for (const auto& mapping : iniMappings) {
    const QString profileIniPath = QDir::cleanPath(mapping.first);
    const QString prefixIniPath  = QDir::cleanPath(mapping.second);
    MOBase::log::debug("syncProfileInisBack: profile='{}' <- prefix='{}'",
                       profileIniPath, prefixIniPath);

    // Find ALL case-insensitive variants of the INI file (e.g. both
    // skyrimprefs.ini and SkyrimPrefs.ini may exist on Linux).
    // Pick the most recently modified one — that's the file the game wrote to.
    const QStringList variants = findCaseVariants(prefixIniPath);

    QString newestVariant;
    QDateTime newestTime;
    for (const QString& variant : variants) {
      const QFileInfo fi(variant);
      if (fi.lastModified() > newestTime) {
        newestTime    = fi.lastModified();
        newestVariant = variant;
      }
    }

    if (newestVariant.isEmpty()) {
      // No INI file found at all — try to restore from any backup.
      const QString backupIniPath = prefixIniPath + BackupIniSuffix;
      if (!restoreBackedUpIni(prefixIniPath, backupIniPath)) {
        allCopied = false;
      }
      continue;
    }

    // Sync the game's version back to the profile.
    if (!copyFileWithParents(newestVariant, profileIniPath)) {
      allCopied = false;
    }

    // Remove ALL variants (including stale deployed copies), then
    // restore ALL backed-up originals.
    for (const QString& variant : variants) {
      QFile::remove(variant);
    }

    // Restore all backups (there may be multiple from different case variants).
    const QStringList backupVariants =
        findCaseVariants(prefixIniPath + BackupIniSuffix);
    for (const QString& backup : backupVariants) {
      const QString livePath =
          backup.left(backup.length() - QString(BackupIniSuffix).length());
      if (!restoreBackedUpIni(livePath, backup)) {
        allCopied = false;
      }
    }
  }

  return allCopied;
}
