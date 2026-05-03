#ifndef VFS_SCANCACHE_H
#define VFS_SCANCACHE_H

#include "vfstree.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Persistent merged-VFS cache.  Saves the post-build VfsTree to disk,
// validated by modlist + per-mod dir mtimes + overwrite/data dir mtimes.
//
// On a hit, mounting skips both scanDataDir() and buildDataDirVfs() —
// the entire tree comes back from a single binary read in ~tens of ms.
// On miss/drift, the caller falls back to the normal scan path and is
// expected to call save() with the freshly-built tree.
struct ScanCacheKey
{
  // For mtime+size validation; cache invalidates if either drifts.
  std::filesystem::path modlist_txt;

  // Whole-dir mtime check; bumps on any direct-child add/remove/rename.
  std::filesystem::path data_dir;
  std::filesystem::path overwrite_dir;

  // (origin name, absolute mod dir) in priority order.  Order matters —
  // any reorder invalidates the cache.
  std::vector<std::pair<std::string, std::string>> mods;
};

class ScanCache
{
public:
  // Cache file path is derived from a hash of the data_dir + mods so
  // each unique mount config gets its own cache file.
  static std::filesystem::path cacheFilePath(const ScanCacheKey& key);

  explicit ScanCache(std::filesystem::path cache_file);

  // Returns a populated tree on hit, nullptr on miss/drift/corruption.
  std::shared_ptr<VfsTree> tryLoad(const ScanCacheKey& key);

  // Best-effort persist; returns false on I/O failure but never throws.
  bool save(const ScanCacheKey& key, const VfsTree& tree);

  // Force the next tryLoad() to miss by removing the cache file.
  void invalidate();

private:
  std::filesystem::path m_cache_file;
};

#endif  // VFS_SCANCACHE_H
