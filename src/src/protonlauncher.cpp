#include "protonlauncher.h"

#include <nak_ffi.h>
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
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
    : m_steamAppId(0), m_useSteamRun(false), m_useSteamDrm(true)
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

  // When Steam DRM is disabled (e.g. GOG games), set UMU_ID so that
  // Proton-GE skips the built-in steam.exe bridge.  Without this, Proton
  // tries to initialise the Steam client which causes an assertion failure
  // for non-Steam executables.
  if (!m_useSteamDrm) {
    env.insert("UMU_ID", "fluorine");
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
