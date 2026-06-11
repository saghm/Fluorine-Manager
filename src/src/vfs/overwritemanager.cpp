#include "overwritemanager.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
namespace fs = std::filesystem;

std::string sanitizeRelative(const std::string& relative)
{
  std::string out = relative;
  for (char& c : out) {
    if (c == '\\') {
      c = '/';
    }
  }
  while (!out.empty() && out.front() == '/') {
    out.erase(out.begin());
  }
  return out;
}

std::string lowercaseAscii(const std::string& value)
{
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

fs::path resolveCaseInsensitivePath(const fs::path& root,
                                    const std::string& relative_path,
                                    bool stop_before_leaf,
                                    std::error_code* out_error = nullptr)
{
  if (out_error != nullptr) {
    out_error->clear();
  }

  fs::path current = root;
  const fs::path relative(sanitizeRelative(relative_path));
  const auto end = relative.end();

  for (auto it = relative.begin(); it != end; ++it) {
    if (stop_before_leaf && std::next(it) == end) {
      current /= *it;
      break;
    }

    std::error_code ec;
    const bool currentExists = fs::exists(current, ec);
    if (ec) {
      if (out_error != nullptr) {
        *out_error = ec;
      }
      return current / *it;
    }

    if (!currentExists) {
      current /= *it;
      continue;
    }

    if (!fs::is_directory(current, ec)) {
      if (out_error != nullptr) {
        *out_error = ec ? ec
                        : std::make_error_code(std::errc::not_a_directory);
      }
      return current / *it;
    }

    const std::string wanted = lowercaseAscii(it->string());
    bool matched = false;
    for (const auto& entry : fs::directory_iterator(
             current, fs::directory_options::skip_permission_denied, ec)) {
      if (lowercaseAscii(entry.path().filename().string()) == wanted) {
        current = entry.path();
        matched = true;
        break;
      }
    }
    if (ec) {
      if (out_error != nullptr) {
        *out_error = ec;
      }
      return current / *it;
    }
    if (!matched) {
      current /= *it;
    }
  }

  return current;
}

fs::path resolveExistingCaseInsensitivePath(const fs::path& root,
                                            const std::string& relative_path,
                                            std::error_code* out_error = nullptr)
{
  return resolveCaseInsensitivePath(root, relative_path, false, out_error);
}

fs::path resolveParentCaseInsensitivePath(const fs::path& root,
                                          const std::string& relative_path,
                                          std::error_code* out_error = nullptr)
{
  return resolveCaseInsensitivePath(root, relative_path, true, out_error);
}

void setError(std::error_code* out_error, std::error_code ec)
{
  if (out_error != nullptr) {
    *out_error = ec;
  }
}
}  // namespace

OverwriteManager::OverwriteManager(const std::string& staging_dir,
                                   const std::string& overwrite_dir)
    : m_stagingDir(staging_dir), m_overwriteDir(overwrite_dir)
{
  std::error_code ec;
  fs::create_directories(m_stagingDir, ec);
  fs::create_directories(m_overwriteDir, ec);
}

std::string OverwriteManager::stagingPath(const std::string& relative_path) const
{
  return (fs::path(m_stagingDir) / sanitizeRelative(relative_path)).string();
}

std::string OverwriteManager::overwritePath(const std::string& relative_path) const
{
  return (fs::path(m_overwriteDir) / sanitizeRelative(relative_path)).string();
}

std::string OverwriteManager::copyOnWrite(const std::string& source_path,
                                          const std::string& relative_path) const
{
  std::error_code ec;
  fs::path dest = resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (ec) {
    throw fs::filesystem_error("copyOnWrite", dest, ec);
  }

  if (!fs::exists(dest, ec)) {
    dest = resolveParentCaseInsensitivePath(m_stagingDir, relative_path, &ec);
    if (ec) {
      throw fs::filesystem_error("copyOnWrite", dest, ec);
    }
  }
  fs::create_directories(dest.parent_path(), ec);
  if (ec) {
    throw fs::filesystem_error("copyOnWrite", dest.parent_path(), ec);
  }

  if (fs::exists(dest, ec)) {
    if (fs::is_directory(dest, ec)) {
      throw fs::filesystem_error(
          "copyOnWrite", dest,
          ec ? ec : std::make_error_code(std::errc::is_a_directory));
    }
    // Ensure write permission even for pre-existing staging files (e.g.
    // orphaned from a previous crash).
    fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add, ec);
    return dest.string();
  }

  if (!source_path.empty() && fs::exists(source_path, ec)) {
    fs::copy_file(source_path, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      throw fs::filesystem_error("copyOnWrite", fs::path(source_path), dest, ec);
    }
    // copy_file preserves source permissions.  If the source was read-only
    // (common for Steam game files or mods extracted from Windows archives),
    // the staging copy would also be read-only, causing subsequent writes
    // to fail with EACCES.  Ensure the owner can always write.
    fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add, ec);
  } else {
    std::ofstream out(dest, std::ios::binary);
    out.close();
  }

  return dest.string();
}

std::string OverwriteManager::copyOnWriteFromFd(int dir_fd,
                                                const std::string& relative_path) const
{
  return copyOnWriteFromFd(dir_fd, relative_path, relative_path);
}

std::string OverwriteManager::copyOnWriteFromFd(
    int dir_fd, const std::string& source_relative_path,
    const std::string& dest_relative_path) const
{
  std::error_code ec;
  fs::path dest = resolveExistingCaseInsensitivePath(m_stagingDir, dest_relative_path, &ec);
  if (ec) {
    throw fs::filesystem_error("copyOnWriteFromFd", dest, ec);
  }

  if (!fs::exists(dest, ec)) {
    dest = resolveParentCaseInsensitivePath(m_stagingDir, dest_relative_path, &ec);
    if (ec) {
      throw fs::filesystem_error("copyOnWriteFromFd", dest, ec);
    }
  }
  fs::create_directories(dest.parent_path(), ec);
  if (ec) {
    throw fs::filesystem_error("copyOnWriteFromFd", dest.parent_path(), ec);
  }

  if (fs::exists(dest, ec)) {
    if (fs::is_directory(dest, ec)) {
      throw fs::filesystem_error(
          "copyOnWriteFromFd", dest,
          ec ? ec : std::make_error_code(std::errc::is_a_directory));
    }
    return dest.string();
  }

  const std::string rel = sanitizeRelative(source_relative_path);
  const int src_fd      = openat(dir_fd, rel.c_str(), O_RDONLY);
  if (src_fd < 0) {
    // Source doesn't exist in backing dir, create empty file
    std::ofstream out(dest, std::ios::binary);
    out.close();
    return dest.string();
  }

  std::ofstream out(dest, std::ios::binary);
  if (!out) {
    close(src_fd);
    throw fs::filesystem_error("copyOnWriteFromFd", dest,
                               std::error_code(errno, std::generic_category()));
  }

  char buf[65536];
  ssize_t n;
  while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
    out.write(buf, n);
  }

  close(src_fd);
  out.close();
  fs::permissions(dest, fs::perms::owner_write, fs::perm_options::add, ec);
  return dest.string();
}

std::string OverwriteManager::writeFile(const std::string& relative_path,
                                        const std::vector<uint8_t>& data) const
{
  std::error_code ec;
  fs::path path = resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (ec) {
    throw fs::filesystem_error("writeFile", path, ec);
  }

  if (!fs::exists(path, ec)) {
    path = resolveParentCaseInsensitivePath(m_stagingDir, relative_path, &ec);
    if (ec) {
      throw fs::filesystem_error("writeFile", path, ec);
    }
  }
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    throw fs::filesystem_error("writeFile", path.parent_path(), ec);
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw fs::filesystem_error("writeFile", path, std::error_code(errno, std::generic_category()));
  }

  if (!data.empty()) {
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  return path.string();
}

int OverwriteManager::createFile(const std::string& relative_path, mode_t mode,
                                 std::string* real_path,
                                 std::error_code* out_error) const
{
  std::error_code ec;
  fs::path path = resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (ec) {
    setError(out_error, ec);
    return -1;
  }

  if (fs::exists(path, ec)) {
    if (ec) {
      setError(out_error, ec);
      return -1;
    }
    if (fs::is_directory(path, ec)) {
      setError(out_error, ec ? ec : std::make_error_code(std::errc::is_a_directory));
      return -1;
    }
  } else {
    path = resolveParentCaseInsensitivePath(m_stagingDir, relative_path, &ec);
    if (ec) {
      setError(out_error, ec);
      return -1;
    }
  }

  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    setError(out_error, ec);
    return -1;
  }
  if (out_error != nullptr) {
    out_error->clear();
  }

  const mode_t fileMode = mode != 0 ? (mode & 07777) : 0644;
  const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC,
                      fileMode);
  if (fd < 0) {
    setError(out_error, std::error_code(errno, std::generic_category()));
  }
  if (fd >= 0 && real_path != nullptr) {
    *real_path = path.string();
  }
  return fd;
}

bool OverwriteManager::rename(const std::string& old_relative,
                              const std::string& new_relative,
                              std::error_code* out_error) const
{
  std::error_code ec;

  fs::path from = resolveExistingCaseInsensitivePath(m_stagingDir, old_relative, &ec);
  fs::path to   = resolveParentCaseInsensitivePath(m_stagingDir, new_relative, &ec);
  if (ec) {
    setError(out_error, ec);
    return false;
  }

  if (!fs::exists(from, ec)) {
    from = resolveExistingCaseInsensitivePath(m_overwriteDir, old_relative, &ec);
    to   = resolveParentCaseInsensitivePath(m_overwriteDir, new_relative, &ec);
    if (ec) {
      setError(out_error, ec);
      return false;
    }
    if (!fs::exists(from, ec)) {
      setError(out_error, std::make_error_code(std::errc::no_such_file_or_directory));
      return false;
    }
  }

  fs::create_directories(to.parent_path(), ec);
  if (ec) {
    setError(out_error, ec);
    return false;
  }
  fs::rename(from, to, ec);
  setError(out_error, ec);
  return !ec;
}

bool OverwriteManager::removeFile(const std::string& relative_path) const
{
  std::error_code ec;
  fs::path staged = resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (fs::exists(staged, ec)) {
    return fs::remove(staged, ec);
  }

  fs::path overwrite = resolveExistingCaseInsensitivePath(m_overwriteDir, relative_path, &ec);
  if (fs::exists(overwrite, ec)) {
    return fs::remove(overwrite, ec);
  }

  return false;
}

bool OverwriteManager::removeDirectory(const std::string& relative_path,
                                       bool* out_not_empty) const
{
  if (out_not_empty)
    *out_not_empty = false;

  std::error_code ec;
  bool removedAny = false;

  for (const fs::path candidate : {
           resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec),
           resolveExistingCaseInsensitivePath(m_overwriteDir, relative_path, &ec)}) {
    if (!fs::exists(candidate, ec))
      continue;
    if (!fs::is_directory(candidate, ec))
      return false;

    if (!fs::is_empty(candidate, ec)) {
      if (out_not_empty)
        *out_not_empty = true;
      return false;
    }

    if (fs::remove(candidate, ec))
      removedAny = true;
  }

  return removedAny;
}

bool OverwriteManager::createDirectory(const std::string& relative_path,
                                       std::error_code* out_error) const
{
  std::error_code ec;
  fs::path path = resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (ec) {
    setError(out_error, ec);
    return false;
  }

  if (fs::exists(path, ec)) {
    if (ec) {
      setError(out_error, ec);
      return false;
    }
    if (!fs::is_directory(path, ec)) {
      setError(out_error, ec ? ec : std::make_error_code(std::errc::file_exists));
      return false;
    }
    setError(out_error, {});
    return true;
  }

  path = resolveParentCaseInsensitivePath(m_stagingDir, relative_path, &ec);
  if (ec) {
    setError(out_error, ec);
    return false;
  }
  fs::create_directories(path, ec);
  setError(out_error, ec);
  return !ec;
}

bool OverwriteManager::exists(const std::string& relative_path) const
{
  std::error_code ec;
  return fs::exists(resolveExistingCaseInsensitivePath(m_stagingDir, relative_path, &ec),
                    ec) ||
         fs::exists(resolveExistingCaseInsensitivePath(m_overwriteDir, relative_path, &ec),
                    ec);
}
