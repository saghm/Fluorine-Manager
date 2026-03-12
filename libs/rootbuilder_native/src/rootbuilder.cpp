#include "rootbuilder.h"

#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/iplugingame.h>
#include <uibase/versioninfo.h>

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

// Storage constants
static const QString STORAGE_SUBDIR   = QStringLiteral("rootbuilder");
static const QString MANIFEST_NAME    = QStringLiteral("manifest.json");
static const QString SETTINGS_NAME    = QStringLiteral("settings.json");
static const QString BACKUP_SUBDIR    = QStringLiteral("backup");
static const QString LEGACY_MANIFEST  = QStringLiteral(".rootbuilder_manifest.json");
static const QString LEGACY_BACKUP    = QStringLiteral(".rootbuilder_backup");

// ============================================================================
// Construction / IPlugin
// ============================================================================

RootBuilderNative::RootBuilderNative() = default;

bool RootBuilderNative::init(MOBase::IOrganizer* organizer)
{
    m_organizer = organizer;
    migrateLegacy();
    checkThirdPartyRootBuilder();

    organizer->onAboutToRun([this](const QString& exe) {
        return onAboutToRun(exe);
    });
    organizer->onFinishedRun([this](const QString& exe, unsigned int code) {
        onFinishedRun(exe, code);
    });

    return true;
}

QString RootBuilderNative::name() const
{
    return QStringLiteral("Root Builder (Native)");
}

QString RootBuilderNative::localizedName() const
{
    return QStringLiteral("Root Builder (Native)");
}

QString RootBuilderNative::author() const
{
    return QStringLiteral("Fluorine Manager");
}

QString RootBuilderNative::description() const
{
    return QStringLiteral(
        "Deploys mod files from Root/ subdirectories to the game's root directory. "
        "Supports copy and symlink modes with auto-deploy on launch.");
}

MOBase::VersionInfo RootBuilderNative::version() const
{
    return MOBase::VersionInfo(1, 0, 0);
}

QList<MOBase::PluginSetting> RootBuilderNative::settings() const
{
    return {};
}

bool RootBuilderNative::enabledByDefault() const
{
    return true;
}

// ============================================================================
// IPluginTool
// ============================================================================

QString RootBuilderNative::displayName() const
{
    return QStringLiteral("Root Builder");
}

QString RootBuilderNative::tooltip() const
{
    return QStringLiteral("Deploy mod Root/ files to the game directory");
}

QIcon RootBuilderNative::icon() const
{
    return QIcon();
}

void RootBuilderNative::display() const
{
    QJsonObject settings = loadSettings();

    // We need non-const this for build/clear — display() is const in the
    // interface, but build/clear mutate the filesystem (not object state).
    auto* self = const_cast<RootBuilderNative*>(this);

    auto* dialog = new QDialog(parentWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Root Builder"));
    dialog->resize(350, 220);

    auto* layout = new QVBoxLayout(dialog);

    auto* desc = new QLabel(
        QStringLiteral("Deploys files from mod Root/ folders to the game directory."));
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Enable checkbox
    auto* enableCheck = new QCheckBox(QStringLiteral("Auto-deploy on game launch"));
    enableCheck->setChecked(settings.value(QStringLiteral("enabled")).toBool(false));
    layout->addWidget(enableCheck);

    // Mode selector
    auto* modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(QStringLiteral("Deploy mode:")));
    auto* modeCombo = new QComboBox();
    modeCombo->addItems({QStringLiteral("copy"), QStringLiteral("link")});
    modeCombo->setCurrentText(
        settings.value(QStringLiteral("mode")).toString(QStringLiteral("copy")));
    modeLayout->addWidget(modeCombo);
    layout->addLayout(modeLayout);

    // Manual build/clear buttons
    auto* btnLayout = new QHBoxLayout();
    auto* buildBtn  = new QPushButton(QStringLiteral("Build Now"));
    auto* clearBtn  = new QPushButton(QStringLiteral("Clear Now"));
    btnLayout->addWidget(buildBtn);
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    // Status label
    auto* statusLabel = new QLabel();
    layout->addWidget(statusLabel);

    // Close button
    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    layout->addWidget(closeBtn);

    // Save helper — captures enableCheck and modeCombo
    auto doSave = [this, enableCheck, modeCombo]() {
        QJsonObject s;
        s[QStringLiteral("enabled")] = enableCheck->isChecked();
        s[QStringLiteral("mode")]    = modeCombo->currentText();
        saveSettings(s);
    };

    QObject::connect(enableCheck, &QCheckBox::stateChanged, dialog, [doSave](int) {
        doSave();
    });
    QObject::connect(modeCombo, &QComboBox::currentTextChanged, dialog,
                     [doSave](const QString&) { doSave(); });

    QObject::connect(buildBtn, &QPushButton::clicked, dialog,
                     [self, statusLabel]() {
                         int count = self->build();
                         statusLabel->setText(
                             QStringLiteral("Deployed %1 file(s).").arg(count));
                     });

    QObject::connect(clearBtn, &QPushButton::clicked, dialog,
                     [self, statusLabel]() {
                         int count = self->clear();
                         statusLabel->setText(
                             QStringLiteral("Cleared %1 file(s).").arg(count));
                     });

    QObject::connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);

    dialog->exec();
}

// ============================================================================
// Hooks
// ============================================================================

bool RootBuilderNative::onAboutToRun(const QString& /*executable*/)
{
    if (isAutoEnabled())
        build();
    return true;
}

void RootBuilderNative::onFinishedRun(const QString& /*executable*/,
                                      unsigned int /*exitCode*/)
{
    if (isAutoEnabled())
        clear();
}

// ============================================================================
// Storage paths
// ============================================================================

QString RootBuilderNative::storageDir() const
{
    QString d = m_organizer->basePath() + QStringLiteral("/") + STORAGE_SUBDIR;
    QDir().mkpath(d);
    return d;
}

QString RootBuilderNative::backupDir() const
{
    return storageDir() + QStringLiteral("/") + BACKUP_SUBDIR;
}

QString RootBuilderNative::manifestPath() const
{
    return storageDir() + QStringLiteral("/") + MANIFEST_NAME;
}

QString RootBuilderNative::settingsPath() const
{
    return storageDir() + QStringLiteral("/") + SETTINGS_NAME;
}

// ============================================================================
// Settings (own JSON)
// ============================================================================

QJsonObject RootBuilderNative::loadSettings() const
{
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject{
            {QStringLiteral("enabled"), false},
            {QStringLiteral("mode"), QStringLiteral("copy")}};

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject{
            {QStringLiteral("enabled"), false},
            {QStringLiteral("mode"), QStringLiteral("copy")}};

    return doc.object();
}

void RootBuilderNative::saveSettings(const QJsonObject& settings) const
{
    QDir().mkpath(storageDir());
    QFile f(settingsPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
    }
}

bool RootBuilderNative::isAutoEnabled() const
{
    return loadSettings().value(QStringLiteral("enabled")).toBool(false);
}

// ============================================================================
// Manifest
// ============================================================================

QJsonObject RootBuilderNative::loadManifest() const
{
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();

    return doc.object();
}

void RootBuilderNative::saveManifest(const QJsonObject& manifest) const
{
    QDir().mkpath(storageDir());
    QFile f(manifestPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    }
}

void RootBuilderNative::removeManifest() const
{
    QFile::remove(manifestPath());
}

// ============================================================================
// Legacy migration
// ============================================================================

void RootBuilderNative::migrateLegacy() const
{
    auto* game = m_organizer->managedGame();
    if (!game)
        return;

    QString gameDir = game->gameDirectory().absolutePath();
    QString storage = storageDir();

    // Migrate manifest
    QString oldManifest = gameDir + QStringLiteral("/") + LEGACY_MANIFEST;
    if (QFileInfo::exists(oldManifest)) {
        QString newPath = storage + QStringLiteral("/") + MANIFEST_NAME;
        if (!QFileInfo::exists(newPath))
            QFile::copy(oldManifest, newPath);
        forceRemove(oldManifest);
    }

    // Migrate backup directory
    QString oldBackup = gameDir + QStringLiteral("/") + LEGACY_BACKUP;
    QFileInfo oldBackupInfo(oldBackup);
    if (oldBackupInfo.isDir()) {
        QString newBackup = storage + QStringLiteral("/") + BACKUP_SUBDIR;
        if (!QFileInfo(newBackup).isDir()) {
            // Copy tree
            QDir().mkpath(newBackup);
            QDirIterator it(oldBackup, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                QString rel = QDir(oldBackup).relativeFilePath(it.filePath());
                QString dst = newBackup + QStringLiteral("/") + rel;
                QDir().mkpath(QFileInfo(dst).absolutePath());
                QFile::copy(it.filePath(), dst);
            }
        }
        // Remove old backup tree
        QDir(oldBackup).removeRecursively();
    }
}

// ============================================================================
// Third-party conflict detection
// ============================================================================

void RootBuilderNative::checkThirdPartyRootBuilder() const
{
    // Locate the plugins directory (where .py/.so plugins live, next to the binary)
    QString pluginsDir = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
    if (!QDir(pluginsDir).exists())
        return;

    QString disabledDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/DisabledPlugins");

    QDir dir(pluginsDir);
    const auto entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const auto& entry : entries) {
        bool conflict = false;
        QString entryName = entry.fileName();

        if (entry.isDir() && entryName.compare(QStringLiteral("rootbuilder"),
                                                Qt::CaseInsensitive) == 0) {
            conflict = true;
        } else if (entry.isFile() &&
                   entryName.toLower().startsWith(QStringLiteral("rootbuilder")) &&
                   entryName.toLower().endsWith(QStringLiteral(".py"))) {
            // Don't move our own bundled rootbuilder.py
            if (entryName == QStringLiteral("rootbuilder.py"))
                continue;
            conflict = true;
        }

        if (conflict) {
            QDir().mkpath(disabledDir);
            QString dst = disabledDir + QStringLiteral("/") + entryName;
            if (QFile::rename(entry.absoluteFilePath(), dst)) {
                qInfo("Root Builder: moved incompatible third-party plugin "
                      "'%s' to DisabledPlugins/.",
                      qUtf8Printable(entryName));
            } else {
                qWarning("Root Builder: failed to move third-party plugin "
                         "'%s' to DisabledPlugins/.",
                         qUtf8Printable(entryName));
            }
        }
    }
}

// ============================================================================
// File helpers
// ============================================================================

QString RootBuilderNative::findRootDir(const QString& modPath)
{
    QDir dir(modPath);
    const auto entries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (entry.fileName().compare(QStringLiteral("root"),
                                     Qt::CaseInsensitive) == 0) {
            return entry.absoluteFilePath();
        }
    }
    return QString();
}

void RootBuilderNative::ensureReadable(const QString& path)
{
    QFileInfo fi(path);
    if (fi.exists() && !fi.isReadable()) {
        QFile::setPermissions(path, fi.permissions() | QFileDevice::ReadOwner);
    }
}

void RootBuilderNative::ensureWritable(const QString& path)
{
    QFileInfo fi(path);
    if (fi.exists()) {
        QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner);
    }
}

void RootBuilderNative::reflinkCopy(const QString& src, const QString& dst)
{
    ensureReadable(src);

    // Try cp --reflink=auto first
    QProcess proc;
    proc.setProgram(QStringLiteral("cp"));
    proc.setArguments({QStringLiteral("--reflink=auto"), QStringLiteral("-f"),
                       QStringLiteral("--"), src, dst});
    proc.start();
    if (proc.waitForFinished(10000) && proc.exitCode() == 0)
        return;

    // Fallback to QFile::copy (remove dst first since QFile::copy won't overwrite)
    QFile::remove(dst);
    if (QFile::copy(src, dst))
        return;

    qWarning("Root Builder: failed to copy %s -> %s",
             qUtf8Printable(src), qUtf8Printable(dst));
}

bool RootBuilderNative::forceRemove(const QString& path)
{
    if (QFile::remove(path))
        return true;

    // Fix permissions and retry
    ensureWritable(QFileInfo(path).absolutePath());
    ensureWritable(path);
    if (QFile::remove(path))
        return true;

    qWarning("Root Builder: could not remove %s", qUtf8Printable(path));
    return false;
}

void RootBuilderNative::cleanupEmptyDirs(const QString& baseDir,
                                         const QStringList& paths)
{
    std::set<QString> dirsToCheck;
    for (const auto& path : paths) {
        QString parent = QFileInfo(path).absolutePath();
        while (!parent.isEmpty() && parent != baseDir) {
            // Check samefile
            QFileInfo parentInfo(parent);
            QFileInfo baseInfo(baseDir);
            if (parentInfo.absoluteFilePath() == baseInfo.absoluteFilePath())
                break;
            dirsToCheck.insert(parent);
            parent = QFileInfo(parent).absolutePath();
        }
    }

    // Sort by length descending (deepest first)
    QStringList sorted(dirsToCheck.begin(), dirsToCheck.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const QString& a, const QString& b) {
                  return a.length() > b.length();
              });

    for (const auto& d : sorted) {
        QDir dir(d);
        if (dir.exists() && dir.isEmpty())
            dir.rmdir(QStringLiteral("."));
    }
}

// ============================================================================
// Build
// ============================================================================

int RootBuilderNative::build() const
{
    auto* game = m_organizer->managedGame();
    if (!game)
        return 0;

    QString gameDir = game->gameDirectory().absolutePath();
    QString storage = storageDir();
    auto* modList   = m_organizer->modList();
    QString mode    = loadSettings().value(QStringLiteral("mode"))
                          .toString(QStringLiteral("copy"));

    // Clear any previous deployment first
    QJsonObject existingManifest = loadManifest();
    if (!existingManifest.isEmpty())
        const_cast<RootBuilderNative*>(this)->clear();

    QJsonArray deployed;
    QJsonObject backups;
    QSet<QString> deployedSet;

    QStringList mods = modList->allModsByProfilePriority();
    for (const auto& modName : mods) {
        if (!(modList->state(modName) & MOBase::IModList::STATE_ACTIVE))
            continue;

        auto* mod = modList->getMod(modName);
        if (!mod)
            continue;
        if (mod->isSeparator() || mod->isBackup() || mod->isForeign())
            continue;

        QString modPath = mod->absolutePath();
        QString rootDir = findRootDir(modPath);
        if (rootDir.isEmpty())
            continue;

        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QString srcFile = it.filePath();
            QString rel     = QDir(rootDir).relativeFilePath(srcFile);
            QString dst     = gameDir + QStringLiteral("/") + rel;

            // Backup existing file if not already deployed by us
            if (QFile::exists(dst) && !deployedSet.contains(dst)) {
                QString bak =
                    backupDir() + QStringLiteral("/") + rel;
                QDir().mkpath(QFileInfo(bak).absolutePath());
                ensureWritable(dst);
                QFile::copy(dst, bak);
                backups[dst] = bak;
            }

            QDir().mkpath(QFileInfo(dst).absolutePath());

            // Remove existing file/symlink
            if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink())
                forceRemove(dst);

            // In link mode, .exe and .dll must be copied (Wine resolves
            // symlinked exe paths to the target, breaking sibling lookups)
            QString ext = QFileInfo(srcFile).suffix().toLower();
            if (mode == QStringLiteral("link") && ext != QStringLiteral("exe") &&
                ext != QStringLiteral("dll")) {
                QFile::link(srcFile, dst);
            } else {
                reflinkCopy(srcFile, dst);
            }

            if (!deployedSet.contains(dst)) {
                deployed.append(dst);
                deployedSet.insert(dst);
            }
        }
    }

    QJsonObject manifest;
    manifest[QStringLiteral("deployed")] = deployed;
    manifest[QStringLiteral("backups")]  = backups;
    saveManifest(manifest);

    return deployed.count();
}

// ============================================================================
// Clear
// ============================================================================

int RootBuilderNative::clear() const
{
    auto* game = m_organizer->managedGame();
    if (!game)
        return 0;

    QString gameDir = game->gameDirectory().absolutePath();
    QString storage = storageDir();

    QJsonObject manifest = loadManifest();
    if (manifest.isEmpty())
        return 0;

    int count = 0;
    QJsonArray deployedArr = manifest.value(QStringLiteral("deployed")).toArray();
    QJsonObject backupsObj = manifest.value(QStringLiteral("backups")).toObject();

    QStringList failed;
    QStringList cleared;

    // Remove deployed files
    for (const auto& val : deployedArr) {
        QString path = val.toString();
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
            if (forceRemove(path)) {
                ++count;
                cleared.append(path);
            } else {
                failed.append(path);
            }
        } else {
            cleared.append(path);
        }
    }

    // Restore backups
    for (auto it = backupsObj.begin(); it != backupsObj.end(); ++it) {
        QString dst = it.key();
        QString bak = it.value().toString();
        if (QFile::exists(bak)) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            ensureWritable(QFileInfo(dst).absolutePath());
            if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink())
                forceRemove(dst);
            if (!QFile::rename(bak, dst)) {
                qWarning("Root Builder: could not restore backup %s -> %s",
                         qUtf8Printable(bak), qUtf8Printable(dst));
            }
        }
    }

    // Clean up backup dir
    QString bDir = backupDir();
    if (QFileInfo(bDir).isDir())
        QDir(bDir).removeRecursively();

    if (!failed.isEmpty()) {
        // Update manifest to only contain files we couldn't remove
        QJsonArray failedArr;
        for (const auto& f : failed)
            failedArr.append(f);

        QJsonObject retryManifest;
        retryManifest[QStringLiteral("deployed")] = failedArr;
        retryManifest[QStringLiteral("backups")]  = QJsonObject();
        saveManifest(retryManifest);

        qWarning("Root Builder: %d file(s) could not be removed. "
                 "They will be retried on next clear.",
                 static_cast<int>(failed.size()));
    } else {
        removeManifest();
    }

    cleanupEmptyDirs(gameDir, cleared);
    return count;
}
