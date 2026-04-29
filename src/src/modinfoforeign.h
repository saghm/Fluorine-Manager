#ifndef MODINFOFOREIGN_H
#define MODINFOFOREIGN_H

#include <limits>

#include "modinfowithconflictinfo.h"

class ModInfoForeign : public ModInfoWithConflictInfo
{

  Q_OBJECT

  friend class ModInfo;

public:
  bool updateAvailable() const override { return false; }
  bool updateIgnored() const override { return false; }
  bool downgradeAvailable() const override { return false; }
  bool updateNXMInfo() override { return false; }
  void setCategory(int, bool) override {}
  bool setName(const QString&) override { return false; }
  void setComments(const QString&) override {}
  void setNotes(const QString&) override {}
  void setGameName(const QString& gameName) override {}
  void setNexusID(int) override {}
  void setNewestVersion(const MOBase::VersionInfo&) override {}
  void ignoreUpdate(bool) override {}
  void setNexusDescription(const QString&) override {}
  void setInstallationFile(const QString&) override {}
  void addNexusCategory(int) override {}
  void setIsEndorsed(bool) override {}
  void setNeverEndorse() override {}
  void setIsTracked(bool) override {}
  void endorse(bool) override {}
  void track(bool) override {}
  bool isEmpty() const override { return false; }
  QString name() const override { return m_Name; }
  QString internalName() const override { return m_InternalName; }
  QString comments() const override { return ""; }
  QString notes() const override { return ""; }
  QDateTime creationTime() const override;
  QString absolutePath() const override { return m_BaseDirectory; }
  MOBase::VersionInfo newestVersion() const override { return QString(); }
  MOBase::VersionInfo ignoredVersion() const override { return QString(); }
  QString installationFile() const override { return ""; }
  bool converted() const override { return false; }
  bool validated() const override { return false; }
  QString gameName() const override { return ""; }
  int nexusId() const override { return -1; }
  bool isForeign() const override { return true; }
  QDateTime getExpires() const override { return QDateTime(); }
  std::vector<QString> getIniTweaks() const override
  {
    return std::vector<QString>();
  }
  std::vector<ModInfo::EFlag> getFlags() const override;
  int getHighlight() const override;
  QString getDescription() const override;
  int getNexusFileStatus() const override { return 0; }
  void setNexusFileStatus(int) override {}
  QDateTime getLastNexusUpdate() const override { return QDateTime(); }
  void setLastNexusUpdate(QDateTime) override {}
  int getNexusCategory() const override { return 0; }
  void setNexusCategory(int) override {}
  QDateTime getLastNexusQuery() const override { return QDateTime(); }
  void setLastNexusQuery(QDateTime) override {}
  QDateTime getNexusLastModified() const override { return QDateTime(); }
  void setNexusLastModified(QDateTime) override {}
  QString getNexusDescription() const override { return QString(); }
  QString author() const override { return QString(); }
  void setAuthor(const QString&) override {}
  QString uploader() const override { return QString(); }
  void setUploader(const QString&) override {}
  QString uploaderUrl() const override { return QString(); }
  void setUploaderUrl(const QString&) override {}
  QStringList archives(bool = false) override { return m_Archives; }
  QStringList stealFiles() const override
  {
    return m_Archives + QStringList(m_ReferenceFile);
  }
  bool alwaysEnabled() const override { return true; }
  void addInstalledFile(int, int) override {}
  std::set<std::pair<int, int>> installedFiles() const override { return {}; }

  QVariant pluginSetting(const QString& pluginName, const QString& key,
                                 const QVariant& defaultValue) const override
  {
    return defaultValue;
  }
  std::map<QString, QVariant>
  pluginSettings(const QString& pluginName) const override
  {
    return {};
  }
  bool setPluginSetting(const QString& pluginName, const QString& key,
                                const QVariant& value) override
  {
    return false;
  }
  std::map<QString, QVariant>
  clearPluginSettings(const QString& pluginName) override
  {
    return {};
  }

  ModInfo::EModType modType() const { return m_ModType; }

protected:
  ModInfoForeign(const QString& modName, const QString& referenceFile,
                 const QStringList& archives, ModInfo::EModType modType,
                 OrganizerCore& core);

private:
  QString m_Name;
  QString m_InternalName;
  QString m_ReferenceFile;
  QString m_BaseDirectory;
  QStringList m_Archives;
  QDateTime m_CreationTime;
  int m_Priority;
  ModInfo::EModType m_ModType;
};

#endif  // MODINFOFOREIGN_H
