#include "vfstree.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>

namespace
{
namespace fs = std::filesystem;

// Clock offset computed once and reused for all file time conversions.
// Avoids two now() syscalls per file (was ~600K calls for 300K files).
struct ClockOffset
{
  std::chrono::system_clock::duration offset;

  ClockOffset()
  {
    const auto nowFs  = fs::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    offset = nowSys.time_since_epoch() -
             std::chrono::duration_cast<std::chrono::system_clock::duration>(
                 nowFs.time_since_epoch());
  }

  std::chrono::system_clock::time_point convert(const fs::file_time_type& t) const
  {
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            t.time_since_epoch()) +
        offset);
  }
};

static const ClockOffset g_clockOffset;

std::chrono::system_clock::time_point
fsTimeToSystemClock(const fs::file_time_type& t)
{
  return g_clockOffset.convert(t);
}

std::vector<std::string> splitPath(const std::string& path)
{
  std::vector<std::string> out;
  std::string clean = path;
  std::replace(clean.begin(), clean.end(), '\\', '/');

  size_t start = 0;
  while (start < clean.size()) {
    while (start < clean.size() && clean[start] == '/') {
      ++start;
    }
    if (start >= clean.size()) {
      break;
    }
    const size_t end = clean.find('/', start);
    if (end == std::string::npos) {
      out.push_back(clean.substr(start));
      break;
    }
    out.push_back(clean.substr(start, end - start));
    start = end + 1;
  }

  return out;
}

// Fast relative path extraction by stripping a known prefix string.
// Avoids fs::relative() which does expensive canonicalization (~300K calls).
std::string fastRelative(const std::string& fullPath, const std::string& prefix)
{
  if (fullPath.size() <= prefix.size()) {
    return {};
  }
  size_t start = prefix.size();
  // Skip separators after prefix
  while (start < fullPath.size() && (fullPath[start] == '/' || fullPath[start] == '\\')) {
    ++start;
  }
  if (start >= fullPath.size()) {
    return {};
  }
  return fullPath.substr(start);
}

// Per-directory intermediate results for parallel tree building.
struct DirScanResult
{
  struct FileEntry
  {
    std::vector<std::string> components;
    std::string real_path;
    uint64_t size;
    std::chrono::system_clock::time_point mtime;
  };

  std::vector<FileEntry> files;
  std::vector<std::vector<std::string>> dirs;
};

DirScanResult scanOneModDir(const fs::path& walkDir, const std::string& prefixStr,
                            const std::vector<std::string>& prefix)
{
  DirScanResult result;

  if (!fs::exists(walkDir)) {
    return result;
  }

  for (auto it = fs::recursive_directory_iterator(
           walkDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), prefixStr);
    if (relStr.empty()) {
      continue;
    }

    // Skip meta.ini at top level only
    if (relStr == "meta.ini") {
      continue;
    }

    auto relParts = splitPath(relStr);
    std::vector<std::string> components;
    components.reserve(prefix.size() + relParts.size());
    components.insert(components.end(), prefix.begin(), prefix.end());
    components.insert(components.end(), relParts.begin(), relParts.end());

    if (entry.is_directory(ec)) {
      result.dirs.push_back(std::move(components));
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto size = entry.file_size(ec);
    std::error_code mtimeEc;
    const auto mtime = entry.last_write_time(mtimeEc);

    DirScanResult::FileEntry fe;
    fe.components = std::move(components);
    fe.real_path  = entry.path().string();
    fe.size       = ec ? 0ULL : size;
    fe.mtime      = mtimeEc ? std::chrono::system_clock::time_point{}
                            : fsTimeToSystemClock(mtime);
    result.files.push_back(std::move(fe));
  }

  return result;
}

void addDirectoryToTree(VfsTree& tree, const fs::path& walkDir,
                        const fs::path& stripPrefix, const std::string& origin,
                        const std::vector<std::string>& prefix,
                        bool is_backing = false)
{
  if (!fs::exists(walkDir)) {
    return;
  }

  const std::string prefixStr = stripPrefix.string();

  for (auto it = fs::recursive_directory_iterator(
           walkDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), prefixStr);
    if (relStr.empty()) {
      continue;
    }

    if (relStr == "meta.ini") {
      continue;
    }

    auto relParts = splitPath(relStr);
    std::vector<std::string> components;
    components.reserve(prefix.size() + relParts.size());
    components.insert(components.end(), prefix.begin(), prefix.end());
    components.insert(components.end(), relParts.begin(), relParts.end());

    if (entry.is_directory(ec)) {
      tree.root.insertDirectory(components);
      ++tree.dir_count;
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto size  = entry.file_size(ec);
    std::error_code mtimeEc;
    const auto mtime = entry.last_write_time(mtimeEc);
    tree.root.insertFile(components, entry.path().string(), ec ? 0ULL : size,
                         mtimeEc ? std::chrono::system_clock::time_point{}
                                 : fsTimeToSystemClock(mtime),
                         origin, is_backing);
    ++tree.file_count;
  }
}

bool removeNodeRecursive(VfsNode* node, const std::vector<std::string>& components,
                         size_t index)
{
  if (node == nullptr || !node->is_directory || index >= components.size()) {
    return false;
  }

  const std::string key = normalizeForLookup(components[index]);
  auto it               = node->dir_info.children.find(key);
  if (it == node->dir_info.children.end()) {
    return false;
  }

  if (index + 1 == components.size()) {
    node->dir_info.children.erase(it);
    node->dir_info.display_names.erase(key);
    return true;
  }

  VfsNode* child = it->second.get();
  if (!removeNodeRecursive(child, components, index + 1)) {
    return false;
  }

  if (child->is_directory && child->dir_info.children.empty()) {
    node->dir_info.children.erase(key);
    node->dir_info.display_names.erase(key);
  }

  return true;
}

}  // namespace

std::string normalizeForLookup(const std::string& path)
{
  std::string result;
  result.reserve(path.size());
  for (unsigned char c : path) {
    result.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(c)));
  }
  return result;
}

void VfsNode::insertFile(const std::vector<std::string>& components,
                         const std::string& real_path, uint64_t size,
                         std::chrono::system_clock::time_point mtime,
                         const std::string& origin, bool is_backing)
{
  if (components.empty()) {
    return;
  }

  VfsNode* current = this;
  for (size_t i = 0; i < components.size(); ++i) {
    const std::string& part = components[i];
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      current->is_directory = true;
      current->dir_info     = VfsDirInfo{};
    }

    const std::string key = normalizeForLookup(part);
    current->dir_info.display_names[key] = part;

    if (i + 1 == components.size()) {
      auto fileNode              = std::make_unique<VfsNode>();
      fileNode->is_directory     = false;
      fileNode->file_info        = {real_path, size, mtime, origin, is_backing};
      current->dir_info.children[key] = std::move(fileNode);
      return;
    }

    auto it = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end() || !it->second->is_directory) {
      auto newDir          = std::make_unique<VfsNode>();
      newDir->is_directory = true;
      current->dir_info.children[key] = std::move(newDir);
    }

    current = current->dir_info.children[key].get();
  }
}

void VfsNode::insertDirectory(const std::vector<std::string>& components)
{
  if (components.empty()) {
    return;
  }

  VfsNode* current = this;
  for (const auto& part : components) {
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      current->is_directory = true;
      current->dir_info     = VfsDirInfo{};
    }

    const std::string key = normalizeForLookup(part);
    current->dir_info.display_names[key] = part;

    auto it = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end() || !it->second->is_directory) {
      auto newDir          = std::make_unique<VfsNode>();
      newDir->is_directory = true;
      current->dir_info.children[key] = std::move(newDir);
    }

    current = current->dir_info.children[key].get();
  }
}

const VfsNode* VfsNode::resolve(const std::vector<std::string>& components) const
{
  const VfsNode* current = this;

  for (const auto& part : components) {
    if (part.empty()) {
      continue;
    }

    if (!current->is_directory) {
      return nullptr;
    }

    const std::string key = normalizeForLookup(part);
    auto it               = current->dir_info.children.find(key);
    if (it == current->dir_info.children.end()) {
      return nullptr;
    }

    current = it->second.get();
  }

  return current;
}

std::vector<std::pair<std::string, const VfsNode*>> VfsNode::listChildren() const
{
  std::vector<std::pair<std::string, const VfsNode*>> out;
  if (!is_directory) {
    return out;
  }

  out.reserve(dir_info.children.size());
  for (const auto& [key, node] : dir_info.children) {
    auto display = dir_info.display_names.find(key);
    if (display == dir_info.display_names.end()) {
      continue;
    }
    out.emplace_back(display->second, node.get());
  }

  return out;
}

bool VfsNode::removeFromTree(const std::vector<std::string>& components)
{
  if (components.empty()) {
    return false;
  }
  return removeNodeRecursive(this, components, 0);
}

VfsTree buildVfsTree(const std::vector<std::pair<std::string, std::string>>& mods,
                     const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  for (const auto& [modName, modPath] : mods) {
    addDirectoryToTree(tree, fs::path(modPath), fs::path(modPath), modName, {});
  }

  return tree;
}

VfsTree buildFullGameVfs(const std::string& game_dir, const std::string& data_dir,
                         const std::vector<std::pair<std::string, std::string>>& mods,
                         const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  addDirectoryToTree(tree, fs::path(game_dir), fs::path(game_dir), "_base_game", {});
  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  const auto dataPrefix = splitPath(data_dir);
  for (const auto& [modName, modPath] : mods) {
    // Step D requirement: no Root/ handling. Every mod file is projected under data_dir.
    addDirectoryToTree(tree, fs::path(modPath), fs::path(modPath), modName, dataPrefix);
  }

  return tree;
}

std::vector<CachedBaseFile> scanDataDir(const std::string& data_dir_path)
{
  std::vector<CachedBaseFile> cache;
  const fs::path dataDir(data_dir_path);

  if (!fs::exists(dataDir)) {
    return cache;
  }

  const std::string dataDirStr = dataDir.string();

  for (auto it = fs::recursive_directory_iterator(
           dataDir, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    std::error_code ec;

    const std::string relStr = fastRelative(entry.path().string(), dataDirStr);
    if (relStr.empty()) {
      continue;
    }

    CachedBaseFile cf;
    cf.relative_path = relStr;
    cf.is_dir        = entry.is_directory(ec);

    if (!cf.is_dir) {
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      cf.size  = entry.file_size(ec);
      std::error_code mtimeEc;
      const auto mtime = entry.last_write_time(mtimeEc);
      cf.mtime = mtimeEc ? std::chrono::system_clock::time_point{}
                          : fsTimeToSystemClock(mtime);
    }

    cache.push_back(std::move(cf));
  }

  return cache;
}

VfsTree buildDataDirVfs(const std::vector<CachedBaseFile>& cached_files,
                        const std::string& data_dir,
                        const std::vector<std::pair<std::string, std::string>>& mods,
                        const std::string& overwrite_dir)
{
  VfsTree tree;
  tree.root.is_directory = true;
  tree.file_count        = 0;
  tree.dir_count         = 1;

  // Layer 1: Base game files from cache (is_backing=true)
  // real_path stores the relative path; FUSE handler uses openat(backing_fd, rel)
  for (const auto& cf : cached_files) {
    const auto components = splitPath(cf.relative_path);
    if (cf.is_dir) {
      tree.root.insertDirectory(components);
      ++tree.dir_count;
    } else {
      tree.root.insertFile(components, cf.relative_path, cf.size, cf.mtime,
                           "_base_game", /*is_backing=*/true);
      ++tree.file_count;
    }
  }

  // Layer 2: Mods in priority order (higher priority).
  // Scan mod directories in parallel — each mod is independent.
  // Use a bounded number of threads to avoid overwhelming the IO subsystem.
  {
    const size_t numThreads =
        std::min(static_cast<size_t>(std::thread::hardware_concurrency()),
                 std::max(mods.size(), size_t{1}));

    // Launch parallel scans
    std::vector<std::future<DirScanResult>> futures;
    futures.reserve(mods.size());
    for (const auto& [modName, modPath] : mods) {
      futures.push_back(std::async(
          std::launch::async,
          [&modPath]() {
            return scanOneModDir(fs::path(modPath), modPath, {});
          }));
    }

    // Merge results into tree in priority order (sequential — tree isn't thread-safe)
    for (size_t i = 0; i < mods.size(); ++i) {
      auto result = futures[i].get();
      const auto& origin = mods[i].first;

      for (auto& dir : result.dirs) {
        tree.root.insertDirectory(dir);
        ++tree.dir_count;
      }
      for (auto& fe : result.files) {
        tree.root.insertFile(fe.components, fe.real_path, fe.size, fe.mtime,
                             origin, false);
        ++tree.file_count;
      }
    }
  }

  // Layer 3: Overwrite (highest priority)
  addDirectoryToTree(tree, fs::path(overwrite_dir), fs::path(overwrite_dir),
                     "Overwrite", {});

  return tree;
}

void injectExtraFiles(
    VfsTree& tree,
    const std::vector<std::pair<std::string, std::string>>& extra_files)
{
  for (const auto& [relPath, realPath] : extra_files) {
    auto components = splitPath(relPath);
    if (components.empty()) {
      continue;
    }

    std::error_code ec;
    const auto size = fs::file_size(realPath, ec);
    tree.root.insertFile(components, realPath, ec ? 0ULL : size,
                         std::chrono::system_clock::now(), "_profile",
                         /*is_backing=*/false);
    ++tree.file_count;
  }
}

void stampPluginTimestamps(VfsTree& tree,
                           const std::vector<std::string>& load_order)
{
  if (load_order.empty()) {
    return;
  }

  // Start from a base time and increment by 60 seconds per plugin.
  // This gives LOOT a clear, unambiguous ordering via file timestamps.
  auto baseTime = std::chrono::system_clock::now() -
                  std::chrono::seconds(static_cast<long>(load_order.size()) * 60);

  for (size_t i = 0; i < load_order.size(); ++i) {
    const auto components = splitPath(load_order[i]);
    if (components.empty()) {
      continue;
    }

    const std::string key = normalizeForLookup(load_order[i]);
    auto it = tree.root.dir_info.children.find(key);
    if (it == tree.root.dir_info.children.end()) {
      continue;
    }

    VfsNode* node = it->second.get();
    if (node != nullptr && !node->is_directory) {
      node->file_info.mtime = baseTime + std::chrono::seconds(static_cast<long>(i) * 60);
    }
  }
}
