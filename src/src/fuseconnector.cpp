#include "fuseconnector.h"

#include "settings.h"
#include "vfs/vfstree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVariant>

#include <iplugingame.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using namespace MOBase;

// Global mount point for signal-handler cleanup (async-signal-safe access).
static char g_fuseMountPoint[4096] = {0};

void setFuseMountPointForCrashCleanup(const char* path)
{
  if (path != nullptr) {
    std::strncpy(g_fuseMountPoint, path, sizeof(g_fuseMountPoint) - 1);
    g_fuseMountPoint[sizeof(g_fuseMountPoint) - 1] = '\0';
  } else {
    g_fuseMountPoint[0] = '\0';
  }
}

const char* getFuseMountPointForCrashCleanup()
{
  return g_fuseMountPoint[0] != '\0' ? g_fuseMountPoint : nullptr;
}

namespace
{
namespace fs = std::filesystem;

std::string decodeProcMountField(const std::string& in)
{
  std::string out;
  out.reserve(in.size());

  for (size_t i = 0; i < in.size();) {
    if (in[i] == '\\' && i + 3 < in.size() && std::isdigit(in[i + 1]) &&
        std::isdigit(in[i + 2]) && std::isdigit(in[i + 3])) {
      const std::string oct = in.substr(i + 1, 3);
      const int value       = std::stoi(oct, nullptr, 8);
      out.push_back(static_cast<char>(value));
      i += 4;
      continue;
    }

    out.push_back(in[i]);
    ++i;
  }

  return out;
}

bool isMountPoint(const QString& path)
{
  QFile mounts(QStringLiteral("/proc/mounts"));
  if (!mounts.open(QIODevice::ReadOnly)) {
    return false;
  }

  const auto mountPoint = QDir::cleanPath(path);
  while (!mounts.atEnd()) {
    const auto line  = QString::fromUtf8(mounts.readLine()).trimmed();
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
      continue;
    }

    const QString current = QString::fromStdString(
        decodeProcMountField(parts[1].toStdString()));
    if (QDir::cleanPath(current) == mountPoint) {
      return true;
    }
  }

  return false;
}

bool runUnmountCommand(const QString& program, const QStringList& args)
{
  // Suppress stderr from fusermount/umount to avoid confusing terminal output
  // when unmount fails (e.g. permission denied in Flatpak sandbox).
  auto tryRun = [&](const QString& cmd, const QStringList& cmdArgs) -> bool {
    QProcess p;
    p.setStandardErrorFile(QProcess::nullDevice());
    p.start(cmd, cmdArgs);
    if (!p.waitForFinished(3000)) {
      p.kill();
      return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
  };

  return tryRun(program, args);
}

std::vector<std::pair<std::string, std::string>>
buildModsFromMapping(const MappingType& mapping, const QString& dataDir,
                     const QString& overwriteDir)
{
  std::vector<std::pair<std::string, std::string>> mods;
  std::set<std::string> seen;

  const QString dataPrefix = QDir::cleanPath(dataDir) + "/";
  const QString overPrefix = QDir::cleanPath(overwriteDir) + "/";

  for (const auto& map : mapping) {
    if (!map.isDirectory) {
      continue;
    }

    const QString src = QDir::cleanPath(QDir::fromNativeSeparators(map.source));
    const QString dst = QDir::cleanPath(QDir::fromNativeSeparators(map.destination));

    if (!(dst == QDir::cleanPath(dataDir) || dst.startsWith(dataPrefix))) {
      continue;
    }

    if (src == QDir::cleanPath(overwriteDir) || src.startsWith(overPrefix)) {
      continue;
    }

    const std::string srcStd = src.toStdString();
    if (!seen.insert(srcStd).second) {
      continue;
    }

    const QString name = QFileInfo(src).fileName();
    mods.emplace_back(name.toStdString(), srcStd);
  }

  return mods;
}

void setupFuseOps(struct fuse_lowlevel_ops* ops)
{
  std::memset(ops, 0, sizeof(struct fuse_lowlevel_ops));
  ops->init    = mo2_init;
  ops->lookup  = mo2_lookup;
  ops->getattr = mo2_getattr;
  ops->opendir     = mo2_opendir;
  ops->readdir     = mo2_readdir;
  ops->readdirplus = mo2_readdirplus;
  ops->open        = mo2_open;
  ops->read    = mo2_read;
  ops->write   = mo2_write;
  ops->create  = mo2_create;
  ops->rename  = mo2_rename;
  ops->setattr = mo2_setattr;
  ops->unlink  = mo2_unlink;
  ops->mkdir   = mo2_mkdir;
  ops->release = mo2_release;
  ops->releasedir = mo2_releasedir;
  ops->access  = mo2_access;
}

}  // namespace

FuseConnector::FuseConnector(QObject* parent) : QObject(parent)
{
  log::debug("FUSE connector initialized");
}

FuseConnector::~FuseConnector()
{
  unmount();
}

bool FuseConnector::mount(
    const QString& mount_point, const QString& overwrite_dir, const QString& game_dir,
    const QString& data_dir_name,
    const std::vector<std::pair<std::string, std::string>>& mods)
{
  if (m_mounted) {
    unmount();
  }

  m_overwriteDir = overwrite_dir.toStdString();
  m_gameDir      = game_dir.toStdString();
  m_dataDirName  = data_dir_name.toStdString();
  m_lastMods     = mods;

  // Use the caller-supplied data directory path directly.  Re-computing it
  // as gameDir/dataDirName breaks games where the data directory IS the game
  // directory (e.g. BG3 with GameDataPath=""), because dirName() returns the
  // last path component and appending it produces a non-existent double path.
  m_dataDirPath = mount_point.toStdString();
  m_mountPoint  = m_dataDirPath;

  if (!fs::exists(m_dataDirPath)) {
    throw FuseConnectorException(
        QObject::tr("Game data directory does not exist: %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  tryCleanupStaleMount(QString::fromStdString(m_mountPoint));

  const fs::path overwritePath(m_overwriteDir);
  m_stagingDir = (overwritePath.parent_path() / "VFS_staging").string();

  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);
  fs::create_directories(m_overwriteDir, ec);
  if (!m_customOutputDir.empty()) {
    fs::create_directories(m_customOutputDir, ec);
  }

  const auto mountStart = std::chrono::steady_clock::now();

  // Scan + cache base game files BEFORE mounting (after mount they're hidden).
  // Reuse the cache across mount/unmount cycles since base game files don't
  // change between runs — this avoids a full recursive directory walk on
  // every launch.
  {
    const auto t0 = std::chrono::steady_clock::now();
    if (m_baseFileCache.empty() || m_dataDirPath != m_cachedDataDirPath) {
      m_baseFileCache    = scanDataDir(m_dataDirPath);
      m_cachedDataDirPath = m_dataDirPath;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[VFS] scanned %zu base game entries in %lldms (%s)\n",
                 m_baseFileCache.size(), static_cast<long long>(ms),
                 m_dataDirPath == m_cachedDataDirPath ? "cached" : "fresh");
  }

  // Open fd to data dir BEFORE mounting so we can access original files
  m_backingFd = open(m_dataDirPath.c_str(), O_RDONLY | O_DIRECTORY);
  if (m_backingFd < 0) {
    throw FuseConnectorException(
        QObject::tr("Failed to open backing fd for %1")
            .arg(QString::fromStdString(m_dataDirPath)));
  }

  // Build tree using cached base files + mods + overwrite
  const auto treeStart = std::chrono::steady_clock::now();
  auto tree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, mods, m_overwriteDir));

  // Inject file-level data-dir mappings (e.g. plugins.txt, loadorder.txt)
  injectExtraFiles(*tree, m_extraVfsFiles);

  // Stamp plugin timestamps to match load order so LOOT sees unambiguous ordering
  if (!m_pluginLoadOrder.empty()) {
    stampPluginTimestamps(*tree, m_pluginLoadOrder);
  }

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - treeStart).count();
    std::fprintf(stderr, "[VFS] built tree (%zu files, %zu dirs) in %lldms\n",
                 tree->file_count, tree->dir_count, static_cast<long long>(ms));
  }

  // Load tracked writes (files user moved from Overwrite to a mod)
  m_trackedWrites = std::make_shared<TrackedWrites>();
  std::fprintf(stderr, "[VFS] tracking file path: '%s' (overwrite: '%s', %zu mods)\n",
               m_trackingFilePath.c_str(), m_overwriteDir.c_str(), mods.size());
  if (!m_trackingFilePath.empty()) {
    const bool existed = fs::exists(m_trackingFilePath);
    std::fprintf(stderr, "[VFS] tracking file %s\n", existed ? "exists" : "does NOT exist (first run)");
    m_trackedWrites->load(m_trackingFilePath);
    if (existed) {
      m_trackedWrites->detectManualMoves(m_overwriteDir, mods);
    }
    // NOTE: initialScan() is no longer called on first run.  Its heuristic
    // ("file exists in both overwrite and a mod → track it") produces false
    // positives: game-generated overwrite files that happen to share a name
    // with a mod file get incorrectly tracked.  Tracking now only happens
    // through explicit user actions (UI move/sync/drag-drop) or the
    // snapshot-based detectManualMoves().
    m_trackedWrites->save(m_trackingFilePath);
  } else {
    std::fprintf(stderr, "[VFS] WARNING: tracking file path is empty!\n");
  }

  m_context                        = std::make_shared<Mo2FsContext>();
  m_context->tree                  = tree;
  m_context->inodes                = std::make_unique<InodeTable>();
  m_context->overwrite             = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);
  m_context->tracked_writes        = m_trackedWrites;
  m_context->backing_dir_fd        = m_backingFd;
  m_context->uid                   = ::getuid();
  m_context->gid                   = ::getgid();
  // NOTE: Do NOT include mount_point here — low-level API passes it
  // separately to fuse_session_mount(). Including it here causes
  // "fuse: unknown option(s)" error.
  std::vector<std::string> argvStorage = {
      "mo2fuse", "-o", "fsname=mo2linux", "-o", "noatime"};

  std::vector<char*> argv;
  argv.reserve(argvStorage.size());
  for (auto& s : argvStorage) {
    argv.push_back(s.data());
  }

  struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(argv.size()), argv.data());

  struct fuse_lowlevel_ops ops;
  setupFuseOps(&ops);

  m_session = fuse_session_new(&args, &ops, sizeof(ops), m_context.get());
  if (m_session == nullptr) {
    close(m_backingFd);
    m_backingFd = -1;
    throw FuseConnectorException(QObject::tr("Failed to create FUSE session"));
  }

  if (fuse_session_mount(m_session, m_mountPoint.c_str()) != 0) {
    fuse_session_destroy(m_session);
    m_session = nullptr;
    close(m_backingFd);
    m_backingFd = -1;
    throw FuseConnectorException(
        QObject::tr("Failed to mount FUSE at %1")
            .arg(QString::fromStdString(m_mountPoint)));
  }

  m_fuseThread = std::thread([this]() {
    // Enable clone_fd: each worker thread gets its own /dev/fuse fd,
    // eliminating contention on a single fd lock under heavy parallel I/O.
    struct fuse_loop_config* cfg = fuse_loop_cfg_create();
    fuse_loop_cfg_set_clone_fd(cfg, 1);
    fuse_loop_cfg_set_max_threads(cfg, 16);
    fuse_session_loop_mt(m_session, cfg);
    fuse_loop_cfg_destroy(cfg);
  });

  m_mounted = true;
  setFuseMountPointForCrashCleanup(m_mountPoint.c_str());
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mountStart).count();
    std::fprintf(stderr, "[VFS] mounted on '%s' in %lldms total\n",
                 m_mountPoint.c_str(), static_cast<long long>(ms));
  }
  return true;
}

void FuseConnector::unmount()
{
  if (!m_mounted) {
    return;
  }

  const auto unmountStart = std::chrono::steady_clock::now();

  if (m_session != nullptr) {
    fuse_session_exit(m_session);
    fuse_session_unmount(m_session);
  }

  if (m_fuseThread.joinable()) {
    m_fuseThread.join();
  }

  if (m_session != nullptr) {
    fuse_session_destroy(m_session);
    m_session = nullptr;
  }

  {
    const auto t0 = std::chrono::steady_clock::now();
    if (m_discardStaging) {
      // Discard all COW'd files instead of moving them to overwrite.
      log::info("Discarding staging directory (discard flag set)");
      std::error_code ec;
      fs::remove_all(m_stagingDir, ec);
      m_discardStaging = false;
    } else {
      flushStaging();
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[VFS] flushed staging in %lldms\n",
                 static_cast<long long>(ms));
  }

  // Snapshot overwrite contents for next session's manual-move detection,
  // then save tracking data.
  //
  // NOTE: initialScan() is intentionally NOT called here.  It was previously
  // run at every unmount, which caused false-positive tracking: any file that
  // exists in both overwrite and a mod got auto-tracked, even if the overwrite
  // copy was a game-generated modification (not a user move).  Tracking should
  // only happen through explicit user actions (UI move/sync) or the
  // snapshot-based detectManualMoves() at mount time.
  if (m_trackedWrites && !m_trackingFilePath.empty()) {
    m_trackedWrites->snapshotOverwrite(m_overwriteDir);
    m_trackedWrites->save(m_trackingFilePath);
  }

  if (m_backingFd >= 0) {
    close(m_backingFd);
    m_backingFd = -1;
  }

  m_context.reset();
  m_mounted = false;
  setFuseMountPointForCrashCleanup(nullptr);

  // Clean up symlinks created for non-data-dir mappings.
  cleanupExternalMappings();

  // VFS Root Builder: remove deployed root files and restore backups.
  if (m_rootBuilderEnabled) {
    clearRootFiles();
  }

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - unmountStart).count();
    std::fprintf(stderr, "[VFS] unmounted from '%s' in %lldms total\n",
                 m_mountPoint.c_str(), static_cast<long long>(ms));
  }
}

bool FuseConnector::isMounted() const
{
  return m_mounted;
}

void FuseConnector::discardStagingOnUnmount()
{
  m_discardStaging = true;
}

void FuseConnector::setPluginLoadOrder(const std::vector<std::string>& load_order)
{
  m_pluginLoadOrder = load_order;
}

void FuseConnector::setTrackingFilePath(const std::string& path)
{
  m_trackingFilePath = path;
  std::fprintf(stderr, "[VFS] setTrackingFilePath: '%s'\n", path.c_str());
}

std::shared_ptr<TrackedWrites> FuseConnector::trackedWrites() const
{
  return m_trackedWrites;
}

void FuseConnector::rebuild(
    const std::vector<std::pair<std::string, std::string>>& mods,
    const QString& overwrite_dir, const QString& data_dir_name)
{
  if (!m_mounted) {
    return;
  }

  m_overwriteDir = overwrite_dir.toStdString();
  m_dataDirName  = data_dir_name.toStdString();
  m_lastMods     = mods;

  if (m_context == nullptr) {
    return;
  }

  // Use cached base files - can't re-scan the data dir since it's behind our mount
  auto newTree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, mods, m_overwriteDir));

  // Inject file-level data-dir mappings (e.g. plugins.txt, loadorder.txt)
  injectExtraFiles(*newTree, m_extraVfsFiles);

  // Stamp plugin timestamps to match load order
  if (!m_pluginLoadOrder.empty()) {
    stampPluginTimestamps(*newTree, m_pluginLoadOrder);
  }

  {
    std::unique_lock lock(m_context->tree_mutex);
    m_context->tree.swap(newTree);
  }
  {
    std::scoped_lock lock(m_context->open_dirs_mutex);
    m_context->open_dirs.clear();
  }
}

void FuseConnector::updateMapping(const MappingType& mapping)
{
  const auto updateStart = std::chrono::steady_clock::now();
  auto* game = qApp->property("managed_game").value<MOBase::IPluginGame*>();
  if (game == nullptr) {
    throw FuseConnectorException(QObject::tr("Managed game not available"));
  }

  const QString gameDir      = game->gameDirectory().absolutePath();
  const QString dataDirPath  = game->dataDirectory().absolutePath();
  const QString dataDirName  = game->dataDirectory().dirName();
  const QString overwriteDir = Settings::instance().paths().overwrite();

  // Set m_gameDir early so deployRootFiles() can use it before mount().
  m_gameDir = gameDir.toStdString();

  // Auto-derive tracking file path if not explicitly set
  if (m_trackingFilePath.empty() && !overwriteDir.isEmpty()) {
    QDir owDir(overwriteDir);
    QString trackPath = QDir::cleanPath(owDir.absoluteFilePath("../tracked_writes.json"));
    m_trackingFilePath = trackPath.toStdString();
    std::fprintf(stderr, "[VFS] auto-derived tracking path: '%s'\n",
                 m_trackingFilePath.c_str());
  }

  auto mods = buildModsFromMapping(mapping, dataDirPath, overwriteDir);

  // Check if any data-dir mapping has createTarget set — that directory
  // should receive newly created files instead of the overwrite directory.
  // Only consider mappings that target the data directory; non-data-dir
  // mappings (e.g. profile-specific saves → __MO_Saves in My Games) are
  // deployed as real symlinks and should NOT redirect VFS staging output.
  m_customOutputDir.clear();
  {
    const QString cleanDataDir = QDir::cleanPath(dataDirPath);
    const QString dataPrefix   = cleanDataDir + QStringLiteral("/");
    for (const auto& map : mapping) {
      if (!map.createTarget || !map.isDirectory) {
        continue;
      }
      const QString dst =
          QDir::cleanPath(QDir::fromNativeSeparators(map.destination));
      const bool targetsDataDir =
          (dst == cleanDataDir || dst.startsWith(dataPrefix));
      log::debug("Found createTarget mapping: source='{}', dest='{}', "
                 "targetsDataDir={}",
                 map.source, map.destination, targetsDataDir);
      if (targetsDataDir) {
        m_customOutputDir =
            QDir::cleanPath(QDir::fromNativeSeparators(map.source)).toStdString();
        log::debug("Custom output directory set to: {}",
                   QString::fromStdString(m_customOutputDir));
        break;
      }
    }
  }
  if (m_customOutputDir.empty()) {
    log::debug("No data-dir createTarget mapping found, using overwrite dir");
  }

  // Deploy non-data-dir mappings as real symlinks and collect file-level
  // data-dir mappings for VFS tree injection.
  {
    const auto t0 = std::chrono::steady_clock::now();
    deployExternalMappings(mapping, dataDirPath);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "[VFS] deployed external mappings (%zu symlinks, %zu extra files) "
                 "in %lldms\n",
                 m_externalSymlinks.size(), m_extraVfsFiles.size(),
                 static_cast<long long>(ms));
  }

  // VFS Root Builder: deploy Root/ files to game dir BEFORE mounting.
  // This ensures files deployed under Data/ are included in the base file scan.
  if (m_rootBuilderEnabled) {
    deployRootFiles(mods);
    m_baseFileCache.clear();  // force rescan to include root-deployed Data/ files
  }

  if (!m_mounted) {
    mount(dataDirPath, overwriteDir, gameDir, dataDirName, mods);
  } else {
    rebuild(mods, overwriteDir, dataDirName);
  }

  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - updateStart).count();
    std::fprintf(stderr, "[VFS] updateMapping completed in %lldms total\n",
                 static_cast<long long>(ms));
  }
}

void FuseConnector::deployExternalMappings(const MappingType& mapping,
                                            const QString& dataDir)
{
  cleanupExternalMappings();
  m_extraVfsFiles.clear();

  const QString cleanDataDir = QDir::cleanPath(dataDir);
  const QString dataPrefix   = cleanDataDir + QStringLiteral("/");

  for (const auto& map : mapping) {
    const QString src =
        QDir::cleanPath(QDir::fromNativeSeparators(map.source));
    const QString dst =
        QDir::cleanPath(QDir::fromNativeSeparators(map.destination));

    const bool targetsDataDir =
        (dst == cleanDataDir || dst.startsWith(dataPrefix));

    if (targetsDataDir) {
      if (!map.isDirectory) {
        // File-level mapping INTO the data directory (e.g. plugins.txt).
        // FUSE sits on top, so we cannot create a physical symlink there.
        // Record it for injection into the VFS tree instead.
        const QString relPath = dst.startsWith(dataPrefix)
                                    ? dst.mid(dataPrefix.length())
                                    : QFileInfo(src).fileName();
        m_extraVfsFiles.emplace_back(relPath.toStdString(), src.toStdString());
      }
      // Directory-level data-dir mappings are handled by the FUSE VFS.
      continue;
    }

    // Non-data-dir mapping — deploy via real symlinks so the game
    // (running through Proton) can see the files.
    std::error_code ec;

    if (map.isDirectory) {
      const fs::path srcPath(src.toStdString());
      if (!fs::exists(srcPath, ec)) {
        continue;
      }

      for (auto it = fs::recursive_directory_iterator(
               srcPath, fs::directory_options::skip_permission_denied);
           it != fs::recursive_directory_iterator(); ++it) {
        const auto& entry = *it;
        const fs::path rel = fs::relative(entry.path(), srcPath, ec);
        if (ec || rel.empty()) {
          continue;
        }

        const fs::path destPath = fs::path(dst.toStdString()) / rel;
        if (entry.is_directory(ec)) {
          fs::create_directories(destPath, ec);
        } else if (entry.is_regular_file(ec) || entry.is_symlink(ec)) {
          fs::create_directories(destPath.parent_path(), ec);
          if (fs::exists(destPath, ec) && !fs::is_symlink(destPath, ec)) {
            // Never overwrite real game files — only replace our own symlinks.
            continue;
          }
          if (fs::is_symlink(destPath, ec)) {
            fs::remove(destPath, ec);
          }
          fs::create_symlink(entry.path(), destPath, ec);
          if (!ec) {
            m_externalSymlinks.push_back(destPath.string());
          } else {
            log::warn("Failed to symlink {} -> {}: {}",
                      QString::fromStdString(destPath.string()),
                      QString::fromStdString(entry.path().string()),
                      QString::fromStdString(ec.message()));
          }
        }
      }
    } else {
      // Single file symlink.
      const fs::path destPath(dst.toStdString());
      fs::create_directories(destPath.parent_path(), ec);
      if (fs::exists(destPath, ec) && !fs::is_symlink(destPath, ec)) {
        continue;
      }
      if (fs::is_symlink(destPath, ec)) {
        fs::remove(destPath, ec);
      }
      fs::create_symlink(fs::path(src.toStdString()), destPath, ec);
      if (!ec) {
        m_externalSymlinks.push_back(destPath.string());
      } else {
        log::warn("Failed to symlink {} -> {}: {}", dst, src,
                  QString::fromStdString(ec.message()));
      }
    }
  }

  if (!m_externalSymlinks.empty()) {
    log::debug("Deployed {} external symlinks for non-data-dir mappings",
               m_externalSymlinks.size());
  }
  if (!m_extraVfsFiles.empty()) {
    log::debug("Collected {} extra file mappings for VFS injection",
               m_extraVfsFiles.size());
  }
}

void FuseConnector::cleanupExternalMappings()
{
  if (m_externalSymlinks.empty()) {
    return;
  }

  std::error_code ec;
  for (const auto& path : m_externalSymlinks) {
    if (fs::is_symlink(path, ec)) {
      fs::remove(path, ec);
    }
  }

  log::debug("Cleaned up {} external symlinks", m_externalSymlinks.size());
  m_externalSymlinks.clear();
}

void FuseConnector::updateParams(MOBase::log::Levels /*logLevel*/,
                                 env::CoreDumpTypes /*coreDumpType*/,
                                 const QString& /*crashDumpsPath*/,
                                 std::chrono::seconds /*spawnDelay*/,
                                 QString /*executableBlacklist*/,
                                 const QStringList& /*skipFileSuffixes*/,
                                 const QStringList& /*skipDirectories*/)
{}

void FuseConnector::updateForcedLibraries(
    const QList<MOBase::ExecutableForcedLoadSetting>& /*forced*/)
{}

void FuseConnector::flushStaging()
{
  if (m_stagingDir.empty() || m_overwriteDir.empty()) {
    return;
  }

  const fs::path staging(m_stagingDir);
  const fs::path overwrite = m_customOutputDir.empty()
                                 ? fs::path(m_overwriteDir)
                                 : fs::path(m_customOutputDir);

  log::debug("flushStaging: staging='{}', customOutput='{}', dest='{}'",
             QString::fromStdString(m_stagingDir),
             QString::fromStdString(m_customOutputDir),
             QString::fromStdString(overwrite.string()));

  if (!fs::exists(staging)) {
    log::debug("flushStaging: staging dir does not exist, nothing to flush");
    return;
  }

  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(
           staging, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    const fs::path rel = fs::relative(entry.path(), staging, ec);
    if (ec || rel.empty()) {
      continue;
    }

    const fs::path dest = overwrite / rel;
    if (entry.is_directory(ec)) {
      fs::create_directories(dest, ec);
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    fs::create_directories(dest.parent_path(), ec);
    fs::rename(entry.path(), dest, ec);
    if (ec) {
      ec.clear();
      fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
      if (!ec) {
        fs::remove(entry.path(), ec);
      }
    }
  }

  fs::remove_all(staging, ec);
}

void FuseConnector::flushStagingLive()
{
  if (!m_mounted) {
    return;
  }

  if (m_context == nullptr) {
    return;
  }

  // Move staged files to overwrite
  flushStaging();

  // Re-create the staging dir (flushStaging removes it)
  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);

  // Rebuild the VFS tree to pick up new overwrite files
  auto newTree = std::make_shared<VfsTree>(
      buildDataDirVfs(m_baseFileCache, m_dataDirPath, m_lastMods, m_overwriteDir));

  {
    std::unique_lock lock(m_context->tree_mutex);
    m_context->tree.swap(newTree);
  }
  {
    std::scoped_lock lock(m_context->open_dirs_mutex);
    m_context->open_dirs.clear();
  }

  // Re-create OverwriteManager with fresh staging dir
  m_context->overwrite = std::make_unique<OverwriteManager>(m_stagingDir, m_overwriteDir);

  log::debug("Live staging flush complete");
}

// Detect a stale FUSE mount by probing with stat().  Returns true if
// the path exists in the mount table OR if accessing it gives ENOTCONN
// (which happens when the FUSE daemon died but the mount is listed
// under a different path due to symlinks).
static bool isStaleOrMounted(const QString& path)
{
  if (isMountPoint(path)) {
    return true;
  }

  // Probe the path directly — ENOTCONN means dead FUSE mount even if
  // /proc/mounts lists it under a different (canonical) path.
  struct stat st;
  if (::stat(path.toLocal8Bit().constData(), &st) != 0 && errno == ENOTCONN) {
    return true;
  }

  return false;
}

static void doUnmount(const QString& path)
{
  const QString clean = QDir::cleanPath(path);

  if (runUnmountCommand("fusermount3", {"-u", clean}) ||
      runUnmountCommand("fusermount", {"-u", clean})) {
    log::info("stale mount at '{}' cleaned up successfully", path);
    return;
  }

  // Graceful unmount failed — try force/lazy variants.
  runUnmountCommand("umount", {clean});
  runUnmountCommand("umount", {"-l", clean});
  runUnmountCommand("fusermount3", {"-uz", clean});
  runUnmountCommand("fusermount", {"-uz", clean});

  if (!isStaleOrMounted(path)) {
    log::info("stale mount at '{}' cleaned up (lazy unmount)", path);
  } else {
    log::error("failed to clean up stale mount at '{}'", path);
  }
}

static void cleanupStaleMo2Mounts(const QString& keepPath)
{
  QFile mounts(QStringLiteral("/proc/mounts"));
  if (!mounts.open(QIODevice::ReadOnly)) {
    return;
  }

  const QString cleanKeep = QDir::cleanPath(keepPath);
  const QString trashRoot =
      QDir::cleanPath(QDir::homePath() + "/.local/share/Trash/files");

  while (!mounts.atEnd()) {
    const auto line  = QString::fromUtf8(mounts.readLine()).trimmed();
    const auto parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 3) {
      continue;
    }
    if (parts[0] != QStringLiteral("mo2linux")) {
      continue;
    }

    const QString mp = QDir::cleanPath(QString::fromStdString(
        decodeProcMountField(parts[1].toStdString())));
    if (mp == cleanKeep) {
      continue;
    }

    const bool underTrash =
        mp == trashRoot || mp.startsWith(trashRoot + QDir::separator());
    if (!underTrash && !isStaleOrMounted(mp)) {
      continue;
    }

    log::warn("cleaning stale mo2linux mount at '{}'", mp);
    doUnmount(mp);
  }
}

void FuseConnector::tryCleanupStaleMount(const QString& path)
{
  cleanupStaleMo2Mounts(path);

  if (!isStaleOrMounted(path)) {
    return;
  }

  log::warn("stale FUSE mount detected at '{}', attempting cleanup", path);
  doUnmount(path);
}

// ── VFS Root Builder ─────────────────────────────────────────────────────────

void FuseConnector::setRootBuilderEnabled(bool enabled,
                                          const std::string& storageDir)
{
  m_rootBuilderEnabled = enabled;
  m_rootStorageDir     = storageDir;
}

static std::string findRootDir(const std::string& modPath)
{
  // Case-insensitive search for "Root" subdirectory
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(modPath, ec)) {
    if (entry.is_directory(ec)) {
      const auto name = entry.path().filename().string();
      if (name.size() == 4) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "root") {
          return entry.path().string();
        }
      }
    }
  }
  return {};
}

static bool reflinkCopy(const std::string& src, const std::string& dst)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(fs::path(dst).parent_path(), ec);

  // Try reflink (CoW) first via cp --reflink=auto
  if (QProcess::execute("cp", {"--reflink=auto", "--no-preserve=mode",
                                QString::fromStdString(src),
                                QString::fromStdString(dst)}) == 0) {
    return true;
  }

  // Fallback to regular copy
  return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

static void loadRootManifest(const std::string& storageDir,
                             std::vector<std::string>& deployed,
                             std::map<std::string, std::string>& backups)
{
  namespace fs = std::filesystem;
  const auto manifestPath = fs::path(storageDir) / "manifest.json";
  std::ifstream in(manifestPath);
  if (!in.is_open()) return;

  try {
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    // Simple JSON parsing — the manifest is { "deployed": [...], "backups": {...} }
    // Use Qt's JSON for simplicity
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(content));
    if (doc.isNull()) return;

    const auto obj = doc.object();
    for (const auto& v : obj["deployed"].toArray()) {
      deployed.push_back(v.toString().toStdString());
    }
    const auto bk = obj["backups"].toObject();
    for (auto it = bk.begin(); it != bk.end(); ++it) {
      backups[it.key().toStdString()] = it.value().toString().toStdString();
    }
  } catch (...) {}
}

static void saveRootManifest(const std::string& storageDir,
                             const std::vector<std::string>& deployed,
                             const std::map<std::string, std::string>& backups)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(storageDir, ec);

  QJsonArray arr;
  for (const auto& f : deployed) {
    arr.append(QString::fromStdString(f));
  }

  QJsonObject bk;
  for (const auto& [dst, bak] : backups) {
    bk[QString::fromStdString(dst)] = QString::fromStdString(bak);
  }

  QJsonObject obj;
  obj["deployed"] = arr;
  obj["backups"]  = bk;

  const auto manifestPath = fs::path(storageDir) / "manifest.json";
  std::ofstream out(manifestPath);
  if (out.is_open()) {
    out << QJsonDocument(obj).toJson().toStdString();
  }
}

void FuseConnector::deployRootFiles(
    const std::vector<std::pair<std::string, std::string>>& mods)
{
  if (!m_rootBuilderEnabled || m_gameDir.empty() || m_rootStorageDir.empty()) {
    return;
  }

  namespace fs = std::filesystem;
  const auto t0 = std::chrono::steady_clock::now();

  // Clear any previous deployment
  clearRootFiles();

  m_rootDeployedFiles.clear();
  m_rootBackups.clear();

  const std::string backupDir = (fs::path(m_rootStorageDir) / "backup").string();
  std::set<std::string> deployedSet;

  for (const auto& [modName, modPath] : mods) {
    const auto rootDir = findRootDir(modPath);
    if (rootDir.empty()) continue;

    std::error_code ec;
    for (const auto& entry :
         fs::recursive_directory_iterator(rootDir, ec)) {
      if (!entry.is_regular_file(ec)) continue;

      const auto relPath = fs::relative(entry.path(), rootDir, ec).string();
      const auto dst     = (fs::path(m_gameDir) / relPath).string();

      if (deployedSet.count(dst)) continue;  // higher-priority mod already deployed

      // Backup existing file
      if (fs::exists(dst, ec) && !deployedSet.count(dst)) {
        const auto bak = (fs::path(backupDir) / relPath).string();
        fs::create_directories(fs::path(bak).parent_path(), ec);
        fs::copy_file(dst, bak, fs::copy_options::overwrite_existing, ec);
        m_rootBackups[dst] = bak;
      }

      // Deploy: always copy (exe/dll need it, and symlinks can confuse Wine)
      if (fs::exists(dst, ec) || fs::is_symlink(dst, ec)) {
        fs::remove(dst, ec);
      }
      fs::create_directories(fs::path(dst).parent_path(), ec);

      if (!reflinkCopy(entry.path().string(), dst)) {
        std::fprintf(stderr, "[RootBuilder] failed to copy '%s' -> '%s'\n",
                     entry.path().c_str(), dst.c_str());
        continue;
      }

      m_rootDeployedFiles.push_back(dst);
      deployedSet.insert(dst);
    }
  }

  saveRootManifest(m_rootStorageDir, m_rootDeployedFiles, m_rootBackups);

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr, "[RootBuilder] deployed %zu files (%zu backups) in %lldms\n",
               m_rootDeployedFiles.size(), m_rootBackups.size(),
               static_cast<long long>(ms));
}

void FuseConnector::clearRootFiles()
{
  if (m_rootStorageDir.empty()) return;

  namespace fs = std::filesystem;
  std::error_code ec;

  // Load manifest if we don't have in-memory state
  if (m_rootDeployedFiles.empty()) {
    loadRootManifest(m_rootStorageDir, m_rootDeployedFiles, m_rootBackups);
  }

  if (m_rootDeployedFiles.empty()) return;

  int removed = 0;
  for (const auto& dst : m_rootDeployedFiles) {
    if (fs::exists(dst, ec) || fs::is_symlink(dst, ec)) {
      fs::remove(dst, ec);
      ++removed;
    }
  }

  // Restore backups
  for (const auto& [dst, bak] : m_rootBackups) {
    if (fs::exists(bak, ec)) {
      fs::create_directories(fs::path(dst).parent_path(), ec);
      fs::rename(bak, dst, ec);
    }
  }

  // Clean up backup directory and manifest
  const auto backupDir = fs::path(m_rootStorageDir) / "backup";
  fs::remove_all(backupDir, ec);
  fs::remove(fs::path(m_rootStorageDir) / "manifest.json", ec);

  std::fprintf(stderr, "[RootBuilder] cleared %d deployed files, restored %zu backups\n",
               removed, m_rootBackups.size());

  m_rootDeployedFiles.clear();
  m_rootBackups.clear();
}
