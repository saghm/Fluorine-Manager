#include "steamutils.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

static QStringList findSteamPaths()
{
  QStringList paths;
  QString home = QDir::homePath();

  QStringList candidates = {
      home + "/.local/share/Steam",
      home + "/.steam/debian-installation",
      home + "/.steam/steam",
      home + "/.var/app/com.valvesoftware.Steam/data/Steam",
      home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
      home + "/snap/steam/common/.local/share/Steam",
  };

  // When Steam launches an app (e.g. in game mode) it sets
  // STEAM_COMPAT_CLIENT_INSTALL_PATH to its own root.  Add that as a
  // candidate so we find the right installation even if $HOME resolves
  // differently inside the gamescope session.
  const QString steamEnvPath =
      QString::fromLocal8Bit(qgetenv("STEAM_COMPAT_CLIENT_INSTALL_PATH"));
  if (!steamEnvPath.isEmpty()) {
    candidates.prepend(steamEnvPath);
  }

  for (const auto& candidate : candidates) {
    QDir dir(candidate);
    if (dir.exists("steamapps") || QFile::exists(candidate + "/steam.pid")) {
      // Resolve symlinks to avoid duplicates
      QString canonical = QFileInfo(candidate).canonicalFilePath();
      if (!canonical.isEmpty() && !paths.contains(canonical)) {
        paths.append(canonical);
      }
    }
  }

  return paths;
}

static QStringList parseLibraryFolders(const QString& vdfPath)
{
  QStringList folders;
  QFile file(vdfPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return folders;

  QTextStream in(&file);
  QString content = in.readAll();

  // Match "path" entries in VDF format: "path"   "/path/to/library"
  static QRegularExpression re(R"--("path"\s+"([^"]+)")--");
  auto it = re.globalMatch(content);
  while (it.hasNext()) {
    auto match = it.next();
    QString path = match.captured(1);
    if (QDir(path).exists()) {
      folders.append(path);
    }
  }

  return folders;
}

static QStringList getAllLibraryFolders(const QString& steamPath)
{
  QStringList folders;
  folders.append(steamPath);

  // Parse libraryfolders.vdf
  for (const auto& vdfName :
       {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"}) {
    QString vdfPath = steamPath + "/" + vdfName;
    for (const auto& path : parseLibraryFolders(vdfPath)) {
      // Canonicalize before dedup so /home/ and /var/home/ paths compare equal
      QString canonical = QFileInfo(path).canonicalFilePath();
      if (canonical.isEmpty())
        canonical = path;
      if (!folders.contains(canonical)) {
        folders.append(canonical);
      }
    }
  }

  return folders;
}

struct AppManifest {
  int appId       = 0;
  QString name;
  QString installDir;
  int stateFlags  = 0;

  bool isInstalled() const { return (stateFlags & 4) != 0; }
};

static AppManifest parseAppManifest(const QString& path)
{
  AppManifest manifest;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return manifest;

  QTextStream in(&file);
  QString content = in.readAll();

  static QRegularExpression reAppId(R"--("appid"\s+"(\d+)")--");
  static QRegularExpression reName(R"--("name"\s+"([^"]+)")--");
  static QRegularExpression reInstallDir(R"--("installdir"\s+"([^"]+)")--");
  static QRegularExpression reState(R"--("StateFlags"\s+"(\d+)")--");

  auto m = reAppId.match(content);
  if (m.hasMatch())
    manifest.appId = m.captured(1).toInt();

  m = reName.match(content);
  if (m.hasMatch())
    manifest.name = m.captured(1);

  m = reInstallDir.match(content);
  if (m.hasMatch())
    manifest.installDir = m.captured(1);

  m = reState.match(content);
  if (m.hasMatch())
    manifest.stateFlags = m.captured(1).toInt();

  return manifest;
}

// Cache: built once per process, cleared on explicit refresh.
static QHash<int, QString> s_steamGamesCache;
static bool s_steamGamesCacheValid = false;

QHash<int, QString> findSteamGames()
{
  if (s_steamGamesCacheValid)
    return s_steamGamesCache;

  QHash<int, QString> games;

  for (const auto& steamPath : findSteamPaths()) {
    for (const auto& libraryPath : getAllLibraryFolders(steamPath)) {
      QDir steamapps(libraryPath + "/steamapps");
      if (!steamapps.exists())
        continue;

      QStringList manifests =
          steamapps.entryList({"appmanifest_*.acf"}, QDir::Files);
      for (const auto& manifestFile : manifests) {
        auto manifest = parseAppManifest(steamapps.filePath(manifestFile));
        if (manifest.appId > 0 && manifest.isInstalled() &&
            !manifest.installDir.isEmpty()) {
          QString installPath =
              steamapps.filePath("common/" + manifest.installDir);
          if (QDir(installPath).exists()) {
            games.insert(manifest.appId, installPath);
          }
        }
      }
    }
  }

  s_steamGamesCache      = games;
  s_steamGamesCacheValid = true;
  return games;
}

QString findSteamGamePath(int appId)
{
  return findSteamGames().value(appId);
}
