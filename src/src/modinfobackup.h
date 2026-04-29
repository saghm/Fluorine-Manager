#ifndef MODINFOBACKUP_H
#define MODINFOBACKUP_H

#include "modinforegular.h"

class ModInfoBackup : public ModInfoRegular
{

  Q_OBJECT

  friend class ModInfo;

public:
  bool updateAvailable() const override { return false; }
  bool updateIgnored() const override { return false; }
  bool downgradeAvailable() const override { return false; }
  bool updateNXMInfo() override { return false; }
  void setGameName(const QString& gameName) override {}
  void setNexusID(int) override {}
  void endorse(bool) override {}
  void ignoreUpdate(bool) override {}
  bool alwaysDisabled() const override { return true; }
  bool canBeUpdated() const override { return false; }
  QDateTime getExpires() const override { return {}; }
  bool canBeEnabled() const override { return false; }
  std::vector<QString> getIniTweaks() const override
  {
    return {};
  }
  std::vector<EFlag> getFlags() const override;
  QString getDescription() const override;
  int getNexusFileStatus() const override { return 0; }
  void setNexusFileStatus(int) override {}
  QDateTime getLastNexusQuery() const override { return {}; }
  void setLastNexusQuery(QDateTime) override {}
  QDateTime getLastNexusUpdate() const override { return {}; }
  void setLastNexusUpdate(QDateTime) override {}
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
  bool isBackup() const override { return true; }

  void addInstalledFile(int, int) override {}

private:
  ModInfoBackup(const QDir& path, OrganizerCore& core);
};

#endif  // MODINFOBACKUP_H
