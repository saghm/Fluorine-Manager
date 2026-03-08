#include "mo2filesystem.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

#if __has_include(<linux/msdos_fs.h>)
#include <linux/msdos_fs.h>
#endif

namespace
{
namespace fs = std::filesystem;

constexpr double TTL_SECONDS = 30.0;
constexpr double NEGATIVE_TTL_SECONDS = 5.0;
constexpr double ATTR_CACHE_SECONDS = 30.0;

void fillStatForDir(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid);
void fillStatForFile(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid,
                     uint64_t size,
                     const std::chrono::system_clock::time_point& mtime);

void maybeLogCounters(Mo2FsContext* ctx)
{
  if (ctx == nullptr) {
    return;
  }

  const uint64_t tick = ctx->op_tick.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((tick % 50000) != 0) {
    return;
  }

  std::fprintf(stderr,
               "[VFS] ops lookup=%llu getattr=%llu readdir=%llu open=%llu read=%llu ioctl=%llu",
               static_cast<unsigned long long>(
                   ctx->lookup_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->getattr_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->readdir_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->open_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->read_count.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   ctx->ioctl_count.load(std::memory_order_relaxed)));
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    std::fprintf(stderr, " open_handles=%zu\n", ctx->open_files.size());
  }

  auto logTop = [](const char* label, const std::unordered_map<std::string, uint64_t>& m) {
    if (m.empty()) {
      return;
    }

    std::vector<std::pair<std::string, uint64_t>> top(m.begin(), m.end());
    const size_t keep = std::min<size_t>(3, top.size());
    std::partial_sort(
        top.begin(), top.begin() + static_cast<std::ptrdiff_t>(keep), top.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    std::fprintf(stderr, "[VFS] hot %s:", label);
    for (size_t i = 0; i < keep; ++i) {
      std::fprintf(stderr, " [%llu] %s",
                   static_cast<unsigned long long>(top[i].second),
                   top[i].first.c_str());
    }
    std::fputc('\n', stderr);
  };

  {
    std::scoped_lock lock(ctx->path_stats_mutex);
    logTop("lookup_hit", ctx->lookup_hit_paths);
    logTop("lookup_miss", ctx->lookup_miss_paths);
    logTop("getattr", ctx->getattr_paths);
    logTop("readdir", ctx->readdir_paths);
    ctx->lookup_hit_paths.clear();
    ctx->lookup_miss_paths.clear();
    ctx->getattr_paths.clear();
    ctx->readdir_paths.clear();
  }
}

void invalidateDirCache(Mo2FsContext* ctx, const std::string& dirPath)
{
  if (ctx == nullptr) {
    return;
  }

  // Invalidate only the affected directory (and its open-dir handles), not all.
  {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    for (auto it = ctx->open_dirs.begin(); it != ctx->open_dirs.end();) {
      if (it->second.path == dirPath) {
        it = ctx->open_dirs.erase(it);
      } else {
        ++it;
      }
    }
  }
  {
    std::scoped_lock cacheLock(ctx->dir_cache_mutex);
    ctx->dir_cache.erase(dirPath);
    ctx->readdir_blob_cache.erase(dirPath);
    ctx->readdirplus_blob_cache.erase(dirPath);
  }
}

struct NodeSnapshot
{
  bool found        = false;
  bool is_directory = false;
  bool is_backing   = false;
  uint64_t size     = 0;
  std::chrono::system_clock::time_point mtime{};
  std::string real_path;
};

Mo2FsContext* getContext(fuse_req_t req)
{
  return static_cast<Mo2FsContext*>(fuse_req_userdata(req));
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

std::string joinPath(const std::string& base, const std::string& name)
{
  if (base.empty()) {
    return name;
  }
  return base + "/" + name;
}

std::string inodeToPath(const Mo2FsContext* ctx, fuse_ino_t ino, bool* ok)
{
  std::shared_lock lock(ctx->inode_mutex);
  const std::string path = ctx->inodes->getPath(ino);

  if (ino == 1) {
    *ok = true;
    return "";
  }

  *ok = !path.empty();
  return path;
}

NodeSnapshot snapshotForPath(const Mo2FsContext* ctx, const std::string& path)
{
  NodeSnapshot snap;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr) {
    return snap;
  }

  snap.found        = true;
  snap.is_directory = node->is_directory;
  if (!node->is_directory) {
    snap.real_path  = node->file_info.real_path;
    snap.size       = node->file_info.size;
    snap.mtime      = node->file_info.mtime;
    snap.is_backing = node->file_info.is_backing;
  }

  return snap;
}

struct ChildSnapshot
{
  std::string name;
  bool is_dir = false;
  uint64_t size = 0;
  std::chrono::system_clock::time_point mtime{};
};

std::vector<ChildSnapshot> listChildrenSnapshot(
    const Mo2FsContext* ctx, const std::string& path, bool* ok)
{
  std::vector<ChildSnapshot> out;
  std::shared_lock lock(ctx->tree_mutex);

  const VfsNode* node = path.empty() ? &ctx->tree->root : ctx->tree->root.resolve(splitPath(path));
  if (node == nullptr || !node->is_directory) {
    *ok = false;
    return out;
  }

  *ok = true;
  for (const auto& [name, child] : node->listChildren()) {
    ChildSnapshot snap;
    snap.name   = name;
    snap.is_dir = child->is_directory;
    if (!child->is_directory) {
      snap.size  = child->file_info.size;
      snap.mtime = child->file_info.mtime;
    }
    out.push_back(std::move(snap));
  }

  return out;
}

std::vector<Mo2FsContext::DirEntry> buildDirEntries(
    Mo2FsContext* ctx, const std::string& path, fuse_ino_t selfIno, bool* ok)
{
  auto children = listChildrenSnapshot(ctx, path, ok);
  if (!*ok) {
    return {};
  }

  std::vector<Mo2FsContext::DirEntry> entries;
  entries.reserve(children.size() + 2);
  entries.push_back(Mo2FsContext::DirEntry{selfIno, ".", true});
  entries.push_back(Mo2FsContext::DirEntry{1, "..", true});

  std::unique_lock lock(ctx->inode_mutex);
  for (const auto& child : children) {
    const std::string childPath = joinPath(path, child.name);
    entries.push_back(
        Mo2FsContext::DirEntry{ctx->inodes->getOrCreate(childPath), child.name,
                               child.is_dir, child.size, child.mtime});
  }

  return entries;
}

void samplePathStat(Mo2FsContext* ctx, const char* op, const std::string& path,
                    bool miss = false)
{
  if (ctx == nullptr) {
    return;
  }

  const uint64_t sampleTick =
      ctx->path_sample_tick.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((sampleTick % 128) != 0) {
    return;
  }

  std::scoped_lock lock(ctx->path_stats_mutex);
  if (std::strcmp(op, "lookup") == 0) {
    if (miss) {
      ++ctx->lookup_miss_paths[path];
    } else {
      ++ctx->lookup_hit_paths[path];
    }
    return;
  }
  if (std::strcmp(op, "getattr") == 0) {
    ++ctx->getattr_paths[path];
    return;
  }
  if (std::strcmp(op, "readdir") == 0) {
    ++ctx->readdir_paths[path];
  }
}

std::shared_ptr<std::vector<Mo2FsContext::DirEntry>> getOrBuildDirEntries(
    Mo2FsContext* ctx, const std::string& path, fuse_ino_t ino, bool* ok)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->dir_cache.find(path);
    if (it != ctx->dir_cache.end()) {
      *ok = true;
      return it->second;
    }
  }

  auto entries = std::make_shared<std::vector<Mo2FsContext::DirEntry>>(
      buildDirEntries(ctx, path, ino, ok));
  if (!*ok) {
    return {};
  }

  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->dir_cache.emplace(path, entries);
    if (!inserted) {
      return it->second;
    }
  }

  return entries;
}

std::vector<char> buildReaddirBlob(
    fuse_req_t req, const std::vector<Mo2FsContext::DirEntry>& entries)
{
  std::vector<char> blob;
  for (const auto& entry : entries) {
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    st.st_ino  = entry.ino;
    st.st_mode = entry.is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);

    const size_t entSize =
        fuse_add_direntry(req, nullptr, 0, entry.name.c_str(), &st, 0);
    if (entSize == 0) {
      continue;
    }
    const off_t nextOff = static_cast<off_t>(blob.size() + entSize);
    const size_t oldLen = blob.size();
    blob.resize(oldLen + entSize);
    fuse_add_direntry(req, blob.data() + oldLen, entSize, entry.name.c_str(), &st,
                      nextOff);
  }
  return blob;
}

std::vector<char> buildReaddirPlusBlob(
    fuse_req_t req, const Mo2FsContext* ctx,
    const std::vector<Mo2FsContext::DirEntry>& entries)
{
  std::vector<char> blob;
  for (const auto& entry : entries) {
    struct fuse_entry_param e;
    std::memset(&e, 0, sizeof(e));
    e.ino           = entry.ino;
    e.attr_timeout  = TTL_SECONDS;
    e.entry_timeout = TTL_SECONDS;

    if (entry.is_dir) {
      fillStatForDir(&e.attr, entry.ino, ctx->uid, ctx->gid);
    } else {
      fillStatForFile(&e.attr, entry.ino, ctx->uid, ctx->gid, entry.size,
                      entry.mtime);
    }

    const size_t entSize =
        fuse_add_direntry_plus(req, nullptr, 0, entry.name.c_str(), &e, 0);
    if (entSize == 0) {
      continue;
    }
    const off_t nextOff = static_cast<off_t>(blob.size() + entSize);
    const size_t oldLen = blob.size();
    blob.resize(oldLen + entSize);
    fuse_add_direntry_plus(req, blob.data() + oldLen, entSize, entry.name.c_str(),
                           &e, nextOff);
  }
  return blob;
}

std::shared_ptr<std::vector<char>> getOrBuildReaddirBlob(
    Mo2FsContext* ctx, fuse_req_t req, const std::string& path,
    const std::shared_ptr<std::vector<Mo2FsContext::DirEntry>>& entries)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->readdir_blob_cache.find(path);
    if (it != ctx->readdir_blob_cache.end()) {
      return it->second;
    }
  }

  auto built = std::make_shared<std::vector<char>>(buildReaddirBlob(req, *entries));
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->readdir_blob_cache.emplace(path, built);
    if (!inserted) {
      return it->second;
    }
  }
  return built;
}

std::shared_ptr<std::vector<char>> getOrBuildReaddirPlusBlob(
    Mo2FsContext* ctx, fuse_req_t req, const std::string& path,
    const std::shared_ptr<std::vector<Mo2FsContext::DirEntry>>& entries)
{
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto it = ctx->readdirplus_blob_cache.find(path);
    if (it != ctx->readdirplus_blob_cache.end()) {
      return it->second;
    }
  }

  auto built = std::make_shared<std::vector<char>>(
      buildReaddirPlusBlob(req, ctx, *entries));
  {
    std::scoped_lock lock(ctx->dir_cache_mutex);
    auto [it, inserted] = ctx->readdirplus_blob_cache.emplace(path, built);
    if (!inserted) {
      return it->second;
    }
  }
  return built;
}

void fillStatForDir(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_mode  = S_IFDIR | 0755;
  st->st_nlink = 2;
  st->st_uid   = uid;
  st->st_gid   = gid;
  // Keep synthetic directory timestamps stable so kernel/user-space attr caching
  // stays effective across repeated getattr/readdir probes.
  constexpr time_t kVirtualDirTime = 946684800;  // 2000-01-01 00:00:00 UTC
  st->st_mtim.tv_sec = kVirtualDirTime;
  st->st_atim.tv_sec = kVirtualDirTime;
  st->st_ctim.tv_sec = kVirtualDirTime;
}

void fillStatForFile(struct stat* st, fuse_ino_t ino, uid_t uid, gid_t gid,
                     uint64_t size,
                     const std::chrono::system_clock::time_point& mtime)
{
  std::memset(st, 0, sizeof(struct stat));
  st->st_ino   = ino;
  st->st_mode  = S_IFREG | 0644;
  st->st_nlink = 1;
  st->st_uid   = uid;
  st->st_gid   = gid;
  st->st_size  = static_cast<off_t>(size);

  const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
      mtime.time_since_epoch());
  st->st_mtim.tv_sec = secs.count();
  st->st_ctim.tv_sec = secs.count();
  st->st_atim.tv_sec = secs.count();
}

void replyEntryFromSnapshot(fuse_req_t req, const Mo2FsContext* ctx, fuse_ino_t ino,
                            const NodeSnapshot& snap)
{
  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = ino;
  e.attr_timeout  = TTL_SECONDS;
  e.entry_timeout = TTL_SECONDS;

  if (snap.is_directory) {
    fillStatForDir(&e.attr, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&e.attr, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }

  fuse_reply_entry(req, &e);
}

bool isWritableOpen(int flags)
{
  return (flags & O_WRONLY) != 0 || (flags & O_RDWR) != 0;
}

std::chrono::system_clock::time_point fileMtimeOrNow(const std::string& path)
{
  std::error_code ec;
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return std::chrono::system_clock::now();
  }

  const auto nowFs  = fs::file_time_type::clock::now();
  const auto nowSys = std::chrono::system_clock::now();
  return nowSys + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                      mtime - nowFs);
}

void updateFileNode(Mo2FsContext* ctx, const std::string& relative,
                    const std::string& realPath, const std::string& origin)
{
  std::error_code ec;
  const uint64_t size = static_cast<uint64_t>(fs::file_size(realPath, ec));
  const auto mtime    = fileMtimeOrNow(realPath);

  std::unique_lock lock(ctx->tree_mutex);
  ctx->tree->root.insertFile(splitPath(relative), realPath, ec ? 0 : size, mtime,
                             origin);
}

}  // namespace

void mo2_init(void* userdata, struct fuse_conn_info* conn)
{
  auto* ctx = static_cast<Mo2FsContext*>(userdata);

  // Keep kernel page cache valid across open/close as long as mtime/size are
  // unchanged.  Mod files are immutable during a game session, so this avoids
  // re-reading file data from userspace on every open().
  if (conn->capable & FUSE_CAP_AUTO_INVAL_DATA) {
    conn->want |= FUSE_CAP_AUTO_INVAL_DATA;
  }

  // Increase max read/write buffer to 1MB (kernel 4.20+ supports this).
  // Reduces FUSE round-trips for large file reads (textures, meshes, BSAs).
  constexpr unsigned int ONE_MB = 1048576;
  if (conn->max_readahead < ONE_MB) {
    conn->max_readahead = ONE_MB;
  }
  if (conn->max_write < ONE_MB) {
    conn->max_write = ONE_MB;
  }

  std::fprintf(stderr,
               "[VFS] init: auto_inval=%d max_readahead=%u max_write=%u\n",
               (conn->want & FUSE_CAP_AUTO_INVAL_DATA) ? 1 : 0,
               conn->max_readahead, conn->max_write);
}

void mo2_lookup(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->lookup_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string childPath = joinPath(parentPath, name);
  const auto snap             = snapshotForPath(ctx, childPath);
  if (!snap.found) {
    samplePathStat(ctx, "lookup", childPath, true);
    struct fuse_entry_param e;
    std::memset(&e, 0, sizeof(e));
    e.ino           = 0;
    e.attr_timeout  = NEGATIVE_TTL_SECONDS;
    e.entry_timeout = NEGATIVE_TTL_SECONDS;
    fuse_reply_entry(req, &e);
    return;
  }
  samplePathStat(ctx, "lookup", childPath, false);

  fuse_ino_t childIno = 0;
  {
    std::shared_lock lock(ctx->inode_mutex);
    childIno = ctx->inodes->get(childPath);
  }
  if (childIno == 0) {
    std::unique_lock lock(ctx->inode_mutex);
    childIno = ctx->inodes->getOrCreate(childPath);
  }

  replyEntryFromSnapshot(req, ctx, childIno, snap);
}

void mo2_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* /*fi*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->getattr_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  {
    std::scoped_lock lock(ctx->attr_cache_mutex);
    auto it = ctx->attr_cache.find(ino);
    if (it != ctx->attr_cache.end() && it->second.valid &&
        std::chrono::steady_clock::now() < it->second.expires_at) {
      fuse_reply_attr(req, &it->second.st, TTL_SECONDS);
      return;
    }
  }

  if (ino == 1) {
    struct stat st;
    fillStatForDir(&st, 1, ctx->uid, ctx->gid);
    {
      std::scoped_lock lock(ctx->attr_cache_mutex);
      auto& c       = ctx->attr_cache[1];
      c.st          = st;
      c.expires_at  = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(
                         static_cast<int>(ATTR_CACHE_SECONDS * 1000.0));
      c.valid       = true;
    }
    fuse_reply_attr(req, &st, TTL_SECONDS);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  samplePathStat(ctx, "getattr", path);

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }

  {
    std::scoped_lock lock(ctx->attr_cache_mutex);
    auto& c      = ctx->attr_cache[ino];
    c.st         = st;
    c.expires_at = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(
                      static_cast<int>(ATTR_CACHE_SECONDS * 1000.0));
    c.valid      = true;
  }

  fuse_reply_attr(req, &st, TTL_SECONDS);
}

void mo2_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  bool listOk = false;
  auto entries = getOrBuildDirEntries(ctx, path, ino, &listOk);
  if (!listOk || !entries) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }
  samplePathStat(ctx, "readdir", path);

  const uint64_t dh = ctx->next_dh.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    Mo2FsContext::OpenDir od;
    od.path          = path;
    od.entries       = std::move(entries);
    od.readdir_blob  = getOrBuildReaddirBlob(ctx, req, path, od.entries);
    od.readdirplus_blob.reset();
    ctx->open_dirs[dh] = std::move(od);
  }

  fi->fh            = dh;
  fi->cache_readdir = 1;
  fuse_reply_open(req, fi);
}

void mo2_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->readdir_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  std::shared_ptr<std::vector<Mo2FsContext::DirEntry>> entries;
  std::shared_ptr<std::vector<char>> readdirBlob;
  std::string path;
  bool haveCached = false;
  if (fi != nullptr) {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    auto it = ctx->open_dirs.find(fi->fh);
    if (it != ctx->open_dirs.end()) {
      path        = it->second.path;
      entries     = it->second.entries;
      readdirBlob = it->second.readdir_blob;
      haveCached  = true;
    }
  }

  if (readdirBlob != nullptr) {
    samplePathStat(ctx, "readdir", path);
    const size_t start = static_cast<size_t>(off);
    if (start >= readdirBlob->size()) {
      fuse_reply_buf(req, nullptr, 0);
      return;
    }
    const size_t n = std::min<size_t>(size, readdirBlob->size() - start);
    fuse_reply_buf(req, readdirBlob->data() + start, n);
    return;
  }

  if (!haveCached) {
    bool ok = false;
    path = inodeToPath(ctx, ino, &ok);
    if (!ok) {
      fuse_reply_err(req, ENOENT);
      return;
    }

    bool listOk = false;
    entries = getOrBuildDirEntries(ctx, path, ino, &listOk);
    if (!listOk || !entries) {
      fuse_reply_err(req, ENOTDIR);
      return;
    }
  }
  samplePathStat(ctx, "readdir", path);

  std::vector<char> buf(size);
  size_t used = 0;

  for (size_t i = static_cast<size_t>(off); i < entries->size(); ++i) {
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    st.st_ino  = (*entries)[i].ino;
    st.st_mode = (*entries)[i].is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);

    const size_t ent = fuse_add_direntry(req, buf.data() + used, size - used,
                                         (*entries)[i].name.c_str(), &st,
                                         static_cast<off_t>(i + 1));
    if (ent > size - used) {
      break;
    }
    used += ent;
  }

  fuse_reply_buf(req, buf.data(), used);
}

void mo2_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->readdir_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  std::shared_ptr<std::vector<Mo2FsContext::DirEntry>> entries;
  std::shared_ptr<std::vector<char>> readdirPlusBlob;
  std::string path;
  bool haveCached = false;
  if (fi != nullptr) {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    auto it = ctx->open_dirs.find(fi->fh);
    if (it != ctx->open_dirs.end()) {
      path           = it->second.path;
      entries        = it->second.entries;
      readdirPlusBlob = it->second.readdirplus_blob;
      haveCached     = true;
    }
  }

  if (readdirPlusBlob == nullptr && haveCached && entries != nullptr) {
    auto built = getOrBuildReaddirPlusBlob(ctx, req, path, entries);
    std::scoped_lock lock(ctx->open_dirs_mutex);
    auto it = ctx->open_dirs.find(fi->fh);
    if (it != ctx->open_dirs.end()) {
      if (it->second.readdirplus_blob == nullptr) {
        it->second.readdirplus_blob = built;
      }
      readdirPlusBlob = it->second.readdirplus_blob;
    } else {
      readdirPlusBlob = std::move(built);
    }
  }

  if (readdirPlusBlob != nullptr) {
    samplePathStat(ctx, "readdir", path);
    const size_t start = static_cast<size_t>(off);
    if (start >= readdirPlusBlob->size()) {
      fuse_reply_buf(req, nullptr, 0);
      return;
    }
    const size_t n = std::min<size_t>(size, readdirPlusBlob->size() - start);
    fuse_reply_buf(req, readdirPlusBlob->data() + start, n);
    return;
  }

  if (!haveCached) {
    bool ok = false;
    path = inodeToPath(ctx, ino, &ok);
    if (!ok) {
      fuse_reply_err(req, ENOENT);
      return;
    }

    bool listOk = false;
    entries = getOrBuildDirEntries(ctx, path, ino, &listOk);
    if (!listOk || !entries) {
      fuse_reply_err(req, ENOTDIR);
      return;
    }
  }
  samplePathStat(ctx, "readdir", path);

  std::vector<char> buf(size);
  size_t used = 0;

  for (size_t i = static_cast<size_t>(off); i < entries->size(); ++i) {
    struct fuse_entry_param e;
    std::memset(&e, 0, sizeof(e));
    e.ino           = (*entries)[i].ino;
    e.attr_timeout  = TTL_SECONDS;
    e.entry_timeout = TTL_SECONDS;

    if ((*entries)[i].is_dir) {
      fillStatForDir(&e.attr, (*entries)[i].ino, ctx->uid, ctx->gid);
    } else {
      fillStatForFile(&e.attr, (*entries)[i].ino, ctx->uid, ctx->gid,
                      (*entries)[i].size, (*entries)[i].mtime);
    }

    const size_t ent = fuse_add_direntry_plus(
        req, buf.data() + used, size - used, (*entries)[i].name.c_str(), &e,
        static_cast<off_t>(i + 1));
    if (ent > size - used) {
      break;
    }
    used += ent;
  }

  fuse_reply_buf(req, buf.data(), used);
}

void mo2_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->open_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found || snap.is_directory) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  std::string realPath = snap.real_path;
  const bool writable  = isWritableOpen(fi->flags);
  bool isBacking       = snap.is_backing;

  if (writable) {
    try {
      if (isBacking && ctx->backing_dir_fd >= 0) {
        realPath = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
      } else {
        realPath = ctx->overwrite->copyOnWrite(realPath, path);
      }
      isBacking = false;
      updateFileNode(ctx, path, realPath, "Staging");
    } catch (...) {
      fuse_reply_err(req, EIO);
      return;
    }
  }

  int fd = -1;

  if (writable) {
    const int openFlags = O_RDWR;
    if (isBacking && ctx->backing_dir_fd >= 0) {
      fd = openat(ctx->backing_dir_fd, realPath.c_str(), openFlags);
    } else {
      fd = open(realPath.c_str(), openFlags);
    }
    if (fd < 0) {
      fuse_reply_err(req, EIO);
      return;
    }
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);
  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.fd           = fd;
    of.real_path     = realPath;
    of.writable      = writable;
    of.is_backing    = isBacking;
    of.relative_path = path;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh = fh;
  fi->keep_cache = 1;
  fuse_reply_open(req, fi);
}

void mo2_read(fuse_req_t req, fuse_ino_t /*ino*/, size_t size, off_t off,
              struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }
  ctx->read_count.fetch_add(1, std::memory_order_relaxed);
  maybeLogCounters(ctx);

  int fd = -1;
  std::string realPath;
  bool isBacking = false;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    fd       = it->second.fd;
    realPath = it->second.real_path;
    isBacking = it->second.is_backing;
  }

  int localFd = fd;
  bool openedTempFd = false;
  if (localFd < 0) {
    if (isBacking && ctx->backing_dir_fd >= 0) {
      localFd = openat(ctx->backing_dir_fd, realPath.c_str(), O_RDONLY);
    } else {
      localFd = open(realPath.c_str(), O_RDONLY);
    }
    if (localFd < 0) {
      fuse_reply_err(req, EIO);
      return;
    }
    openedTempFd = true;
  }

  std::vector<char> out(size);
  const ssize_t n = pread(localFd, out.data(), size, off);
  if (openedTempFd) {
    close(localFd);
  }

  if (n < 0) {
    fuse_reply_err(req, EIO);
    return;
  }

  fuse_reply_buf(req, out.data(), static_cast<size_t>(n));
}

void mo2_write(fuse_req_t req, fuse_ino_t /*ino*/, const char* buf, size_t size,
               off_t off, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || off < 0 || (buf == nullptr && size > 0)) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  int fd = -1;
  std::string relativePath;
  std::string realPath;
  bool writable = false;
  {
    std::shared_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it == ctx->open_files.end()) {
      fuse_reply_err(req, EBADF);
      return;
    }
    fd           = it->second.fd;
    writable     = it->second.writable;
    relativePath = it->second.relative_path;
    realPath     = it->second.real_path;
  }

  if (!writable) {
    fuse_reply_err(req, EACCES);
    return;
  }

  if (fd < 0) {
    fuse_reply_err(req, EBADF);
    return;
  }

  const ssize_t written = pwrite(fd, buf, size, off);
  if (written < 0) {
    fuse_reply_err(req, EIO);
    return;
  }

  updateFileNode(ctx, relativePath, realPath, "Staging");
  fuse_reply_write(req, static_cast<size_t>(written));
}

void mo2_create(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t /*mode*/,
                struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);

  std::string realPath;
  try {
    realPath = ctx->overwrite->writeFile(relative, {});
  } catch (...) {
    fuse_reply_err(req, EIO);
    return;
  }

  updateFileNode(ctx, relative, realPath, "Staging");
  invalidateDirCache(ctx, parentPath);
  {
    std::unique_lock lock(ctx->tree_mutex);
    ++ctx->tree->file_count;
  }

  fuse_ino_t newIno;
  {
    std::unique_lock lock(ctx->inode_mutex);
    newIno = ctx->inodes->getOrCreate(relative);
  }

  const auto snap = snapshotForPath(ctx, relative);
  if (!snap.found || snap.is_directory) {
    fuse_reply_err(req, EIO);
    return;
  }

  const uint64_t fh = ctx->next_fh.fetch_add(1, std::memory_order_relaxed);
  const int fd      = open(realPath.c_str(), O_RDWR);
  if (fd < 0) {
    fuse_reply_err(req, EIO);
    return;
  }

  {
    std::scoped_lock lock(ctx->open_files_mutex);
    Mo2FsContext::OpenFile of;
    of.fd           = fd;
    of.real_path     = realPath;
    of.writable      = true;
    of.is_backing    = false;
    of.relative_path = relative;
    ctx->open_files[fh] = std::move(of);
  }

  fi->fh         = fh;
  fi->keep_cache = 1;

  struct fuse_entry_param e;
  std::memset(&e, 0, sizeof(e));
  e.ino           = newIno;
  e.attr_timeout  = TTL_SECONDS;
  e.entry_timeout = TTL_SECONDS;
  fillStatForFile(&e.attr, newIno, ctx->uid, ctx->gid, snap.size, snap.mtime);

  fuse_reply_create(req, &e, fi);
}

void mo2_rename(fuse_req_t req, fuse_ino_t parent, const char* name,
                fuse_ino_t newparent, const char* newname, unsigned int flags)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr || newname == nullptr || flags != 0) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool okParent = false;
  bool okNewParent = false;
  const std::string parentPath = inodeToPath(ctx, parent, &okParent);
  const std::string newParentPath = inodeToPath(ctx, newparent, &okNewParent);
  if (!okParent || !okNewParent) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string oldRelative = joinPath(parentPath, name);
  const std::string newRelative = joinPath(newParentPath, newname);

  const auto oldSnap = snapshotForPath(ctx, oldRelative);
  if (!oldSnap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (!ctx->overwrite->rename(oldRelative, newRelative)) {
    fuse_reply_err(req, EACCES);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    ctx->tree->root.removeFromTree(splitPath(oldRelative));

    if (oldSnap.is_directory) {
      ctx->tree->root.insertDirectory(splitPath(newRelative));
    } else {
      const std::string staged = ctx->overwrite->stagingPath(newRelative);
      const std::string over   = ctx->overwrite->overwritePath(newRelative);
      const std::string real   = fs::exists(staged) ? staged : over;
      ctx->tree->root.insertFile(splitPath(newRelative), real, oldSnap.size,
                                 oldSnap.mtime, "Staging");
    }
  }

  {
    std::unique_lock lock(ctx->inode_mutex);
    ctx->inodes->rename(oldRelative, newRelative);
  }
  invalidateDirCache(ctx, parentPath);
  if (newParentPath != parentPath) {
    invalidateDirCache(ctx, newParentPath);
  }

  fuse_reply_err(req, 0);
}

void mo2_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set,
                 struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  if (ino == 1) {
    struct stat st;
    fillStatForDir(&st, 1, ctx->uid, ctx->gid);
    fuse_reply_attr(req, &st, TTL_SECONDS);
    return;
  }

  bool ok = false;
  const std::string path = inodeToPath(ctx, ino, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if ((to_set & FUSE_SET_ATTR_SIZE) != 0 && attr != nullptr) {
    std::string target;
    bool targetIsBacking = false;
    uint64_t fh = 0;

    if (fi != nullptr) {
      fh = fi->fh;
      std::shared_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end()) {
        target          = it->second.real_path;
        targetIsBacking = it->second.is_backing;
      }
    }

    if (target.empty()) {
      const auto snap = snapshotForPath(ctx, path);
      if (!snap.found || snap.is_directory) {
        fuse_reply_err(req, ENOENT);
        return;
      }
      target          = snap.real_path;
      targetIsBacking = snap.is_backing;
    }

    const std::string stagedPath = ctx->overwrite->stagingPath(path);
    if (fs::path(target).lexically_normal().string() !=
        fs::path(stagedPath).lexically_normal().string()) {
      try {
        if (targetIsBacking && ctx->backing_dir_fd >= 0) {
          target = ctx->overwrite->copyOnWriteFromFd(ctx->backing_dir_fd, path);
        } else {
          target = ctx->overwrite->copyOnWrite(target, path);
        }
      } catch (...) {
        fuse_reply_err(req, EIO);
        return;
      }

      if (fi != nullptr) {
        std::scoped_lock lock(ctx->open_files_mutex);
        auto it = ctx->open_files.find(fh);
        if (it != ctx->open_files.end()) {
          const int newFd = open(target.c_str(), O_RDWR);
          if (newFd < 0) {
            fuse_reply_err(req, EIO);
            return;
          }
          if (it->second.fd >= 0) {
            close(it->second.fd);
          }
          it->second.fd       = newFd;
          it->second.real_path = target;
          it->second.writable  = true;
          it->second.is_backing = false;
        }
      }
    }

    bool resized = false;
    if (fi != nullptr) {
      std::scoped_lock lock(ctx->open_files_mutex);
      auto it = ctx->open_files.find(fh);
      if (it != ctx->open_files.end() && it->second.fd >= 0) {
        if (ftruncate(it->second.fd, static_cast<off_t>(attr->st_size)) != 0) {
          fuse_reply_err(req, EIO);
          return;
        }
        resized = true;
      }
    }

    if (!resized) {
      std::error_code ec;
      fs::resize_file(target, static_cast<uint64_t>(attr->st_size), ec);
      if (ec) {
        fuse_reply_err(req, EIO);
        return;
      }
    }

    updateFileNode(ctx, path, target, "Staging");
  }

  // Handle explicit timestamp changes (utimensat / Wine SetFileTime)
  if ((to_set & (FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW |
                 FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_ATIME_NOW)) != 0 &&
      attr != nullptr) {
    const auto snap = snapshotForPath(ctx, path);
    if (snap.found && !snap.is_directory) {
      // Apply the timestamp to the real file on disk
      struct timespec times[2];
      // atime
      if (to_set & FUSE_SET_ATTR_ATIME_NOW) {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_NOW;
      } else if (to_set & FUSE_SET_ATTR_ATIME) {
        times[0] = attr->st_atim;
      } else {
        times[0].tv_sec  = 0;
        times[0].tv_nsec = UTIME_OMIT;
      }
      // mtime
      if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_NOW;
      } else if (to_set & FUSE_SET_ATTR_MTIME) {
        times[1] = attr->st_mtim;
      } else {
        times[1].tv_sec  = 0;
        times[1].tv_nsec = UTIME_OMIT;
      }

      // Only write timestamps to disk for files we own (staging/overwrite).
      // For mod and base game files, just update the VFS tree in-memory.
      if (!snap.is_backing) {
        utimensat(AT_FDCWD, snap.real_path.c_str(), times, 0);
      }

      // Update the VFS tree mtime without changing origin or triggering COW.
      if (to_set & (FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW)) {
        std::chrono::system_clock::time_point newMtime;
        if (to_set & FUSE_SET_ATTR_MTIME_NOW) {
          newMtime = std::chrono::system_clock::now();
        } else {
          newMtime = std::chrono::system_clock::time_point(
              std::chrono::seconds(attr->st_mtim.tv_sec));
        }
        std::unique_lock lock(ctx->tree_mutex);
        auto components = splitPath(path);
        VfsNode* cur    = &ctx->tree->root;
        for (const auto& part : components) {
          if (!cur->is_directory) {
            cur = nullptr;
            break;
          }
          auto it = cur->dir_info.children.find(normalizeForLookup(part));
          if (it == cur->dir_info.children.end()) {
            cur = nullptr;
            break;
          }
          cur = it->second.get();
        }
        if (cur != nullptr && !cur->is_directory) {
          cur->file_info.mtime = newMtime;
        }
      }
    }
  }

  const auto snap = snapshotForPath(ctx, path);
  if (!snap.found) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat st;
  if (snap.is_directory) {
    fillStatForDir(&st, ino, ctx->uid, ctx->gid);
  } else {
    fillStatForFile(&st, ino, ctx->uid, ctx->gid, snap.size, snap.mtime);
  }
  fuse_reply_attr(req, &st, TTL_SECONDS);
}

void mo2_unlink(fuse_req_t req, fuse_ino_t parent, const char* name)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);
  if (!ctx->overwrite->removeFile(relative)) {
    fuse_reply_err(req, EACCES);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    if (ctx->tree->root.removeFromTree(splitPath(relative))) {
      ctx->tree->file_count = ctx->tree->file_count > 0 ? ctx->tree->file_count - 1 : 0;
    }
  }
  invalidateDirCache(ctx, parentPath);

  fuse_reply_err(req, 0);
}

void mo2_mkdir(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t /*mode*/)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || name == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  bool ok = false;
  const std::string parentPath = inodeToPath(ctx, parent, &ok);
  if (!ok) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  const std::string relative = joinPath(parentPath, name);
  if (!ctx->overwrite->createDirectory(relative)) {
    fuse_reply_err(req, EIO);
    return;
  }

  {
    std::unique_lock lock(ctx->tree_mutex);
    ctx->tree->root.insertDirectory(splitPath(relative));
    ++ctx->tree->dir_count;
  }
  invalidateDirCache(ctx, parentPath);

  fuse_ino_t dirIno;
  {
    std::unique_lock lock(ctx->inode_mutex);
    dirIno = ctx->inodes->getOrCreate(relative);
  }

  const auto snap = snapshotForPath(ctx, relative);
  if (!snap.found) {
    fuse_reply_err(req, EIO);
    return;
  }

  replyEntryFromSnapshot(req, ctx, dirIno, snap);
}

void mo2_release(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  {
    std::scoped_lock lock(ctx->open_files_mutex);
    auto it = ctx->open_files.find(fi->fh);
    if (it != ctx->open_files.end()) {
      if (it->second.fd >= 0) {
        close(it->second.fd);
      }
      ctx->open_files.erase(it);
    }
  }

  fuse_reply_err(req, 0);
}

void mo2_releasedir(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* fi)
{
  Mo2FsContext* ctx = getContext(req);
  if (ctx == nullptr || fi == nullptr) {
    fuse_reply_err(req, EINVAL);
    return;
  }

  {
    std::scoped_lock lock(ctx->open_dirs_mutex);
    ctx->open_dirs.erase(fi->fh);
  }

  fuse_reply_err(req, 0);
}

#if FUSE_USE_VERSION < 35
void mo2_ioctl(fuse_req_t req, fuse_ino_t /*ino*/, int cmd, void* /*arg*/,
               struct fuse_file_info* /*fi*/, unsigned /*flags*/,
               const void* /*in_buf*/, size_t /*in_bufsz*/, size_t out_bufsz)
#else
void mo2_ioctl(fuse_req_t req, fuse_ino_t /*ino*/, unsigned int cmd, void* /*arg*/,
               struct fuse_file_info* /*fi*/, unsigned /*flags*/,
               const void* /*in_buf*/, size_t /*in_bufsz*/, size_t out_bufsz)
#endif
{
  static std::atomic<uint64_t> ioctlSeen{0};
  static std::atomic<unsigned int> lastCmd{0};

  Mo2FsContext* ctx = getContext(req);
  if (ctx != nullptr) {
    ctx->ioctl_count.fetch_add(1, std::memory_order_relaxed);
    maybeLogCounters(ctx);
  }

  const unsigned int ucmd = static_cast<unsigned int>(cmd);
  lastCmd.store(ucmd, std::memory_order_relaxed);
  const uint64_t seen = ioctlSeen.fetch_add(1, std::memory_order_relaxed) + 1;
  if ((seen % 500000) == 0) {
    std::fprintf(stderr, "[VFS] ioctl hotloop cmd=0x%x out=%zu\n",
                 lastCmd.load(std::memory_order_relaxed), out_bufsz);
  }

#ifdef VFAT_IOCTL_READDIR_BOTH
  if (ucmd == static_cast<unsigned int>(VFAT_IOCTL_READDIR_BOTH)) {
    // Force Wine fallback path for vfat-specific ioctl probes.
    fuse_reply_err(req, ENOTTY);
    return;
  }
#endif

  if (ucmd == static_cast<unsigned int>(FS_IOC_GETFLAGS)) {
    // Some callers probe this heavily; ENOTTY avoids fake-success loops.
    fuse_reply_err(req, ENOTTY);
    return;
  }

  fuse_reply_err(req, ENOTTY);
}
