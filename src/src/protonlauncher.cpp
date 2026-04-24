#include "protonlauncher.h"

#include "steamdetection.h"
#include "slrmanager.h"
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <log.h>


namespace
{
// Restore the pre-AppImage environment for child processes (Proton, Wine).
// The AppRun script saves FLUORINE_ORIG_* vars before
// modifying PATH, LD_LIBRARY_PATH, etc.  We restore from those saved values
// so game processes get a clean host environment without AppImage library paths.
void cleanAppImageEnv(QProcessEnvironment& env)
{
  // Remove Fluorine/AppImage-specific vars that should never leak to game processes.
  env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
  env.remove("MO2_PLUGINS_DIR");
  env.remove("MO2_DLLS_DIR");
  env.remove("MO2_PYTHON_DIR");
  env.remove("MO2_BASE_DIR");

  // AppImage runtime injects these — they can confuse Proton/Wine.
  env.remove("APPIMAGE");
  env.remove("APPDIR");
  env.remove("OWD");
  env.remove("ARGV0");
  env.remove("APPIMAGE_ORIGINAL_EXEC");

  env.remove("DESKTOPINTEGRATION");

  // Restore saved pre-AppImage values.  AppRun sets FLUORINE_ORIG_* before
  // modifying PATH, LD_LIBRARY_PATH, etc.  If those vars exist, use them to
  // restore the original host environment.  If not (standalone/non-AppImage),
  // fall back to stripping known AppImage patterns.
  auto restoreOrStrip = [](const QString& var, const QString& origVar,
                           QProcessEnvironment& e) {
    if (e.contains(origVar)) {
      const QString orig = e.value(origVar);
      if (orig.isEmpty()) {
        e.remove(var);
      } else {
        e.insert(var, orig);
      }
      e.remove(origVar);
    } else {
      // Fallback: strip AppImage mount paths by pattern.
      const QString value = e.value(var);
      if (value.isEmpty()) return;
      QStringList kept;
      for (const QString& p : value.split(':')) {
        if (p.contains(".mount_Fluori") || p.contains("/fluorine/python")) {
          continue;
        }
        kept.append(p);
      }
      if (kept.isEmpty()) {
        e.remove(var);
      } else {
        e.insert(var, kept.join(':'));
      }
    }
  };

  const bool hasOrigVars = env.contains("FLUORINE_ORIG_PATH");
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH", env);
  restoreOrStrip("LD_PRELOAD", "FLUORINE_ORIG_LD_PRELOAD", env);
  restoreOrStrip("PATH", "FLUORINE_ORIG_PATH", env);
  restoreOrStrip("XDG_DATA_DIRS", "FLUORINE_ORIG_XDG_DATA_DIRS", env);
  restoreOrStrip("QT_PLUGIN_PATH", "FLUORINE_ORIG_QT_PLUGIN_PATH", env);

  MOBase::log::debug("cleanAppImageEnv: {} (LD_LIBRARY_PATH='{}')",
                     hasOrigVars ? "restored from FLUORINE_ORIG_*" : "pattern-strip fallback",
                     env.value("LD_LIBRARY_PATH", "<unset>"));
}

// GE-Proton crashes in update_builtin_libs() if tracked_files doesn't exist
// (e.g. prefix was created by plain Wine, not Proton).  Create an empty one.
void ensureTrackedFilesExist(const QString& compatDataPath)
{
  if (compatDataPath.isEmpty()) return;
  const QString tracked = QDir(compatDataPath).filePath("tracked_files");
  if (!QFileInfo::exists(tracked)) {
    QDir().mkpath(compatDataPath);
    QFile f(tracked);
    if (f.open(QIODevice::WriteOnly)) {
      f.close();
      MOBase::log::debug("created empty tracked_files at '{}'", tracked);
    }
  }
}

QString compatDataPathFromPrefix(const QString& prefixPath)
{
  if (prefixPath.isEmpty()) {
    return {};
  }

  QDir prefixDir(prefixPath);

  // Case 1: prefix_path is `<compatdata>/pfx` — the usual Fluorine layout.
  if (prefixDir.dirName() == "pfx") {
    if (prefixDir.cdUp()) {
      return QDir::cleanPath(prefixDir.absolutePath());
    }
  }

  // Case 2: prefix_path is the compatdata directory itself (contains `pfx/`).
  // Steam expects STEAM_COMPAT_DATA_PATH to be the compatdata dir, not pfx.
  if (prefixDir.exists(QStringLiteral("pfx/drive_c"))) {
    return QDir::cleanPath(prefixDir.absolutePath());
  }

  // Case 3: prefix_path is a plain Wine prefix (contains drive_c directly,
  // no pfx wrapper). Pointing STEAM_COMPAT_DATA_PATH at the parent would be
  // wrong (Proton would look for `<parent>/pfx` which doesn't exist and
  // create a fresh prefix there). Returning empty lets the caller skip
  // setting STEAM_COMPAT_DATA_PATH — Proton then respects WINEPREFIX only.
  if (prefixDir.exists(QStringLiteral("drive_c"))) {
    return {};
  }

  // Last-resort legacy behaviour — keep the old fallback for callers that
  // pass non-existent paths (prefix creation UI flow).
  return QDir::cleanPath(QFileInfo(prefixPath).dir().absolutePath());
}

QString detectSteamPath()
{
  {
    const QString steamPath = findSteamPath().trimmed();
    if (!steamPath.isEmpty()) {
      return steamPath;
    }
  }

  const QString homeSteam = QDir::home().filePath(".steam/steam");
  if (QFileInfo::exists(homeSteam)) {
    return homeSteam;
  }

  const QString homeRoot = QDir::home().filePath(".steam/root");
  if (QFileInfo::exists(homeRoot)) {
    return homeRoot;
  }

  return {};
}

// Find the Steam Linux Runtime run script (steamrt4 preferred, sniper fallback).
// SLR wraps the Proton launch in a pressure-vessel container that provides
// GStreamer, 32-bit libs, and an FHS-compliant environment — required on
// NixOS and other non-FHS distros. Proton 11+ requires steamrt4.
// Returns empty string if SLR is not installed.
QString detectSLRRunScript()
{
  const QString steamPath = detectSteamPath();

  const QStringList candidates = {
      steamPath + "/steamapps/common/SteamLinuxRuntime_4/run",
      steamPath + "/steamapps/common/SteamLinuxRuntime_sniper/run",
      QDir::home().filePath(".local/share/Steam/steamapps/common/SteamLinuxRuntime_4/run"),
      QDir::home().filePath(".local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/run"),
      "/usr/lib/pressure-vessel/wrap",
  };

  for (const QString& path : candidates) {
    if (!path.isEmpty() && QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

// Detect an available terminal emulator on the system.
QString findTerminal()
{
  static const char* candidates[] = {
      "konsole", "gnome-terminal", "xfce4-terminal", "alacritty",
      "kitty", "foot", "xterm"};
  for (const auto* name : candidates) {
    const QString path = QStandardPaths::findExecutable(name);
    if (!path.isEmpty())
      return path;
  }
  return {};
}

// Wrap a program + args in a terminal emulator.
// Modifies program and arguments in-place.
void wrapInTerminal(QString& program, QStringList& arguments)
{
  const QString term = findTerminal();
  if (term.isEmpty()) {
    MOBase::log::warn("No terminal emulator found; launching without terminal");
    return;
  }

  const QString termName = QFileInfo(term).fileName();

  // Build a shell command for the inner program + args.
  // Append a read prompt so the terminal stays open after exit.
  QString innerCmd = "'" + QString(program).replace("'", "'\\''") + "'";
  for (const auto& a : arguments)
    innerCmd += " '" + QString(a).replace("'", "'\\''") + "'";
  innerCmd += "; echo; echo '--- Process exited with code '$?' ---'; "
              "echo 'Press Enter to close...'; read";

  QStringList termArgs;
  if (termName == "konsole") {
    termArgs << "--hold" << "-e" << "bash" << "-c" << innerCmd;
  } else if (termName == "gnome-terminal") {
    termArgs << "--" << "bash" << "-c" << innerCmd;
  } else if (termName == "xfce4-terminal") {
    termArgs << "--hold" << "-e" << ("bash -c " + innerCmd);
  } else if (termName == "alacritty") {
    termArgs << "-e" << "bash" << "-c" << innerCmd;
  } else if (termName == "kitty") {
    termArgs << "bash" << "-c" << innerCmd;
  } else if (termName == "foot") {
    termArgs << "bash" << "-c" << innerCmd;
  } else {
    // xterm and others
    termArgs << "-e" << "bash" << "-c" << innerCmd;
  }

  MOBase::log::info("Launching in terminal: {} {}", term,
                     termArgs.join(' '));
  program   = term;
  arguments = termArgs;
}

bool startWithEnv(const QString& program, const QStringList& arguments,
                  const QString& workingDir,
                  const QProcessEnvironment& environment, qint64& pid)
{
  auto* process = new QProcess();
  process->setProgram(program);
  process->setArguments(arguments);

  if (!workingDir.isEmpty()) {
    process->setWorkingDirectory(workingDir);
  }

  process->setProcessEnvironment(environment);
  process->setProcessChannelMode(QProcess::ForwardedOutputChannel);

  // Filter noisy Wine/Proton stderr (GStreamer warnings, etc.) while
  // forwarding everything else to our stderr.
  QObject::connect(process, &QProcess::readyReadStandardError, process, [process]() {
    const QByteArray data = process->readAllStandardError();
    for (const QByteArray& line : data.split('\n')) {
      if (line.isEmpty())
        continue;
      if (line.contains("GStreamer-WARNING") || line.contains("Failed to load plugin") ||
          line.contains("ProtonFixes[") || line.contains("wineserver: NTSync") ||
          line.contains("[MANGOHUD]") || line.contains("radv is not a conformant"))
        continue;
      std::fwrite(line.constData(), 1, line.size(), stderr);
      std::fputc('\n', stderr);
    }
  });

  QObject::connect(process,
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   process, &QProcess::deleteLater);

  process->start();
  if (!process->waitForStarted(5000)) {
    delete process;
    return false;
  }

  pid = process->processId();
  return true;
}

void wrapProgram(const QStringList& wrapperCommands, const QString& program,
                 const QStringList& arguments, QString& wrappedProgram,
                 QStringList& wrappedArguments)
{
  if (wrapperCommands.isEmpty()) {
    wrappedProgram   = program;
    wrappedArguments = arguments;
    return;
  }

  wrappedProgram = wrapperCommands.first();
  wrappedArguments.clear();

  if (wrapperCommands.size() > 1) {
    wrappedArguments.append(wrapperCommands.mid(1));
  }

  wrappedArguments.append(program);
  wrappedArguments.append(arguments);
}

bool isValidEnvKey(const QString& key)
{
  if (key.isEmpty()) {
    return false;
  }

  const QChar first = key.front();
  if (!(first.isLetter() || first == QChar('_'))) {
    return false;
  }

  for (const QChar c : key) {
    if (!(c.isLetterOrNumber() || c == QChar('_'))) {
      return false;
    }
  }

  return true;
}

bool parseEnvAssignment(const QString& token, QString& keyOut, QString& valueOut)
{
  const int eq = token.indexOf('=');
  if (eq <= 0) {
    return false;
  }

  const QString key = token.left(eq);
  if (!isValidEnvKey(key)) {
    return false;
  }

  keyOut   = key;
  valueOut = token.mid(eq + 1);
  return true;
}
}  // namespace

ProtonLauncher::ProtonLauncher()
    : m_steamAppId(0), m_useSteamDrm(true)
{}

ProtonLauncher& ProtonLauncher::setBinary(const QString& path)
{
  m_binary = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setArguments(const QStringList& args)
{
  m_arguments = args;
  return *this;
}

ProtonLauncher& ProtonLauncher::setWorkingDir(const QString& dir)
{
  m_workingDir = dir.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setProtonPath(const QString& path)
{
  m_protonPath = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setPrefix(const QString& path)
{
  m_prefixPath = path.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::setSteamAppId(uint32_t id)
{
  m_steamAppId = id;
  return *this;
}

ProtonLauncher& ProtonLauncher::setWrapper(const QString& wrapperCmd)
{
  m_wrapperCommands.clear();
  m_wrapperEnvVars.clear();

  const QStringList parts = QProcess::splitCommand(wrapperCmd.trimmed());
  for (const QString& part : parts) {
    if (part.compare("%command%", Qt::CaseInsensitive) == 0) {
      continue;
    }
    QString key;
    QString value;
    if (parseEnvAssignment(part, key, value)) {
      m_wrapperEnvVars.insert(key, value);
    } else {
      m_wrapperCommands.push_back(part);
    }
  }

  return *this;
}

ProtonLauncher& ProtonLauncher::setSteamDrm(bool useSteamDrm)
{
  m_useSteamDrm = useSteamDrm;
  return *this;
}

ProtonLauncher& ProtonLauncher::setUseSLR(bool useSLR)
{
  m_useSLR = useSLR;
  return *this;
}

ProtonLauncher& ProtonLauncher::setStoreVariant(const QString& variant)
{
  m_storeVariant = variant.trimmed();
  return *this;
}

ProtonLauncher& ProtonLauncher::addEnvVar(const QString& key, const QString& value)
{
  if (!key.isEmpty()) {
    m_envVars.insert(key, value);
  }

  return *this;
}

ProtonLauncher& ProtonLauncher::setUseTerminal(bool useTerminal)
{
  m_useTerminal = useTerminal;
  return *this;
}

ProtonLauncher& ProtonLauncher::setSavesBindMount(const QString& source,
                                                  const QString& target)
{
  m_bindMountSource = source.trimmed();
  m_bindMountTarget = target.trimmed();
  return *this;
}

bool ProtonLauncher::unprivilegedBindMountSupported()
{
  // Need both an `unshare` binary and a kernel that lets unprivileged users
  // enter a user namespace with CAP_SYS_ADMIN (which is what makes
  // `mount --bind` work without root).  Debian stable + some hardened
  // kernels disable this via a sysctl; treat missing/zero as "no".
  if (QStandardPaths::findExecutable("unshare").isEmpty()) {
    return false;
  }

  QFile f(QStringLiteral("/proc/sys/kernel/unprivileged_userns_clone"));
  if (f.open(QIODevice::ReadOnly)) {
    const QByteArray v = f.readAll().trimmed();
    // File exists and explicitly says "0" -> disabled.  Any other value
    // (including "1") or an unreadable/missing file -> assume enabled, which
    // is the modern-kernel default.
    if (v == "0") {
      return false;
    }
  }
  return true;
}

std::pair<bool, qint64> ProtonLauncher::launch() const
{
  qint64 pid = -1;

  if (!m_protonPath.isEmpty()) {
    return {launchWithProton(pid), pid};
  }

  return {launchDirect(pid), pid};
}

bool ProtonLauncher::launchWithProton(qint64& pid) const
{
  if (m_binary.isEmpty() || m_protonPath.isEmpty()) {
    return false;
  }

  if (m_useSteamDrm) {
    ensureSteamRunning();
  }

  QString protonScript = m_protonPath;
  if (QFileInfo(protonScript).isDir()) {
    protonScript = QDir(m_protonPath).filePath("proton");
  }

  // Use "waitforexitandrun" instead of "run": this tells Proton to wait for
  // any existing wineserver to shut down first, then launch the game.  This is
  // what umu-launcher uses and ensures the previous session is fully cleaned up
  // before starting a new one.
  const QStringList protonArgs = QStringList() << "waitforexitandrun" << m_binary << m_arguments;

  // If SLR is enabled, wrap the whole proton invocation inside the
  // pressure-vessel container provided by SteamLinuxRuntime_sniper.
  // The `run` script accepts `-- <command> [args...]` and re-executes the
  // command inside the container.
  QString program;
  QStringList arguments;
  if (m_useSLR) {
    const QString runScript = getSlrRunScript();
    if (!runScript.isEmpty()) {
      MOBase::log::info("SLR: wrapping launch with {}", runScript);
      // Build: [wrappers] run_script [--filesystem=...] -- proton_script protonArgs
      QStringList slrArgs;

      // Expose host directories to the pressure-vessel container.
      // Without --filesystem= flags, the container's mount namespace
      // may not see FUSE mounts or system-installed Proton paths.
      if (!m_binary.isEmpty()) {
        const QString gameDir = QFileInfo(m_binary).absolutePath();
        slrArgs << QStringLiteral("--filesystem=%1").arg(gameDir);
      }
      if (!m_prefixPath.isEmpty()) {
        slrArgs << QStringLiteral("--filesystem=%1").arg(m_prefixPath);
      }
      // Expose the Proton installation directory — needed for
      // system-installed Protons (e.g. /usr/share/steam/compatibilitytools.d/)
      // whose files may not be visible inside the container by default.
      {
        const QString protonDir = QFileInfo(protonScript).absolutePath();
        slrArgs << QStringLiteral("--filesystem=%1").arg(protonDir);
      }

      // Expose dedicated xrandr dir so container sees our injected xrandr
      // (steamrt4 ships without it, required by Proton-GE protonfixes).
      // Pressure-vessel forces PATH=/usr/bin:/bin inside the container and
      // ignores host PATH, so we prepend via `env PATH=...` as the command.
      QStringList containerCmd;
      {
        const QString xrandrDir =
            QDir::homePath() + "/.local/share/fluorine/steamrt/xrandr-bin";
        if (QDir(xrandrDir).exists()) {
          slrArgs << QStringLiteral("--filesystem=%1").arg(xrandrDir);
          containerCmd << QStringLiteral("/usr/bin/env")
                       << QStringLiteral("PATH=%1:/usr/bin:/bin").arg(xrandrDir);
        }
      }
      containerCmd << protonScript << protonArgs;

      slrArgs << "--";
      slrArgs.append(containerCmd);
      wrapProgram(m_wrapperCommands, runScript, slrArgs, program, arguments);
    } else {
      MOBase::log::warn("SLR enabled but run script not found — launching without SLR");
      wrapProgram(m_wrapperCommands, protonScript, protonArgs, program, arguments);
    }
  } else {
    wrapProgram(m_wrapperCommands, protonScript, protonArgs, program, arguments);
  }

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  cleanAppImageEnv(env);

  // Prepend fluorine's bin dir to PATH so the container sees our injected
  // xrandr (steamrt4 ships without it; Proton-GE protonfixes require it).
  {
    const QString fluorineBin =
        QDir::homePath() + "/.local/share/fluorine/bin";
    const QString existing = env.value("PATH");
    env.insert("PATH", existing.isEmpty() ? fluorineBin
                                          : fluorineBin + ":" + existing);
  }

  if (!m_prefixPath.isEmpty()) {
    env.insert("WINEPREFIX", m_prefixPath);
  }

  const QString compatDataPath = compatDataPathFromPrefix(m_prefixPath);
  if (!compatDataPath.isEmpty()) {
    ensureTrackedFilesExist(compatDataPath);
    env.insert("STEAM_COMPAT_DATA_PATH", compatDataPath);
  }

  const QString steamPath = detectSteamPath();
  if (!steamPath.isEmpty()) {
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath);
  }

  if (m_steamAppId != 0) {
    const QString appId = QString::number(m_steamAppId);
    env.insert("SteamAppId", appId);
    env.insert("SteamGameId", appId);
  }

  // When Steam DRM is disabled (e.g. GOG games), set UMU_ID so that
  // Proton-GE skips the built-in steam.exe bridge.  Without this, Proton
  // tries to initialise the Steam client which causes an assertion failure
  // for non-Steam executables.
  if (!m_useSteamDrm) {
    env.insert("UMU_ID", "fluorine");
  }

  env.insert("DOTNET_ROOT", "");
  env.insert("DOTNET_MULTILEVEL_LOOKUP", "0");

  // Detect ntsync availability: newer Proton/Wine builds prefer the
  // in-kernel ntsync driver for synchronization primitives, but it's
  // only present on kernels ≥ 6.14 (or with the out-of-tree module
  // loaded). When it's missing, games emit "Cannot open synchronization
  // device: No such file or directory" and fall back to an unusable
  // state (see issue from user on 2026-04-23). Force the esync/fsync
  // fallback if /dev/ntsync isn't usable.
  if (!QFileInfo::exists(QStringLiteral("/dev/ntsync"))) {
    if (!env.contains("PROTON_NO_NTSYNC")) {
      env.insert("PROTON_NO_NTSYNC", "1");
    }
    if (!env.contains("WINENTSYNC")) {
      env.insert("WINENTSYNC", "0");
    }
    if (!env.contains("WINE_DISABLE_FAST_SYNC")) {
      env.insert("WINE_DISABLE_FAST_SYNC", "1");
    }
    static bool warned = false;
    if (!warned) {
      MOBase::log::info(
          "/dev/ntsync missing — disabling Proton fast-sync "
          "(set PROTON_NO_NTSYNC=1, WINENTSYNC=0, WINE_DISABLE_FAST_SYNC=1). "
          "Kernel ≥ 6.14 or the ntsync kmod is required for ntsync.");
      warned = true;
    }
  }

  // Ensure Wine's Unix codepage is UTF-8 so non-ASCII filenames (CJK,
  // Cyrillic, accented Latin) round-trip correctly between Linux FS and
  // Win32 WCHAR APIs.  Wine picks the codepage from LC_ALL > LC_CTYPE >
  // LANG via nl_langinfo(CODESET); a C/POSIX locale collapses to CP1252
  // and makes MSVC std::filesystem throw "Invalid name" on CJK entries
  // (WineHQ#46039, Proton#3434).  Steam's pressure-vessel can strip the
  // user's locale, so we override only if no UTF-8 locale is already set
  // — keeps de_DE.UTF-8, ja_JP.UTF-8 etc. intact for users who have them.
  {
    const auto isUtf8 = [](const QString& v) {
      return v.contains("UTF-8", Qt::CaseInsensitive) ||
             v.contains("UTF8", Qt::CaseInsensitive);
    };
    const QString lcAll   = env.value("LC_ALL");
    const QString lcCtype = env.value("LC_CTYPE");
    const QString lang    = env.value("LANG");
    const bool haveUtf8 =
        (!lcAll.isEmpty() && isUtf8(lcAll)) ||
        (lcAll.isEmpty() && !lcCtype.isEmpty() && isUtf8(lcCtype)) ||
        (lcAll.isEmpty() && lcCtype.isEmpty() && isUtf8(lang));
    if (!haveUtf8) {
      MOBase::log::info("Locale not UTF-8 (LC_ALL='{}', LC_CTYPE='{}', "
                        "LANG='{}'); forcing C.UTF-8 for Wine",
                        lcAll, lcCtype, lang);
      env.insert("LC_ALL", "C.UTF-8");
      env.insert("LANG", "C.UTF-8");
    }
  }

  // Force-disable DXVK graphics-pipeline-library.  GPL causes very long shader
  // compile stalls on first launch for heavily modded Bethesda games and the
  // benefit is modest for us.  We write a small dxvk.conf into the prefix and
  // point DXVK_CONFIG_FILE at it so every DXVK-rendered process picks it up.
  if (!m_prefixPath.isEmpty()) {
    const QString dxvkConfPath = QDir(m_prefixPath).filePath("dxvk.conf");
    QFile dxvkConfFile(dxvkConfPath);
    if (dxvkConfFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      dxvkConfFile.write("dxvk.enableGraphicsPipelineLibrary = False\n");
      dxvkConfFile.close();
      env.insert("DXVK_CONFIG_FILE", dxvkConfPath);
    } else {
      MOBase::log::warn("Failed to write dxvk.conf at '{}'", dxvkConfPath);
    }
  }

  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  // If a saves bind mount was requested (and the kernel supports
  // unprivileged user namespaces), wrap the whole invocation in
  // `unshare --user --mount -r` so the mount lives only inside the game's
  // process tree and is torn down automatically on exit.  Must happen
  // AFTER wrapProgram but BEFORE startWithEnv so that SLR/pressure-vessel
  // inherits the mount from the outer namespace.
  if (!m_bindMountSource.isEmpty() && !m_bindMountTarget.isEmpty() &&
      unprivilegedBindMountSupported()) {
    const QString unshareBin = QStandardPaths::findExecutable("unshare");
    QStringList newArgs;
    newArgs << "--user" << "--mount" << "-r" << "--"
            << "/bin/sh" << "-c"
            << "mount --bind \"$1\" \"$2\" && shift 2 && exec \"$@\""
            << "_mo2bind"
            << m_bindMountSource
            << m_bindMountTarget
            << program;
    newArgs.append(arguments);
    program   = unshareBin;
    arguments = newArgs;
    MOBase::log::info("Saves bind mount: '{}' -> '{}'", m_bindMountSource,
                      m_bindMountTarget);
  } else if (!m_bindMountSource.isEmpty()) {
    MOBase::log::warn("Saves bind mount requested but unprivileged user "
                      "namespaces unavailable; game will write to prefix "
                      "'{}' directly",
                      m_bindMountTarget);
  }

  MOBase::log::info("Proton launch: '{}' run '{}'", protonScript, m_binary);
  MOBase::log::info("Final command: '{}' {}", program,
      arguments.join(" ").toStdString());

  if (!m_workingDir.isEmpty()) {
    env.insert("PWD", m_workingDir);
  }

  if (m_useTerminal) {
    wrapInTerminal(program, arguments);
  }

  return startWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::launchDirect(qint64& pid) const
{
  if (m_binary.isEmpty()) {
    return false;
  }

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, m_binary, m_arguments, program, arguments);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  cleanAppImageEnv(env);
  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }
  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  if (m_useTerminal) {
    wrapInTerminal(program, arguments);
  }

  return startWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::ensureSteamRunning()
{
  QProcess pgrep;
  pgrep.start("pgrep", {"-x", "steam"});
  if (pgrep.waitForFinished(2000) && pgrep.exitCode() == 0) {
    return true;
  }

  qint64 pid = -1;
  if (QProcess::startDetached("steam", {"-silent"}, QString(), &pid)) {
    MOBase::log::warn("Steam was not running, started it in silent mode");
    return true;
  }

  return false;
}
