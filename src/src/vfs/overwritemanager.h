#ifndef VFS_OVERWRITEMANAGER_H
#define VFS_OVERWRITEMANAGER_H

#include <cstdint>
#include <string>
#include <vector>

class OverwriteManager
{
public:
  OverwriteManager(const std::string& staging_dir, const std::string& overwrite_dir);

  std::string copyOnWrite(const std::string& source_path,
                          const std::string& relative_path) const;

  std::string copyOnWriteFromFd(int dir_fd, const std::string& relative_path) const;
  std::string copyOnWriteFromFd(int dir_fd, const std::string& source_relative_path,
                                const std::string& dest_relative_path) const;

  std::string writeFile(const std::string& relative_path,
                        const std::vector<uint8_t>& data) const;

  bool rename(const std::string& old_relative, const std::string& new_relative) const;
  bool removeFile(const std::string& relative_path) const;
  bool removeDirectory(const std::string& relative_path, bool* out_not_empty = nullptr) const;
  bool createDirectory(const std::string& relative_path) const;

  bool exists(const std::string& relative_path) const;
  std::string overwritePath(const std::string& relative_path) const;
  std::string stagingPath(const std::string& relative_path) const;

private:
  std::string m_stagingDir;
  std::string m_overwriteDir;
};

#endif
