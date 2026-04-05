#include "prefixsetuprunner.h"

#include "fluorinepaths.h"

#include <nak_ffi.h>

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

static const char* DOTNET9_SDK_URL =
    "https://builds.dotnet.microsoft.com/dotnet/Sdk/9.0.310/"
    "dotnet-sdk-9.0.310-win-x64.exe";

static const char* DOTNET_DESKTOP10_URL =
    "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/10.0.2/"
    "windowsdesktop-runtime-10.0.2-win-x64.exe";

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
"d3dx11_43"="native"
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
    m_steps.append({id, name, SetupStep::Pending, {}});
    m_stepFunctions.append(std::move(fn));
  };

  addStep("proton_init", "Initialize Wine Prefix",
          [this] { return stepProtonInit(); });

  addStep("drive_cleanup", "Clean Up Drive Letters",
          [this] { return stepDriveCleanup(); });

  // DirectX DLL extraction (cab-based, no Wine needed).
  addStep("d3dcompiler_47", "d3dcompiler_47",
          [this] { return stepD3DCompiler47(); });
  addStep("d3dcompiler_43", "d3dcompiler_43",
          [this] { return stepD3DCompiler43(); });
  addStep("d3dx9", "d3dx9",
          [this] { return stepD3dx9(); });
  addStep("d3dx11_43", "d3dx11_43",
          [this] { return stepD3dx11_43(); });
  addStep("xact", "XACT (32-bit)",
          [this] { return stepXact(); });
  addStep("xact_x64", "XACT (64-bit)",
          [this] { return stepXact64(); });

  // Runtime installers (run via Wine).
  addStep("vcrun2022", "Visual C++ 2022",
          [this] { return stepVcrun2022(); });
  addStep("dotnet6", ".NET Runtime 6",
          [this] { return stepDotNetInstallPair(DOTNET6_X86_URL, DOTNET6_X64_URL, ".NET 6"); });
  addStep("dotnet7", ".NET Runtime 7",
          [this] { return stepDotNetInstallPair(DOTNET7_X86_URL, DOTNET7_X64_URL, ".NET 7"); });
  addStep("dotnet8", ".NET Runtime 8",
          [this] { return stepDotNetInstallPair(DOTNET8_X86_URL, DOTNET8_X64_URL, ".NET 8"); });
  addStep("dotnetdesktop6", ".NET Desktop Runtime 6",
          [this] { return stepDotNetInstallPair(DOTNET_DESKTOP6_X86_URL, DOTNET_DESKTOP6_X64_URL, ".NET Desktop 6"); });
  addStep("dotnet9_sdk", ".NET 9 SDK",
          [this] { return stepDotNetInstall(DOTNET9_SDK_URL, "dotnet-sdk-9"); });
  addStep("dotnet_desktop10", ".NET Desktop Runtime 10",
          [this] { return stepDotNetInstall(DOTNET_DESKTOP10_URL, "dotnet-desktop-10"); });

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

    allOk = runStep(i) && allOk;
    emit progressChanged(static_cast<float>(i + 1) / total);
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
       "MO2_DLLS_DIR", "MO2_PYTHON_DIR", "MO2_BASE_DIR",
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

    slrArgs << "--" << exe;

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
                                  int timeoutMs)
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
      if (!data.isEmpty())
        emitFilteredOutput(this, data);
    }

    if (isCancelled()) {
      proc->kill();
      proc->waitForFinished(5000);
      proc->deleteLater();
      return -1;
    }
  }

  const QByteArray remaining = proc->readAll();
  if (!remaining.isEmpty())
    emitFilteredOutput(this, remaining);

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

  const QString steamPath = detectSteamPath();

  // The compatdata path is the PARENT of the pfx directory.
  const QString compatDataPath = QDir(m_prefixPath).filePath("..");
  const QString cleanCompat    = QDir::cleanPath(compatDataPath);

  QMap<QString, QString> env;
  env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = steamPath;
  env["STEAM_COMPAT_DATA_PATH"]           = cleanCompat;
  env["SteamAppId"]                       = QString::number(m_appId);
  env["SteamGameId"]                      = QString::number(m_appId);
  env["DISPLAY"]                          = "";
  env["WAYLAND_DISPLAY"]                  = "";
  env["WINEDEBUG"]                        = "-all";
  env["WINEDLLOVERRIDES"] = "msdia80.dll=n;conhost.exe=d;cmd.exe=d";

  emit logMessage("Initializing Wine prefix with Proton...");

  const int rc = runProcess(protonScript,
                            {"run", "wineboot", "-u"},
                            env);
  if (rc != 0) {
    m_steps.last().errorMessage =
        QStringLiteral("proton wineboot failed (exit code %1)").arg(rc);
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

bool PrefixSetupRunner::stepD3DCompiler43()
{
  emit logMessage("Installing d3dcompiler_43...");

  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  emit logMessage("Extracting d3dcompiler_43 (32-bit)...");
  if (!extractFromRedist(redistPath, "*d3dcompiler_43*x86*", "d3dcompiler_43.dll", dllDir32))
    return false;

  emit logMessage("Extracting d3dcompiler_43 (64-bit)...");
  if (!extractFromRedist(redistPath, "*d3dcompiler_43*x64*", "d3dcompiler_43.dll", dllDir64))
    return false;

  emit logMessage("d3dcompiler_43 installed");
  return true;
}

bool PrefixSetupRunner::stepD3dx9()
{
  emit logMessage("Installing d3dx9...");

  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  emit logMessage("Extracting d3dx9 (32-bit)...");
  if (!extractFromRedist(redistPath, "*d3dx9*x86*", "d3dx9*.dll", dllDir32))
    return false;

  emit logMessage("Extracting d3dx9 (64-bit)...");
  if (!extractFromRedist(redistPath, "*d3dx9*x64*", "d3dx9*.dll", dllDir64))
    return false;

  emit logMessage("d3dx9 installed");
  return true;
}

bool PrefixSetupRunner::stepD3dx11_43()
{
  emit logMessage("Installing d3dx11_43...");

  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";
  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  emit logMessage("Extracting d3dx11_43 (32-bit)...");
  if (!extractFromRedist(redistPath, "*d3dx11_43*x86*", "d3dx11_43.dll", dllDir32))
    return false;

  emit logMessage("Extracting d3dx11_43 (64-bit)...");
  if (!extractFromRedist(redistPath, "*d3dx11_43*x64*", "d3dx11_43.dll", dllDir64))
    return false;

  emit logMessage("d3dx11_43 installed");
  return true;
}

bool PrefixSetupRunner::stepXact()
{
  emit logMessage("Installing XACT (32-bit)...");

  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  const QString dllDir32 = m_prefixPath + "/drive_c/windows/syswow64";

  // Extract all xact-related cabs and DLLs.
  for (const char* cabPat : {"*_xact_*x86*", "*_x3daudio_*x86*", "*_xaudio_*x86*"}) {
    for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"}) {
      // Ignore failures for individual patterns — not all cabs contain all DLL types.
      extractFromRedist(redistPath, cabPat, dllPat, dllDir32);
    }
  }

  // Batch-register all xactengine and xaudio DLLs in a single regsvr32 call.
  QStringList regDlls;

  const QStringList xactDlls = QDir(dllDir32).entryList({"xactengine*.dll"}, QDir::Files);
  regDlls.append(xactDlls);

  for (int i = 0; i <= 7; ++i) {
    const QString dll = QStringLiteral("xaudio2_%1.dll").arg(i);
    if (QFileInfo::exists(dllDir32 + "/" + dll))
      regDlls.append(dll);
  }

  if (!regDlls.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 DLLs...").arg(regDlls.size()));
    QMap<QString, QString> env = baseWineEnv();
    env["WINEDLLOVERRIDES"] = "mshtml=d";
    QStringList args = {"regsvr32", "/S"};
    args.append(regDlls);
    runProcess(m_wineBin, args, env);
  }

  emit logMessage("XACT (32-bit) installed");
  return true;
}

bool PrefixSetupRunner::stepXact64()
{
  emit logMessage("Installing XACT (64-bit)...");

  QString redistPath;
  if (!ensureDirectXRedist(redistPath))
    return false;

  const QString dllDir64 = m_prefixPath + "/drive_c/windows/system32";

  // Extract all xact-related cabs and DLLs.
  for (const char* cabPat : {"*_xact_*x64*", "*_x3daudio_*x64*", "*_xaudio_*x64*"}) {
    for (const char* dllPat : {"xactengine*.dll", "xaudio*.dll", "x3daudio*.dll", "xapofx*.dll"}) {
      extractFromRedist(redistPath, cabPat, dllPat, dllDir64);
    }
  }

  // Batch-register all 64-bit xactengine and xaudio DLLs.
  QStringList regDlls;

  const QStringList xactDlls = QDir(dllDir64).entryList({"xactengine*.dll"}, QDir::Files);
  regDlls.append(xactDlls);

  for (int i = 0; i <= 7; ++i) {
    const QString dll = QStringLiteral("xaudio2_%1.dll").arg(i);
    if (QFileInfo::exists(dllDir64 + "/" + dll))
      regDlls.append(dll);
  }

  if (!regDlls.isEmpty()) {
    emit logMessage(QStringLiteral("Registering %1 DLLs...").arg(regDlls.size()));
    QMap<QString, QString> env = baseWineEnv();
    env["WINEDLLOVERRIDES"] = "mshtml=d";
    QStringList args = {"regsvr32", "/S"};
    args.append(regDlls);
    runProcess(m_wineBin, args, env);
  }

  emit logMessage("XACT (64-bit) installed");
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

  NakGameList gameList = nak_detect_all_games();
  if (gameList.count == 0) {
    nak_game_list_free(gameList);
    emit logMessage("No games detected");
    return true;
  }

  // Build a single .reg file with all game registry entries.
  QString regContent = QStringLiteral("Windows Registry Editor Version 5.00\n\n");
  int gameCount = 0;

  for (size_t i = 0; i < gameList.count; ++i) {
    const NakGame& game = gameList.games[i];
    if (!game.registry_path || !game.registry_value)
      continue;

    const QString gameName    = QString::fromUtf8(game.name);
    const QString installPath = QString::fromUtf8(game.install_path);
    const QString rPath       = QString::fromUtf8(game.registry_path);
    const QString rVal        = QString::fromUtf8(game.registry_value);

    // Convert Linux path to Wine Z: drive path with escaped backslashes.
    QString winePath = "Z:" + QString(installPath).replace('/', "\\\\");

    regContent += QStringLiteral(
        "[HKEY_LOCAL_MACHINE\\%1]\n\"%2\"=\"%3\"\n\n"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%4]\n\"%5\"=\"%6\"\n\n")
        .arg(rPath, rVal, winePath,
             rPath.mid(rPath.indexOf('\\') + 1),
             rVal, winePath);

    emit logMessage(QStringLiteral("  Found: %1 -> %2").arg(gameName, installPath));
    ++gameCount;
  }

  nak_game_list_free(gameList);

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
  const QByteArray prefixUtf8 = m_prefixPath.toUtf8();
  nak_ensure_temp_directory(prefixUtf8.constData());

  // Create game symlinks.
  nak_create_game_symlinks_auto(prefixUtf8.constData());

  // Ensure DXVK config.
  if (char* err = nak_ensure_dxvk_conf(); err != nullptr) {
    emit logMessage(QStringLiteral("Warning: dxvk.conf: %1").arg(QString::fromUtf8(err)));
    nak_string_free(err);
    // Non-fatal.
  }

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

QString PrefixSetupRunner::detectSteamPath() const
{
  // Use NaK FFI first.
  if (char* path = nak_find_steam_path(); path != nullptr) {
    QString result = QString::fromUtf8(path);
    nak_string_free(path);
    if (!result.isEmpty())
      return result;
  }

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
  // Check NaK-downloaded SLR first.
  const QString nakSlr = fluorineDataDir() + "/steamrt/SteamLinuxRuntime_sniper/run";
  if (QFileInfo::exists(nakSlr))
    return nakSlr;

  const QString steamPath = detectSteamPath();

  const QStringList candidates = {
      steamPath + "/steamapps/common/SteamLinuxRuntime_sniper/run",
      QDir::homePath() + "/.local/share/Steam/steamapps/common/SteamLinuxRuntime_sniper/run",
      "/usr/lib/pressure-vessel/wrap",
  };

  for (const QString& p : candidates) {
    if (!p.isEmpty() && QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString PrefixSetupRunner::fluorineBinDir() const
{
  return fluorineDataDir() + "/bin";
}

QString PrefixSetupRunner::fluorineCacheDir() const
{
  return fluorineDataDir() + "/cache";
}

QString PrefixSetupRunner::fluorineTmpDir() const
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
