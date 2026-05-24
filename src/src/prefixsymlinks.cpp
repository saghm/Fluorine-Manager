#include "prefixsymlinks.h"
#include "gamedetection.h"
#include "steamappinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <uibase/log.h>
#include <unistd.h>

namespace {

static const char* SKIP_DIRS[] = {
    "Temp", "Microsoft", "wine", "Public", "root",
    "Application Data", "Cookies", "Local Settings",
    "NetHood", "PrintHood", "Recent", "SendTo",
    "Start Menu", "Templates", "My Documents", "My Music",
    "My Pictures", "My Videos", "Desktop", "Downloads",
    "Favorites", "Links", "Searches",
    "Contacts", "3D Objects",
};

bool shouldSkip(const QString& name)
{
  for (const char* s : SKIP_DIRS) {
    if (name.compare(QLatin1String(s), Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}

/// Find the username directory inside drive_c/users/.
QString findPrefixUsername(const QString& usersDir)
{
  QDir dir(usersDir);
  for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (entry != QStringLiteral("Public") && entry != QStringLiteral("root"))
      return entry;
  }
  return QStringLiteral("steamuser");
}

/// Create a symlink. If a stale symlink already points elsewhere and
/// `replaceStale` is true, replace it. Never clobbers a real directory.
bool createSymlinkIfNeeded(const QString& linkPath, const QString& target,
                           bool replaceStale)
{
  QFileInfo fi(linkPath);
  if (fi.isSymLink()) {
    if (fi.symLinkTarget() == target)
      return true;
    if (!replaceStale)
      return false;
    QFile::remove(linkPath);
  } else if (fi.exists()) {
    return false;  // real directory — don't clobber
  }

  QDir().mkpath(QFileInfo(linkPath).absolutePath());
  if (symlink(target.toUtf8().constData(), linkPath.toUtf8().constData()) != 0) {
    MOBase::log::warn("Failed to create symlink {} -> {}", linkPath, target);
    return false;
  }
  return true;
}

/// Scan all subdirectories in gameBase and symlink them into nakBase.
/// `replaceStale` — when true, stale symlinks (e.g. left from a previous
/// run pointing to a lower-priority prefix) get overwritten.
int scanAndLinkAll(const QString& nakBase, const QString& gameBase,
                   const QString& label, const QString& gameName,
                   bool skipMyGames = false, bool replaceStale = false)
{
  QDir dir(gameBase);
  if (!dir.exists())
    return 0;

  int count = 0;
  for (const QString& folder : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (shouldSkip(folder))
      continue;
    if (skipMyGames && folder == QStringLiteral("My Games"))
      continue;

    const QString linkPath = nakBase + "/" + folder;
    const QString target   = gameBase + "/" + folder;

    if (createSymlinkIfNeeded(linkPath, target, replaceStale)) {
      MOBase::log::info("Linked {}/{} -> {} ({})", label, folder, target, gameName);
      ++count;
    }
  }
  return count;
}

}  // namespace

void ensureTempDirectory(const QString& prefixPath)
{
  const QString usersDir = prefixPath + "/drive_c/users";
  const QString username = findPrefixUsername(usersDir);
  const QString tempDir  = usersDir + "/" + username + "/AppData/Local/Temp";

  if (QDir().mkpath(tempDir))
    MOBase::log::info("Ensured AppData/Local/Temp directory exists");
  else
    MOBase::log::warn("Failed to create Temp directory at {}", tempDir);
}

void createGameSymlinksAuto(const QString& prefixPath)
{
  GameScanResult result = detectAllGames();

  const QString usersDir = prefixPath + "/drive_c/users";
  const QString username = findPrefixUsername(usersDir);
  const QString userDir  = usersDir + "/" + username;
  const QString documents    = userDir + "/Documents";
  const QString myGames      = documents + "/My Games";
  const QString appdataLocal = userDir + "/AppData/Local";
  const QString appdataRoaming = userDir + "/AppData/Roaming";

  QDir().mkpath(myGames);
  QDir().mkpath(appdataLocal);
  QDir().mkpath(appdataRoaming);

  // Sort detected games so that actual Games win over Tools/Editors when
  // two prefixes share a folder name (e.g. Skyrim SE vs Creation Kit both
  // have "My Games/Skyrim Special Edition"). Without this, scanAndLinkAll
  // would pick whichever appeared first and shadow the real game.
  // Locate Steam install (mirrors the path list in findSteamInstallations()).
  QString steamPath;
  {
    const QString home = QDir::homePath();
    static const char* PATHS[] = {
        ".local/share/Steam", ".steam/debian-installation", ".steam/steam",
        ".var/app/com.valvesoftware.Steam/data/Steam",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam",
        "snap/steam/common/.local/share/Steam",
    };
    for (const char* rel : PATHS) {
      const QString full = QDir(home).filePath(QString::fromLatin1(rel));
      if (QFileInfo::exists(full + "/appcache/appinfo.vdf")) {
        steamPath = full;
        break;
      }
    }
  }
  const QHash<quint32, SteamAppInfo>& appInfo =
      steamPath.isEmpty() ? QHash<quint32, SteamAppInfo>{}
                          : loadSteamAppInfo(steamPath);

  auto appType = [&appInfo](const QString& appIdStr) -> QString {
    bool ok = false;
    const quint32 id = appIdStr.toUInt(&ok);
    if (!ok)
      return {};
    const auto it = appInfo.constFind(id);
    return it == appInfo.constEnd() ? QString{} : it->type;
  };

  std::vector<DetectedGame> ranked(result.games.begin(), result.games.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [&](const DetectedGame& a, const DetectedGame& b) {
                     return steamAppTypeRank(appType(a.app_id))
                            < steamAppTypeRank(appType(b.app_id));
                   });

  int linked = 0;
  bool first = true;
  for (const DetectedGame& game : ranked) {
    if (game.prefix_path.isEmpty())
      continue;

    const QString gameUsersDir = game.prefix_path + "/drive_c/users";
    const QString gameUsername = findPrefixUsername(gameUsersDir);
    const QString gameUserDir  = gameUsersDir + "/" + gameUsername;

    // Only the highest-ranked candidate is allowed to replace stale symlinks
    // from previous runs. Lower-ranked tools/demos must not overwrite real
    // games' links even if they happen to share a folder name.
    const bool replaceStale = first;
    first = false;

    linked += scanAndLinkAll(myGames, gameUserDir + "/Documents/My Games",
                             QStringLiteral("Documents/My Games"), game.name,
                             false, replaceStale);
    linked += scanAndLinkAll(documents, gameUserDir + "/Documents",
                             QStringLiteral("Documents"), game.name, true,
                             replaceStale);
    linked += scanAndLinkAll(appdataLocal, gameUserDir + "/AppData/Local",
                             QStringLiteral("AppData/Local"), game.name,
                             false, replaceStale);
    linked += scanAndLinkAll(appdataRoaming, gameUserDir + "/AppData/Roaming",
                             QStringLiteral("AppData/Roaming"), game.name,
                             false, replaceStale);
  }

  if (linked > 0)
    MOBase::log::info("Created {} symlinks to game prefixes", linked);

  // "My Documents" compat symlink.
  const QString myDocs = userDir + "/My Documents";
  if (!QFileInfo::exists(myDocs) && !QFileInfo(myDocs).isSymLink() &&
      symlink("Documents", myDocs.toUtf8().constData()) != 0) {
    MOBase::log::warn("Failed to create symlink {} -> Documents", myDocs);
  }
}
