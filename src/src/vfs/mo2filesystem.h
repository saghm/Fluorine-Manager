#ifndef VFS_MO2FILESYSTEM_H
#define VFS_MO2FILESYSTEM_H

#include <fuse3/fuse_lowlevel.h>

#include "inodetable.h"
#include "overwritemanager.h"
#include "trackedwrites.h"
#include "vfstree.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// FUSE passthrough support (kernel 6.9+, libfuse 3.16+).
// When available, the kernel serves reads directly from the backing file
// without round-tripping through userspace — near-native I/O performance.
#ifndef FUSE_CAP_PASSTHROUGH
#define FUSE_CAP_PASSTHROUGH 0
#endif

struct Mo2FsContext
{
  std::shared_ptr<VfsTree> tree;
  mutable std::shared_mutex tree_mutex;

  std::unique_ptr<InodeTable> inodes;
  mutable std::shared_mutex inode_mutex;

  std::unique_ptr<OverwriteManager> overwrite;
  std::shared_ptr<TrackedWrites> tracked_writes;

  int backing_dir_fd = -1;

  struct OpenFile
  {
    int fd = -1;
    std::string real_path;
    bool writable    = false;
    bool is_backing  = false;
    bool cow_pending = false;   // true = opened R/O, will COW on first write()
    bool is_tracked  = false;   // true = user moved this from Overwrite to a mod
    std::string relative_path;
  };

  std::unordered_map<uint64_t, OpenFile> open_files;
  mutable std::shared_mutex open_files_mutex;
  std::atomic<uint64_t> next_fh{1};

  struct DirEntry
  {
    fuse_ino_t ino = 0;
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
    std::chrono::system_clock::time_point mtime{};
  };
  struct OpenDir
  {
    std::string path;
    std::shared_ptr<std::vector<DirEntry>> entries;
    std::shared_ptr<std::vector<char>> readdir_blob;
    std::shared_ptr<std::vector<char>> readdirplus_blob;
  };
  std::unordered_map<uint64_t, OpenDir> open_dirs;
  mutable std::mutex open_dirs_mutex;
  std::unordered_map<std::string, std::shared_ptr<std::vector<DirEntry>>> dir_cache;
  std::unordered_map<std::string, std::shared_ptr<std::vector<char>>> readdir_blob_cache;
  std::unordered_map<std::string, std::shared_ptr<std::vector<char>>> readdirplus_blob_cache;
  mutable std::mutex dir_cache_mutex;
  struct CachedAttr
  {
    struct stat st {};
    std::chrono::steady_clock::time_point expires_at{};
    bool valid = false;
  };
  std::unordered_map<fuse_ino_t, CachedAttr> attr_cache;
  mutable std::mutex attr_cache_mutex;
  std::atomic<uint64_t> next_dh{1};
  std::atomic<uint64_t> lookup_count{0};
  std::atomic<uint64_t> getattr_count{0};
  std::atomic<uint64_t> readdir_count{0};
  std::atomic<uint64_t> open_count{0};
  std::atomic<uint64_t> read_count{0};
  std::atomic<uint64_t> ioctl_count{0};
  std::atomic<uint64_t> op_tick{0};
  std::unordered_map<std::string, uint64_t> lookup_hit_paths;
  std::unordered_map<std::string, uint64_t> lookup_miss_paths;
  std::unordered_map<std::string, uint64_t> getattr_paths;
  std::unordered_map<std::string, uint64_t> readdir_paths;
  mutable std::mutex path_stats_mutex;
  std::atomic<uint64_t> path_sample_tick{0};

  uid_t uid = 0;
  gid_t gid = 0;

  // FUSE passthrough: when true AND kernel supports it, read-only file opens
  // use kernel-level passthrough (zero-copy I/O bypassing userspace).
  bool passthrough_requested = false;
  bool passthrough_active    = false;  // set in mo2_init after capability check
};

void mo2_init(void* userdata, struct fuse_conn_info* conn);
void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* fi);
void mo2_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info* fi);
void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
              struct fuse_file_info* fi);
void mo2_write(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi);
void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode,
                struct fuse_file_info* fi);
void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags);
void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi);
void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name);
void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode);
void mo2_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
void mo2_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
#if FUSE_USE_VERSION < 35
void mo2_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void* arg,
               struct fuse_file_info* fi, unsigned flags, const void* in_buf,
               size_t in_bufsz, size_t out_bufsz);
#else
void mo2_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void* arg,
               struct fuse_file_info* fi, unsigned flags, const void* in_buf,
               size_t in_bufsz, size_t out_bufsz);
#endif

#endif
