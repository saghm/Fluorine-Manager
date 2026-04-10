#ifndef BETHINIPIE_H
#define BETHINIPIE_H

#include <uibase/iplugintool.h>

class QNetworkAccessManager;

class BethiniPie : public MOBase::IPluginTool
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginTool)
  Q_PLUGIN_METADATA(IID "com.fluorine.BethiniPie")

public:
  BethiniPie();

  bool init(MOBase::IOrganizer* moInfo) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  QString displayName() const override;
  QString tooltip() const override;
  QIcon icon() const override;

public slots:
  void display() const override;

private:
  /// Returns ~/.local/share/fluorine/tools/bethini-pie/
  QString toolDir() const;

  /// Returns the path to the BethiniPie executable inside toolDir()
  QString executablePath() const;

  /// Returns the stored SHA from .sha file, or empty string
  QString localSha() const;

  /// Writes the SHA to .sha file
  void saveLocalSha(const QString& sha) const;

  /// Maps Fluorine game name to BethIni Pie app name (they match, but provides
  /// a validation point)
  QString bethiniGameName() const;

  /// Returns the BethIni.ini directory key for the current game
  /// e.g. "sSkyrim Special EditionINIPath"
  QString iniDirectoryKey() const;

  /// Writes/updates the Bethini.ini config pointing at the profile's INI directory
  void writeBethiniConfig(const QString& bethiniDir,
                          const QString& iniPath) const;

  /// Checks for update and downloads if needed. Returns true if ready to launch.
  bool ensureUpToDate() const;

  const MOBase::IOrganizer* m_MOInfo;
  mutable QNetworkAccessManager* m_Network;
};

#endif  // BETHINIPIE_H
