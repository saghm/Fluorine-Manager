#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct Entry
{
  char* key;
  char* real_path;
  int directory;
  int backing;
  uint64_t size;
  int64_t mtime_ns;
  uint32_t child_start;
  uint32_t child_count;
} Entry;

static Entry* g_entries = NULL;
static size_t g_entry_count = 0;
static size_t g_entry_cap = 0;
static uint32_t* g_children = NULL;
static size_t g_children_count = 0;
static char* g_mount_point = NULL;
static char* g_data_dir = NULL;
static int g_loaded = 0;
static int g_load_failed = 0;
static int g_debug = 0;
static int g_stats = 0;
// Directory synthesis (libc opendir + open(O_DIRECTORY) → synthetic FlDir fd).
// Default ON. Set FLUORINE_VFS_PRELOAD_DIRS=0 to disable when debugging Wine
// readdir issues (currently NTDLL bypasses our syscall() hook for getdents64
// so synthetic dirs read empty there).
static int g_dirs_enabled = 1;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static __thread int g_inside = 0;
static uint64_t g_resolve_hits = 0;
static uint64_t g_resolve_misses = 0;
static uint64_t g_open_hits = 0;
static uint64_t g_open_fallbacks = 0;
static uint64_t g_stat_hits = 0;
static uint64_t g_access_hits = 0;
static uint64_t g_open_calls = 0;
static uint64_t g_stat_calls = 0;
static uint64_t g_access_calls = 0;
static uint64_t g_statx_calls = 0;
static uint64_t g_syscall_calls = 0;
static uint64_t g_path_mismatches = 0;
static uint64_t g_path_samples = 0;
static uint64_t g_canon_attempts = 0;
static uint64_t g_canon_mount_hits = 0;
static uint64_t g_canon_outside = 0;
static uint64_t g_canon_failed = 0;
static uint64_t g_opendir_calls = 0;
static uint64_t g_opendir_hits = 0;
static uint64_t g_readdir_calls = 0;
static uint64_t g_readdir_hits = 0;
static uint64_t g_getdents_calls = 0;
static uint64_t g_getdents_hits = 0;
// libc getdents64() symbol path. Wine 9+ / glibc 2.30+ may call this directly
// instead of going through syscall(SYS_getdents64, ...). Keeping the counters
// separate so we can tell which path NTDLL actually uses.
static uint64_t g_getdents_libc_calls = 0;
static uint64_t g_getdents_libc_hits = 0;

#define FL_DIR_MAGIC ((uint64_t)0xF10DD1ECDEADC0DEULL)

typedef struct FlDir
{
  uint64_t magic;       // first field: type discrimination
  size_t pos;
  size_t entry_idx;     // directory entry in g_entries
  int real_fd;          // backing /dev/null fd or -1
  struct dirent dirent_buf;
  struct dirent64 dirent64_buf;
} FlDir;

// fd → FlDir* map for getdents64 syscall hook. Open-addressed linear-probe
// hash table. Sized at first insert; bounded by /proc/sys/fs/file-max but in
// practice this stays under ~50 entries per process during a launch.
typedef struct FdSlot
{
  int fd;       // -1 = empty
  FlDir* dir;
} FdSlot;

static FdSlot* g_fd_map = NULL;
static size_t g_fd_map_cap = 0;
static size_t g_fd_map_count = 0;
static pthread_mutex_t g_fd_map_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ensure_loaded(void);

static void* next_symbol(const char* name)
{
  return dlsym(RTLD_NEXT, name);
}

static int flag_enabled(const char* name)
{
  const char* v = getenv(name);
  if (v == NULL) return 0;
  return strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 ||
         strcasecmp(v, "yes") == 0;
}

static char* xstrdup_range(const char* start, size_t len)
{
  char* out = (char*)malloc(len + 1);
  if (out == NULL) return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static char* trim_trailing_slash_dup(const char* s)
{
  if (s == NULL) return NULL;
  size_t len = strlen(s);
  while (len > 1 && s[len - 1] == '/') --len;
  return xstrdup_range(s, len);
}

static char* normalized_slashes_dup(const char* s)
{
  char* out = trim_trailing_slash_dup(s);
  if (out == NULL) return NULL;
  for (char* p = out; *p; ++p) {
    if (*p == '\\') *p = '/';
  }
  while (out[0] == '/' && out[1] == '/') {
    memmove(out, out + 1, strlen(out));
  }
  return out;
}

static char* wine_dos_path_to_unix_dup(const char* s)
{
  if (s == NULL) return NULL;
  const char* p = s;
  if (strncmp(p, "\\\\??\\", 5) == 0) p += 5;
  if (strncmp(p, "\\??\\", 4) == 0) p += 4;
  if (strncmp(p, "\\\\?\\", 4) == 0) p += 4;

  if (isalpha((unsigned char)p[0]) && p[1] == ':') {
    const char drive = (char)tolower((unsigned char)p[0]);
    p += 2;
    while (*p == '\\' || *p == '/') ++p;

    if (drive == 'z') {
      const size_t len = strlen(p) + 2;
      char* out = (char*)malloc(len);
      if (out == NULL) return NULL;
      out[0] = '/';
      strcpy(out + 1, p);
      for (char* c = out; *c; ++c) {
        if (*c == '\\') *c = '/';
      }
      while (strlen(out) > 1 && out[strlen(out) - 1] == '/') {
        out[strlen(out) - 1] = '\0';
      }
      return out;
    }
  }

  return NULL;
}

static void lowercase_vfs_path(char* s)
{
  char* r = s;
  char* w = s;
  while (*r == '/') ++r;
  while (*r) {
    char c = *r++;
    if (c == '\\') c = '/';
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    *w++ = c;
  }
  while (w > s && w[-1] == '/') --w;
  *w = '\0';
}

static char* json_string_field(const char* line, const char* key)
{
  char needle[128];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = strstr(line, needle);
  if (p == NULL) return NULL;
  p = strchr(p + strlen(needle), '"');
  if (p == NULL) return NULL;
  ++p;

  size_t cap = strlen(p) + 1;
  char* out = (char*)malloc(cap);
  if (out == NULL) return NULL;
  size_t n = 0;

  while (*p) {
    char c = *p++;
    if (c == '"') {
      out[n] = '\0';
      return out;
    }
    if (c == '\\' && *p) {
      char e = *p++;
      switch (e) {
      case '"': c = '"'; break;
      case '\\': c = '\\'; break;
      case '/': c = '/'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case 't': c = '\t'; break;
      default: c = e; break;
      }
    }
    out[n++] = c;
  }

  free(out);
  return NULL;
}

static int json_int_field(const char* line, const char* key, int64_t* out)
{
  char needle[128];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = strstr(line, needle);
  if (p == NULL) return 0;
  p += strlen(needle);
  char* end = NULL;
  long long v = strtoll(p, &end, 10);
  if (end == p) return 0;
  *out = (int64_t)v;
  return 1;
}

static int json_bool_field(const char* line, const char* key)
{
  char needle[128];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = strstr(line, needle);
  if (p == NULL) return 0;
  p += strlen(needle);
  return strncmp(p, "true", 4) == 0;
}

static int add_entry(Entry e)
{
  if (g_entry_count == g_entry_cap) {
    size_t next_cap = g_entry_cap == 0 ? 4096 : g_entry_cap * 2;
    Entry* next = (Entry*)realloc(g_entries, next_cap * sizeof(Entry));
    if (next == NULL) return 0;
    g_entries = next;
    g_entry_cap = next_cap;
  }
  g_entries[g_entry_count++] = e;
  return 1;
}

static int entry_cmp(const void* a, const void* b)
{
  const Entry* ea = (const Entry*)a;
  const Entry* eb = (const Entry*)b;
  return strcmp(ea->key, eb->key);
}

static Entry* find_entry(const char* key)
{
  Entry probe;
  probe.key = (char*)key;
  return (Entry*)bsearch(&probe, g_entries, g_entry_count, sizeof(Entry), entry_cmp);
}

// Returns index in g_entries of the parent of `key`, or SIZE_MAX if none.
// Only valid after qsort. `key` must not contain trailing slash.
static size_t parent_index(const char* key)
{
  if (key == NULL || key[0] == '\0') return SIZE_MAX;
  const char* slash = strrchr(key, '/');
  if (slash == NULL) {
    // Top-level key: parent is root entry "".
    Entry probe;
    probe.key = (char*)"";
    Entry* p = (Entry*)bsearch(&probe, g_entries, g_entry_count, sizeof(Entry), entry_cmp);
    if (p == NULL) return SIZE_MAX;
    return (size_t)(p - g_entries);
  }
  size_t parent_len = (size_t)(slash - key);
  char* parent_key = (char*)malloc(parent_len + 1);
  if (parent_key == NULL) return SIZE_MAX;
  memcpy(parent_key, key, parent_len);
  parent_key[parent_len] = '\0';

  Entry probe;
  probe.key = parent_key;
  Entry* p = (Entry*)bsearch(&probe, g_entries, g_entry_count, sizeof(Entry), entry_cmp);
  free(parent_key);
  if (p == NULL) return SIZE_MAX;
  return (size_t)(p - g_entries);
}

static void build_child_index(void)
{
  if (g_entry_count == 0) return;

  // Pass 1: count children per parent.
  for (size_t i = 0; i < g_entry_count; ++i) {
    size_t pi = parent_index(g_entries[i].key);
    if (pi == SIZE_MAX) continue;
    ++g_entries[pi].child_count;
  }

  // Pass 2: prefix-sum to compute child_start.
  size_t total = 0;
  for (size_t i = 0; i < g_entry_count; ++i) {
    g_entries[i].child_start = (uint32_t)total;
    total += g_entries[i].child_count;
    g_entries[i].child_count = 0;  // refilled in pass 3
  }

  if (total == 0) return;
  g_children = (uint32_t*)malloc(total * sizeof(uint32_t));
  if (g_children == NULL) {
    g_load_failed = 1;
    return;
  }
  g_children_count = total;

  // Pass 3: append child indices.
  for (size_t i = 0; i < g_entry_count; ++i) {
    size_t pi = parent_index(g_entries[i].key);
    if (pi == SIZE_MAX) continue;
    Entry* parent = &g_entries[pi];
    g_children[parent->child_start + parent->child_count++] = (uint32_t)i;
  }
}

static void load_index(void)
{
  g_inside = 1;
  g_debug = flag_enabled("FLUORINE_VFS_BRIDGE_DEBUG");
  g_stats = flag_enabled("FLUORINE_VFS_PRELOAD_STATS");
  // Default-on: disable only when env explicitly says 0/false/no.
  const char* dirs_env = getenv("FLUORINE_VFS_PRELOAD_DIRS");
  if (dirs_env != NULL &&
      (strcmp(dirs_env, "0") == 0 || strcasecmp(dirs_env, "false") == 0 ||
       strcasecmp(dirs_env, "no") == 0)) {
    g_dirs_enabled = 0;
  }

  const char* index = getenv("FLUORINE_VFS_INDEX");
  if (index == NULL || index[0] == '\0') {
    g_inside = 0;
    return;
  }

  FILE* f = fopen(index, "r");
  if (f == NULL) {
    g_load_failed = 1;
    g_inside = 0;
    return;
  }

  char* line = NULL;
  size_t line_cap = 0;
  while (getline(&line, &line_cap, f) > 0) {
    char* record = json_string_field(line, "record");
    if (record == NULL) continue;

    if (strcmp(record, "meta") == 0) {
      char* mount = json_string_field(line, "mount_point");
      char* data = json_string_field(line, "data_dir");
      free(g_mount_point);
      free(g_data_dir);
      g_mount_point = trim_trailing_slash_dup(mount);
      g_data_dir = trim_trailing_slash_dup(data);
      free(mount);
      free(data);
      free(record);
      continue;
    }

    if (strcmp(record, "entry") != 0) {
      free(record);
      continue;
    }
    free(record);

    char* type = json_string_field(line, "type");
    char* key = json_string_field(line, "virtual_path");
    if (type == NULL || key == NULL) {
      free(type);
      free(key);
      continue;
    }
    lowercase_vfs_path(key);

    Entry e;
    memset(&e, 0, sizeof(e));
    e.key = key;
    e.directory = strcmp(type, "directory") == 0;
    free(type);

    if (!e.directory) {
      int64_t size = 0;
      e.real_path = json_string_field(line, "real_path");
      e.backing = json_bool_field(line, "is_backing");
      if (json_int_field(line, "size", &size) && size >= 0) {
        e.size = (uint64_t)size;
      }
      json_int_field(line, "mtime_ns", &e.mtime_ns);
    }

    if (!add_entry(e)) {
      free(e.key);
      free(e.real_path);
      g_load_failed = 1;
      break;
    }
  }

  free(line);
  fclose(f);

  if (g_mount_point != NULL) {
    Entry root;
    memset(&root, 0, sizeof(root));
    root.key = strdup("");
    root.directory = 1;
    if (root.key != NULL) add_entry(root);
  }

  qsort(g_entries, g_entry_count, sizeof(Entry), entry_cmp);
  build_child_index();
  g_loaded = g_mount_point != NULL && !g_load_failed;
  if (g_debug) {
    fprintf(stderr,
            "[fluorine-vfs-preload] pid=%ld loaded=%d entries=%zu children=%zu index='%s' mount='%s'\n",
            (long)getpid(), g_loaded, g_entry_count, g_children_count, index,
            g_mount_point ? g_mount_point : "");
  }
  g_inside = 0;
}

static void ensure_loaded(void)
{
  pthread_once(&g_once, load_index);
}

static void sample_path_mismatch(const char* reason, int dirfd, const char* raw,
                                 const char* clean, const char* canon)
{
  __sync_fetch_and_add(&g_path_mismatches, 1);
  if (!g_stats && !g_debug) return;
  const uint64_t slot = __sync_fetch_and_add(&g_path_samples, 1);
  if (slot >= 24) return;

  fprintf(stderr,
          "[fluorine-vfs-preload] path_miss pid=%ld reason=%s dirfd=%d raw='%s' clean='%s' canon='%s' mount='%s'\n",
          (long)getpid(), reason ? reason : "", dirfd,
          raw ? raw : "", clean ? clean : "", canon ? canon : "",
          g_mount_point ? g_mount_point : "");
}

// realpath() existing prefix; preserve trailing suffix that does not yet exist.
// Returns malloc'd canonical path or NULL when no parent component resolves.
static char* canonicalize_existing_prefix_dup(const char* path)
{
  if (path == NULL || path[0] != '/') return NULL;

  char* canon = realpath(path, NULL);
  if (canon != NULL) return canon;

  size_t len = strlen(path);
  char* work = (char*)malloc(len + 1);
  if (work == NULL) return NULL;
  memcpy(work, path, len + 1);

  for (;;) {
    char* slash = strrchr(work, '/');
    if (slash == NULL || slash == work) {
      free(work);
      return NULL;
    }
    size_t suffix_off = (size_t)(slash - work); // includes leading '/'
    *slash = '\0';

    canon = realpath(work, NULL);
    if (canon != NULL) {
      size_t parent_len = strlen(canon);
      size_t suffix_len = len - suffix_off;
      char* out = (char*)malloc(parent_len + suffix_len + 1);
      if (out != NULL) {
        memcpy(out, canon, parent_len);
        memcpy(out + parent_len, path + suffix_off, suffix_len);
        out[parent_len + suffix_len] = '\0';
      }
      free(canon);
      free(work);
      return out;
    }
    // continue walking up
  }
}

static int starts_with_mount(const char* abs)
{
  if (abs == NULL || g_mount_point == NULL) return 0;
  size_t mp_len = strlen(g_mount_point);
  if (strncmp(abs, g_mount_point, mp_len) != 0) return 0;
  return abs[mp_len] == '\0' || abs[mp_len] == '/';
}

// Skip canonicalization for paths that obviously cannot reach the mount, and
// avoid burning a realpath() syscall per stat for system libs / Wine internals.
static int looks_like_system_path(const char* abs)
{
  if (abs == NULL) return 1;
  static const char* const skip[] = {
      "/proc/", "/sys/", "/dev/", "/run/", "/tmp/", "/usr/",
      "/lib/",  "/lib64/", "/etc/", "/var/", "/opt/", "/bin/",
      "/sbin/", "/boot/",
  };
  for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); ++i) {
    size_t n = strlen(skip[i]);
    if (strncmp(abs, skip[i], n) == 0) return 1;
  }
  return 0;
}

static char* absolute_path_from_dirfd(int dirfd, const char* path)
{
  if (path == NULL) return NULL;

  char* wine = wine_dos_path_to_unix_dup(path);
  if (wine != NULL) return wine;

  if (path[0] == '/' || path[0] == '\\') return normalized_slashes_dup(path);

  char base[4096];
  if (dirfd == AT_FDCWD) {
    if (getcwd(base, sizeof(base)) == NULL) return NULL;
  } else {
    char proc[64];
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", dirfd);
    ssize_t n = readlink(proc, base, sizeof(base) - 1);
    if (n < 0) return NULL;
    base[n] = '\0';
  }

  size_t len = strlen(base) + 1 + strlen(path) + 1;
  char* out = (char*)malloc(len);
  if (out == NULL) return NULL;
  snprintf(out, len, "%s/%s", base, path);
  char* trimmed = normalized_slashes_dup(out);
  free(out);
  return trimmed;
}

static int rel_from_mount_at(int dirfd, const char* path, char** rel)
{
  if (path == NULL) return 0;
  ensure_loaded();
  if (!g_loaded || g_mount_point == NULL) return 0;

  char* clean = absolute_path_from_dirfd(dirfd, path);
  if (clean == NULL) {
    sample_path_mismatch("normalize-failed", dirfd, path, NULL, NULL);
    return 0;
  }

  // Fast path: clean already under mount.
  const char* abs = NULL;
  char* canon = NULL;

  if (starts_with_mount(clean)) {
    abs = clean;
  } else if (!looks_like_system_path(clean)) {
    __sync_fetch_and_add(&g_canon_attempts, 1);
    canon = canonicalize_existing_prefix_dup(clean);
    if (canon == NULL) {
      __sync_fetch_and_add(&g_canon_failed, 1);
      sample_path_mismatch("canon-failed", dirfd, path, clean, NULL);
      free(clean);
      return 0;
    }
    if (starts_with_mount(canon)) {
      __sync_fetch_and_add(&g_canon_mount_hits, 1);
      abs = canon;
    } else {
      __sync_fetch_and_add(&g_canon_outside, 1);
      sample_path_mismatch("outside-mount", dirfd, path, clean, canon);
      free(clean);
      free(canon);
      return 0;
    }
  } else {
    // System paths flood every process; count, do not sample.
    __sync_fetch_and_add(&g_path_mismatches, 1);
    free(clean);
    return 0;
  }

  size_t mp_len = strlen(g_mount_point);
  if (abs[mp_len] == '\0') {
    free(clean);
    free(canon);
    *rel = strdup("");
    return *rel != NULL;
  }

  char* out = strdup(abs + mp_len + 1);
  free(clean);
  free(canon);
  if (out == NULL) return 0;
  lowercase_vfs_path(out);
  *rel = out;
  return 1;
}

static Entry* resolve_entry_at(int dirfd, const char* path)
{
  char* rel = NULL;
  if (!rel_from_mount_at(dirfd, path, &rel)) return NULL;
  Entry* e = find_entry(rel);
  free(rel);
  if (e == NULL) {
    __sync_fetch_and_add(&g_resolve_misses, 1);
    return NULL;
  }
  __sync_fetch_and_add(&g_resolve_hits, 1);
  return e;
}

static Entry* resolve_entry(const char* path)
{
  return resolve_entry_at(AT_FDCWD, path);
}

static char* resolved_real_path(const Entry* e)
{
  if (e == NULL || e->directory || e->real_path == NULL) return NULL;
  if (!e->backing || g_data_dir == NULL) return strdup(e->real_path);

  size_t len = strlen(g_data_dir) + 1 + strlen(e->real_path) + 1;
  char* out = (char*)malloc(len);
  if (out == NULL) return NULL;
  snprintf(out, len, "%s/%s", g_data_dir, e->real_path);
  return out;
}

static int read_only_flags(int flags)
{
  return (flags & O_ACCMODE) == O_RDONLY &&
         (flags & (O_CREAT | O_TRUNC | O_APPEND)) == 0;
}

static void fill_synthetic_stat(struct stat* st, const Entry* e)
{
  memset(st, 0, sizeof(*st));
  st->st_uid = getuid();
  st->st_gid = getgid();
  st->st_nlink = e->directory ? 2 : 1;
  st->st_mode = e->directory ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  st->st_size = (off_t)e->size;
  st->st_mtim.tv_sec = e->mtime_ns / 1000000000LL;
  st->st_mtim.tv_nsec = e->mtime_ns % 1000000000LL;
  st->st_atim = st->st_mtim;
  st->st_ctim = st->st_mtim;
}

static void fill_synthetic_statx(struct statx* stx, const Entry* e)
{
  memset(stx, 0, sizeof(*stx));
  stx->stx_mask = STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID |
                  STATX_GID | STATX_SIZE | STATX_MTIME | STATX_ATIME |
                  STATX_CTIME;
  stx->stx_uid = getuid();
  stx->stx_gid = getgid();
  stx->stx_nlink = e->directory ? 2 : 1;
  stx->stx_mode = e->directory ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  stx->stx_size = e->size;
  stx->stx_mtime.tv_sec = e->mtime_ns / 1000000000LL;
  stx->stx_mtime.tv_nsec = e->mtime_ns % 1000000000LL;
  stx->stx_atime = stx->stx_mtime;
  stx->stx_ctime = stx->stx_mtime;
}

// ---- Directory listing path ---------------------------------------------

// Last component of a virtual key. "a/b/c" -> "c"; root key "" -> "".
static const char* basename_of(const char* key)
{
  const char* slash = strrchr(key, '/');
  return slash != NULL ? slash + 1 : key;
}

static FlDir* fl_dir_alloc(size_t entry_idx)
{
  FlDir* dir = (FlDir*)calloc(1, sizeof(FlDir));
  if (dir == NULL) return NULL;
  dir->magic = FL_DIR_MAGIC;
  dir->entry_idx = entry_idx;
  dir->real_fd = -1;
  return dir;
}

static int is_fl_dir(const void* p)
{
  if (p == NULL) return 0;
  uint64_t magic;
  memcpy(&magic, p, sizeof(magic));
  return magic == FL_DIR_MAGIC;
}

static void fd_map_register(int fd, FlDir* dir)
{
  if (fd < 0 || dir == NULL) return;
  pthread_mutex_lock(&g_fd_map_mutex);
  if (g_fd_map_count + 1 >= (g_fd_map_cap >> 1)) {
    size_t new_cap = g_fd_map_cap == 0 ? 32 : g_fd_map_cap * 2;
    FdSlot* new_map = (FdSlot*)calloc(new_cap, sizeof(FdSlot));
    if (new_map == NULL) {
      pthread_mutex_unlock(&g_fd_map_mutex);
      return;
    }
    for (size_t i = 0; i < new_cap; ++i) new_map[i].fd = -1;
    for (size_t i = 0; i < g_fd_map_cap; ++i) {
      if (g_fd_map[i].fd < 0) continue;
      size_t h = ((size_t)g_fd_map[i].fd * 2654435761u) & (new_cap - 1);
      while (new_map[h].fd >= 0) h = (h + 1) & (new_cap - 1);
      new_map[h] = g_fd_map[i];
    }
    free(g_fd_map);
    g_fd_map = new_map;
    g_fd_map_cap = new_cap;
  }
  size_t h = ((size_t)fd * 2654435761u) & (g_fd_map_cap - 1);
  while (g_fd_map[h].fd >= 0 && g_fd_map[h].fd != fd) {
    h = (h + 1) & (g_fd_map_cap - 1);
  }
  if (g_fd_map[h].fd < 0) ++g_fd_map_count;
  g_fd_map[h].fd = fd;
  g_fd_map[h].dir = dir;
  pthread_mutex_unlock(&g_fd_map_mutex);
}

static FlDir* fd_map_lookup(int fd)
{
  if (fd < 0 || g_fd_map_cap == 0) return NULL;
  pthread_mutex_lock(&g_fd_map_mutex);
  size_t h = ((size_t)fd * 2654435761u) & (g_fd_map_cap - 1);
  FlDir* found = NULL;
  for (size_t probes = 0; probes < g_fd_map_cap; ++probes) {
    if (g_fd_map[h].fd < 0) break;
    if (g_fd_map[h].fd == fd) { found = g_fd_map[h].dir; break; }
    h = (h + 1) & (g_fd_map_cap - 1);
  }
  pthread_mutex_unlock(&g_fd_map_mutex);
  return found;
}

static FlDir* fd_map_take(int fd)
{
  if (fd < 0 || g_fd_map_cap == 0) return NULL;
  pthread_mutex_lock(&g_fd_map_mutex);
  size_t h = ((size_t)fd * 2654435761u) & (g_fd_map_cap - 1);
  FlDir* found = NULL;
  for (size_t probes = 0; probes < g_fd_map_cap; ++probes) {
    if (g_fd_map[h].fd < 0) break;
    if (g_fd_map[h].fd == fd) {
      found = g_fd_map[h].dir;
      g_fd_map[h].fd = -1;
      g_fd_map[h].dir = NULL;
      --g_fd_map_count;
      // Re-insert any clustered entries to repair probe chain.
      size_t j = (h + 1) & (g_fd_map_cap - 1);
      while (g_fd_map[j].fd >= 0) {
        FdSlot moved = g_fd_map[j];
        g_fd_map[j].fd = -1;
        g_fd_map[j].dir = NULL;
        --g_fd_map_count;
        size_t k = ((size_t)moved.fd * 2654435761u) & (g_fd_map_cap - 1);
        while (g_fd_map[k].fd >= 0) k = (k + 1) & (g_fd_map_cap - 1);
        g_fd_map[k] = moved;
        ++g_fd_map_count;
        j = (j + 1) & (g_fd_map_cap - 1);
      }
      break;
    }
    h = (h + 1) & (g_fd_map_cap - 1);
  }
  pthread_mutex_unlock(&g_fd_map_mutex);
  return found;
}

static FlDir* try_open_synthetic_dir(const char* path)
{
  ensure_loaded();
  if (!g_loaded) return NULL;
  Entry* e = resolve_entry(path);
  if (e == NULL || !e->directory) return NULL;
  size_t idx = (size_t)(e - g_entries);
  return fl_dir_alloc(idx);
}

static FlDir* try_open_synthetic_dir_at(int dirfd, const char* path)
{
  ensure_loaded();
  if (!g_loaded) return NULL;
  Entry* e = resolve_entry_at(dirfd, path);
  if (e == NULL || !e->directory) return NULL;
  size_t idx = (size_t)(e - g_entries);
  return fl_dir_alloc(idx);
}

// /dev/null fd to back DIR* when callers ask for dirfd().
static int open_devnull_fd(void)
{
  int (*real_open)(const char*, int, ...) =
      (int (*)(const char*, int, ...))next_symbol("open");
  if (real_open == NULL) return -1;
  g_inside = 1;
  int fd = real_open("/dev/null", O_RDONLY | O_CLOEXEC, 0);
  g_inside = 0;
  return fd;
}

static unsigned char dirent_type_for(const Entry* child)
{
  return child->directory ? DT_DIR : DT_REG;
}

// Returns 1 if a record was filled, 0 when we're past the end.
static int fl_dir_next_dirent(FlDir* dir, struct dirent* out)
{
  if (dir == NULL) return 0;
  Entry* parent = &g_entries[dir->entry_idx];
  size_t total = (size_t)parent->child_count;
  // Synthetic '.' at pos 0, '..' at pos 1, then children.
  while (dir->pos < 2 + total) {
    size_t p = dir->pos++;
    memset(out, 0, sizeof(*out));
    out->d_off = (off_t)dir->pos;
    out->d_reclen = sizeof(struct dirent);
    if (p == 0) {
      out->d_ino = 1;
      out->d_type = DT_DIR;
      strcpy(out->d_name, ".");
      return 1;
    }
    if (p == 1) {
      out->d_ino = 1;
      out->d_type = DT_DIR;
      strcpy(out->d_name, "..");
      return 1;
    }
    uint32_t cidx = g_children[parent->child_start + (p - 2)];
    Entry* child = &g_entries[cidx];
    const char* name = basename_of(child->key);
    size_t nlen = strlen(name);
    if (nlen >= sizeof(out->d_name)) continue;  // skip oversize
    out->d_ino = (ino_t)(cidx + 2);
    out->d_type = dirent_type_for(child);
    memcpy(out->d_name, name, nlen + 1);
    return 1;
  }
  return 0;
}

static int fl_dir_next_dirent64(FlDir* dir, struct dirent64* out)
{
  if (dir == NULL) return 0;
  Entry* parent = &g_entries[dir->entry_idx];
  size_t total = (size_t)parent->child_count;
  while (dir->pos < 2 + total) {
    size_t p = dir->pos++;
    memset(out, 0, sizeof(*out));
    out->d_off = (off_t)dir->pos;
    out->d_reclen = sizeof(struct dirent64);
    if (p == 0) {
      out->d_ino = 1;
      out->d_type = DT_DIR;
      strcpy(out->d_name, ".");
      return 1;
    }
    if (p == 1) {
      out->d_ino = 1;
      out->d_type = DT_DIR;
      strcpy(out->d_name, "..");
      return 1;
    }
    uint32_t cidx = g_children[parent->child_start + (p - 2)];
    Entry* child = &g_entries[cidx];
    const char* name = basename_of(child->key);
    size_t nlen = strlen(name);
    if (nlen >= sizeof(out->d_name)) continue;
    out->d_ino = (ino64_t)(cidx + 2);
    out->d_type = dirent_type_for(child);
    memcpy(out->d_name, name, nlen + 1);
    return 1;
  }
  return 0;
}

// Pack getdents64-style records (struct linux_dirent64). The kernel layout
// matches glibc struct dirent64. Returns bytes written.
static long fl_dir_fill_getdents64(FlDir* dir, void* buf, size_t buf_len)
{
  if (dir == NULL) return 0;
  Entry* parent = &g_entries[dir->entry_idx];
  size_t total = (size_t)parent->child_count;

  size_t off = 0;
  char* out = (char*)buf;

  while (dir->pos < 2 + total) {
    size_t p = dir->pos;
    const char* name;
    char dot[] = ".";
    char dotdot[] = "..";
    Entry* child = NULL;
    uint64_t ino = 1;
    unsigned char dtype = DT_DIR;

    if (p == 0) {
      name = dot;
    } else if (p == 1) {
      name = dotdot;
    } else {
      uint32_t cidx = g_children[parent->child_start + (p - 2)];
      child = &g_entries[cidx];
      name = basename_of(child->key);
      ino = (uint64_t)(cidx + 2);
      dtype = dirent_type_for(child);
    }

    size_t nlen = strlen(name);
    // struct linux_dirent64 layout: u64 ino, s64 off, u16 reclen, u8 type, char[] name (NUL).
    // Aligned to 8.
    size_t reclen = 19 + nlen + 1;
    reclen = (reclen + 7u) & ~(size_t)7u;
    if (off + reclen > buf_len) {
      if (off == 0) {
        errno = EINVAL;
        return -1;
      }
      break;
    }
    char* rec = out + off;
    memset(rec, 0, reclen);
    memcpy(rec, &ino, 8);
    int64_t doff = (int64_t)(p + 1);
    memcpy(rec + 8, &doff, 8);
    uint16_t rl = (uint16_t)reclen;
    memcpy(rec + 16, &rl, 2);
    rec[18] = (char)dtype;
    memcpy(rec + 19, name, nlen + 1);
    off += reclen;
    ++dir->pos;
  }
  return (long)off;
}

// Synthesize a /dev/null-backed fd for an already-resolved directory entry.
// Returns the fd, or -1 on failure. The fd is registered in g_fd_map so
// getdents64 syscalls and fdopendir() can find the FlDir state.
static int synthesize_dir_fd_for(const Entry* e)
{
  if (e == NULL || !e->directory) return -1;
  FlDir* dir = fl_dir_alloc((size_t)(e - g_entries));
  if (dir == NULL) return -1;
  int fd = open_devnull_fd();
  if (fd < 0) {
    free(dir);
    return -1;
  }
  dir->real_fd = fd;
  fd_map_register(fd, dir);
  __sync_fetch_and_add(&g_opendir_hits, 1);
  return fd;
}

DIR* opendir(const char* name)
{
  __sync_fetch_and_add(&g_opendir_calls, 1);
  if (g_dirs_enabled && !g_inside && name != NULL) {
    ensure_loaded();
    if (g_loaded) {
      Entry* e = resolve_entry(name);
      if (e != NULL && e->directory) {
        // Allocate FlDir + register backing fd, then return the FlDir cast as
        // DIR*. dirfd() will then return the registered /dev/null fd.
        FlDir* dir = fl_dir_alloc((size_t)(e - g_entries));
        if (dir != NULL) {
          dir->real_fd = open_devnull_fd();
          if (dir->real_fd >= 0) fd_map_register(dir->real_fd, dir);
          __sync_fetch_and_add(&g_opendir_hits, 1);
          return (DIR*)dir;
        }
      }
    }
  }
  DIR* (*real_opendir)(const char*) =
      (DIR* (*)(const char*))next_symbol("opendir");
  return real_opendir(name);
}

DIR* fdopendir(int fd)
{
  FlDir* dir = fd_map_lookup(fd);
  if (dir != NULL) {
    __sync_fetch_and_add(&g_opendir_hits, 1);
    return (DIR*)dir;
  }
  DIR* (*real_fdopendir)(int) = (DIR* (*)(int))next_symbol("fdopendir");
  return real_fdopendir(fd);
}

struct dirent* readdir(DIR* dir)
{
  __sync_fetch_and_add(&g_readdir_calls, 1);
  if (is_fl_dir(dir)) {
    FlDir* fl = (FlDir*)dir;
    if (fl_dir_next_dirent(fl, &fl->dirent_buf)) {
      __sync_fetch_and_add(&g_readdir_hits, 1);
      return &fl->dirent_buf;
    }
    errno = 0;
    return NULL;
  }
  struct dirent* (*real_readdir)(DIR*) =
      (struct dirent* (*)(DIR*))next_symbol("readdir");
  return real_readdir(dir);
}

struct dirent64* readdir64(DIR* dir)
{
  __sync_fetch_and_add(&g_readdir_calls, 1);
  if (is_fl_dir(dir)) {
    FlDir* fl = (FlDir*)dir;
    if (fl_dir_next_dirent64(fl, &fl->dirent64_buf)) {
      __sync_fetch_and_add(&g_readdir_hits, 1);
      return &fl->dirent64_buf;
    }
    errno = 0;
    return NULL;
  }
  struct dirent64* (*real_readdir64)(DIR*) =
      (struct dirent64* (*)(DIR*))next_symbol("readdir64");
  if (real_readdir64 != NULL) return real_readdir64(dir);
  return (struct dirent64*)readdir(dir);
}

int closedir(DIR* dir)
{
  if (is_fl_dir(dir)) {
    FlDir* fl = (FlDir*)dir;
    int real_fd = fl->real_fd;
    if (real_fd >= 0) fd_map_take(real_fd);
    free(fl);
    if (real_fd >= 0) {
      int (*real_close)(int) = (int (*)(int))next_symbol("close");
      return real_close(real_fd);
    }
    return 0;
  }
  int (*real_closedir)(DIR*) = (int (*)(DIR*))next_symbol("closedir");
  return real_closedir(dir);
}

int dirfd(DIR* dir)
{
  if (is_fl_dir(dir)) return ((FlDir*)dir)->real_fd;
  int (*real_dirfd)(DIR*) = (int (*)(DIR*))next_symbol("dirfd");
  return real_dirfd(dir);
}

void rewinddir(DIR* dir)
{
  if (is_fl_dir(dir)) {
    ((FlDir*)dir)->pos = 0;
    return;
  }
  void (*real_rewinddir)(DIR*) = (void (*)(DIR*))next_symbol("rewinddir");
  real_rewinddir(dir);
}

long telldir(DIR* dir)
{
  if (is_fl_dir(dir)) return (long)((FlDir*)dir)->pos;
  long (*real_telldir)(DIR*) = (long (*)(DIR*))next_symbol("telldir");
  return real_telldir(dir);
}

void seekdir(DIR* dir, long loc)
{
  if (is_fl_dir(dir)) {
    ((FlDir*)dir)->pos = (size_t)(loc < 0 ? 0 : loc);
    return;
  }
  void (*real_seekdir)(DIR*, long) =
      (void (*)(DIR*, long))next_symbol("seekdir");
  real_seekdir(dir, loc);
}

int close(int fd)
{
  int (*real_close)(int) = (int (*)(int))next_symbol("close");
  FlDir* dir = fd_map_take(fd);
  if (dir != NULL) {
    free(dir);
  }
  return real_close(fd);
}

// fstat / fstat64: callers (Wine NTDLL especially) frequently fstat a freshly
// opened directory fd to verify it is a directory before issuing getdents64.
// /dev/null would report S_IFCHR and fail that check, so for any fd we
// synthesised we substitute a synthetic directory stat from the index.
int fstat(int fd, struct stat* st)
{
  if (st != NULL) {
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL) {
      fill_synthetic_stat(st, &g_entries[dir->entry_idx]);
      __sync_fetch_and_add(&g_stat_hits, 1);
      return 0;
    }
  }
  int (*real_fstat)(int, struct stat*) =
      (int (*)(int, struct stat*))next_symbol("fstat");
  return real_fstat(fd, st);
}

int fstat64(int fd, struct stat64* st)
{
  if (st != NULL) {
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL) {
      // struct stat64 layout matches struct stat on x86_64 glibc.
      fill_synthetic_stat((struct stat*)st, &g_entries[dir->entry_idx]);
      __sync_fetch_and_add(&g_stat_hits, 1);
      return 0;
    }
  }
  int (*real_fstat64)(int, struct stat64*) =
      (int (*)(int, struct stat64*))next_symbol("fstat64");
  if (real_fstat64 == NULL) return fstat(fd, (struct stat*)st);
  return real_fstat64(fd, st);
}

// Old glibc compat (still used by some packaged binaries).
int __fxstat(int ver, int fd, struct stat* st)
{
  if (st != NULL) {
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL) {
      fill_synthetic_stat(st, &g_entries[dir->entry_idx]);
      __sync_fetch_and_add(&g_stat_hits, 1);
      return 0;
    }
  }
  int (*real)(int, int, struct stat*) =
      (int (*)(int, int, struct stat*))next_symbol("__fxstat");
  if (real == NULL) return fstat(fd, st);
  return real(ver, fd, st);
}

int __fxstat64(int ver, int fd, struct stat64* st)
{
  if (st != NULL) {
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL) {
      fill_synthetic_stat((struct stat*)st, &g_entries[dir->entry_idx]);
      __sync_fetch_and_add(&g_stat_hits, 1);
      return 0;
    }
  }
  int (*real)(int, int, struct stat64*) =
      (int (*)(int, int, struct stat64*))next_symbol("__fxstat64");
  if (real == NULL) return fstat64(fd, st);
  return real(ver, fd, st);
}

// fcntl(F_GETFL) on the synthetic /dev/null fd reports O_RDONLY which matches
// what NTDLL wants to see for an O_DIRECTORY|O_RDONLY open, so leave that
// alone. lseek(fd, 0, SEEK_SET) is the rewind probe NTDLL emits before
// re-reading entries — translate it to a pos reset on our FlDir.
off_t lseek(int fd, off_t offset, int whence)
{
  FlDir* dir = fd_map_lookup(fd);
  if (dir != NULL) {
    if (whence == SEEK_SET) {
      dir->pos = (size_t)(offset < 0 ? 0 : offset);
      return offset;
    }
    if (whence == SEEK_CUR) {
      // Allow telldir-style queries.
      return (off_t)dir->pos;
    }
  }
  off_t (*real_lseek)(int, off_t, int) =
      (off_t (*)(int, off_t, int))next_symbol("lseek");
  return real_lseek(fd, offset, whence);
}

off_t lseek64(int fd, off_t offset, int whence)
{
  FlDir* dir = fd_map_lookup(fd);
  if (dir != NULL) {
    if (whence == SEEK_SET) {
      dir->pos = (size_t)(offset < 0 ? 0 : offset);
      return offset;
    }
    if (whence == SEEK_CUR) return (off_t)dir->pos;
  }
  off_t (*real_lseek64)(int, off_t, int) =
      (off_t (*)(int, off_t, int))next_symbol("lseek64");
  if (real_lseek64 == NULL) return lseek(fd, offset, whence);
  return real_lseek64(fd, offset, whence);
}

__attribute__((constructor))
static void preload_attach(void)
{
  g_stats = flag_enabled("FLUORINE_VFS_PRELOAD_STATS");
}

__attribute__((destructor))
static void preload_detach(void)
{
  if (!g_stats) return;
  if (!g_debug && g_open_calls == 0 && g_stat_calls == 0 && g_statx_calls == 0 &&
      g_access_calls == 0 && g_syscall_calls == 0 && g_resolve_hits == 0 &&
      g_resolve_misses == 0) {
    return;
  }
  fprintf(stderr,
          "[fluorine-vfs-preload] pid=%ld loaded=%d failed=%d entries=%zu "
          "open_calls=%llu stat_calls=%llu statx_calls=%llu access_calls=%llu "
          "syscall_calls=%llu "
          "path_mismatch=%llu "
          "canon_attempts=%llu canon_mount_hits=%llu canon_outside=%llu canon_failed=%llu "
          "opendir_calls=%llu opendir_hits=%llu "
          "readdir_calls=%llu readdir_hits=%llu "
          "getdents_calls=%llu getdents_hits=%llu "
          "getdents_libc_calls=%llu getdents_libc_hits=%llu "
          "resolve_hit=%llu resolve_miss=%llu open_hit=%llu open_fallback=%llu "
          "stat_hit=%llu access_hit=%llu mount='%s'\n",
          (long)getpid(), g_loaded, g_load_failed, g_entry_count,
          (unsigned long long)g_open_calls,
          (unsigned long long)g_stat_calls,
          (unsigned long long)g_statx_calls,
          (unsigned long long)g_access_calls,
          (unsigned long long)g_syscall_calls,
          (unsigned long long)g_path_mismatches,
          (unsigned long long)g_canon_attempts,
          (unsigned long long)g_canon_mount_hits,
          (unsigned long long)g_canon_outside,
          (unsigned long long)g_canon_failed,
          (unsigned long long)g_opendir_calls,
          (unsigned long long)g_opendir_hits,
          (unsigned long long)g_readdir_calls,
          (unsigned long long)g_readdir_hits,
          (unsigned long long)g_getdents_calls,
          (unsigned long long)g_getdents_hits,
          (unsigned long long)g_getdents_libc_calls,
          (unsigned long long)g_getdents_libc_hits,
          (unsigned long long)g_resolve_hits,
          (unsigned long long)g_resolve_misses,
          (unsigned long long)g_open_hits,
          (unsigned long long)g_open_fallbacks,
          (unsigned long long)g_stat_hits,
          (unsigned long long)g_access_hits,
          g_mount_point ? g_mount_point : "");
}

int open(const char* pathname, int flags, ...)
{
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (mode_t)va_arg(ap, int);
    va_end(ap);
  }

  int (*real_open)(const char*, int, ...) =
      (int (*)(const char*, int, ...))next_symbol("open");
  __sync_fetch_and_add(&g_open_calls, 1);
  if (g_inside || !read_only_flags(flags)) return real_open(pathname, flags, mode);

  Entry* e = resolve_entry(pathname);
  if (g_dirs_enabled && e != NULL && e->directory) {
    int newfd = synthesize_dir_fd_for(e);
    if (newfd >= 0) return newfd;
  }
  char* real_path = resolved_real_path(e);
  if (real_path == NULL) return real_open(pathname, flags, mode);

  g_inside = 1;
  int fd = real_open(real_path, flags, mode);
  if (fd < 0) {
    __sync_fetch_and_add(&g_open_fallbacks, 1);
    fd = real_open(pathname, flags, mode);
  } else {
    __sync_fetch_and_add(&g_open_hits, 1);
  }
  g_inside = 0;
  free(real_path);
  return fd;
}

int open64(const char* pathname, int flags, ...)
{
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  int (*real_open64)(const char*, int, ...) =
      (int (*)(const char*, int, ...))next_symbol("open64");
  if (real_open64 == NULL) real_open64 = (int (*)(const char*, int, ...))next_symbol("open");
  __sync_fetch_and_add(&g_open_calls, 1);
  if (g_inside || !read_only_flags(flags)) return real_open64(pathname, flags, mode);

  Entry* e = resolve_entry(pathname);
  if (g_dirs_enabled && e != NULL && e->directory) {
    int newfd = synthesize_dir_fd_for(e);
    if (newfd >= 0) return newfd;
  }
  char* real_path = resolved_real_path(e);
  if (real_path == NULL) return real_open64(pathname, flags, mode);

  g_inside = 1;
  int fd = real_open64(real_path, flags, mode);
  if (fd < 0) {
    __sync_fetch_and_add(&g_open_fallbacks, 1);
    fd = real_open64(pathname, flags, mode);
  } else {
    __sync_fetch_and_add(&g_open_hits, 1);
  }
  g_inside = 0;
  free(real_path);
  return fd;
}

int openat(int dirfd, const char* pathname, int flags, ...)
{
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  int (*real_openat)(int, const char*, int, ...) =
      (int (*)(int, const char*, int, ...))next_symbol("openat");
  __sync_fetch_and_add(&g_open_calls, 1);
  if (g_inside || pathname == NULL || !read_only_flags(flags)) {
    return real_openat(dirfd, pathname, flags, mode);
  }

  Entry* e = resolve_entry_at(dirfd, pathname);
  if (g_dirs_enabled && e != NULL && e->directory) {
    int newfd = synthesize_dir_fd_for(e);
    if (newfd >= 0) return newfd;
  }
  char* real_path = resolved_real_path(e);
  if (real_path == NULL) return real_openat(dirfd, pathname, flags, mode);

  g_inside = 1;
  int fd = real_openat(AT_FDCWD, real_path, flags, mode);
  if (fd < 0) {
    __sync_fetch_and_add(&g_open_fallbacks, 1);
    fd = real_openat(dirfd, pathname, flags, mode);
  } else {
    __sync_fetch_and_add(&g_open_hits, 1);
  }
  g_inside = 0;
  free(real_path);
  return fd;
}

int openat64(int dirfd, const char* pathname, int flags, ...)
{
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = (mode_t)va_arg(ap, int);
    va_end(ap);
  }
  int (*real_openat64)(int, const char*, int, ...) =
      (int (*)(int, const char*, int, ...))next_symbol("openat64");
  if (real_openat64 == NULL) real_openat64 = (int (*)(int, const char*, int, ...))next_symbol("openat");
  __sync_fetch_and_add(&g_open_calls, 1);
  if (g_inside || pathname == NULL || !read_only_flags(flags)) {
    return real_openat64(dirfd, pathname, flags, mode);
  }

  Entry* e = resolve_entry_at(dirfd, pathname);
  if (g_dirs_enabled && e != NULL && e->directory) {
    int newfd = synthesize_dir_fd_for(e);
    if (newfd >= 0) return newfd;
  }
  char* real_path = resolved_real_path(e);
  if (real_path == NULL) return real_openat64(dirfd, pathname, flags, mode);

  g_inside = 1;
  int fd = real_openat64(AT_FDCWD, real_path, flags, mode);
  if (fd < 0) {
    __sync_fetch_and_add(&g_open_fallbacks, 1);
    fd = real_openat64(dirfd, pathname, flags, mode);
  } else {
    __sync_fetch_and_add(&g_open_hits, 1);
  }
  g_inside = 0;
  free(real_path);
  return fd;
}

static int mapped_stat(const char* path, struct stat* st,
                       int (*real_fn)(const char*, struct stat*))
{
  if (g_inside) return real_fn(path, st);
  __sync_fetch_and_add(&g_stat_calls, 1);
  Entry* e = resolve_entry(path);
  if (e == NULL) return real_fn(path, st);
  char* real_path = resolved_real_path(e);

  g_inside = 1;
  int rc = 0;
  if (e->directory || real_path == NULL) {
    fill_synthetic_stat(st, e);
  } else {
    rc = real_fn(real_path, st);
    if (rc != 0) {
      fill_synthetic_stat(st, e);
      rc = 0;
    }
  }
  if (rc == 0) __sync_fetch_and_add(&g_stat_hits, 1);
  g_inside = 0;
  free(real_path);
  return rc;
}

int stat(const char* path, struct stat* st)
{
  return mapped_stat(path, st, (int (*)(const char*, struct stat*))next_symbol("stat"));
}

int lstat(const char* path, struct stat* st)
{
  return mapped_stat(path, st, (int (*)(const char*, struct stat*))next_symbol("lstat"));
}

int fstatat(int dirfd, const char* path, struct stat* st, int flags)
{
  int (*real_fstatat)(int, const char*, struct stat*, int) =
      (int (*)(int, const char*, struct stat*, int))next_symbol("fstatat");
  __sync_fetch_and_add(&g_stat_calls, 1);
  if (st != NULL && (flags & AT_EMPTY_PATH) &&
      (path == NULL || path[0] == '\0')) {
    FlDir* dir = fd_map_lookup(dirfd);
    if (dir != NULL) {
      fill_synthetic_stat(st, &g_entries[dir->entry_idx]);
      __sync_fetch_and_add(&g_stat_hits, 1);
      return 0;
    }
  }
  if (g_inside || path == NULL) return real_fstatat(dirfd, path, st, flags);
  Entry* e = resolve_entry_at(dirfd, path);
  if (e == NULL) return real_fstatat(dirfd, path, st, flags);
  char* real_path = resolved_real_path(e);

  g_inside = 1;
  int rc = 0;
  if (e->directory || real_path == NULL) {
    fill_synthetic_stat(st, e);
  } else {
    rc = real_fstatat(AT_FDCWD, real_path, st, flags);
    if (rc != 0) {
      fill_synthetic_stat(st, e);
      rc = 0;
    }
  }
  if (rc == 0) __sync_fetch_and_add(&g_stat_hits, 1);
  g_inside = 0;
  free(real_path);
  return rc;
}

int access(const char* path, int mode)
{
  int (*real_access)(const char*, int) = (int (*)(const char*, int))next_symbol("access");
  __sync_fetch_and_add(&g_access_calls, 1);
  if (g_inside) return real_access(path, mode);
  Entry* e = resolve_entry(path);
  if (e == NULL) return real_access(path, mode);
  char* real_path = resolved_real_path(e);
  int rc = 0;
  if (mode == F_OK || e->directory || real_path == NULL) {
    rc = 0;
  } else {
    rc = real_access(real_path, mode);
  }
  if (rc == 0) __sync_fetch_and_add(&g_access_hits, 1);
  free(real_path);
  return rc;
}

int faccessat(int dirfd, const char* path, int mode, int flags)
{
  int (*real_faccessat)(int, const char*, int, int) =
      (int (*)(int, const char*, int, int))next_symbol("faccessat");
  __sync_fetch_and_add(&g_access_calls, 1);
  if (g_inside || path == NULL) return real_faccessat(dirfd, path, mode, flags);
  Entry* e = resolve_entry_at(dirfd, path);
  if (e == NULL) return real_faccessat(dirfd, path, mode, flags);
  char* real_path = resolved_real_path(e);
  int rc = 0;
  if (mode == F_OK || e->directory || real_path == NULL) {
    rc = 0;
  } else {
    rc = real_faccessat(AT_FDCWD, real_path, mode, flags);
  }
  if (rc == 0) __sync_fetch_and_add(&g_access_hits, 1);
  free(real_path);
  return rc;
}

int statx(int dirfd, const char* path, int flags, unsigned int mask, struct statx* stx)
{
  int (*real_statx)(int, const char*, int, unsigned int, struct statx*) =
      (int (*)(int, const char*, int, unsigned int, struct statx*))next_symbol("statx");
  __sync_fetch_and_add(&g_statx_calls, 1);
  if (g_inside || path == NULL) {
    return real_statx ? real_statx(dirfd, path, flags, mask, stx)
                      : (int)syscall(SYS_statx, dirfd, path, flags, mask, stx);
  }
  Entry* e = resolve_entry_at(dirfd, path);
  if (e == NULL) {
    return real_statx ? real_statx(dirfd, path, flags, mask, stx)
                      : (int)syscall(SYS_statx, dirfd, path, flags, mask, stx);
  }
  char* real_path = resolved_real_path(e);

  g_inside = 1;
  int rc = 0;
  if (e->directory || real_path == NULL) {
    fill_synthetic_statx(stx, e);
  } else {
    rc = real_statx ? real_statx(AT_FDCWD, real_path, flags, mask, stx)
                    : (int)syscall(SYS_statx, AT_FDCWD, real_path, flags, mask, stx);
    if (rc != 0) {
      fill_synthetic_statx(stx, e);
      rc = 0;
    }
  }
  if (rc == 0) __sync_fetch_and_add(&g_stat_hits, 1);
  g_inside = 0;
  free(real_path);
  return rc;
}

// Wine 9+ / glibc 2.30+ may resolve getdents64() as a libc symbol rather than
// emitting syscall(SYS_getdents64, ...). Hook the symbol directly so synthetic
// dir fds answer NTDLL's directory reads. Mirrors the SYS_getdents64 branch in
// syscall().
ssize_t getdents64(int fd, void* buf, size_t count)
{
  __sync_fetch_and_add(&g_getdents_libc_calls, 1);
  if (!g_inside) {
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL && buf != NULL) {
      long n = fl_dir_fill_getdents64(dir, buf, count);
      if (n >= 0) __sync_fetch_and_add(&g_getdents_libc_hits, 1);
      return (ssize_t)n;
    }
  }
  ssize_t (*real)(int, void*, size_t) =
      (ssize_t (*)(int, void*, size_t))next_symbol("getdents64");
  if (real != NULL) return real(fd, buf, count);
  return (ssize_t)syscall(SYS_getdents64, fd, buf, count);
}

long syscall(long number, ...)
{
  va_list ap;
  va_start(ap, number);
  long a1 = va_arg(ap, long);
  long a2 = va_arg(ap, long);
  long a3 = va_arg(ap, long);
  long a4 = va_arg(ap, long);
  long a5 = va_arg(ap, long);
  long a6 = va_arg(ap, long);
  va_end(ap);

  long (*real_syscall)(long, ...) = (long (*)(long, ...))next_symbol("syscall");
  __sync_fetch_and_add(&g_syscall_calls, 1);

  if (g_inside) {
    return real_syscall(number, a1, a2, a3, a4, a5, a6);
  }

#ifdef SYS_openat
  if (number == SYS_openat) {
    int dirfd = (int)a1;
    const char* path = (const char*)a2;
    int flags = (int)a3;
    mode_t mode = (mode_t)a4;
    if (path != NULL && read_only_flags(flags)) {
      Entry* e = resolve_entry_at(dirfd, path);
      if (g_dirs_enabled && e != NULL && e->directory) {
        int newfd = synthesize_dir_fd_for(e);
        if (newfd >= 0) return newfd;
      }
      char* real_path = resolved_real_path(e);
      if (real_path != NULL) {
        g_inside = 1;
        long rc = real_syscall(number, AT_FDCWD, (long)real_path, flags, mode);
        if (rc < 0) {
          __sync_fetch_and_add(&g_open_fallbacks, 1);
          rc = real_syscall(number, a1, a2, a3, a4, a5, a6);
        } else {
          __sync_fetch_and_add(&g_open_hits, 1);
        }
        g_inside = 0;
        free(real_path);
        return rc;
      }
    }
  }
#endif

#ifdef SYS_getdents64
  if (number == SYS_getdents64) {
    int fd = (int)a1;
    void* buf = (void*)a2;
    size_t count = (size_t)a3;
    __sync_fetch_and_add(&g_getdents_calls, 1);
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL && buf != NULL) {
      long n = fl_dir_fill_getdents64(dir, buf, count);
      if (n >= 0) __sync_fetch_and_add(&g_getdents_hits, 1);
      return n;
    }
  }
#endif

#ifdef SYS_fstat
  if (number == SYS_fstat) {
    int fd = (int)a1;
    struct stat* st = (struct stat*)a2;
    if (st != NULL) {
      FlDir* dir = fd_map_lookup(fd);
      if (dir != NULL) {
        fill_synthetic_stat(st, &g_entries[dir->entry_idx]);
        __sync_fetch_and_add(&g_stat_hits, 1);
        return 0;
      }
    }
  }
#endif

#ifdef SYS_close
  if (number == SYS_close) {
    int fd = (int)a1;
    FlDir* dir = fd_map_take(fd);
    if (dir != NULL) free(dir);
  }
#endif

#ifdef SYS_lseek
  if (number == SYS_lseek) {
    int fd = (int)a1;
    long offset = a2;
    int whence = (int)a3;
    FlDir* dir = fd_map_lookup(fd);
    if (dir != NULL) {
      if (whence == SEEK_SET) {
        dir->pos = (size_t)(offset < 0 ? 0 : offset);
        return offset;
      }
      if (whence == SEEK_CUR) return (long)dir->pos;
    }
  }
#endif

#ifdef SYS_open
  if (number == SYS_open) {
    const char* path = (const char*)a1;
    int flags = (int)a2;
    mode_t mode = (mode_t)a3;
    if (path != NULL && read_only_flags(flags)) {
      Entry* e = resolve_entry(path);
      if (g_dirs_enabled && e != NULL && e->directory) {
        int newfd = synthesize_dir_fd_for(e);
        if (newfd >= 0) return newfd;
      }
      char* real_path = resolved_real_path(e);
      if (real_path != NULL) {
        g_inside = 1;
        long rc = real_syscall(number, (long)real_path, flags, mode, a4, a5, a6);
        if (rc < 0) {
          __sync_fetch_and_add(&g_open_fallbacks, 1);
          rc = real_syscall(number, a1, a2, a3, a4, a5, a6);
        } else {
          __sync_fetch_and_add(&g_open_hits, 1);
        }
        g_inside = 0;
        free(real_path);
        return rc;
      }
    }
  }
#endif

#ifdef SYS_newfstatat
  if (number == SYS_newfstatat) {
    int dirfd = (int)a1;
    const char* path = (const char*)a2;
    struct stat* st = (struct stat*)a3;
    int flags = (int)a4;
    // AT_EMPTY_PATH + empty path = fstat-on-dirfd. Wine NTDLL emits this to
    // verify the directory fd before calling getdents64.
    if (st != NULL && (flags & AT_EMPTY_PATH) &&
        (path == NULL || path[0] == '\0')) {
      FlDir* dir = fd_map_lookup(dirfd);
      if (dir != NULL) {
        fill_synthetic_stat(st, &g_entries[dir->entry_idx]);
        __sync_fetch_and_add(&g_stat_hits, 1);
        return 0;
      }
    }
    if (path != NULL && st != NULL) {
      Entry* e = resolve_entry_at(dirfd, path);
      char* real_path = resolved_real_path(e);
      if (e != NULL) {
        g_inside = 1;
        long rc = 0;
        if (e->directory || real_path == NULL) {
          fill_synthetic_stat(st, e);
        } else {
          rc = real_syscall(number, AT_FDCWD, (long)real_path, (long)st, flags, a5, a6);
          if (rc != 0) {
            fill_synthetic_stat(st, e);
            rc = 0;
          }
        }
        if (rc == 0) __sync_fetch_and_add(&g_stat_hits, 1);
        g_inside = 0;
        free(real_path);
        return rc;
      }
      free(real_path);
    }
  }
#endif

#ifdef SYS_statx
  if (number == SYS_statx) {
    int dirfd = (int)a1;
    const char* path = (const char*)a2;
    int flags = (int)a3;
    unsigned int mask = (unsigned int)a4;
    struct statx* stx = (struct statx*)a5;
    if (stx != NULL && (flags & AT_EMPTY_PATH) &&
        (path == NULL || path[0] == '\0')) {
      FlDir* dir = fd_map_lookup(dirfd);
      if (dir != NULL) {
        fill_synthetic_statx(stx, &g_entries[dir->entry_idx]);
        __sync_fetch_and_add(&g_stat_hits, 1);
        return 0;
      }
    }
    if (path != NULL && stx != NULL) {
      Entry* e = resolve_entry_at(dirfd, path);
      char* real_path = resolved_real_path(e);
      if (e != NULL) {
        g_inside = 1;
        long rc = 0;
        if (e->directory || real_path == NULL) {
          fill_synthetic_statx(stx, e);
        } else {
          rc = real_syscall(number, AT_FDCWD, (long)real_path, flags, mask, (long)stx, a6);
          if (rc != 0) {
            fill_synthetic_statx(stx, e);
            rc = 0;
          }
        }
        if (rc == 0) __sync_fetch_and_add(&g_stat_hits, 1);
        g_inside = 0;
        free(real_path);
        return rc;
      }
      free(real_path);
    }
  }
#endif

#ifdef SYS_access
  if (number == SYS_access) {
    const char* path = (const char*)a1;
    int mode = (int)a2;
    if (path != NULL) {
      Entry* e = resolve_entry(path);
      char* real_path = resolved_real_path(e);
      if (e != NULL) {
        long rc = 0;
        if (mode == F_OK || e->directory || real_path == NULL) {
          rc = 0;
        } else {
          rc = real_syscall(number, (long)real_path, mode, a3, a4, a5, a6);
        }
        if (rc == 0) __sync_fetch_and_add(&g_access_hits, 1);
        free(real_path);
        return rc;
      }
      free(real_path);
    }
  }
#endif

#ifdef SYS_faccessat
  if (number == SYS_faccessat) {
    int dirfd = (int)a1;
    const char* path = (const char*)a2;
    int mode = (int)a3;
    if (path != NULL) {
      Entry* e = resolve_entry_at(dirfd, path);
      char* real_path = resolved_real_path(e);
      if (e != NULL) {
        long rc = 0;
        if (mode == F_OK || e->directory || real_path == NULL) {
          rc = 0;
        } else {
          rc = real_syscall(number, AT_FDCWD, (long)real_path, mode, a4, a5, a6);
        }
        if (rc == 0) __sync_fetch_and_add(&g_access_hits, 1);
        free(real_path);
        return rc;
      }
      free(real_path);
    }
  }
#endif

  return real_syscall(number, a1, a2, a3, a4, a5, a6);
}
