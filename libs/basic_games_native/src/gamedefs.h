#ifndef GAMEDEFS_H
#define GAMEDEFS_H

#include <uibase/iplugingame.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

struct GameDefinition {
  // Plugin metadata
  QString pluginName;
  QString author;
  QString version;  // parsed as VersionInfo string

  // Game identity
  QString gameName;
  QString gameShortName;
  QStringList validShortNames;

  // Store IDs
  QList<int> steamAppIds;
  QList<int> gogAppIds;
  QStringList epicAppIds;

  // Executables
  QString binaryName;
  QString launcherName;

  // Nexus
  QString gameNexusName;
  int nexusGameId = 0;

  // Directories (may contain %DOCUMENTS%, %USERPROFILE%, %GAME_PATH%,
  // %GAME_DOCUMENTS%)
  QString dataDirectory;
  QString documentsDirectory;
  QString savesDirectory;
  QString saveExtension;

  // Configuration
  QStringList iniFiles;
  QString supportURL;

  // Load order / sorting
  MOBase::IPluginGame::LoadOrderMechanism loadOrderMechanism =
      MOBase::IPluginGame::LoadOrderMechanism::None;
  MOBase::IPluginGame::SortMechanism sortMechanism =
      MOBase::IPluginGame::SortMechanism::NONE;

  // Plugins
  QStringList primaryPlugins;
  QStringList dlcPlugins;
};

// Returns the complete list of all game definitions
const std::vector<GameDefinition>& allGameDefinitions();

#endif  // GAMEDEFS_H
