#include "settingsdialogproton.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"
#include "prefixsetupdialog.h"
#include "ui_settingsdialog.h"

#include <QtConcurrent/QtConcurrentRun>
#include <log.h>
#include <uibase/utility.h>
#include "knowngames.h"
#include "steamdetection.h"
#include "gamedetection.h"
#include "slrmanager.h"
#include <atomic>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace
{
std::atomic<ProtonSettingsTab*> g_activeInstallTab = nullptr;

/// Find the wine binary inside a Proton installation directory.
static QString findWineBinary(const QString& protonPath)
{
  for (const char* subdir : {"files/bin", "dist/bin"}) {
    const QString candidate =
        QDir(protonPath).filePath(QString::fromLatin1(subdir) + "/wine");
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

/// Apply a single game's registry entry via wine regedit.
///
/// Looks up the game by name in NaK's KNOWN_GAMES, builds a .reg file that
/// maps the install path to the game's registry key, and imports it with
/// wine regedit (wrapped in SLR if available).
static QString applyGameRegistryNative(const QString& prefixPath,
                                       const QString& protonPath,
                                       const QString& gameName,
                                       const QString& installPath,
                                       void (*logCb)(const char*))
{
  // Find wine binary.
  const QString wineBin = findWineBinary(protonPath);
  if (wineBin.isEmpty())
    return QStringLiteral("Wine binary not found in Proton at %1").arg(protonPath);

  // Look up registry path/value from known games.
  const KnownGame* foundGame = nullptr;
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    if (gameName == QString::fromLatin1(KNOWN_GAMES[i].name)) {
      foundGame = &KNOWN_GAMES[i];
      break;
    }
  }

  if (!foundGame || !foundGame->registry_path || !foundGame->registry_value)
    return QStringLiteral("Unknown game: %1").arg(gameName);

  const QString rPath = QString::fromLatin1(foundGame->registry_path);
  const QString rVal  = QString::fromLatin1(foundGame->registry_value);

  // Convert Linux path → Wine Z: drive path with escaped backslashes for .reg.
  const QString winePath = "Z:" + QString(installPath).replace('/', "\\\\");

  // Wow6432Node key: strip the leading "Software\" prefix.
  const int firstBackslash = rPath.indexOf('\\');
  const QString wow64Key =
      (firstBackslash >= 0) ? rPath.mid(firstBackslash + 1) : rPath;

  const QString regContent = QStringLiteral(
      "Windows Registry Editor Version 5.00\n\n"
      "[HKEY_LOCAL_MACHINE\\%1]\n\"%2\"=\"%3\"\n\n"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\%4]\n\"%5\"=\"%6\"\n\n")
      .arg(rPath, rVal, winePath, wow64Key, rVal, winePath);

  // Write temp .reg file.
  const QString tmpDir = fluorineDataDir() + "/tmp";
  QDir().mkpath(tmpDir);
  const QString regFile = tmpDir + "/game_reg_apply.reg";

  {
    QFile f(regFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
      return QStringLiteral("Failed to write registry file: %1").arg(regFile);
    f.write(regContent.toUtf8());
  }

  if (logCb) {
    const QByteArray msg =
        QStringLiteral("Applying registry for %1...").arg(gameName).toUtf8();
    logCb(msg.constData());
  }

  // Build the command — wrap in SLR if available.
  QProcess proc;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("WINEPREFIX",       prefixPath);
  env.insert("WINEDLLOVERRIDES", "mshtml=d");
  env.insert("PROTON_USE_XALIA", "0");
  proc.setProcessEnvironment(env);

  const QString slr = getSlrRunScript();
  if (!slr.isEmpty()) {

    QStringList args;
    // Expose directories to the container.
    const QString wineDir = QFileInfo(wineBin).absolutePath();
    if (!wineDir.isEmpty())
      args << QStringLiteral("--filesystem=%1").arg(wineDir);
    if (!prefixPath.isEmpty())
      args << QStringLiteral("--filesystem=%1").arg(prefixPath);
    if (!protonPath.isEmpty())
      args << QStringLiteral("--filesystem=%1").arg(protonPath);
    if (QDir(tmpDir).exists())
      args << QStringLiteral("--filesystem=%1").arg(tmpDir);
    args << "--" << wineBin << "regedit" << regFile;

    proc.setProgram(slr);
    proc.setArguments(args);
  } else {
    proc.setProgram(wineBin);
    proc.setArguments({"regedit", regFile});
  }

  proc.setProcessChannelMode(QProcess::MergedChannels);
  proc.start();
  proc.waitForFinished(60000);
  QFile::remove(regFile);

  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    return QStringLiteral("wine regedit failed (exit code %1)").arg(proc.exitCode());

  if (logCb) {
    const QByteArray msg =
        QStringLiteral("Registry applied for %1").arg(gameName).toUtf8();
    logCb(msg.constData());
  }
  return {};  // success
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
  QObject::connect(ui->fixGameRegistriesButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onFixGameRegistries);
  QObject::connect(ui->winetricksButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onWinetricks);
  QObject::connect(ui->prefixLocationBrowseButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onBrowsePrefixLocation);
  QObject::connect(ui->downloadSLRButton, &QPushButton::clicked, this,
                   &ProtonSettingsTab::onDownloadSLR);

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
  ui->fixGameRegistriesButton->setEnabled(!m_busy && active);
  ui->winetricksButton->setEnabled(!m_busy && active);
  ui->protonVersionCombo->setEnabled(!m_busy);
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
      tr("Downloading Steam Linux Runtime (~180 MB)...\n"
         "Check the MO2 log for progress details."),
      tr("Cancel"), 0, 0, parentWidget());
  progress->setWindowTitle(tr("Steam Linux Runtime"));
  progress->setWindowModality(Qt::WindowModal);
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

void ProtonSettingsTab::onFixGameRegistries()
{
  if (m_busy) {
    return;
  }

  showGameRegistryDialog();
}

void ProtonSettingsTab::showGameRegistryDialog()
{
  auto cfg = FluorineConfig::load();
  if (!cfg.has_value() || !cfg->prefixExists()) {
    ui->protonStatusLabel->setText(tr("No existing prefix"));
    refreshState();
    return;
  }

  QDialog dialog(parentWidget());
  dialog.setWindowTitle(tr("Fix Game Registries"));
  dialog.setMinimumWidth(500);

  auto* layout = new QVBoxLayout(&dialog);

  layout->addWidget(new QLabel(tr("Select the game to apply registry settings for:"),
                               &dialog));

  auto* gameCombo = new QComboBox(&dialog);

  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    const QString name = QString::fromLatin1(KNOWN_GAMES[i].name);
    if (!name.isEmpty()) {
      gameCombo->addItem(name);
    }
  }

  layout->addWidget(gameCombo);

  // Game installation path
  layout->addWidget(new QLabel(tr("Game installation path:"), &dialog));

  auto* pathLayout = new QHBoxLayout();
  auto* pathEdit   = new QLineEdit(&dialog);
  pathEdit->setPlaceholderText(tr("e.g. /home/user/.local/share/Steam/steamapps/common/Skyrim Special Edition"));
  auto* browseBtn = new QPushButton(tr("Browse..."), &dialog);
  pathLayout->addWidget(pathEdit);
  pathLayout->addWidget(browseBtn);
  layout->addLayout(pathLayout);

  QObject::connect(browseBtn, &QPushButton::clicked, &dialog, [&dialog, pathEdit]() {
    const QString dir = QFileDialog::getExistingDirectory(
        &dialog, QObject::tr("Select Game Installation Folder"),
        pathEdit->text().isEmpty() ? QDir::homePath() : pathEdit->text());
    if (!dir.isEmpty()) {
      pathEdit->setText(dir);
    }
  });

  // Try to auto-detect path when game selection changes
  auto autoDetect = [pathEdit, gameCombo]() {
    const QString gameName = gameCombo->currentText();
    if (gameName.isEmpty()) return;

    // Use detectAllGames to find the install path
    const GameScanResult scanResult = detectAllGames();
    for (int i = 0; i < scanResult.games.size(); ++i) {
      const DetectedGame& detected = scanResult.games[i];
      if (detected.name.contains(gameName, Qt::CaseInsensitive) ||
          gameName.contains(detected.name, Qt::CaseInsensitive)) {
        if (!detected.install_path.isEmpty()) {
          pathEdit->setText(detected.install_path);
          break;
        }
      }
    }
  };

  QObject::connect(gameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   &dialog, autoDetect);
  // Auto-detect for initially selected game
  autoDetect();

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString selectedGame  = gameCombo->currentText();
  const QString gamePath      = pathEdit->text();
  const QString prefixPath    = cfg->prefix_path;
  const QString protonName    = cfg->proton_name;
  const QString protonPath    = cfg->proton_path;

  if (gamePath.isEmpty()) {
    QMessageBox::warning(parentWidget(), tr("No Path"),
                         tr("Please provide the game installation path."));
    return;
  }

  setBusy(true);
  ui->protonStatusLabel->setText(tr("Fixing game registries..."));
  ui->nakInstallLog->clear();
  ui->nakInstallLog->setVisible(true);
  ui->toggleInstallLog->setChecked(true);

  g_activeInstallTab.store(this);

  m_pendingPrefixPath = prefixPath;
  m_pendingProtonName = protonName;
  m_pendingProtonPath = protonPath;

  m_installWatcher.setFuture(
      QtConcurrent::run([prefixPath, protonPath,
                         selectedGame, gamePath]() -> InstallResult {
        const QString err = applyGameRegistryNative(
            prefixPath, protonPath, selectedGame, gamePath,
            &ProtonSettingsTab::logCallback);

        InstallResult r;
        if (!err.isEmpty())
          r.error = err;

        return r;
      }));
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

