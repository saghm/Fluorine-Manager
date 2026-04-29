#include "prefixsetuprunner.h"

#include "fluorinepaths.h"

#include "gamedetection.h"
#include "slrmanager.h"
#include "steamdetection.h"
#include "prefixsymlinks.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>

#include <log.h>

#include <csignal>
#include <sys/types.h>

// ============================================================================
// Constants
// ============================================================================

static const char* WINETRICKS_URL =
    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks";

static const char* CABEXTRACT_URL =
    "https://github.com/SulfurNitride/NaK/releases/download/Cabextract/"
    "cabextract-linux-x86_64.zip";

static const char* SEVENZIP_URL =
    "https://github.com/ip7z/7zip/releases/download/26.00/7z2600-linux-x86.tar.xz";

static const char* DOTNET9_RUNTIME_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Runtime/9.0.14/"
    "dotnet-runtime-9.0.14-win-x64.exe";

static const char* DOTNET10_SDK_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.201/"
    "dotnet-sdk-10.0.201-win-x64.exe";

// d3dcompiler_47: prebuilt DLLs from Mozilla's fxc2 repo (Windows 8.1 SDK redist).
static const char* D3DCOMPILER_47_32_URL =
    "https://github.com/mozilla/fxc2/raw/master/dll/d3dcompiler_47_32.dll";
static const char* D3DCOMPILER_47_32_SHA256 =
    "2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851";
static const char* D3DCOMPILER_47_64_URL =
    "https://github.com/mozilla/fxc2/raw/master/dll/d3dcompiler_47.dll";
static const char* D3DCOMPILER_47_64_SHA256 =
    "4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3";

// DirectX End-User Runtimes (June 2010) — shared by d3dcompiler_43, d3dx9,
// d3dx11_43, xact, xact_x64.
static const char* DIRECTX_JUN2010_URL =
    "https://files.holarse-linuxgaming.de/mirrors/microsoft/"
    "directx_Jun2010_redist.exe";
static const char* DIRECTX_JUN2010_SHA256 =
    "8746ee1a84a083a90e37899d71d50d5c7c015e69688a466aa80447f011780c0d";

// Visual C++ 2015-2022 redistributable.
static const char* VCRUN2022_X86_URL = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
static const char* VCRUN2022_X64_URL = "https://aka.ms/vs/17/release/vc_redist.x64.exe";

// .NET runtimes (x86 + x64 pairs for Wine prefix).
static const char* DOTNET6_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/727d79cb-6a4c-4a6b-bd9e-af99ad62de0b/"
    "5cd3550f1589a2f1b3a240c745dd1023/dotnet-runtime-6.0.36-win-x86.exe";
static const char* DOTNET6_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/1a5fc50a-9222-4f33-8f73-3c78485a55c7/"
    "1cb55899b68fcb9d98d206ba56f28b66/dotnet-runtime-6.0.36-win-x64.exe";

static const char* DOTNET7_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/b2e820bd-b591-43df-ab10-1eeb7998cc18/"
    "661ca79db4934c6247f5c7a809a62238/dotnet-runtime-7.0.20-win-x86.exe";
static const char* DOTNET7_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/be7eaed0-4e32-472b-b53e-b08ac3433a22/"
    "fc99a5977c57cbfb93b4afb401953818/dotnet-runtime-7.0.20-win-x64.exe";

static const char* DOTNET8_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/3210417e-ab32-4d14-a152-1ad9a2fcfdd2/"
    "da097cee5aa85bd79b6d593e3866fb7f/dotnet-runtime-8.0.12-win-x86.exe";
static const char* DOTNET8_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/136f4593-e3cd-4d52-bc25-579cdf46e80c/"
    "8b98c1347293b48c56c3a68d72f586a1/dotnet-runtime-8.0.12-win-x64.exe";

static const char* DOTNET_DESKTOP6_X86_URL =
    "https://download.visualstudio.microsoft.com/download/pr/cdc314df-4a4c-4709-868d-b974f336f77f/"
    "acd5ab7637e456c8a3aa667661324f6d/windowsdesktop-runtime-6.0.36-win-x86.exe";
static const char* DOTNET_DESKTOP6_X64_URL =
    "https://download.visualstudio.microsoft.com/download/pr/f6b6c5dc-e02d-4738-9559-296e938dabcb/"
    "b66d365729359df8e8ea131197715076/windowsdesktop-runtime-6.0.36-win-x64.exe";


// Allowed drive letters to keep in the prefix.
static const QStringList ALLOWED_DRIVES = {"c:", "z:"};

/// Filter out Wine/winetricks noise from process output.
/// Returns true if the line should be shown to the user.
static bool shouldShowLogLine(const QString& line)
{
  // Wine fixme/trace/err spam (e.g. "00d4:fixme:wineusb:query_id ...")
  static const QRegularExpression wineDebugRe(
      R"(^[0-9a-f]{4}:(?:fixme|trace|err):.*)");
  if (wineDebugRe.match(line).hasMatch())
    return false;

  // Wine diagnostic messages
  if (line.contains("ntsync:") || line.contains("winediag:") ||
      line.contains("pressure-vessel-wrap["))
    return false;

  // Winetricks execution spam
  if (line.startsWith("Executing ") || line.startsWith("Using winetricks ") ||
      line.startsWith("Executing w_do_call "))
    return false;

  // Blank lines and separator lines
  if (line.isEmpty() || line == "------------------------------------------------------")
    return false;

  return true;
}

/// Emit process output, splitting by line and filtering noise.
static void emitFilteredOutput(PrefixSetupRunner* self, const QByteArray& data)
{
  const QList<QByteArray> lines = data.split('\n');
  for (const QByteArray& raw : lines) {
    const QString line = QString::fromUtf8(raw).trimmed();
    if (shouldShowLogLine(line))
      emit self->logMessage(line);
  }
}

// Wine registry settings (.reg file content).
static const char* WINE_SETTINGS_REG = R"(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\DllOverrides]
"dwrite.dll"="native,builtin"
"dwrite"="native,builtin"
"winmm.dll"="native,builtin"
"winmm"="native,builtin"
"version.dll"="native,builtin"
"version"="native,builtin"
"ArchiveXL.dll"="native,builtin"
"ArchiveXL"="native,builtin"
"Codeware.dll"="native,builtin"
"Codeware"="native,builtin"
"TweakXL.dll"="native,builtin"
"TweakXL"="native,builtin"
"input_loader.dll"="native,builtin"
"input_loader"="native,builtin"
"RED4ext.dll"="native,builtin"
"RED4ext"="native,builtin"
"mod_settings.dll"="native,builtin"
"mod_settings"="native,builtin"
"scc_lib.dll"="native,builtin"
"scc_lib"="native,builtin"
"dxgi.dll"="native,builtin"
"dxgi"="native,builtin"
"dbghelp.dll"="native,builtin"
"dbghelp"="native,builtin"
"d3d12.dll"="native,builtin"
"d3d12"="native,builtin"
"wininet.dll"="native,builtin"
"wininet"="native,builtin"
"winhttp.dll"="native,builtin"
"winhttp"="native,builtin"
"dinput.dll"="native,builtin"
"dinput8"="native,builtin"
"dinput8.dll"="native,builtin"
"d3dcompiler_42"="native"
"d3dcompiler_43"="native"
"d3dcompiler_47"="native"
"d3dx9_24"="native"
"d3dx9_25"="native"
"d3dx9_26"="native"
"d3dx9_27"="native"
"d3dx9_28"="native"
"d3dx9_29"="native"
"d3dx9_30"="native"
"d3dx9_31"="native"
"d3dx9_32"="native"
"d3dx9_33"="native"
"d3dx9_34"="native"
"d3dx9_35"="native"
"d3dx9_36"="native"
"d3dx9_37"="native"
"d3dx9_38"="native"
"d3dx9_39"="native"
"d3dx9_40"="native"
"d3dx9_41"="native"
"d3dx9_42"="native"
"d3dx9_43"="native"
"d3dx10_33"="native"
"d3dx10_34"="native"
"d3dx10_35"="native"
"d3dx10_36"="native"
"d3dx10_37"="native"
"d3dx10_38"="native"
"d3dx10_39"="native"
"d3dx10_40"="native"
"d3dx10_41"="native"
"d3dx10_42"="native"
"d3dx10_43"="native"
"d3dx11_42"="native"
"d3dx11_43"="native"
; xinput left as builtin on Linux — native breaks controllers (SDL path).
"xaudio2_0"="native,builtin"
"xaudio2_1"="native,builtin"
"xaudio2_2"="native,builtin"
"xaudio2_3"="native,builtin"
"xaudio2_4"="native,builtin"
"xaudio2_5"="native,builtin"
"xaudio2_6"="native,builtin"
"xaudio2_7"="native,builtin"
"x3daudio1_0"="native,builtin"
"x3daudio1_1"="native,builtin"
"x3daudio1_2"="native,builtin"
"x3daudio1_3"="native,builtin"
"x3daudio1_4"="native,builtin"
"x3daudio1_5"="native,builtin"
"x3daudio1_6"="native,builtin"
"x3daudio1_7"="native,builtin"
"xapofx1_1"="native,builtin"
"xapofx1_2"="native,builtin"
"xapofx1_3"="native,builtin"
"xapofx1_4"="native,builtin"
"xapofx1_5"="native,builtin"
"xactengine2_0"="native,builtin"
"xactengine2_1"="native,builtin"
"xactengine2_2"="native,builtin"
"xactengine2_3"="native,builtin"
"xactengine2_4"="native,builtin"
"xactengine2_5"="native,builtin"
"xactengine2_6"="native,builtin"
"xactengine2_7"="native,builtin"
"xactengine2_8"="native,builtin"
"xactengine2_9"="native,builtin"
"xactengine2_10"="native,builtin"
"xactengine3_0"="native,builtin"
"xactengine3_1"="native,builtin"
"xactengine3_2"="native,builtin"
"xactengine3_3"="native,builtin"
"xactengine3_4"="native,builtin"
"xactengine3_5"="native,builtin"
"xactengine3_6"="native,builtin"
"xactengine3_7"="native,builtin"
"concrt140"="native,builtin"
"msvcp140"="native,builtin"
"msvcp140_1"="native,builtin"
"msvcp140_2"="native,builtin"
"msvcp140_atomic_wait"="native,builtin"
"msvcp140_codecvt_ids"="native,builtin"
"vcamp140"="native,builtin"
"vccorlib140"="native,builtin"
"vcomp140"="native,builtin"
"vcruntime140"="native,builtin"
"vcruntime140_1"="native,builtin"

[HKEY_CURRENT_USER\Software\Wine]
"ShowDotFiles"="Y"

[HKEY_CURRENT_USER\Control Panel\Desktop]
"FontSmoothing"="2"
"FontSmoothingGamma"=dword:00000578
"FontSmoothingOrientation"=dword:00000001
"FontSmoothingType"=dword:00000002

[HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers]
@="~ HIGHDPIAWARE"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\Pandora Behaviour Engine+.exe\X11 Driver]
"Decorated"="N"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\Vortex.exe\X11 Driver]
"Decorated"="N"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SSEEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SSEEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO4Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO4Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\TES4Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\TES4Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\SF1Edit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FNVEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FNVEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xFOEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xFOEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xSFEEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xSFEEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xTESEdit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\xTESEdit64.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO3Edit.exe]
"Version"="winxp"

[HKEY_CURRENT_USER\Software\Wine\AppDefaults\FO3Edit64.exe]
"Version"="winxp"

; Native file browser integration
[HKEY_CLASSES_ROOT\Folder\shell\explore\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Directory\shell\explore\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Folder\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

[HKEY_CLASSES_ROOT\Directory\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe -nohome \"%1\""

; Native text editor integration
[HKEY_CLASSES_ROOT\txtfile\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe \"%1\""

[HKEY_CLASSES_ROOT\inifile\shell\open\command]
@="C:\\windows\\system32\\winebrowser.exe \"%1\""

[HKEY_CLASSES_ROOT\.txt]
@="txtfile"

[HKEY_CLASSES_ROOT\.ini]
@="inifile"

[HKEY_CLASSES_ROOT\.cfg]
@="txtfile"

[HKEY_CLASSES_ROOT\.log]
@="txtfile"

[HKEY_CLASSES_ROOT\.xml]
@="txtfile"

[HKEY_CLASSES_ROOT\.json]
@="txtfile"

[HKEY_CLASSES_ROOT\.yml]
@="txtfile"

[HKEY_CLASSES_ROOT\.yaml]
@="txtfile"
)";

// ============================================================================
// Construction
// ============================================================================

PrefixSetupRunner::PrefixSetupRunner(const QString& prefixPath,
                                     const QString& protonPath,
                                     uint32_t appId,
                                     QObject* parent)
    : QObject(parent)
    , m_prefixPath(prefixPath)
    , m_protonPath(protonPath)
    , m_appId(appId)
{
  m_wineBin       = findWineBinary();
  m_wineserverBin = findWineserverBinary();
  m_slrRunScript  = detectSLRRunScript();

  buildStepList();
}

// ============================================================================
// Step list construction
// ============================================================================

void PrefixSetupRunner::buildStepList()
{
  m_steps.clear();
  m_stepFunctions.clear();

  auto addStep = [this](const QString& id, const QString& name,
                        std::function<bool()> fn) {
    m_steps.append({.id=id, .displayName=name, .status=SetupStep::Pending, .errorMessage={}});
    m_stepFunctions.append(std::move(fn));
  };

  addStep("proton_init", "Initialize Wine Prefix",
          [this] { return stepProtonInit(); });

  addStep("drive_cleanup", "Clean Up Drive Letters",
          [this] { return stepDriveCleanup(); });

  // DirectX DLL extraction (cab-based, no Wine needed for most).
  addStep("directx_runtime", "DirectX Runtimes",
          [this] { return stepDirectXRuntime(); });

  // Runtime installers (run via Wine).
  addStep("vcrun2022", "Visual C++ 2022",
          [this] { return stepVcrun2022(); });
  addStep("dotnetdesktop6", ".NET Desktop Runtime 6",
          [this] { return stepDotNetInstallPair(DOTNET_DESKTOP6_X86_URL, DOTNET_DESKTOP6_X64_URL, ".NET Desktop 6"); });
  addStep("dotnet_runtimes", ".NET Runtimes (6-9)",
          [this] { return stepDotNetRuntimes(); });
  addStep("dotnet10_sdk", ".NET 10 SDK",
          [this] { return stepDotNetInstall(DOTNET10_SDK_URL, ".NET 10 SDK"); });

  addStep("game_detection", "Auto-Detect Games",
          [this] { return stepGameDetection(); });

  addStep("wine_registry", "Wine Registry Settings",
          [this] { return stepWineRegistry(); });

  addStep("win11_mode", "Windows 11 Mode",
          [this] { return stepWin11Mode(); });

  addStep("post_setup", "Post-Setup (symlinks, dxvk)",
          [this] { return stepPostSetup(); });
}

// ============================================================================
// Execution
// ============================================================================

void PrefixSetupRunner::start()
{
  m_cancelled.storeRelease(0);

  // Ensure tools are available before starting steps.
  // cabextract is needed for DirectX cab extraction and vcrun2022 workaround.
  // winetricks is only needed for win11 mode (non-fatal if missing).
  if (!ensureCabextract()) {
    emit finished(false);
    return;
  }
  ensureWinetricks();  // Best-effort for win11 step.

  const int total = m_steps.size();
  bool allOk = true;

  for (int i = 0; i < total; ++i) {
    if (isCancelled()) {
      emit logMessage("Cancelled by user.");
      allOk = false;
      break;
    }

    if (m_steps[i].status == SetupStep::Succeeded)
      continue;

    const bool stepOk = runStep(i);
    allOk             = stepOk && allOk;
    emit progressChanged(static_cast<float>(i + 1) / total);

    // The prefix-init step is a hard prerequisite for everything that
    // follows. If it fails (e.g. broken Proton install), running downstream
    // steps just produces a cascade of misleading "version mismatch" errors
    // against a half-initialized prefix. Surface the real error and stop.
    if (!stepOk && m_steps[i].id == "proton_init") {
      emit logMessage(
          "Prefix initialization failed — skipping remaining setup steps. "
          "Fix the Proton installation and retry.");
      break;
    }
  }

  emit finished(allOk);
}

void PrefixSetupRunner::retryFailed()
{
  m_cancelled.storeRelease(0);

  const int total = m_steps.size();
  bool allOk = true;

  for (int i = 0; i < total; ++i) {
    if (isCancelled()) break;

    if (m_steps[i].status != SetupStep::Failed)
      continue;

    allOk = runStep(i) && allOk;
  }

  emit finished(allOk);
}

void PrefixSetupRunner::retryStep(int index)
{
  if (index < 0 || index >= m_steps.size()) return;
  m_cancelled.storeRelease(0);

  runStep(index);

  // Check if everything is now good.
  bool allOk = true;
  for (const auto& s : m_steps) {
    if (s.status == SetupStep::Failed) { allOk = false; break; }
  }
  emit finished(allOk);
}

bool PrefixSetupRunner::runStep(int index)
{
  m_steps[index].status       = SetupStep::Running;
  m_steps[index].errorMessage.clear();
  emit stepStarted(index);

  const bool ok = m_stepFunctions[index]();

  m_steps[index].status = ok ? SetupStep::Succeeded : SetupStep::Failed;
  if (!ok && m_steps[index].errorMessage.isEmpty())
    m_steps[index].errorMessage = "Step failed (see log for details)";

  emit stepFinished(index, ok, m_steps[index].errorMessage);
  return ok;
}

// ============================================================================
// Process execution
// ============================================================================

QProcess* PrefixSetupRunner::buildWrappedProcess(
    const QString& exe,
    const QMap<QString, QString>& extraEnv)
{
  auto* proc = new QProcess(this);

  // Start from the system environment and clean AppImage vars.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Remove AppImage/Fluorine vars that can confuse Wine.
  for (const char* var : {"QT_QPA_PLATFORM_PLUGIN_PATH", "MO2_PLUGINS_DIR",
       "MO2_LIBS_DIR", "MO2_PYTHON_DIR", "MO2_BASE_DIR",
       "APPIMAGE", "APPDIR", "OWD", "ARGV0",
       "APPIMAGE_ORIGINAL_EXEC", "DESKTOPINTEGRATION"}) {
    env.remove(var);
  }

  // Restore pre-AppImage environment if available.
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("PATH",            "FLUORINE_ORIG_PATH");
  restoreOrStrip("XDG_DATA_DIRS",   "FLUORINE_ORIG_XDG_DATA_DIRS");
  restoreOrStrip("QT_PLUGIN_PATH",  "FLUORINE_ORIG_QT_PLUGIN_PATH");

  // Expose the injected xrandr (steamrt4 ships without it) so protonfixes
  // and Proton-GE init scripts can find it. Pressure-vessel forces PATH
  // inside the container, so we prepend the xrandr dir on the HOST PATH
  // and also pass it through --filesystem below.
  const QString xrandrDir =
      QDir::homePath() + "/.local/share/fluorine/steamrt/xrandr-bin";
  if (QDir(xrandrDir).exists()) {
    const QString existing = env.value("PATH");
    env.insert("PATH", existing.isEmpty()
                           ? xrandrDir
                           : xrandrDir + QLatin1Char(':') + existing);
  }

  // Apply caller-provided env vars.
  for (auto it = extraEnv.begin(); it != extraEnv.end(); ++it) {
    env.insert(it.key(), it.value());
  }

  proc->setProcessEnvironment(env);

  // Wrap in SLR if available.
  if (!m_slrRunScript.isEmpty()) {
    QStringList slrArgs;

    // Expose the executable's parent directory.
    const QString exeDir = QFileInfo(exe).absolutePath();
    if (!exeDir.isEmpty() && QDir(exeDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(exeDir);

    // Expose the Wine prefix.
    if (!m_prefixPath.isEmpty())
      slrArgs << QStringLiteral("--filesystem=%1").arg(m_prefixPath);

    // Expose Proton directory.
    if (!m_protonPath.isEmpty())
      slrArgs << QStringLiteral("--filesystem=%1").arg(m_protonPath);

    // Expose fluorine bin dir (cabextract, winetricks).
    const QString binDir = fluorineBinDir();
    if (QDir(binDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(binDir);

    // Expose cache dir.
    const QString cacheDir = fluorineCacheDir();
    if (QDir(cacheDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(cacheDir);

    // Expose the injected xrandr bin dir so Proton-GE's protonfixes can
    // invoke it during wineboot -u. Without this the container's PATH
    // (forced to /usr/bin:/bin) has no xrandr and init fails on some
    // modern Proton builds. See issue #49.
    if (QDir(xrandrDir).exists())
      slrArgs << QStringLiteral("--filesystem=%1").arg(xrandrDir);

    // Pressure-vessel resets PATH inside the container. Wrap the exec
    // through /usr/bin/env to inject the xrandr dir back into the
    // container's PATH.
    if (QDir(xrandrDir).exists()) {
      slrArgs << "--" << QStringLiteral("/usr/bin/env")
              << QStringLiteral("PATH=%1:/usr/bin:/bin").arg(xrandrDir) << exe;
    } else {
      slrArgs << "--" << exe;
    }

    proc->setProgram(m_slrRunScript);
    proc->setArguments(slrArgs);
  } else {
    proc->setProgram(exe);
  }

  return proc;
}

int PrefixSetupRunner::runProcess(const QString& exe,
                                  const QStringList& args,
                                  const QMap<QString, QString>& extraEnv,
                                  int timeoutMs,
                                  QByteArray* captured)
{
  QProcess* proc = buildWrappedProcess(exe, extraEnv);

  // Append the actual arguments after the SLR wrapper arguments.
  QStringList fullArgs = proc->arguments();
  fullArgs.append(args);
  proc->setArguments(fullArgs);

  proc->setProcessChannelMode(QProcess::MergedChannels);
  proc->start();

  // Poll for output and cancellation.
  while (proc->state() != QProcess::NotRunning) {
    proc->waitForReadyRead(250);

    if (proc->canReadLine() || proc->bytesAvailable() > 0) {
      const QByteArray data = proc->readAll();
      if (!data.isEmpty()) {
        if (captured) captured->append(data);
        emitFilteredOutput(this, data);
      }
    }

    if (isCancelled()) {
      proc->kill();
      proc->waitForFinished(5000);
      proc->deleteLater();
      return -1;
    }
  }

  const QByteArray remaining = proc->readAll();
  if (!remaining.isEmpty()) {
    if (captured) captured->append(remaining);
    emitFilteredOutput(this, remaining);
  }

  const int exitCode = proc->exitCode();
  proc->deleteLater();
  return exitCode;
}

int PrefixSetupRunner::runHostProcess(const QString& exe,
                                      const QStringList& args,
                                      int timeoutMs)
{
  QProcess proc;
  proc.setProgram(exe);
  proc.setArguments(args);
  proc.setProcessChannelMode(QProcess::MergedChannels);

  // Clean AppImage/Fluorine env so host tools find their system libraries.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreOrStrip = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("PATH",            "FLUORINE_ORIG_PATH");
  proc.setProcessEnvironment(env);

  proc.start();

  while (proc.state() != QProcess::NotRunning) {
    proc.waitForReadyRead(250);
    if (proc.bytesAvailable() > 0)
      emitFilteredOutput(this, proc.readAll());
    if (isCancelled()) {
      proc.kill();
      proc.waitForFinished(5000);
      return -1;
    }
  }

  const QByteArray remaining = proc.readAll();
  if (!remaining.isEmpty())
    emitFilteredOutput(this, remaining);

  return proc.exitCode();
}

int PrefixSetupRunner::runHostProcessWithEnv(const QString& exe,
                                             const QStringList& args,
                                             const QMap<QString, QString>& extraEnv,
                                             int timeoutMs)
{
  QProcess proc;
  proc.setProgram(exe);
  proc.setArguments(args);
  proc.setProcessChannelMode(QProcess::MergedChannels);

  // Start from cleaned host environment, then apply caller's env vars.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  auto restoreVar = [&env](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty()) env.remove(var);
      else env.insert(var, orig);
      env.remove(origVar);
    }
  };
  restoreVar("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreVar("LD_PRELOAD",      "FLUORINE_ORIG_LD_PRELOAD");

  for (auto it = extraEnv.begin(); it != extraEnv.end(); ++it)
    env.insert(it.key(), it.value());

  proc.setProcessEnvironment(env);
  proc.start();

  while (proc.state() != QProcess::NotRunning) {
    proc.waitForReadyRead(250);
    if (proc.bytesAvailable() > 0)
      emitFilteredOutput(this, proc.readAll());
    if (isCancelled()) {
      proc.kill();
      proc.waitForFinished(5000);
      return -1;
    }
  }

  const QByteArray remaining = proc.readAll();
  if (!remaining.isEmpty())
    emitFilteredOutput(this, remaining);

  return proc.exitCode();
}

// ============================================================================
// Step implementations
// ============================================================================

bool PrefixSetupRunner::stepProtonInit()
{
  const QString protonScript = findProtonScript();
  if (protonScript.isEmpty()) {
    m_steps.last().errorMessage = "Proton wrapper script not found";
    return false;
  }

  // Kill any stale wineboot/wineserver/pv-adverb processes bound to this
  // prefix. If a previous run was aborted mid-init, those processes hold
  // registry/filesystem locks and any new wineboot -u deadlocks waiting
  // for them. Nothing cleans them up automatically.
  killStalePrefixProcesses();

  // Proton-GE invokes `xrandr` during protonfixes at wineboot time. The
  // steamrt4 pressure-vessel container ships without it, so back-fill our
  // injected copy before init if missing — otherwise the protonfix
  // silently no-ops and prefix setup can hang or fall back to a broken
  // state on multi-monitor machines. See issue #49.
  if (!isXrandrInjected()) {
    emit logMessage("xrandr helper missing; downloading…");
    ensureXrandrInstalled(
        nullptr, [this](const QString& msg) { emit logMessage(msg); });
  }

  const QString steamPath = detectSteamPath();

  // The compatdata path is the PARENT of the pfx directory.
  const QString compatDataPath = QDir(m_prefixPath).filePath("..");
  const QString cleanCompat    = QDir::cleanPath(compatDataPath);

  QMap<QString, QString> env;
  env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = steamPath;
  env["STEAM_COMPAT_DATA_PATH"]           = cleanCompat;
  env["SteamAppId"]                       = QString::number(m_appId);
  env["SteamGameId"]                      = QString::number(m_appId);
  // Keep DISPLAY/WAYLAND_DISPLAY from the host: Proton-GE protonfixes runs
  // `xrandr` during wineboot -u to detect monitors, and xrandr exits 1 if
  // it can't open a display, which cascades to a failed prefix init on
  // newer Proton-GE builds. wineboot -u is unattended and doesn't pop UI.
  env["WINEDLLOVERRIDES"] = "msdia80.dll=n;conhost.exe=d;cmd.exe=d";
  // ntsync on kernel 7.0+ can deadlock wineboot -u during prefix init
  // under Proton 11 (wineboot blocks in ntsync_char_ioctl forever). Force
  // the older fsync/esync fallback for the init phase — once the prefix
  // is created, regular game launches can use whatever sync they want.
  env["WINE_DISABLE_FAST_SYNC"] = "1";
  env["PROTON_NO_NTSYNC"]       = "1";
  env["WINENTSYNC"]             = "0";

  emit logMessage("Initializing Wine prefix with Proton...");

  QByteArray protonOutput;
  // waitforexitandrun (not "run") is required for Proton 11+ which uses
  // use_sessions=1 — a plain "run" forks into a session manager that never
  // exits, hanging prefix init.
  const int rc = runProcess(protonScript,
                            {"waitforexitandrun", "wineboot", "-u"},
                            env, -1, &protonOutput);
  if (rc != 0) {
    // Detect a broken Proton install: setup_prefix copies DLLs from Proton's
    // bundled default_pfx template, so a FileNotFoundError there means the
    // Proton distribution itself is missing files — not a Fluorine issue.
    const QByteArray out = protonOutput;
    if (out.contains("FileNotFoundError") &&
        out.contains("default_pfx/drive_c")) {
      int start = out.indexOf("default_pfx/drive_c");
      int end   = out.indexOf('\'', start);
      if (end < 0) end = out.indexOf('"', start);
      const QString missing =
          (end > start) ? QString::fromUtf8(out.mid(start, end - start))
                        : QStringLiteral("default_pfx/drive_c/...");
      m_steps.last().errorMessage =
          QStringLiteral(
              "Proton install is incomplete: missing '%1'. "
              "Reinstall or verify your Proton build (e.g. GE-Proton) — "
              "this is not a Fluorine bug.")
              .arg(missing);
    } else {
      m_steps.last().errorMessage =
          QStringLiteral("proton wineboot failed (exit code %1)").arg(rc);
    }
    return false;
  }

  // Wait briefly for files to settle.
  QThread::sleep(2);

  if (!QDir(m_prefixPath).exists()) {
    m_steps.last().errorMessage = "Prefix directory not created after wineboot";
    return false;
  }

  return true;
}

bool PrefixSetupRunner::stepDriveCleanup()
{
  const QString dosdevices = m_prefixPath + "/dosdevices";
  if (!QDir(dosdevices).exists()) {
    emit logMessage("dosdevices not found, skipping drive cleanup");
    return true;
  }

  emit logMessage("Removing unwanted drive letters...");

  QStringList removed;
  const QDir dir(dosdevices);
  for (const QString& entry : dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries)) {
    const QString lower = entry.toLower();
    if (lower.length() != 2 || !lower.endsWith(':'))
      continue;
    if (!lower.at(0).isLetter())
      continue;
    if (ALLOWED_DRIVES.contains(lower))
      continue;

    QFile::remove(dir.filePath(entry));
    removed << entry.toUpper();
  }

  if (!removed.isEmpty()) {
    emit logMessage(QStringLiteral("Removed drive symlinks: %1").arg(removed.join(", ")));

    // Clean registry entries for removed drives.
    const QString tmpDir = fluorineTmpDir();
    QDir().mkpath(tmpDir);

    QString regContent = "Windows Registry Editor Version 5.00\n\n";
    for (const QString& drive : removed) {
      regContent += QStringLiteral(
          "[HKEY_LOCAL_MACHINE\\Software\\Wine\\Drives]\n\"%1\"=-\n\n").arg(drive);
    }

    const QString regFile = tmpDir + "/drive_cleanup.reg";
    QFile f(regFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      f.write(regContent.toUtf8());
      f.close();

      QMap<QString, QString> env = baseWineEnv();
      env["WINEDLLOVERRIDES"] = "mshtml=d";
      env["PROTON_USE_XALIA"] = "0";

      runProcess(m_wineBin, {"regedit", regFile}, env);
      QFile::remove(regFile);
    }
  }

  return true;
}

// ============================================================================
// DirectX cab extraction helpers
// ============================================================================

bool PrefixSetupRunner::ensureDirectXRedist(QString& redistPath)
{
  const QString cacheDir = fluorineCacheDir() + "/directx9";
  QDir().mkpath(cacheDir);
  redistPath = cacheDir + "/directx_Jun2010_redist.exe";

  if (!QFileInfo::exists(redistPath)) {
    emit logMessage("Downloading DirectX June 2010 redistributable...");
    return downloadAndVerify(DIRECTX_JUN2010_URL, redistPath, DIRECTX_JUN2010_SHA256);
  }

  if (!verifySha256(redistPath, DIRECTX_JUN2010_SHA256)) {
    emit logMessage("Cached redist has bad checksum, re-downloading...");
    QFile::remove(redistPath);
    return downloadAndVerify(DIRECTX_JUN2010_URL, redistPath, DIRECTX_JUN2010_SHA256);
  }

  return true;
}

bool PrefixSetupRunner::extractFromRedist(const QString& redistPath,
                                          const QString& cabFilter,
                                          const QString& dllFilter,
                                          const QString& destDir)
{
  const QString tmpDir = fluorineTmpDir() + "/dxextract";
  QDir().mkpath(tmpDir);

  // Clean tmp dir.
  QDir tmp(tmpDir);
  for (const QString& f : tmp.entryList(QDir::Files))
    tmp.remove(f);

  const QString cabextractBin = fluorineBinDir() + "/cabextract";

  // Stage 1: extract inner cab(s) matching filter from the outer redist.
  int rc = runHostProcess(cabextractBin,
      {"-d", tmpDir, "-L", "-F", cabFilter, redistPath});
  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("cabextract failed (filter: %1, exit %2)").arg(cabFilter).arg(rc);
    return false;
  }

  // Stage 2: extract DLL(s) from each inner cab.
  const QStringList cabs = QDir(tmpDir).entryList({"*.cab"}, QDir::Files);
  if (cabs.isEmpty()) {
    m_steps.last().errorMessage =
        QStringLiteral("No inner cab found for filter: %1").arg(cabFilter);
    return false;
  }

  for (const QString& cab : cabs) {
    rc = runHostProcess(cabextractBin,
        {"-d", destDir, "-L", "-F", dllFilter, tmpDir + "/" + cab});
    if (rc != 0) {
      m_steps.last().errorMessage =
          QStringLiteral("cabextract inner failed (filter: %1, exit %2)").arg(dllFilter).arg(rc);
      return false;
    }
  }

  // Clean up tmp.
  QDir(tmpDir).removeRecursively();
  return true;
}

// ============================================================================
// DirectX DLL steps
// ============================================================================

bool PrefixSetupRunner::stepD3DCompiler47()
{
  emit logMessage("Installing d3dcompiler_47...");

  const QString cacheDir = fluorineCacheDir() + "/d3dcompiler_47";
  QDir().mkpath(cacheDir);

  // On a win64 prefix (matching real Windows):
  //   system32  = 64-bit DLLs
  //   syswow64  = 32-bit DLLs
  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  // 32-bit DLL → syswow64
  {
    const QString cached = cacheDir + "/d3dcompiler_47_32.dll";
    if (!QFileInfo::exists(cached)) {
      emit logMessage("Downloading d3dcompiler_47 (32-bit)...");
      if (!downloadAndVerify(D3DCOMPILER_47_32_URL, cached, D3DCOMPILER_47_32_SHA256))
        return false;
    }
    const QString dest = dllDir32 + "/d3dcompiler_47.dll";
    QFile::remove(dest);
    if (!QFile::copy(cached, dest)) {
      m_steps.last().errorMessage = "Failed to copy d3dcompiler_47.dll to syswow64";
      return false;
    }
  }

  // 64-bit DLL → system32
  {
    const QString cached = cacheDir + "/d3dcompiler_47.dll";
    if (!QFileInfo::exists(cached)) {
      emit logMessage("Downloading d3dcompiler_47 (64-bit)...");
      if (!downloadAndVerify(D3DCOMPILER_47_64_URL, cached, D3DCOMPILER_47_64_SHA256))
        return false;
    }
    const QString dest = dllDir64 + "/d3dcompiler_47.dll";
    QFile::remove(dest);
    if (!QFile::copy(cached, dest)) {
      m_steps.last().errorMessage = "Failed to copy d3dcompiler_47.dll to system32";
      return false;
    }
  }

  emit logMessage("d3dcompiler_47 installed");
  return true;
}

bool PrefixSetupRunner::stepDirectXRuntime()
{
  emit logMessage("Installing DirectX runtimes...");

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  // d3dcompiler_47: prebuilt DLLs from Mozilla fxc2 (not in the June 2010 redist).
  if (!stepD3DCompiler47())
    return false;

  // Everything else comes from the DirectX June 2010 redist.
  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  // All single-DLL extractions: {cabFilter, dllFilter, displayName}
  struct DllEntry { const char* cabFilter; const char* dllFilter; const char* name; };
  static const DllEntry singleDlls[] = {
    {.cabFilter="*d3dcompiler_42*", .dllFilter="d3dcompiler_42.dll", .name="d3dcompiler_42"},
    {.cabFilter="*d3dcompiler_43*", .dllFilter="d3dcompiler_43.dll", .name="d3dcompiler_43"},
    {.cabFilter="*d3dx11_42*",      .dllFilter="d3dx11_42.dll",      .name="d3dx11_42"},
    {.cabFilter="*d3dx11_43*",      .dllFilter="d3dx11_43.dll",      .name="d3dx11_43"},
  };

  // Multi-DLL extractions (wildcards): {cabFilter, dllFilter, displayName}
  static const DllEntry multiDlls[] = {
    {.cabFilter="*d3dx9*",    .dllFilter="d3dx9*.dll",  .name="d3dx9"},
    {.cabFilter="*d3dx10*",   .dllFilter="d3dx10*.dll", .name="d3dx10"},
    {.cabFilter="*_xinput_*", .dllFilter="xinput*.dll", .name="xinput"},
  };

  // Extract single DLLs (both arches).
  for (const auto& e : singleDlls) {
    if (isCancelled()) return false;
    emit logMessage(QStringLiteral("Extracting %1...").arg(e.name));
    extractFromRedist(redistPath, QStringLiteral("%1x86*").arg(e.cabFilter), e.dllFilter, dllDir32);
    extractFromRedist(redistPath, QStringLiteral("%1x64*").arg(e.cabFilter), e.dllFilter, dllDir64);
  }

  // Extract multi-DLLs (both arches).
  for (const auto& e : multiDlls) {
    if (isCancelled()) return false;
    emit logMessage(QStringLiteral("Extracting %1...").arg(e.name));
    extractFromRedist(redistPath, QStringLiteral("%1x86*").arg(e.cabFilter), e.dllFilter, dllDir32);
    extractFromRedist(redistPath, QStringLiteral("%1x64*").arg(e.cabFilter), e.dllFilter, dllDir64);
  }

  // XACT / XAudio / X3DAudio / XAPOFX — 32-bit.
  if (!isCancelled()) {
    emit logMessage("Extracting XACT (32-bit)...");
    for (const char* cabPat : {"*_xact_*x86*", "*_x3daudio_*x86*", "*_xaudio_*x86*"})
      for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"})
        extractFromRedist(redistPath, cabPat, dllPat, dllDir32);
  }

  // XACT — 64-bit.
  if (!isCancelled()) {
    emit logMessage("Extracting XACT (64-bit)...");
    for (const char* cabPat : {"*_xact_*x64*", "*_x3daudio_*x64*", "*_xaudio_*x64*"})
      for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"})
        extractFromRedist(redistPath, cabPat, dllPat, dllDir64);
  }

  // Batch-register XACT DLLs via regsvr32.
  auto collectRegDlls = [](const QString& dir) -> QStringList {
    QStringList dlls;
    dlls.append(QDir(dir).entryList({"xactengine*.dll"}, QDir::Files));
    for (int i = 0; i <= 7; ++i) {
      const QString dll = QStringLiteral("xaudio2_%1.dll").arg(i);
      if (QFileInfo::exists(dir + "/" + dll))
        dlls.append(dll);
    }
    return dlls;
  };

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  // 32-bit registration.
  QStringList reg32 = collectRegDlls(dllDir32);
  if (!reg32.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 32-bit DLLs...").arg(reg32.size()));
    QStringList args = {"regsvr32", "/S"};
    args.append(reg32);
    runProcess(m_wineBin, args, env);
  }

  // 64-bit registration.
  QStringList reg64 = collectRegDlls(dllDir64);
  if (!reg64.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 64-bit DLLs...").arg(reg64.size()));
    QStringList args = {"regsvr32", "/S"};
    args.append(reg64);
    runProcess(m_wineBin, args, env);
  }

  emit logMessage("DirectX runtimes installed");
  return true;
}

// ============================================================================
// Runtime installer steps
// ============================================================================

bool PrefixSetupRunner::stepVcrun2022()
{
  emit logMessage("Installing Visual C++ 2022...");

  const QString cacheDir = fluorineCacheDir() + "/vcrun2022";
  const QString tmpDir   = fluorineTmpDir() + "/vcrun2022";
  QDir().mkpath(cacheDir);
  QDir().mkpath(tmpDir);

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";
  const QString cabextractBin = fluorineBinDir() + "/cabextract";

  // Download x86 installer.
  const QString x86Path = cacheDir + "/vc_redist.x86.exe";
  if (!QFileInfo::exists(x86Path)) {
    emit logMessage("Downloading vc_redist.x86.exe...");
    if (!downloadFile(VCRUN2022_X86_URL, x86Path)) {
      m_steps.last().errorMessage = "Failed to download vc_redist.x86.exe";
      return false;
    }
  }

  // Wine bug #57518 workaround: manually extract msvcp140.dll before running
  // the installer, because the installer refuses to replace the builtin
  // (builtin version number is higher).
  emit logMessage("Extracting msvcp140.dll (32-bit)...");
  runHostProcess(cabextractBin, {"--directory=" + tmpDir + "/win32", x86Path, "-F", "a10"});
  runHostProcess(cabextractBin,
      {"--directory=" + dllDir32, tmpDir + "/win32/a10", "-F", "msvcp140.dll"});

  // Run 32-bit installer.
  emit logMessage("Running vc_redist.x86.exe...");
  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  int rc = runProcess(m_wineBin, {x86Path, "/install", "/quiet", "/norestart"}, env);
  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("vc_redist.x86.exe failed (exit code %1)").arg(rc);
    return false;
  }

  // Download and run x64 installer.
  const QString x64Path = cacheDir + "/vc_redist.x64.exe";
  if (!QFileInfo::exists(x64Path)) {
    emit logMessage("Downloading vc_redist.x64.exe...");
    if (!downloadFile(VCRUN2022_X64_URL, x64Path)) {
      m_steps.last().errorMessage = "Failed to download vc_redist.x64.exe";
      return false;
    }
  }

  emit logMessage("Extracting msvcp140.dll (64-bit)...");
  runHostProcess(cabextractBin, {"--directory=" + tmpDir + "/win64", x64Path, "-F", "a12"});
  runHostProcess(cabextractBin,
      {"--directory=" + dllDir64, tmpDir + "/win64/a12", "-F", "msvcp140.dll"});

  emit logMessage("Running vc_redist.x64.exe...");
  rc = runProcess(m_wineBin, {x64Path, "/install", "/quiet", "/norestart"}, env);
  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("vc_redist.x64.exe failed (exit code %1)").arg(rc);
    return false;
  }

  QDir(tmpDir).removeRecursively();
  emit logMessage("Visual C++ 2022 installed");
  return true;
}

bool PrefixSetupRunner::stepDotNetRuntimes()
{
  emit logMessage("Installing .NET Runtimes (6-9)...");

  struct RuntimePair { const char* url32; const char* url64; const char* name; };
  static const RuntimePair runtimes[] = {
    {.url32=DOTNET6_X86_URL, .url64=DOTNET6_X64_URL, .name=".NET 6"},
    {.url32=DOTNET7_X86_URL, .url64=DOTNET7_X64_URL, .name=".NET 7"},
    {.url32=DOTNET8_X86_URL, .url64=DOTNET8_X64_URL, .name=".NET 8"},
  };

  for (const auto& rt : runtimes) {
    if (isCancelled()) return false;
    if (!stepDotNetInstallPair(rt.url32, rt.url64, rt.name))
      return false;
  }

  // .NET 9 is 64-bit only.
  if (isCancelled()) return false;
  if (!stepDotNetInstall(DOTNET9_RUNTIME_URL, ".NET 9 Runtime"))
    return false;

  emit logMessage(".NET Runtimes installed");
  return true;
}

bool PrefixSetupRunner::stepDotNetInstallPair(const QString& url32, const QString& url64,
                                              const QString& name)
{
  const QString cacheDir = fluorineCacheDir();
  QDir().mkpath(cacheDir);

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  // Install 32-bit runtime.
  {
    const QString filename = QUrl(url32).fileName();
    const QString path     = cacheDir + "/" + filename;

    if (!QFileInfo::exists(path)) {
      emit logMessage(QStringLiteral("Downloading %1 (32-bit)...").arg(name));
      if (!downloadFile(url32, path)) {
        m_steps.last().errorMessage = QStringLiteral("Failed to download %1 x86").arg(name);
        return false;
      }
    }

    emit logMessage(QStringLiteral("Installing %1 (32-bit)...").arg(name));
    const int rc = runProcess(m_wineBin, {path, "/install", "/quiet", "/norestart"}, env);
    if (rc != 0) {
      m_steps.last().errorMessage =
          QStringLiteral("%1 x86 installer failed (exit code %2)").arg(name).arg(rc);
      return false;
    }
  }

  // Install 64-bit runtime.
  {
    const QString filename = QUrl(url64).fileName();
    const QString path     = cacheDir + "/" + filename;

    if (!QFileInfo::exists(path)) {
      emit logMessage(QStringLiteral("Downloading %1 (64-bit)...").arg(name));
      if (!downloadFile(url64, path)) {
        m_steps.last().errorMessage = QStringLiteral("Failed to download %1 x64").arg(name);
        return false;
      }
    }

    emit logMessage(QStringLiteral("Installing %1 (64-bit)...").arg(name));
    const int rc = runProcess(m_wineBin, {path, "/install", "/quiet", "/norestart"}, env);
    if (rc != 0) {
      m_steps.last().errorMessage =
          QStringLiteral("%1 x64 installer failed (exit code %2)").arg(name).arg(rc);
      return false;
    }
  }

  emit logMessage(QStringLiteral("%1 installed").arg(name));
  return true;
}

bool PrefixSetupRunner::stepDotNetInstall(const QString& url, const QString& name)
{
  const QString cacheDir = fluorineCacheDir();
  QDir().mkpath(cacheDir);

  const QString filename      = QUrl(url).fileName();
  const QString installerPath = cacheDir + "/" + filename;

  // Download if not cached.
  if (!QFileInfo::exists(installerPath)) {
    emit logMessage(QStringLiteral("Downloading %1...").arg(name));

    if (!downloadFile(url, installerPath)) {
      m_steps.last().errorMessage =
          QStringLiteral("Failed to download %1").arg(name);
      return false;
    }
  }

  emit logMessage(QStringLiteral("Installing %1...").arg(name));

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";

  const int rc = runProcess(
      m_wineBin,
      {installerPath, "/install", "/quiet", "/norestart"},
      env);

  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("%1 installer failed (exit code %2)").arg(name).arg(rc);
    return false;
  }

  return true;
}

bool PrefixSetupRunner::stepGameDetection()
{
  emit logMessage("Auto-detecting installed games...");

  GameScanResult scanResult = detectAllGames();
  if (scanResult.games.isEmpty()) {
    emit logMessage("No games detected");
    return true;
  }

  // Build a single .reg file with all game registry entries.
  QString regContent = QStringLiteral("Windows Registry Editor Version 5.00\n\n");
  int gameCount = 0;

  for (const DetectedGame& game : scanResult.games) {
    if (game.registry_path.isEmpty() || game.registry_value.isEmpty())
      continue;

    const QString& gameName    = game.name;
    const QString& installPath = game.install_path;
    const QString& rPath       = game.registry_path;
    const QString& rVal        = game.registry_value;

    // Convert Linux path to Wine Z: drive path with escaped backslashes.
    // Trailing backslash required — game launchers expect it (matches Steam's format).
    QString winePath = "Z:" + QString(installPath).replace('/', "\\\\");
    if (!winePath.endsWith("\\\\"))
      winePath += "\\\\";

    regContent += QStringLiteral(
        "[HKEY_LOCAL_MACHINE\\%1]\n\"%2\"=\"%3\"\n\n"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%4]\n\"%5\"=\"%6\"\n\n")
        .arg(rPath, rVal, winePath,
             rPath.mid(rPath.indexOf('\\') + 1),
             rVal, winePath);

    emit logMessage(QStringLiteral("  Found: %1 -> %2").arg(gameName, installPath));
    ++gameCount;
  }

  if (gameCount == 0) {
    emit logMessage("No games with valid registry paths");
    return true;
  }

  // Write and import the combined .reg file in a single regedit call.
  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);

  const QString regFile = tmpDir + "/game_registry.reg";
  QFile f(regFile);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_steps.last().errorMessage = "Failed to write game registry file";
    return false;
  }
  f.write(regContent.toUtf8());
  f.close();

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";
  env["PROTON_USE_XALIA"] = "0";

  emit logMessage(QStringLiteral("Applying registry for %1 game(s)...").arg(gameCount));
  const int rc = runProcess(m_wineBin, {"regedit", regFile}, env);
  QFile::remove(regFile);

  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("wine regedit failed (exit code %1)").arg(rc);
    return false;
  }

  emit logMessage(QStringLiteral("Configured %1 game(s) in registry").arg(gameCount));
  return true;
}

bool PrefixSetupRunner::stepWineRegistry()
{
  emit logMessage("Applying Wine registry settings...");

  const QString tmpDir = fluorineTmpDir();
  QDir().mkpath(tmpDir);

  const QString regFile = tmpDir + "/wine_settings.reg";
  QFile f(regFile);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    m_steps.last().errorMessage = "Failed to write registry file";
    return false;
  }
  f.write(WINE_SETTINGS_REG);
  f.close();

  QMap<QString, QString> env = baseWineEnv();
  env["WINEDLLOVERRIDES"] = "mshtml=d";
  env["PROTON_USE_XALIA"] = "0";

  const int rc = runProcess(m_wineBin, {"regedit", regFile}, env);
  QFile::remove(regFile);

  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("wine regedit failed (exit code %1)").arg(rc);
    return false;
  }

  emit logMessage("Registry settings applied successfully");
  return true;
}

bool PrefixSetupRunner::stepWin11Mode()
{
  emit logMessage("Setting Windows 11 mode...");

  const QString binDir = fluorineBinDir();

  QMap<QString, QString> env;
  env["WINE"]       = m_wineBin;
  env["WINESERVER"] = m_wineserverBin;
  env["WINEPREFIX"] = m_prefixPath;

  const QString shellCmd = QStringLiteral(
      "export PATH='%1':\"$PATH\"; exec '%2' -q win11")
      .arg(binDir, m_winetricksPath);

  const int rc = runProcess("/bin/sh", {"-c", shellCmd}, env);
  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("winetricks win11 failed (exit code %1)").arg(rc);
    return false;
  }

  return true;
}

bool PrefixSetupRunner::stepPostSetup()
{
  emit logMessage("Running post-setup tasks...");

  // Ensure AppData temp directory exists.
  ensureTempDirectory(m_prefixPath);

  // Create game symlinks.
  createGameSymlinksAuto(m_prefixPath);

  emit logMessage("Post-setup complete");
  return true;
}

// ============================================================================
// Tool management
// ============================================================================

static void setExecPermissions(const QString& path)
{
  QFile::setPermissions(path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup |
      QFileDevice::ReadOther | QFileDevice::ExeOther);
}

bool PrefixSetupRunner::downloadFile(const QString& url, const QString& destPath)
{
  // Use Qt networking — no dependency on host curl.
  QNetworkAccessManager nam;
  QNetworkRequest request{QUrl(url)};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader, "Fluorine-Manager");

  QNetworkReply* reply = nam.get(request);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    emit logMessage(QStringLiteral("Download failed: %1").arg(reply->errorString()));
    reply->deleteLater();
    return false;
  }

  QFile file(destPath);
  if (!file.open(QIODevice::WriteOnly)) {
    emit logMessage(QStringLiteral("Failed to write: %1").arg(destPath));
    reply->deleteLater();
    return false;
  }

  file.write(reply->readAll());
  file.close();
  reply->deleteLater();
  return true;
}

bool PrefixSetupRunner::verifySha256(const QString& filePath, const QString& expectedHex)
{
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return false;

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return false;

  return hash.result().toHex() == expectedHex.toLatin1();
}

bool PrefixSetupRunner::downloadAndVerify(const QString& url, const QString& destPath,
                                          const QString& expectedSha256)
{
  if (!downloadFile(url, destPath)) {
    m_steps.last().errorMessage = QStringLiteral("Failed to download %1").arg(url);
    return false;
  }

  if (!verifySha256(destPath, expectedSha256)) {
    QFile::remove(destPath);
    m_steps.last().errorMessage =
        QStringLiteral("SHA256 mismatch for %1").arg(QUrl(url).fileName());
    return false;
  }

  return true;
}

bool PrefixSetupRunner::ensure7zz()
{
  const QString binDir = fluorineBinDir();
  m_7zzPath = binDir + "/7zz";

  if (QFileInfo::exists(m_7zzPath))
    return true;

  emit logMessage("Downloading 7-Zip...");
  QDir().mkpath(binDir);

  const QString tarPath = binDir + "/7zz.tar.xz";

  if (!downloadFile(SEVENZIP_URL, tarPath)) {
    emit logMessage("ERROR: Failed to download 7-Zip");
    return false;
  }

  // Extract 7zz binary from the tar.xz using host tar (always available).
  const int rc = runHostProcess(
      "tar",
      {"xf", tarPath, "-C", binDir, "7zz"});

  QFile::remove(tarPath);

  if (rc != 0 || !QFileInfo::exists(m_7zzPath)) {
    emit logMessage("ERROR: Failed to extract 7-Zip");
    return false;
  }

  setExecPermissions(m_7zzPath);
  return true;
}

bool PrefixSetupRunner::ensureWinetricks()
{
  const QString binDir = fluorineBinDir();
  QDir().mkpath(binDir);
  m_winetricksPath = binDir + "/winetricks";

  emit logMessage("Checking for winetricks...");

  if (!downloadFile(WINETRICKS_URL, m_winetricksPath)) {
    if (!QFileInfo::exists(m_winetricksPath)) {
      emit logMessage("ERROR: Failed to download winetricks");
      return false;
    }
    // Existing copy is fine.
  }

  setExecPermissions(m_winetricksPath);
  return true;
}

bool PrefixSetupRunner::ensureCabextract()
{
  const QString binDir         = fluorineBinDir();
  const QString cabextractPath = binDir + "/cabextract";

  if (QFileInfo::exists(cabextractPath))
    return true;

  emit logMessage("Downloading cabextract...");
  QDir().mkpath(binDir);

  const QString zipPath = binDir + "/cabextract.zip";

  if (!downloadFile(CABEXTRACT_URL, zipPath)) {
    emit logMessage("ERROR: Failed to download cabextract");
    return false;
  }

  // Extract using our downloaded 7zz (no host unzip dependency).
  if (!ensure7zz()) {
    QFile::remove(zipPath);
    return false;
  }

  const int rc = runHostProcess(
      m_7zzPath,
      {"x", zipPath, "-o" + binDir, "-y"});

  QFile::remove(zipPath);

  if (rc != 0 || !QFileInfo::exists(cabextractPath)) {
    emit logMessage("ERROR: Failed to extract cabextract");
    return false;
  }

  setExecPermissions(cabextractPath);
  return true;
}

// ============================================================================
// Wine environment helpers
// ============================================================================

QString PrefixSetupRunner::findWineBinary() const
{
  for (const char* subdir : {"files/bin", "dist/bin"}) {
    const QString candidate = QDir(m_protonPath).filePath(
        QString::fromLatin1(subdir) + "/wine");
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

QString PrefixSetupRunner::findWineserverBinary() const
{
  for (const char* subdir : {"files/bin", "dist/bin"}) {
    const QString candidate = QDir(m_protonPath).filePath(
        QString::fromLatin1(subdir) + "/wineserver");
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

QString PrefixSetupRunner::findProtonScript() const
{
  const QString script = QDir(m_protonPath).filePath("proton");
  return QFileInfo::exists(script) ? script : QString();
}

QString PrefixSetupRunner::detectSteamPath()
{
  // Use native Steam detection first.
  const QString steamPath = findSteamPath();
  if (!steamPath.isEmpty())
    return steamPath;

  // Fallback.
  const QString home = QDir::homePath();
  const QStringList candidates = {
      home + "/.local/share/Steam",
      home + "/.steam/steam",
      home + "/.steam/root",
  };
  for (const QString& p : candidates) {
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString PrefixSetupRunner::detectSLRRunScript() const
{
  // Check Fluorine-downloaded SLR first (steamrt4 preferred, sniper fallback).
  const QStringList nakCandidates = {
      fluorineDataDir() + "/steamrt/SteamLinuxRuntime_4/run",
      fluorineDataDir() + "/steamrt/SteamLinuxRuntime_sniper/run",
  };
  for (const QString& p : nakCandidates) {
    if (QFileInfo::exists(p))
      return p;
  }

  const QString steamPath = detectSteamPath();

  const QStringList candidates = {
      steamPath + "/steamapps/common/SteamLinuxRuntime_4/run",
      steamPath + "/steamapps/common/SteamLinuxRuntime_sniper/run",
      QDir::homePath() + "/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4/run",
      QDir::homePath() + "/.local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/run",
      "/usr/lib/pressure-vessel/wrap",
  };

  for (const QString& p : candidates) {
    if (!p.isEmpty() && QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString PrefixSetupRunner::fluorineBinDir()
{
  return fluorineDataDir() + "/bin";
}

void PrefixSetupRunner::killStalePrefixProcesses() const
{
  if (m_prefixPath.isEmpty())
    return;

  const QString cleanPrefix = QDir::cleanPath(m_prefixPath);
  const QString cleanCompat = QDir::cleanPath(QDir(m_prefixPath).filePath(".."));

  QDir procDir("/proc");
  const QStringList pids =
      procDir.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);

  QList<qint64> victims;
  for (const QString& pid : pids) {
    // Read cmdline (fast filter for wine-like processes).
    QFile cmdF("/proc/" + pid + "/cmdline");
    if (!cmdF.open(QIODevice::ReadOnly))
      continue;
    QByteArray cmdline = cmdF.readAll();
    if (cmdline.isEmpty())
      continue;
    const QString cmdStr = QString::fromUtf8(cmdline.replace('\0', ' '));
    const bool wineLike = cmdStr.contains("wineboot") ||
                          cmdStr.contains("wineserver") ||
                          cmdStr.contains("pv-adverb") ||
                          cmdStr.contains("wine-preloader") ||
                          cmdStr.contains("steam.exe");
    if (!wineLike)
      continue;

    // Definitive match: process's own WINEPREFIX env points at our prefix.
    // Wine processes have Windows-style cmdlines ("c:\windows\...") that
    // don't include the Linux prefix path, so cmdline-matching misses them.
    bool mine = cmdStr.contains(cleanPrefix) || cmdStr.contains(cleanCompat);
    if (!mine) {
      QFile envF("/proc/" + pid + "/environ");
      if (envF.open(QIODevice::ReadOnly)) {
        QByteArray environ = envF.readAll();
        for (const QByteArray& kv : environ.split('\0')) {
          if (kv.startsWith("WINEPREFIX=")) {
            const QString val = QString::fromUtf8(kv.mid(11));
            if (QDir::cleanPath(val) == cleanPrefix)
              mine = true;
            break;
          }
          if (kv.startsWith("STEAM_COMPAT_DATA_PATH=")) {
            const QString val = QString::fromUtf8(kv.mid(23));
            if (QDir::cleanPath(val) == cleanCompat)
              mine = true;
          }
        }
      }
    }

    if (mine) {
      bool ok = false;
      const qint64 p = pid.toLongLong(&ok);
      if (ok)
        victims.append(p);
    }
  }

  if (victims.isEmpty())
    return;

  MOBase::log::warn("Found {} stale wine process(es) bound to prefix — killing",
                    victims.size());
  for (qint64 p : victims)
    ::kill(static_cast<pid_t>(p), SIGTERM);

  QThread::msleep(300);

  for (qint64 p : victims) {
    if (::kill(static_cast<pid_t>(p), 0) == 0)
      ::kill(static_cast<pid_t>(p), SIGKILL);
  }

  QThread::msleep(100);
}

QString PrefixSetupRunner::fluorineCacheDir()
{
  return fluorineDataDir() + "/cache";
}

QString PrefixSetupRunner::fluorineTmpDir()
{
  return fluorineDataDir() + "/tmp";
}

QMap<QString, QString> PrefixSetupRunner::baseWineEnv() const
{
  QMap<QString, QString> env;
  env["WINEPREFIX"] = m_prefixPath;
  env["WINE"]       = m_wineBin;
  env["WINESERVER"] = m_wineserverBin;
  return env;
}
