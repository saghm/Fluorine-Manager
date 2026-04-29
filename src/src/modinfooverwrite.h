#ifndef MODINFOOVERWRITE_H
#define MODINFOOVERWRITE_H

#include <limits>

#include "modinfowithconflictinfo.h"

#include <QDateTime>

class ModInfoOverwrite : public ModInfoWithConflictInfo
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
  bool alwaysEnabled() const override { return true; }
  bool isEmpty() const override;
  QString name() const override { return "Overwrite"; }
  QString comments() const override { return ""; }
  QString notes() const override { return ""; }
  QDateTime creationTime() const override { return {}; }
  QString absolutePath() const override;
  MOBase::VersionInfo newestVersion() const override { return QString(); }
  MOBase::VersionInfo ignoredVersion() const override { return QString(); }
  QString installationFile() const override { return ""; }
  bool converted() const override { return false; }
  bool validated() const override { return false; }
  QString gameName() const override { return ""; }
  int nexusId() const override { return -1; }
  bool isOverwrite() const override { return true; }
  QDateTime getExpires() const override { return {}; }
  std::vector<QString> getIniTweaks() const override
  {
    return {};
  }
  std::vector<ModInfo::EFlag> getFlags() const override;
  std::vector<ModInfo::EConflictFlag> getConflictFlags() const override;
  int getHighlight() const override;
  QString getDescription() const override;
  int getNexusFileStatus() const override { return 0; }
  void setNexusFileStatus(int) override {}
  QDateTime getLastNexusUpdate() const override { return {}; }
  void setLastNexusUpdate(QDateTime) override {}
  QDateTime getLastNexusQuery() const override { return {}; }
  void setLastNexusQuery(QDateTime) override {}
  QDateTime getNexusLastModified() const override { return {}; }
  void setNexusLastModified(QDateTime) override {}
  QString getNexusDescription() const override { return {}; }
  void setNexusCategory(int) override {}
  int getNexusCategory() const override { return 0; }
  QString author() const override { return {}; }
  void setAuthor(const QString&) override {}
  QString uploader() const override { return {}; }
  void setUploader(const QString&) override {}
  QString uploaderUrl() const override { return {}; }
  void setUploaderUrl(const QString&) override {}
  QStringList archives(bool checkOnDisk = false) override;
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

private:
  ModInfoOverwrite(OrganizerCore& core);
};

#endif  // MODINFOOVERWRITE_H
