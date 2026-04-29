#include "sanitychecks.h"
#include "env.h"
#include "envmodule.h"
#include "settings.h"

#include <iplugingame.h>
#include <log.h>
#include <utility.h>

namespace sanity
{

using namespace MOBase;

int checkMissingFiles()
{
  log::debug("  . checking Linux dependencies");
  int n = 0;

  // Check for FUSE (check both unversioned .so and versioned .so.3 — on
  // Debian/Ubuntu the unversioned symlink is only in libfuse3-dev).
  static const char* fusePaths[] = {
      "/usr/lib/libfuse3.so",
      "/usr/lib/libfuse3.so.3",
      "/usr/lib64/libfuse3.so",
      "/usr/lib64/libfuse3.so.3",
      "/usr/lib/x86_64-linux-gnu/libfuse3.so",
      "/usr/lib/x86_64-linux-gnu/libfuse3.so.3",
      nullptr};

  bool fuseFound = false;
  for (int i = 0; fusePaths[i]; ++i) {
    if (QFileInfo::exists(fusePaths[i])) {
      fuseFound = true;
      break;
    }
  }
  if (!fuseFound) {
    log::warn("libfuse3 not found - FUSE VFS will not work");
    ++n;
  }

  return n;
}

int checkIncompatibleModule(const env::Module& /*m*/)
{
  // No Wine/Linux equivalent of the Win32 OSD/usvfs incompatibility list.
  return 0;
}

int checkProtected(const QDir& d, const QString& what)
{
  const auto path = d.absolutePath();
  log::debug("  . {}: {}", what, path);

  // Warn if running from a system-owned directory.
  if (path.startsWith("/root") || path.startsWith("/usr") || path.startsWith("/bin")) {
    log::warn("{} is in a system directory; this may cause permission issues", what);
    return 1;
  }

  return 0;
}

int checkPaths(IPluginGame& game, const Settings& s)
{
  log::debug("checking paths");

  int n = 0;

  n += checkProtected(game.gameDirectory(), "the game");
  n += checkProtected(QApplication::applicationDirPath(), "Mod Organizer");

  if (checkProtected(s.paths().base(), "the instance base directory")) {
    ++n;
  } else {
    n += checkProtected(s.paths().downloads(), "the downloads directory");
    n += checkProtected(s.paths().mods(), "the mods directory");
    n += checkProtected(s.paths().cache(), "the cache directory");
    n += checkProtected(s.paths().profiles(), "the profiles directory");
    n += checkProtected(s.paths().overwrite(), "the overwrite directory");
  }

  return n;
}

void checkEnvironment(const env::Environment& e)
{
  log::debug("running sanity checks...");

  int n = 0;

  n += checkMissingFiles();
  Q_UNUSED(e);

  log::debug("sanity checks done, {}",
             (n > 0 ? "problems were found" : "everything looks okay"));
}

}  // namespace sanity
