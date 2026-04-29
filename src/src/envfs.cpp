#include "envfs.h"
#include "env.h"
#include "shared/util.h"

#include <log.h>
#include <utility.h>

#include <chrono>
#include <filesystem>

namespace env
{

namespace fs = std::filesystem;

File::File(std::wstring_view n, FILETIME ft, uint64_t s)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name)), lastModified(ft),
      size(s)
{}

Directory::Directory() = default;

Directory::Directory(std::wstring_view n)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name))
{}

void setHandleCloserThreadCount(std::size_t /*n*/)
{
  // No-op on Linux: there are no win32 handles to close on background
  // threads. Kept as a stub so callers from upstream don't need #ifdef.
}

namespace
{
  // Convert a filesystem clock time to a Win32 FILETIME (100ns ticks since
  // 1601-01-01 UTC). Used so the rest of the codebase can carry timestamps in
  // a single representation regardless of host OS.
  FILETIME toFileTime(fs::file_time_type t)
  {
    using namespace std::chrono;
    const auto sysTime = time_point_cast<system_clock::duration>(
        t - decltype(t)::clock::now() + system_clock::now());
    const auto epoch  = sysTime.time_since_epoch();
    const auto ticks  = duration_cast<duration<int64_t, std::ratio<1, 10000000>>>(epoch)
                           .count();
    // Win32 epoch is 1601-01-01; Unix epoch is 1970-01-01. Offset = 11644473600s.
    constexpr int64_t epochDiff = 116444736000000000LL;
    const int64_t winTicks      = ticks + epochDiff;

    FILETIME ft;
    ft.dwLowDateTime  = static_cast<uint32_t>(winTicks & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<uint32_t>((winTicks >> 32) & 0xFFFFFFFF);
    return ft;
  }

  // Walk `path` recursively, calling the C-style callbacks. dirStartF and
  // dirEndF wrap a directory's contents so callers can build a tree.
  void walkDirectory(const fs::path& path, void* cx, DirStartF* dirStartF,
                     DirEndF* dirEndF, FileF* fileF)
  {
    std::error_code ec;
    fs::directory_iterator it(path, ec);
    if (ec) {
      return;
    }

    for (const auto& entry : it) {
      const auto name = entry.path().filename().wstring();

      if (entry.is_directory(ec)) {
        if (dirStartF) {
          dirStartF(cx, name);
        }
        walkDirectory(entry.path(), cx, dirStartF, dirEndF, fileF);
        if (dirEndF) {
          dirEndF(cx, name);
        }
      } else if (entry.is_regular_file(ec)) {
        if (fileF) {
          const auto size = static_cast<uint64_t>(entry.file_size(ec));
          const auto ft   = toFileTime(entry.last_write_time(ec));
          fileF(cx, name, ft, size);
        }
      }
    }
  }
}  // namespace

void DirectoryWalker::forEachEntry(const std::wstring& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF,
                                   FileF* fileF)
{
  walkDirectory(fs::path(path), cx, dirStartF, dirEndF, fileF);
}

void forEachEntry(const std::wstring& path, void* cx, DirStartF* dirStartF,
                  DirEndF* dirEndF, FileF* fileF)
{
  walkDirectory(fs::path(path), cx, dirStartF, dirEndF, fileF);
}

}  // namespace env
