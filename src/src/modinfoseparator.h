#ifndef MODINFOSEPARATOR_H
#define MODINFOSEPARATOR_H

#include "modinforegular.h"

class ModInfoSeparator : public ModInfoRegular
{
  Q_OBJECT;

  friend class ModInfo;

public:
  bool updateAvailable() const override { return false; }
  bool updateIgnored() const override { return false; }
  bool downgradeAvailable() const override { return false; }
  bool updateNXMInfo() override { return false; }
  bool isValid() const override { return true; }
  // TODO: Fix renaming method to avoid priority reset
  bool setName(const QString& name) override;

  int nexusId() const override { return -1; }
  void setGameName(const QString& gameName) override {}
  void setNexusID(int /*modID*/) override {}
  void endorse(bool /*doEndorse*/) override {}
  void ignoreUpdate(bool /*ignore*/) override {}
  bool canBeUpdated() const override { return false; }
  QDateTime getExpires() const override { return {}; }
  bool canBeEnabled() const override { return false; }
  std::vector<QString> getIniTweaks() const override
  {
    return {};
  }
  std::vector<EFlag> getFlags() const override;
  int getHighlight() const override;
  QString getDescription() const override;
  QString name() const override;
  QString gameName() const override { return ""; }
  QString installationFile() const override { return ""; }
  QString repository() const override { return ""; }
  int getNexusFileStatus() const override { return 0; }
  void setNexusFileStatus(int) override {}
  QDateTime getLastNexusUpdate() const override { return {}; }
  void setLastNexusUpdate(QDateTime) override {}
  QDateTime getLastNexusQuery() const override { return {}; }
  void setLastNexusQuery(QDateTime) override {}
  QDateTime getNexusLastModified() const override { return {}; }
  void setNexusLastModified(QDateTime) override {}
  int getNexusCategory() const override { return 0; }
  void setNexusCategory(int) override {}
  QDateTime creationTime() const override { return {}; }
  QString getNexusDescription() const override { return {}; }
  QString author() const override { return {}; }
  void setAuthor(const QString&) override {}
  QString uploader() const override { return {}; }
  void setUploader(const QString&) override {}
  QString uploaderUrl() const override { return {}; }
  void setUploaderUrl(const QString&) override {}
  void addInstalledFile(int /*modId*/, int /*fileId*/) override {}
  bool isSeparator() const override { return true; }

protected:
  bool doIsValid() const override { return true; }

private:
  ModInfoSeparator(const QDir& path, OrganizerCore& core);
};

#endif
