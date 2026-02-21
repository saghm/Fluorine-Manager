#include "protonlauncher.h"

#include <nak_ffi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <log.h>


namespace
{
// Restore the pre-AppImage environment for child processes (umu-run, Proton,
// pressure-vessel).  The AppRun script saves FLUORINE_ORIG_* vars before
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

  // AppImage runtime injects these — they can confuse pressure-vessel/umu-run.
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
  if (prefixDir.dirName() == "pfx") {
    if (prefixDir.cdUp()) {
      return QDir::cleanPath(prefixDir.absolutePath());
    }
  }

  return QDir::cleanPath(QFileInfo(prefixPath).dir().absolutePath());
}

QString detectSteamPath()
{
  if (char* steamPathRaw = nak_find_steam_path(); steamPathRaw != nullptr) {
    const QString steamPath = QString::fromUtf8(steamPathRaw).trimmed();
    nak_string_free(steamPathRaw);

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

bool startDetachedWithEnv(const QString& program, const QStringList& arguments,
                          const QString& workingDir,
                          const QProcessEnvironment& environment, qint64& pid)
{
  QProcess process;
  process.setProgram(program);
  process.setArguments(arguments);

  if (!workingDir.isEmpty()) {
    process.setWorkingDirectory(workingDir);
  }

  process.setProcessEnvironment(environment);
  return process.startDetached(&pid);
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

void maybeWrapWithSteamRun(bool useSteamRun, QString& program, QStringList& arguments)
{
  if (!useSteamRun) {
    return;
  }

  QStringList wrappedArgs;
  wrappedArgs.append(program);
  wrappedArgs.append(arguments);
  program   = QStringLiteral("steam-run");
  arguments = wrappedArgs;
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
    : m_steamAppId(0), m_useUmu(false), m_preferSystemUmu(false),
      m_useSteamRun(false), m_useSteamDrm(true)
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

ProtonLauncher& ProtonLauncher::setUmu(bool useUmu)
{
  m_useUmu = useUmu;
  return *this;
}

ProtonLauncher& ProtonLauncher::setPreferSystemUmu(bool preferSystemUmu)
{
  m_preferSystemUmu = preferSystemUmu;
  return *this;
}

ProtonLauncher& ProtonLauncher::setUseSteamRun(bool useSteamRun)
{
  m_useSteamRun = useSteamRun;
  return *this;
}

ProtonLauncher& ProtonLauncher::setSteamDrm(bool useSteamDrm)
{
  m_useSteamDrm = useSteamDrm;
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

std::pair<bool, qint64> ProtonLauncher::launch() const
{
  qint64 pid = -1;

  if (m_useUmu) {
    if (launchWithUmu(pid)) {
      return {true, pid};
    }
    MOBase::log::warn("UMU launch failed, falling back to Proton");
  }

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

  const QStringList protonArgs = QStringList() << "run" << m_binary << m_arguments;

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, protonScript, protonArgs, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  cleanAppImageEnv(env);

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

  env.insert("DOTNET_ROOT", "");
  env.insert("DOTNET_MULTILEVEL_LOOKUP", "0");

  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  // Set DXVK config if available
  if (char* dxvkPath = nak_get_dxvk_conf_path(); dxvkPath != nullptr) {
    const QString dxvkConf = QString::fromUtf8(dxvkPath);
    nak_string_free(dxvkPath);
    if (QFileInfo::exists(dxvkConf)) {
      env.insert("DXVK_CONFIG_FILE", dxvkConf);
    }
  }

  MOBase::log::info("Proton launch: '{}' run '{}'", protonScript, m_binary);

  if (!m_workingDir.isEmpty()) {
    env.insert("PWD", m_workingDir);
  }

  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::launchWithUmu(qint64& pid) const
{
  if (m_binary.isEmpty()) {
    return false;
  }

  // Steam must be running for games with Steamworks DRM (Application Load
  // Error 5:0000065434 occurs otherwise).
  if (m_useSteamDrm) {
    ensureSteamRunning();
  }

  // Resolve umu-run: prefer the copy deployed to the Fluorine data dir
  // (~/.local/share/fluorine/umu-run), then system PATH, then AppImage bundled.
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString bundled = appDir + QStringLiteral("/umu-run");
  const QString dataDir = QDir::home().filePath(".local/share/fluorine/umu-run");

  // Search PATH for a system umu-run, excluding our own app directory
  // (the launcher prepends it to PATH, so findExecutable would find the
  // bundled copy otherwise).
  QString system;
  const QStringList pathDirs =
      QString::fromLocal8Bit(qgetenv("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
  for (const QString& dir : pathDirs) {
    if (QDir(dir) == QDir(appDir))
      continue;
    const QString candidate = QStandardPaths::findExecutable(
        QStringLiteral("umu-run"), {dir});
    if (!candidate.isEmpty()) {
      system = candidate;
      break;
    }
  }

  // Priority: data dir > system (if preferred) > bundled > system (fallback)
  QString umuRun;
  if (QFileInfo::exists(dataDir)) {
    umuRun = dataDir;
  } else if (m_preferSystemUmu && !system.isEmpty()) {
    umuRun = system;
  } else if (QFileInfo::exists(bundled)) {
    umuRun = bundled;
  } else if (!system.isEmpty()) {
    umuRun = system;
  }

  MOBase::log::info("umu-run: dataDir='{}' (exists={}), bundled='{}' (exists={}), system='{}', selected='{}'",
      dataDir, QFileInfo::exists(dataDir), bundled, QFileInfo::exists(bundled), system, umuRun);

  if (umuRun.isEmpty()) {
    MOBase::log::warn("umu-run not found (bundled or in PATH)");
    return false;
  }

  const QStringList umuArgs = QStringList() << m_binary << m_arguments;

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, umuRun, umuArgs, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  cleanAppImageEnv(env);

  if (!m_prefixPath.isEmpty()) {
    env.insert("WINEPREFIX", m_prefixPath);
  }

  if (!m_protonPath.isEmpty()) {
    env.insert("PROTONPATH", m_protonPath);
  }

  // umu-run sets STEAM_COMPAT_DATA_PATH internally from WINEPREFIX, so we
  // do NOT set it here.  However, ensure tracked_files exists for Proton.
  ensureTrackedFilesExist(compatDataPathFromPrefix(m_prefixPath));

  uint32_t effectiveSteamAppId = m_steamAppId;
  if (effectiveSteamAppId == 0) {
    bool ok = false;
    const QString inheritedSteamAppId =
        env.value("SteamAPPId", env.value("SteamAppId")).trimmed();
    const uint32_t parsed = inheritedSteamAppId.toUInt(&ok);
    if (ok) {
      effectiveSteamAppId = parsed;
    }
  }

  // Always pass the game's Steam App ID as GAMEID for protonfixes lookup.
  // GAMEID identifies the game (for compatibility fixes), separate from DRM.
  if (effectiveSteamAppId != 0) {
    env.insert("GAMEID", QStringLiteral("umu-") + QString::number(effectiveSteamAppId));
  } else {
    env.insert("GAMEID", QStringLiteral("umu-0"));
  }

  if (m_useSteamDrm) {
    // Steam DRM games need the Steam client path and Steam identity env vars.
    env.insert("STORE", QStringLiteral("steam"));
    const QString steamPath = detectSteamPath();
    if (!steamPath.isEmpty()) {
      env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath);
    }
    if (effectiveSteamAppId != 0) {
      env.insert("SteamAppId", QString::number(effectiveSteamAppId));
      env.insert("SteamGameId", QString::number(effectiveSteamAppId));
    }
  } else {
    // Non-Steam games: do NOT set SteamAppId/SteamGameId — those can trigger
    // Steamworks initialization in Wine which crashes GOG/Epic games.
    env.remove("SteamAPPId");
    env.remove("SteamAppId");
    env.remove("SteamGameId");

    // Set STORE so umu-run knows this is a non-Steam game.  Without STORE,
    // umu-run may attempt Steam-specific behavior that breaks GOG/Epic titles.
    // umu-run expects lowercase values (gog, egs, etc.).
    if (!m_storeVariant.isEmpty()) {
      env.insert("STORE", m_storeVariant.toLower());
    } else {
      env.insert("STORE", QStringLiteral("gog"));
    }
  }

  // Remove FUSE VFS file descriptors — these are Fluorine-internal and must
  // not leak into game processes (especially pressure-vessel containers).
  env.remove("_FUSE_COMMFD");
  env.remove("_FUSE_COMMFD2");

  // Remove Qt/KDE theme vars that can confuse pressure-vessel/Wine.
  env.remove("QML_DISABLE_DISK_CACHE");
  env.remove("QT_ICON_THEME_NAME");
  env.remove("QT_STYLE_OVERRIDE");
  env.remove("OLDPWD");

  if (!m_workingDir.isEmpty()) {
    env.insert("PWD", m_workingDir);
  }

  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  // Set DXVK config if available
  if (char* dxvkPath = nak_get_dxvk_conf_path(); dxvkPath != nullptr) {
    const QString dxvkConf = QString::fromUtf8(dxvkPath);
    nak_string_free(dxvkPath);
    if (QFileInfo::exists(dxvkConf)) {
      env.insert("DXVK_CONFIG_FILE", dxvkConf);
    }
  }

  MOBase::log::info("UMU launch: '{}' '{}' (GAMEID={}, STORE={}, steam_drm={})",
                    umuRun, m_binary,
                    env.value("GAMEID"),
                    env.value("STORE", "<unset>"),
                    m_useSteamDrm);

  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
}

bool ProtonLauncher::launchDirect(qint64& pid) const
{
  if (m_binary.isEmpty()) {
    return false;
  }

  QString program;
  QStringList arguments;
  wrapProgram(m_wrapperCommands, m_binary, m_arguments, program, arguments);
  maybeWrapWithSteamRun(m_useSteamRun, program, arguments);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.remove("PYTHONHOME");
  cleanAppImageEnv(env);
  for (auto it = m_wrapperEnvVars.cbegin(); it != m_wrapperEnvVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }
  for (auto it = m_envVars.cbegin(); it != m_envVars.cend(); ++it) {
    env.insert(it.key(), it.value());
  }

  return startDetachedWithEnv(program, arguments, m_workingDir, env, pid);
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
