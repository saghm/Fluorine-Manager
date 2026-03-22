#ifndef WINEPREFIX_H
#define WINEPREFIX_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

class WinePrefix
{
public:
  explicit WinePrefix(const QString& prefixPath);

  bool isValid() const;  // drive_c/ exists
  QString driveC() const;
  QString documentsPath() const;  // drive_c/users/steamuser/Documents
  QString myGamesPath() const;    // .../Documents/My Games
  QString appdataLocal() const;   // .../AppData/Local
  QString userProfilePath() const; // drive_c/users/steamuser

  // Deploy profile files into prefix
  bool deployPlugins(const QStringList& plugins, const QString& dataDir) const;
  bool deployProfileIni(const QString& sourceIniPath,
                        const QString& targetIniPath) const;
  bool deployProfileSaves(const QString& profileSaveDir,
                          const QString& absoluteSaveDir,
                          bool clearDestination) const;

  // Sync saves back from prefix to profile
  bool syncSavesBack(const QString& profileSaveDir,
                     const QString& absoluteSaveDir) const;
  bool syncProfileInisBack(
      const QList<QPair<QString, QString>>& iniMappings) const;

  // Restore any stale .mo2linux_backup INI/save files left by a crash.
  // Should be called at startup before any game runs.
  void restoreStaleBackups() const;

  // Wine registry (system.reg / user.reg) access.
  // subKey uses Wine format: "Software\\\\Bethesda Softworks\\\\Skyrim Special Edition"
  // (double-escaped backslashes as stored in .reg files).
  // Convenience overload accepts normal backslash paths and escapes internally.
  QString readRegistryValue(const QString& regFile, const QString& subKey,
                            const QString& valueName) const;
  bool writeRegistryValue(const QString& regFile, const QString& subKey,
                          const QString& valueName, const QString& value) const;

  // High-level: read/write HKLM values via system.reg
  QString readHklmValue(const QString& subKey, const QString& valueName) const;
  bool writeHklmValue(const QString& subKey, const QString& valueName,
                      const QString& value) const;

private:
  QString m_prefixPath;
};

#endif  // WINEPREFIX_H
