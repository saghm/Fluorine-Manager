#ifndef BASICGAMEPLUGIN_H
#define BASICGAMEPLUGIN_H

#include "gamedefs.h"

#include <uibase/iplugingame.h>
#include <uibase/imoinfo.h>

#include <QDir>
#include <QIcon>
#include <QString>

class BasicGamePlugin : public MOBase::IPluginGame
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginGame)

public:
  explicit BasicGamePlugin(const GameDefinition& def);

  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  // IPluginGame
  QString gameName() const override;
  void detectGame() override;
  void initializeProfile(const QDir& directory,
                         ProfileSettings settings) const override;
  std::vector<std::shared_ptr<const MOBase::ISaveGame>>
  listSaves(QDir folder) const override;
  bool isInstalled() const override;
  QIcon gameIcon() const override;
  QDir gameDirectory() const override;
  QDir dataDirectory() const override;
  void setGamePath(const QString& path) override;
  QDir documentsDirectory() const override;
  QDir savesDirectory() const override;
  QList<MOBase::ExecutableInfo> executables() const override;
  QList<MOBase::ExecutableForcedLoadSetting> executableForcedLoads() const override;
  QString steamAPPId() const override;
  QStringList primaryPlugins() const override;
  QStringList gameVariants() const override;
  void setGameVariant(const QString& variant) override;
  QString binaryName() const override;
  QString gameShortName() const override;
  QStringList validShortNames() const override;
  QString gameNexusName() const override;
  QStringList iniFiles() const override;
  QStringList DLCPlugins() const override;
  LoadOrderMechanism loadOrderMechanism() const override;
  SortMechanism sortMechanism() const override;
  int nexusGameID() const override;
  bool looksValid(QDir const& dir) const override;
  QString gameVersion() const override;
  QString getLauncherName() const override;
  QString getSupportURL() const override;

private:
  QString resolveVariables(const QString& input) const;

  GameDefinition m_def;
  MOBase::IOrganizer* m_organizer = nullptr;
  QString m_gameDir;
  bool m_installed = false;
};

#endif  // BASICGAMEPLUGIN_H
