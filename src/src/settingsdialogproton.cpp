#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "prefixsetupdialog.h"
#include "ui_settingsdialog.h"

#include <QtConcurrent/QtConcurrentRun>
#include <log.h>
#include <uibase/utility.h>
#include "steamdetection.h"
#include "slrmanager.h"
#include <atomic>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
std::atomic<ProtonSettingsTab*> g_activeInstallTab = nullptr;

constexpr const char* kFuseIoUringParameter =
    "/sys/module/fuse/parameters/enable_uring";

enum class FuseIoUringState
{
  Unsupported,
  Disabled,
  Enabled,
  Unknown
};

FuseIoUringState readFuseIoUringState()
{
  QFile parameter(QString::fromUtf8(kFuseIoUringParameter));
  if (!parameter.exists()) {
    return FuseIoUringState::Unsupported;
  }

  if (!parameter.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return FuseIoUringState::Unknown;
  }

  const QByteArray value = parameter.readAll().trimmed().toUpper();
  if (value == "Y" || value == "1") {
    return FuseIoUringState::Enabled;
  }
  if (value == "N" || value == "0") {
    return FuseIoUringState::Disabled;
  }

  return FuseIoUringState::Unknown;
}

QString findPrivilegeHelper(QStringList* arguments)
{
  const QString command =
      QStringLiteral("printf Y > %1").arg(QString::fromUtf8(kFuseIoUringParameter));

  const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
  if (!pkexec.isEmpty()) {
    *arguments = {QStringLiteral("/bin/sh"), QStringLiteral("-c"), command};
    return pkexec;
  }

  const QString sudo = QStandardPaths::findExecutable(QStringLiteral("sudo"));
  if (!sudo.isEmpty()) {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.value(QStringLiteral("SUDO_ASKPASS")).isEmpty()) {
      *arguments = {QStringLiteral("-A"), QStringLiteral("/bin/sh"),
                    QStringLiteral("-c"), command};
      return sudo;
    }

    const QFileInfo stdinInfo(QStringLiteral("/proc/self/fd/0"));
    const QString stdinTarget = stdinInfo.symLinkTarget();
    if (stdinInfo.exists() &&
        (stdinTarget.startsWith(QStringLiteral("/dev/pts/")) ||
         stdinTarget == QStringLiteral("/dev/tty"))) {
      *arguments = {QStringLiteral("/bin/sh"), QStringLiteral("-c"), command};
      return sudo;
    }
  }

  return {};
}

QString enableFuseIoUring()
{
  QStringList arguments;
  const QString helper = findPrivilegeHelper(&arguments);
  if (helper.isEmpty()) {
    return QObject::tr("Install pkexec/polkit, or configure sudo askpass, then try "
                       "again.");
  }

  QProcess process;
  process.start(helper, arguments);
  if (!process.waitForStarted(5000)) {
    return QObject::tr("Failed to start %1.").arg(helper);
  }

  if (!process.waitForFinished(120000)) {
    process.kill();
    process.waitForFinished(3000);
    return QObject::tr("Timed out waiting for administrator authentication.");
  }

  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError())
                                   .trimmed();
    if (!stderrText.isEmpty()) {
      return stderrText;
    }
    return QObject::tr("Administrator authentication was cancelled or failed.");
  }

  if (readFuseIoUringState() != FuseIoUringState::Enabled) {
    return QObject::tr("The kernel did not accept the FUSE io_uring setting.");
  }

  return {};
}
}

ProtonSettingsTab::ProtonSettingsTab(Settings& s, SettingsDialog& d)
    : QObject(&d), SettingsTab(s, d)
{
  ui->protonProgressBar->setRange(0, 100);
  ui->protonProgressBar->setValue(0);
  ui->protonProgressBar->setVisible(false);

  ui->launchWrapperEdit->setPlaceholderText("mangohud --dlsym");
  ui->launchWrapperEdit->setText(QSettings().value("fluorine/launch_wrapper").toString());

  populateProtons();

  QObject::connect(ui->protonVersionCombo, &QComboBox::currentIndexChanged, this,
                   [this](int index) {
                     if (index < 0) {
                       return;
                     }

                     auto cfg = FluorineConfig::load();
                     if (!cfg.has_value()) {
                       return;
                     }

                     const QString protonName =
                         ui->protonVersionCombo->currentText().trimmed();
                     const QString protonPath = ui->protonVersionCombo
                                                    ->itemData(index, Qt::UserRole + 1)
                                                    .toString()
                                                    .trimmed();

                     if (protonName.isEmpty() || protonPath.isEmpty()) {
                       MOBase::log::warn("Proton combo change: name='{}' path='{}' — "
                                         "skipping save (empty)", protonName, protonPath);
                       return;
                     }

                     if (cfg->proton_name != protonName ||
                         cfg->proton_path != protonPath) {
                       cfg->proton_name = protonName;
                       cfg->proton_path = protonPath;
                       cfg->save();
                       MOBase::log::info("Updated Proton config: {} ({})",
                                         protonName, protonPath);
                     }
                   });

  QObject::connect(ui->createPrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onCreatePrefix);
  QObject::connect(ui->deletePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDeletePrefix);
  QObject::connect(ui->recreatePrefixButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onRecreatePrefix);
  QObject::connect(ui->openPrefixFolderButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onOpenPrefixFolder);
  QObject::connect(ui->winetricksButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onWinetricks);
  QObject::connect(ui->prefixLocationBrowseButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onBrowsePrefixLocation);
  QObject::connect(ui->downloadSLRButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDownloadSLR);
  QObject::connect(ui->fuseIoUringCheckBox, &QCheckBox::clicked, this,
                   &ProtonSettingsTab::onFuseIoUringToggled);

  QObject::connect(&m_installWatcher, &QFutureWatcher<InstallResult>::finished, this,
                   &ProtonSettingsTab::onInstallFinished);

  // install log viewer
  ui->nakInstallLog->setVisible(false);
  QObject::connect(ui->toggleInstallLog, &QPushButton::toggled, this,
                   [this](bool checked) {
                     ui->nakInstallLog->setVisible(checked);
                     ui->toggleInstallLog->setText(
                         checked ? tr("Hide Install Log") : tr("Show Install Log"));
                   });

  refreshState();
}

void ProtonSettingsTab::update()
{
  QSettings().setValue("fluorine/launch_wrapper", ui->launchWrapperEdit->text());
}

void ProtonSettingsTab::populateProtons()
{
  ui->protonVersionCombo->clear();

  const auto protonList = findSteamProtons();

  for (int i = 0; i < protonList.size(); ++i) {
    const SteamProtonInfo& proton = protonList[i];

    if (proton.name.isEmpty() || proton.path.isEmpty()) {
      continue;
    }

    ui->protonVersionCombo->addItem(proton.name);
    ui->protonVersionCombo->setItemData(ui->protonVersionCombo->count() - 1, proton.path,
                                        Qt::UserRole + 1);
  }

  if (auto cfg = FluorineConfig::load(); cfg.has_value()) {
    const int idx = ui->protonVersionCombo->findText(cfg->proton_name);
    if (idx >= 0) {
      ui->protonVersionCombo->setCurrentIndex(idx);
    } else if (ui->protonVersionCombo->count() > 0) {
      // Saved Proton version no longer exists — select first available and
      // update the config so the stale path doesn't cause launch failures.
      MOBase::log::warn("Saved Proton '{}' not found, defaulting to '{}'",
                        cfg->proton_name,
                        ui->protonVersionCombo->itemText(0));
      ui->protonVersionCombo->setCurrentIndex(0);
      cfg->proton_name = ui->protonVersionCombo->itemText(0).trimmed();
      cfg->proton_path = ui->protonVersionCombo->itemData(0, Qt::UserRole + 1)
                             .toString().trimmed();
      cfg->save();
    }
  }
}

void ProtonSettingsTab::refreshState()
{
  const auto prefix = FluorineConfig::prefixPath();
  const bool active = prefix.has_value();

  if (!m_busy) {
    ui->protonStatusLabel->setText(active ? tr("Prefix Active") : tr("No Prefix"));
    ui->protonProgressBar->setVisible(false);
  }

  if (active) {
    ui->prefixLocationEdit->setText(*prefix);
    ui->prefixLocationEdit->setReadOnly(true);
  } else {
    ui->prefixLocationEdit->setReadOnly(false);
    if (ui->prefixLocationEdit->text().isEmpty()) {
      ui->prefixLocationEdit->setText(
          fluorineDataDir() + "/Prefix");
    }
  }

  ui->prefixLocationBrowseButton->setEnabled(!m_busy && !active);
  ui->createPrefixButton->setEnabled(!m_busy && !active);
  ui->deletePrefixButton->setEnabled(!m_busy && active);
  ui->recreatePrefixButton->setEnabled(!m_busy && active);
  ui->openPrefixFolderButton->setEnabled(!m_busy && active);
  ui->winetricksButton->setEnabled(!m_busy && active);
  ui->protonVersionCombo->setEnabled(!m_busy);

  refreshFuseIoUringState();
}

void ProtonSettingsTab::setBusy(bool busy)
{
  m_busy = busy;
  ui->protonProgressBar->setVisible(busy);

  if (!busy) {
    ui->protonProgressBar->setValue(0);
  }

  refreshState();
}

void ProtonSettingsTab::onCreatePrefix()
{
  if (m_busy) {
    return;
  }

  const QString protonName = ui->protonVersionCombo->currentText().trimmed();
  const QString protonPath =
      ui->protonVersionCombo->currentData(Qt::UserRole + 1).toString().trimmed();

  if (protonName.isEmpty() || protonPath.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Select a Proton version first"));
    return;
  }

  const QString basePath = ui->prefixLocationEdit->text().trimmed();
  if (basePath.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Select a prefix location first"));
    return;
  }

  const QString pfxPath = QDir(basePath).filePath("pfx");
  if (!QDir().mkpath(pfxPath)) {
    ui->protonStatusLabel->setText(tr("Failed to create prefix directory"));
    return;
  }

  runPrefixSetupDialog(0, pfxPath, protonName, protonPath);
}

void ProtonSettingsTab::onDeletePrefix()
{
  if (m_busy) {
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }

  cfg->destroyPrefix();

  ui->prefixLocationEdit->clear();
  ui->protonStatusLabel->setText(tr("No Prefix"));
  refreshState();
}

void ProtonSettingsTab::onRecreatePrefix()
{
  if (m_busy) {
    return;
  }

  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix to recreate"));
    refreshState();
    return;
  }

  QDir prefixDir(cfg->prefix_path);
  if (prefixDir.exists() && !prefixDir.removeRecursively()) {
    ui->protonStatusLabel->setText(tr("Failed to delete existing prefix"));
    refreshState();
    return;
  }

  runPrefixSetupDialog(cfg->app_id, cfg->prefix_path, cfg->proton_name,
                       cfg->proton_path);
}

void ProtonSettingsTab::onOpenPrefixFolder()
{
  auto path = FluorineConfig::prefixPath();
  if (!path.has_value()) {
    ui->protonStatusLabel->setText(tr("No Prefix"));
    return;
  }

  MOBase::shell::Explore(QDir(*path));
}

void ProtonSettingsTab::onDownloadSLR()
{
  if (isSlrInstalled()) {
    QMessageBox::information(parentWidget(), tr("Steam Linux Runtime"),
        tr("Steam Linux Runtime is already installed."));
    return;
  }

  ui->downloadSLRButton->setEnabled(false);
  auto* progress = new QProgressDialog(
      tr("Downloading Steam Linux Runtime (~200 MB)...\n"
         "Check the MO2 log for progress details."),
      tr("Cancel"), 0, 0, parentWidget());
  progress->setWindowTitle(tr("Steam Linux Runtime"));
  progress->setWindowModality(Qt::WindowModal);
  progress->setAttribute(Qt::WA_ShowWithoutActivating);
  progress->setMinimumDuration(0);

  auto* cancelFlag = new int(0);
  connect(progress, &QProgressDialog::canceled, this, [cancelFlag] {
    *cancelFlag = 1;
  });

  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this,
      [this, watcher, progress, cancelFlag] {
        progress->close();
        watcher->deleteLater();
        progress->deleteLater();
        ui->downloadSLRButton->setEnabled(true);

        const QString err = watcher->result();
        if (!err.isEmpty()) {
          MOBase::log::error("[SLR] Download failed: {}", err);
          QMessageBox::warning(parentWidget(), tr("Steam Linux Runtime"),
              tr("Download failed:\n%1").arg(err));
        } else {
          MOBase::log::info("[SLR] Steam Linux Runtime installed successfully");
          QMessageBox::information(parentWidget(), tr("Steam Linux Runtime"),
              tr("Steam Linux Runtime installed successfully."));
        }
        delete cancelFlag;
      });

  int* cancelPtr = cancelFlag;
  watcher->setFuture(QtConcurrent::run([cancelPtr]() -> QString {
    return downloadSlr(nullptr, nullptr, cancelPtr);
  }));
  progress->show();
}

void ProtonSettingsTab::refreshFuseIoUringState()
{
  QSignalBlocker blocker(ui->fuseIoUringCheckBox);
  Q_UNUSED(blocker);

  const FuseIoUringState state = readFuseIoUringState();
  ui->fuseIoUringCheckBox->setVisible(true);

  switch (state) {
  case FuseIoUringState::Enabled:
    ui->fuseIoUringCheckBox->setChecked(true);
    ui->fuseIoUringCheckBox->setEnabled(false);
    ui->fuseIoUringCheckBox->setToolTip(
        tr("FUSE io_uring is enabled in the kernel. It applies to new VFS "
           "mounts after relaunch/remount."));
    break;
  case FuseIoUringState::Disabled:
    ui->fuseIoUringCheckBox->setChecked(false);
    ui->fuseIoUringCheckBox->setEnabled(!m_busy);
    ui->fuseIoUringCheckBox->setToolTip(
        tr("Enable the kernel FUSE io_uring transport for new VFS mounts. "
           "Requires administrator authentication and a VFS remount; Fluorine "
           "falls back automatically when unsupported."));
    break;
  case FuseIoUringState::Unsupported:
    ui->fuseIoUringCheckBox->setChecked(false);
    ui->fuseIoUringCheckBox->setEnabled(false);
    ui->fuseIoUringCheckBox->setToolTip(
        tr("This kernel does not expose %1. FUSE io_uring requires kernel "
           "support for CONFIG_FUSE_IO_URING.")
            .arg(QString::fromUtf8(kFuseIoUringParameter)));
    break;
  case FuseIoUringState::Unknown:
    ui->fuseIoUringCheckBox->setChecked(false);
    ui->fuseIoUringCheckBox->setEnabled(!m_busy);
    ui->fuseIoUringCheckBox->setToolTip(
        tr("Fluorine could not read the kernel FUSE io_uring setting. Click "
           "to try enabling it with administrator authentication."));
    break;
  }
}

void ProtonSettingsTab::onFuseIoUringToggled(bool checked)
{
  if (!checked) {
    refreshFuseIoUringState();
    return;
  }

  MOBase::log::info("[VFS] user requested enabling FUSE io_uring via settings UI");

  const QString error = enableFuseIoUring();
  if (!error.isEmpty()) {
    MOBase::log::warn("[VFS] failed to enable FUSE io_uring: {}", error);
    QMessageBox::warning(parentWidget(), tr("FUSE io_uring"),
                         tr("Could not enable FUSE io_uring:\n%1").arg(error));
    refreshFuseIoUringState();
    return;
  }

  MOBase::log::info("[VFS] kernel FUSE io_uring parameter is now enabled");
  QMessageBox::information(
      parentWidget(), tr("FUSE io_uring"),
      tr("FUSE io_uring is enabled. Restart the game/VFS mount before testing; "
         "existing mounts keep their current transport."));
  refreshFuseIoUringState();
}

void ProtonSettingsTab::onBrowsePrefixLocation()
{
  const QString dir = QFileDialog::getExistingDirectory(
      parentWidget(), tr("Select Prefix Location"), ui->prefixLocationEdit->text());
  if (!dir.isEmpty()) {
    ui->prefixLocationEdit->setText(dir);
  }
}

QString ProtonSettingsTab::ensureWinetricks()
{
  const QString nakWinetricks = fluorineDataDir() + "/bin/winetricks";
  if (QFileInfo::exists(nakWinetricks)) {
    return nakWinetricks;
  }

  const QString systemWinetricks = QStandardPaths::findExecutable("winetricks");
  if (!systemWinetricks.isEmpty()) {
    return systemWinetricks;
  }

  const QString nakBinDir = fluorineDataDir() + "/bin";
  QDir().mkpath(nakBinDir);

  QString downloadTool;
  QStringList downloadArgs;

  if (!QStandardPaths::findExecutable("curl").isEmpty()) {
    downloadTool = "curl";
    downloadArgs = {"-L", "-o", nakWinetricks,
                    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/"
                    "winetricks"};
  } else if (!QStandardPaths::findExecutable("wget").isEmpty()) {
    downloadTool = "wget";
    downloadArgs = {"-O", nakWinetricks,
                    "https://raw.githubusercontent.com/Winetricks/winetricks/master/src/"
                    "winetricks"};
  } else {
    return {};
  }

  QProcess proc;
  proc.start(downloadTool, downloadArgs);
  proc.waitForFinished(30000);

  if (proc.exitCode() != 0 || !QFileInfo::exists(nakWinetricks)) {
    return {};
  }

  QFile::setPermissions(nakWinetricks,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);

  return nakWinetricks;
}

QString ProtonSettingsTab::findProtonWine(const QString& protonPath)
{
  QString wine = QDir(protonPath).filePath("files/bin/wine");
  if (QFileInfo::exists(wine)) {
    return wine;
  }

  wine = QDir(protonPath).filePath("dist/bin/wine");
  if (QFileInfo::exists(wine)) {
    return wine;
  }

  return {};
}

void ProtonSettingsTab::onWinetricks()
{
  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix"));
    refreshState();
    return;
  }

  const QString winetricksPath = ensureWinetricks();
  if (winetricksPath.isEmpty()) {
    QMessageBox::warning(
        parentWidget(), tr("Winetricks Not Found"),
        tr("Could not find or download winetricks.\n\n"
           "Please install winetricks manually:\n"
           "  Arch: pacman -S winetricks\n"
           "  Ubuntu: apt install winetricks\n"
           "  Fedora: dnf install winetricks"));
    return;
  }

  // Build env vars for winetricks
  QStringList envFlags;
  envFlags << QStringLiteral("WINEPREFIX=%1").arg(cfg->prefix_path);

  const QString protonWine = findProtonWine(cfg->proton_path);
  if (!protonWine.isEmpty()) {
    envFlags << QStringLiteral("WINE=%1").arg(protonWine);
    const QString wineserver =
        QFileInfo(protonWine).dir().filePath("wineserver");
    if (QFileInfo::exists(wineserver)) {
      envFlags << QStringLiteral("WINESERVER=%1").arg(wineserver);
    }
  }

  QString program = winetricksPath;
  QStringList arguments;
  arguments << QStringLiteral("--gui");

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Restore the original host LD_LIBRARY_PATH so that winetricks (and any
  // GUI helpers it spawns, e.g. kdialog/zenity) don't pick up Fluorine's
  // bundled Qt libraries, which cause symbol-lookup errors.
  // Uses the same restore-or-strip logic as protonlauncher.cpp.
  auto restoreOrStrip = [&](const QString& var, const QString& origVar) {
    if (env.contains(origVar)) {
      const QString orig = env.value(origVar);
      if (orig.isEmpty())
        env.remove(var);
      else
        env.insert(var, orig);
      env.remove(origVar);
    } else {
      // Fallback: strip Fluorine's bundled library paths by pattern.
      const QString value = env.value(var);
      if (value.isEmpty()) return;
      QStringList kept;
      for (const QString& p : value.split(':')) {
        if (p.contains("fluorine") || p.contains(".mount_Fluori")) {
          continue;
        }
        kept.append(p);
      }
      if (kept.isEmpty()) {
        env.remove(var);
      } else {
        env.insert(var, kept.join(':'));
      }
    }
  };
  restoreOrStrip("LD_LIBRARY_PATH", "FLUORINE_ORIG_LD_LIBRARY_PATH");
  restoreOrStrip("LD_PRELOAD", "FLUORINE_ORIG_LD_PRELOAD");
  restoreOrStrip("QT_PLUGIN_PATH", "FLUORINE_ORIG_QT_PLUGIN_PATH");
  env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");

  for (const QString& flag : envFlags) {
    const int eq = flag.indexOf('=');
    if (eq > 0) {
      env.insert(flag.left(eq), flag.mid(eq + 1));
    }
  }
  QProcess proc;
  proc.setProcessEnvironment(env);
  proc.setProgram(program);
  proc.setArguments(arguments);
  proc.startDetached();
}

void ProtonSettingsTab::runPrefixSetupDialog(uint32_t appId,
                                              const QString& prefixPath,
                                              const QString& protonName,
                                              const QString& protonPath)
{
  PrefixSetupDialog dialog(prefixPath, protonPath, appId, parentWidget());
  const int result = dialog.exec();

  if (result == QDialog::Accepted && dialog.succeeded()) {
    // All steps succeeded — save config to mark prefix as complete.
    FluorineConfig cfg;
    cfg.app_id      = appId;
    cfg.prefix_path = prefixPath;
    cfg.proton_name = protonName;
    cfg.proton_path = protonPath;
    cfg.created     = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (!cfg.save()) {
      ui->protonStatusLabel->setText(tr("Error saving Fluorine config"));
    } else {
      ui->protonStatusLabel->setText(tr("Prefix Active"));
    }
  } else {
    ui->protonStatusLabel->setText(tr("Prefix setup incomplete"));
  }

  refreshState();
}

void ProtonSettingsTab::appendInstallLog(const QString& message)
{
  ui->nakInstallLog->append(message);
}

void ProtonSettingsTab::logCallback(const char* message)
{
  if (message && *message) {
    MOBase::log::info("{}", message);
  }

  if (auto* tab = g_activeInstallTab.load(); tab != nullptr && message && *message) {
    const QString msg = QString::fromUtf8(message);
    QMetaObject::invokeMethod(tab,
                              [tab, msg] {
                                tab->appendInstallLog(msg);
                              },
                              Qt::QueuedConnection);
  }
}

void ProtonSettingsTab::onInstallFinished()
{
  g_activeInstallTab.store(nullptr);

  const InstallResult result = m_installWatcher.result();

  setBusy(false);

  if (!result.error.isEmpty()) {
    ui->protonStatusLabel->setText(tr("Error: %1").arg(result.error));
    return;
  }

  ui->protonStatusLabel->setText(tr("Done"));
  refreshState();
}
