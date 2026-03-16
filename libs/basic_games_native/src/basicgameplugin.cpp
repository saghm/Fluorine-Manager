#include "basicgameplugin.h"
#include "steamutils.h"

#include <uibase/utility.h>
#include <uibase/versioninfo.h>

#include <QDateTime>
#include <QDirIterator>
#include <QStandardPaths>

// Simple ISaveGame implementation for basic games
class BasicSaveGame : public MOBase::ISaveGame
{
public:
  BasicSaveGame(const QString& filePath)
      : m_filePath(filePath), m_fileInfo(filePath)
  {
  }

  QString getFilepath() const override { return m_filePath; }

  QDateTime getCreationTime() const override
  {
    return m_fileInfo.lastModified();
  }

  QString getName() const override
  {
    return m_fileInfo.completeBaseName();
  }

  QString getSaveGroupIdentifier() const override { return ""; }

  QStringList allFiles() const override { return {m_filePath}; }

private:
  QString m_filePath;
  QFileInfo m_fileInfo;
};

BasicGamePlugin::BasicGamePlugin(const GameDefinition& def) : m_def(def) {}

bool BasicGamePlugin::init(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;
  return true;
}

QString BasicGamePlugin::name() const
{
  return m_def.pluginName;
}

QString BasicGamePlugin::localizedName() const
{
  return m_def.pluginName + " (Native)";
}

QString BasicGamePlugin::author() const
{
  return m_def.author;
}

QString BasicGamePlugin::description() const
{
  return "Adds support for " + m_def.gameName;
}

MOBase::VersionInfo BasicGamePlugin::version() const
{
  return MOBase::VersionInfo(m_def.version);
}

QList<MOBase::PluginSetting> BasicGamePlugin::settings() const
{
  return {};
}

QString BasicGamePlugin::gameName() const
{
  return m_def.gameName;
}

void BasicGamePlugin::detectGame()
{
  // Try Steam first
  for (int steamId : m_def.steamAppIds) {
    QString path = findSteamGamePath(steamId);
    if (!path.isEmpty()) {
      setGamePath(path);
      return;
    }
  }

  // GOG via Heroic launcher
  if (!m_def.gogAppIds.isEmpty()) {
    // Check Heroic GOG installed games
    QStringList heroicPaths = {
        QDir::homePath() + "/.config/heroic/gog_store/installed.json",
        QDir::homePath() +
            "/.var/app/com.heroicgameslauncher.hgl/config/heroic/"
            "gog_store/installed.json",
    };
    for (const auto& heroicPath : heroicPaths) {
      QFile file(heroicPath);
      if (!file.open(QIODevice::ReadOnly))
        continue;
      QByteArray data = file.readAll();
      // Simple JSON parsing for install_path
      for (int gogId : m_def.gogAppIds) {
        QString idStr = QString::number(gogId);
        if (data.contains(idStr.toUtf8())) {
          // Find the install_path for this entry
          int idx = data.indexOf(idStr.toUtf8());
          int pathIdx = data.indexOf("install_path", idx);
          if (pathIdx >= 0) {
            // Find the value after "install_path"
            int colonIdx = data.indexOf(':', pathIdx);
            int quoteStart = data.indexOf('"', colonIdx + 1);
            int quoteEnd = data.indexOf('"', quoteStart + 1);
            if (quoteStart >= 0 && quoteEnd > quoteStart) {
              QString path =
                  QString::fromUtf8(data.mid(quoteStart + 1, quoteEnd - quoteStart - 1));
              if (QDir(path).exists()) {
                setGamePath(path);
                return;
              }
            }
          }
        }
      }
    }
  }
}

void BasicGamePlugin::initializeProfile(const QDir& directory,
                                        ProfileSettings settings) const
{
  // Create the profile directory if needed
  if (!directory.exists()) {
    directory.mkpath(".");
  }
}

std::vector<std::shared_ptr<const MOBase::ISaveGame>>
BasicGamePlugin::listSaves(QDir folder) const
{
  std::vector<std::shared_ptr<const MOBase::ISaveGame>> saves;

  if (!m_def.saveExtension.isEmpty() && folder.exists()) {
    QStringList filters;
    filters << "*." + m_def.saveExtension;
    QStringList entries = folder.entryList(filters, QDir::Files, QDir::Time);
    for (const auto& entry : entries) {
      saves.push_back(
          std::make_shared<BasicSaveGame>(folder.filePath(entry)));
    }
  }

  return saves;
}

bool BasicGamePlugin::isInstalled() const
{
  return m_installed;
}

QIcon BasicGamePlugin::gameIcon() const
{
  return MOBase::iconForExecutable(
      gameDirectory().absoluteFilePath(binaryName()));
}

QDir BasicGamePlugin::gameDirectory() const
{
  return QDir(m_gameDir);
}

QDir BasicGamePlugin::dataDirectory() const
{
  QString dataDir = resolveVariables(m_def.dataDirectory);
  if (dataDir.isEmpty()) {
    return gameDirectory();
  }
  QDir dir(dataDir);
  if (dir.isAbsolute()) {
    return dir;
  }
  return QDir(m_gameDir + "/" + dataDir);
}

void BasicGamePlugin::setGamePath(const QString& path)
{
  m_gameDir   = path;
  m_installed = !path.isEmpty() && QDir(path).exists();
}

QDir BasicGamePlugin::documentsDirectory() const
{
  if (m_def.documentsDirectory.isEmpty()) {
    // Default: try My Games/<gameName> then <gameName> under Documents
    QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir myGames(docs + "/My Games/" + m_def.gameName);
    if (myGames.exists())
      return myGames;
    QDir plain(docs + "/" + m_def.gameName);
    if (plain.exists())
      return plain;
    return QDir();
  }
  return QDir(resolveVariables(m_def.documentsDirectory));
}

QDir BasicGamePlugin::savesDirectory() const
{
  if (m_def.savesDirectory.isEmpty()) {
    return documentsDirectory();
  }
  return QDir(resolveVariables(m_def.savesDirectory));
}

QList<MOBase::ExecutableInfo> BasicGamePlugin::executables() const
{
  QList<MOBase::ExecutableInfo> list;
  QDir dir = gameDirectory();
  QFileInfo binary(dir.filePath(m_def.binaryName));
  if (binary.exists()) {
    list.append(MOBase::ExecutableInfo(m_def.gameName, binary));
  }
  if (!m_def.launcherName.isEmpty()) {
    QFileInfo launcher(dir.filePath(m_def.launcherName));
    if (launcher.exists()) {
      list.append(MOBase::ExecutableInfo(m_def.gameName + " Launcher", launcher));
    }
  }
  return list;
}

QList<MOBase::ExecutableForcedLoadSetting>
BasicGamePlugin::executableForcedLoads() const
{
  return {};
}

QString BasicGamePlugin::steamAPPId() const
{
  if (!m_def.steamAppIds.isEmpty()) {
    return QString::number(m_def.steamAppIds.first());
  }
  return "";
}

QStringList BasicGamePlugin::primaryPlugins() const
{
  return m_def.primaryPlugins;
}

QStringList BasicGamePlugin::gameVariants() const
{
  return {};
}

void BasicGamePlugin::setGameVariant(const QString&) {}

QString BasicGamePlugin::binaryName() const
{
  return m_def.binaryName;
}

QString BasicGamePlugin::gameShortName() const
{
  return m_def.gameShortName;
}

QStringList BasicGamePlugin::validShortNames() const
{
  return m_def.validShortNames;
}

QString BasicGamePlugin::gameNexusName() const
{
  return m_def.gameNexusName;
}

QStringList BasicGamePlugin::iniFiles() const
{
  QStringList resolved;
  for (const auto& ini : m_def.iniFiles) {
    resolved.append(resolveVariables(ini));
  }
  return resolved;
}

QStringList BasicGamePlugin::DLCPlugins() const
{
  return m_def.dlcPlugins;
}

MOBase::IPluginGame::LoadOrderMechanism
BasicGamePlugin::loadOrderMechanism() const
{
  return m_def.loadOrderMechanism;
}

MOBase::IPluginGame::SortMechanism BasicGamePlugin::sortMechanism() const
{
  return m_def.sortMechanism;
}

int BasicGamePlugin::nexusGameID() const
{
  return m_def.nexusGameId;
}

bool BasicGamePlugin::looksValid(QDir const& dir) const
{
  // Primary check: binary at game root
  if (dir.exists(m_def.binaryName))
    return true;

  // Fallback: check if the data directory exists relative to this path.
  // Some UE5 games (e.g. Oblivion Remastered) have their root exe nested
  // under a subdirectory even though the appmanifest installdir points one
  // level up. If the unique data path resolves, it's the right directory.
  if (!m_def.dataDirectory.isEmpty()) {
    QString dataDir = m_def.dataDirectory;
    dataDir.replace('\\', '/');
    // Only handle %GAME_PATH% here; other vars need context we don't have
    if (!dataDir.contains('%') || dataDir.startsWith("%GAME_PATH%")) {
      dataDir.replace("%GAME_PATH%", dir.absolutePath());
      if (!dataDir.contains('%') && QDir(dataDir).exists())
        return true;
    }
  }

  return false;
}

QString BasicGamePlugin::gameVersion() const
{
  // On Linux we can't easily read PE version resources
  // Return empty; MO2 will show "N/A"
  return "";
}

QString BasicGamePlugin::getLauncherName() const
{
  return m_def.launcherName;
}

QString BasicGamePlugin::getSupportURL() const
{
  return m_def.supportURL;
}

QString BasicGamePlugin::resolveVariables(const QString& input) const
{
  if (input.isEmpty())
    return input;

  QString result = input;

  // Normalize backslashes to forward slashes first
  result.replace('\\', '/');

  // %DOCUMENTS%
  if (result.contains("%DOCUMENTS%")) {
    QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    result.replace("%DOCUMENTS%", docs);
  }

  // %USERPROFILE% - on Linux, resolve to the Wine prefix userprofile
  if (result.contains("%USERPROFILE%")) {
    QString userProfile;
    // Try the global Fluorine prefix
    QString prefixPath =
        QDir::homePath() +
        "/.local/share/fluorine/Prefix/pfx/drive_c/users/steamuser";
    if (QDir(prefixPath).exists()) {
      userProfile = prefixPath;
    } else {
      // Fallback to home directory
      userProfile = QDir::homePath();
    }
    result.replace("%USERPROFILE%", userProfile);
  }

  // %GAME_PATH%
  if (result.contains("%GAME_PATH%")) {
    result.replace("%GAME_PATH%", m_gameDir);
  }

  // %GAME_DOCUMENTS%
  if (result.contains("%GAME_DOCUMENTS%")) {
    result.replace("%GAME_DOCUMENTS%", documentsDirectory().absolutePath());
  }

  return result;
}
