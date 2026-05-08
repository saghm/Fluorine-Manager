#ifndef VFS_VFSBRIDGE_H
#define VFS_VFSBRIDGE_H

#include "vfstree.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct VfsBridgeExportResult
{
  bool ok = false;
  std::filesystem::path path;
  std::size_t records_written = 0;
  std::string error;
};

std::filesystem::path vfsBridgeIndexPath(
    const std::string& data_dir, const std::string& overwrite_dir,
    const std::vector<std::pair<std::string, std::string>>& mods);

VfsBridgeExportResult exportVfsBridgeIndex(
    const VfsTree& tree, const std::filesystem::path& path,
    const std::string& data_dir, const std::string& overwrite_dir,
    const std::string& mount_point);

#endif  // VFS_VFSBRIDGE_H
