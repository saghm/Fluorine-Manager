#include "prefixsymlinks.h"
#include "gamedetection.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

/// Create a symlink if the path doesn't already exist (or is already correct).
bool createSymlinkIfNeeded(const QString& linkPath, const QString& target)
{
  QFileInfo fi(linkPath);
  if (fi.exists() || fi.isSymLink()) {
    if (fi.isSymLink() && fi.symLinkTarget() == target)
      return true;
    return false;  // something else exists
  }

  QDir().mkpath(QFileInfo(linkPath).absolutePath());
  if (symlink(target.toUtf8().constData(), linkPath.toUtf8().constData()) != 0) {
    MOBase::log::warn("Failed to create symlink {} -> {}", linkPath, target);
    return false;
  }
  return true;
}

/// Scan all subdirectories in gameBase and symlink them into nakBase.
int scanAndLinkAll(const QString& nakBase, const QString& gameBase,
                   const QString& label, const QString& gameName,
                   bool skipMyGames = false)
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

    if (createSymlinkIfNeeded(linkPath, target)) {
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

  int linked = 0;
  for (const DetectedGame& game : result.games) {
    if (game.prefix_path.isEmpty())
      continue;

    const QString gameUsersDir = game.prefix_path + "/drive_c/users";
    const QString gameUsername = findPrefixUsername(gameUsersDir);
    const QString gameUserDir  = gameUsersDir + "/" + gameUsername;

    linked += scanAndLinkAll(myGames, gameUserDir + "/Documents/My Games",
                             QStringLiteral("Documents/My Games"), game.name);
    linked += scanAndLinkAll(documents, gameUserDir + "/Documents",
                             QStringLiteral("Documents"), game.name, true);
    linked += scanAndLinkAll(appdataLocal, gameUserDir + "/AppData/Local",
                             QStringLiteral("AppData/Local"), game.name);
    linked += scanAndLinkAll(appdataRoaming, gameUserDir + "/AppData/Roaming",
                             QStringLiteral("AppData/Roaming"), game.name);
  }

  if (linked > 0)
    MOBase::log::info("Created {} symlinks to game prefixes", linked);

  // "My Documents" compat symlink.
  const QString myDocs = userDir + "/My Documents";
  if (!QFileInfo::exists(myDocs) && !QFileInfo(myDocs).isSymLink())
    symlink("Documents", myDocs.toUtf8().constData());
}
