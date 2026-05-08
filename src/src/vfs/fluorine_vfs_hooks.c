#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <MinHook.h>

typedef LONG NTSTATUS;

typedef struct _IO_STATUS_BLOCK_FL
{
  union {
    NTSTATUS Status;
    PVOID Pointer;
  } u;
  ULONG_PTR Information;
} IO_STATUS_BLOCK_FL, *PIO_STATUS_BLOCK_FL;

typedef struct _UNICODE_STRING_FL
{
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
} UNICODE_STRING_FL, *PUNICODE_STRING_FL;

typedef struct _OBJECT_ATTRIBUTES_FL
{
  ULONG Length;
  HANDLE RootDirectory;
  PUNICODE_STRING_FL ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor;
  PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES_FL, *POBJECT_ATTRIBUTES_FL;

typedef VOID(NTAPI* PIO_APC_ROUTINE_FL)(PVOID, PIO_STATUS_BLOCK_FL, ULONG);

typedef NTSTATUS(NTAPI* NtCreateFile_t)(PHANDLE, ACCESS_MASK,
                                        POBJECT_ATTRIBUTES_FL,
                                        PIO_STATUS_BLOCK_FL, PLARGE_INTEGER,
                                        ULONG, ULONG, ULONG, ULONG, PVOID,
                                        ULONG);
typedef NTSTATUS(NTAPI* NtOpenFile_t)(PHANDLE, ACCESS_MASK,
                                      POBJECT_ATTRIBUTES_FL,
                                      PIO_STATUS_BLOCK_FL, ULONG, ULONG);
typedef NTSTATUS(NTAPI* NtClose_t)(HANDLE);
typedef NTSTATUS(NTAPI* NtQueryDirectoryFile_t)(
    HANDLE, HANDLE, PIO_APC_ROUTINE_FL, PVOID, PIO_STATUS_BLOCK_FL, PVOID,
    ULONG, ULONG, BOOLEAN, PUNICODE_STRING_FL, BOOLEAN);
typedef NTSTATUS(NTAPI* NtQueryDirectoryFileEx_t)(
    HANDLE, HANDLE, PIO_APC_ROUTINE_FL, PVOID, PIO_STATUS_BLOCK_FL, PVOID,
    ULONG, ULONG, ULONG, PUNICODE_STRING_FL);
typedef NTSTATUS(NTAPI* NtQueryAttributesFile_t)(POBJECT_ATTRIBUTES_FL,
                                                 PVOID);
typedef NTSTATUS(NTAPI* NtQueryFullAttributesFile_t)(POBJECT_ATTRIBUTES_FL,
                                                     PVOID);
typedef NTSTATUS(NTAPI* NtReadFile_t)(HANDLE, HANDLE, PIO_APC_ROUTINE_FL,
                                      PVOID, PIO_STATUS_BLOCK_FL, PVOID,
                                      ULONG, PLARGE_INTEGER, PULONG);
typedef NTSTATUS(NTAPI* NtSetInformationFile_t)(HANDLE,
                                                PIO_STATUS_BLOCK_FL, PVOID,
                                                ULONG, ULONG);
typedef NTSTATUS(NTAPI* NtQueryInformationFile_t)(HANDLE,
                                                  PIO_STATUS_BLOCK_FL,
                                                  PVOID, ULONG, ULONG);
typedef BOOL(WINAPI* GetFileAttributesExW_t)(LPCWSTR, GET_FILEEX_INFO_LEVELS,
                                             LPVOID);
typedef BOOL(WINAPI* GetFileAttributesExA_t)(LPCSTR, GET_FILEEX_INFO_LEVELS,
                                             LPVOID);

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_NO_MORE_FILES
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#endif
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#endif
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)
#endif

#define FL_FilePositionInformation 14u
#define FL_FileStandardInformation 5u
#define FL_FileEndOfFileInformation 20u

typedef struct _FL_FILE_POSITION_INFORMATION {
  LARGE_INTEGER CurrentByteOffset;
} FL_FILE_POSITION_INFORMATION;

typedef struct _FL_FILE_STANDARD_INFORMATION {
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  ULONG NumberOfLinks;
  BOOLEAN DeletePending;
  BOOLEAN Directory;
} FL_FILE_STANDARD_INFORMATION;

#define FL_FileDirectoryInformation 1u
#define FL_FileFullDirectoryInformation 2u
#define FL_FileBothDirectoryInformation 3u
#define FL_FileNamesInformation 12u
#define FL_FileIdBothDirectoryInformation 37u
#define FL_FileIdFullDirectoryInformation 38u
#define FL_SL_RESTART_SCAN 0x00000001u
#define FL_SL_RETURN_SINGLE_ENTRY 0x00000002u

typedef struct _FL_FILE_DIRECTORY_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FL_FILE_DIRECTORY_INFORMATION;

typedef struct _FL_FILE_FULL_DIR_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  ULONG EaSize;
  WCHAR FileName[1];
} FL_FILE_FULL_DIR_INFORMATION;

typedef struct _FL_FILE_BOTH_DIR_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  ULONG EaSize;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  WCHAR FileName[1];
} FL_FILE_BOTH_DIR_INFORMATION;

typedef struct _FL_FILE_ID_BOTH_DIR_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  ULONG EaSize;
  CCHAR ShortNameLength;
  WCHAR ShortName[12];
  LARGE_INTEGER FileId;
  WCHAR FileName[1];
} FL_FILE_ID_BOTH_DIR_INFORMATION;

typedef struct _FL_FILE_ID_FULL_DIR_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  ULONG EaSize;
  LARGE_INTEGER FileId;
  WCHAR FileName[1];
} FL_FILE_ID_FULL_DIR_INFORMATION;

typedef struct _FL_FILE_NAMES_INFORMATION {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  ULONG FileNameLength;
  WCHAR FileName[1];
} FL_FILE_NAMES_INFORMATION;

typedef struct _FL_FILE_BASIC_INFORMATION {
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  ULONG FileAttributes;
} FL_FILE_BASIC_INFORMATION;

typedef struct _FL_FILE_NETWORK_OPEN_INFORMATION {
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  ULONG FileAttributes;
} FL_FILE_NETWORK_OPEN_INFORMATION;

static NtCreateFile_t Real_NtCreateFile = NULL;
static NtOpenFile_t Real_NtOpenFile = NULL;
static NtClose_t Real_NtClose = NULL;
static NtQueryDirectoryFile_t Real_NtQueryDirectoryFile = NULL;
static NtQueryDirectoryFileEx_t Real_NtQueryDirectoryFileEx = NULL;
static NtQueryAttributesFile_t Real_NtQueryAttributesFile = NULL;
static NtQueryFullAttributesFile_t Real_NtQueryFullAttributesFile = NULL;
static NtReadFile_t Real_NtReadFile = NULL;
static NtSetInformationFile_t Real_NtSetInformationFile = NULL;
static NtQueryInformationFile_t Real_NtQueryInformationFile = NULL;
static GetFileAttributesExW_t Real_GetFileAttributesExW = NULL;
static GetFileAttributesExA_t Real_GetFileAttributesExA = NULL;

static volatile LONG g_started = 0;
static volatile LONG g_installed = 0;
static volatile LONG g_stats_started = 0;
static volatile LONG g_first_create_logged = 0;
static volatile LONG g_first_open_logged = 0;
static volatile LONG g_first_mmap_logged = 0;
static ULONGLONG g_phase_start_tick = 0;
static volatile LONG64 g_nt_create = 0;
static volatile LONG64 g_nt_open = 0;
static volatile LONG64 g_nt_close = 0;
static volatile LONG64 g_nt_qdf = 0;
static volatile LONG64 g_nt_qdfex = 0;
static volatile LONG64 g_nt_qattr = 0;
static volatile LONG64 g_nt_qfullattr = 0;
static volatile LONG64 g_redirect_file = 0;
static volatile LONG64 g_redirect_miss = 0;
static volatile LONG64 g_redirect_skip = 0;
static volatile LONG64 g_redirect_dir = 0;
static volatile LONG64 g_dir_query_served = 0;
static volatile LONG64 g_dir_query_empty = 0;
static volatile LONG64 g_dir_query_bypass = 0;
static volatile LONG64 g_attr_hit = 0;
static volatile LONG64 g_attr_miss = 0;
static volatile LONG64 g_attr_neg = 0;
static volatile LONG64 g_attr_passthru = 0;
static volatile LONG64 g_attr_cache_hit = 0;
static volatile LONG64 g_attr_cache_insert = 0;
static volatile LONG64 g_attr_cache_full = 0;
static volatile LONG64 g_k32_gfaexw = 0;
static volatile LONG64 g_k32_gfaexa = 0;
static volatile LONG64 g_k32_attr_hit = 0;
static volatile LONG64 g_k32_cache_hit = 0;
static volatile LONG64 g_k32_passthru = 0;
static volatile LONG64 g_k32_neg = 0;
static volatile LONG64 g_k32_fast_path = 0;
static volatile LONG64 g_k32_full_path = 0;
static volatile LONG64 g_live_insert = 0;
static volatile LONG64 g_open_neg = 0;
static volatile LONG64 g_mmap_attached = 0;
static volatile LONG64 g_mmap_skip_size = 0;
static volatile LONG64 g_mmap_skip_zero = 0;
static volatile LONG64 g_mmap_map_failed = 0;
static volatile LONG64 g_mmap_view_failed = 0;
static volatile LONG64 g_mmap_reads = 0;
static volatile LONG64 g_mmap_bytes = 0;
static volatile LONG64 g_mmap_passthru = 0;
static volatile LONG64 g_mmap_detached = 0;

typedef struct VfsEntry
{
  char* key;
  WCHAR* real_nt;
  ULONG attrs;
  unsigned long long size;
  FILETIME mtime;
} VfsEntry;

typedef struct ChildEntry
{
  char* key;
  WCHAR* name;
  ULONG attrs;
  unsigned long long size;
  FILETIME mtime;
} ChildEntry;

typedef struct DirBucket
{
  char* key;
  ChildEntry* children;
  size_t child_count;
  size_t child_cap;
  struct DirBucket* next;
} DirBucket;

typedef struct DirHandleState
{
  HANDLE handle;
  DirBucket* bucket;
  size_t cursor;
  WCHAR* filter;
  size_t filter_len;
  int filter_set;
} DirHandleState;

static VfsEntry* g_entries = NULL;
static size_t g_entry_count = 0;
static DirBucket** g_dir_hash = NULL;
static size_t g_dir_count = 0;
static size_t g_child_count = 0;
static DirHandleState* g_dir_handles = NULL;
static size_t g_dir_handle_count = 0;
static size_t g_dir_handle_cap = 0;
static CRITICAL_SECTION g_dir_lock;
static int g_dir_lock_ready = 0;
static HANDLE g_dummy_dir = INVALID_HANDLE_VALUE;
static char* g_mount = NULL;
/* Precomputed mount prefixes for the fast scope gate. Built once in
 * load_index() after g_mount is normalized. The gate keeps the bridge from
 * touching any file outside the game's data dir: every probe to a system
 * DLL, registry-backed object, or unrelated drive bypasses the hook entirely
 * and goes straight to the real syscall. */
static WCHAR* g_mount_w_nt = NULL;       /* "\\??\\Z:\\<mount>" lowercase */
static size_t g_mount_w_nt_chars = 0;
static WCHAR* g_mount_w_dosq = NULL;     /* "\\\\?\\Z:\\<mount>" lowercase */
static size_t g_mount_w_dosq_chars = 0;
static WCHAR* g_mount_w_bare = NULL;     /* "Z:\\<mount>" lowercase */
static size_t g_mount_w_bare_chars = 0;
static char*  g_mount_a_bare = NULL;     /* "z:\\<mount>" lowercase ANSI */
static size_t g_mount_a_bare_len = 0;
static char*  g_mount_a_dosq = NULL;     /* "\\\\?\\z:\\<mount>" lowercase ANSI */
static size_t g_mount_a_dosq_len = 0;
static volatile LONG g_index_ready = 0;
static __thread int t_inside_redirect = 0;
static __thread WCHAR t_redirect_path[32768];

static DWORD copy_str(char* dst, DWORD pos, DWORD cap, const char* src);

static char ascii_lower(char c)
{
  return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int ascii_eq_ci(const char* a, const char* b)
{
  while (*a && *b) {
    char ca = ascii_lower(*a);
    char cb = ascii_lower(*b);
    if (ca != cb) return 0;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

static char* heap_strndup(const char* s, size_t n)
{
  char* out = (char*)malloc(n + 1);
  if (out == NULL) return NULL;
  memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static char* normalize_utf8_path(char* s)
{
  char* r = s;
  char* w = s;
  while (*r == '/' || *r == '\\') ++r;
  while (*r) {
    char c = *r++;
    if (c == '\\') c = '/';
    *w++ = ascii_lower(c);
  }
  while (w > s && w[-1] == '/') --w;
  *w = '\0';
  return s;
}

static char* json_string_field(const char* line, const char* key)
{
  char needle[96];
  DWORD pos = 0;
  pos = copy_str(needle, pos, sizeof(needle), "\"");
  pos = copy_str(needle, pos, sizeof(needle), key);
  pos = copy_str(needle, pos, sizeof(needle), "\":");
  needle[pos] = '\0';

  const char* p = strstr(line, needle);
  if (p == NULL) return NULL;
  p = strchr(p + strlen(needle), '"');
  if (p == NULL) return NULL;
  ++p;

  char* out = (char*)malloc(strlen(p) + 1);
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

static WCHAR* unix_path_to_nt_wide(const char* path)
{
  if (path == NULL || path[0] != '/') return NULL;
  size_t len = strlen(path);
  char* nt = (char*)malloc(len + 8);
  if (nt == NULL) return NULL;
  strcpy(nt, "\\??\\Z:");
  strcpy(nt + 6, path);
  for (char* p = nt; *p; ++p) {
    if (*p == '/') *p = '\\';
  }

  int wlen = MultiByteToWideChar(CP_UTF8, 0, nt, -1, NULL, 0);
  if (wlen <= 0) {
    free(nt);
    return NULL;
  }
  WCHAR* out = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
  if (out == NULL) {
    free(nt);
    return NULL;
  }
  MultiByteToWideChar(CP_UTF8, 0, nt, -1, out, wlen);
  free(nt);
  return out;
}

/* --- Mount-scope fast gate --------------------------------------------------
 *
 * The bridge only ever has knowledge of files inside the FUSE mount (i.e. the
 * game's Data dir). Any probe to a path outside that subtree must be invisible
 * to our hooks: pass straight through to Wine, no normalization, no cache
 * lookup, no possible chance of a wrong answer.
 *
 * To make that cheap, we precompute the mount prefix in the three forms that
 * Wine/games commonly produce, lowercased, and do a byte/wide prefix compare
 * directly on the input the kernel/k32 hook receives. Out-of-mount paths fail
 * the compare in a handful of characters and pass through without any of the
 * UTF-8/slash/lowercase machinery in nt_path_to_key().
 */

static WCHAR ascii_lower_w(WCHAR c)
{
  return (c >= L'A' && c <= L'Z') ? (WCHAR)(c + 32) : c;
}

static int wide_match_prefix(const WCHAR* path, size_t path_chars,
                             const WCHAR* prefix, size_t prefix_chars)
{
  if (prefix == NULL || prefix_chars == 0) return 0;
  if (path_chars < prefix_chars) return 0;
  for (size_t i = 0; i < prefix_chars; ++i) {
    if (ascii_lower_w(path[i]) != prefix[i]) return 0;
  }
  if (path_chars == prefix_chars) return 1;
  WCHAR n = path[prefix_chars];
  return n == L'\\' || n == L'/';
}

static int byte_match_prefix(const char* path, size_t path_len,
                             const char* prefix, size_t prefix_len)
{
  if (prefix == NULL || prefix_len == 0) return 0;
  if (path_len < prefix_len) return 0;
  for (size_t i = 0; i < prefix_len; ++i) {
    if (ascii_lower(path[i]) != prefix[i]) return 0;
  }
  if (path_len == prefix_len) return 1;
  char n = path[prefix_len];
  return n == '\\' || n == '/';
}

static void build_mount_prefixes(void)
{
  if (g_mount == NULL) return;
  size_t mlen = strlen(g_mount);
  if (mlen == 0) return;

  /* g_mount is lowercase, forward-slash, no leading or trailing slash.
   * Build "z:\\<mount-with-backslashes>" in ANSI lowercase first. */
  size_t bare_len = 3 + mlen;            /* "z:\\" + mlen */
  char* a_bare = (char*)malloc(bare_len + 1);
  if (a_bare == NULL) return;
  a_bare[0] = 'z';
  a_bare[1] = ':';
  a_bare[2] = '\\';
  for (size_t i = 0; i < mlen; ++i) {
    char c = g_mount[i];
    a_bare[3 + i] = (c == '/') ? '\\' : c;
  }
  a_bare[bare_len] = '\0';
  g_mount_a_bare = a_bare;
  g_mount_a_bare_len = bare_len;

  /* "\\\\?\\z:\\<...>" */
  size_t dosq_len = 4 + bare_len;
  char* a_dosq = (char*)malloc(dosq_len + 1);
  if (a_dosq != NULL) {
    a_dosq[0] = '\\';
    a_dosq[1] = '\\';
    a_dosq[2] = '?';
    a_dosq[3] = '\\';
    memcpy(a_dosq + 4, a_bare, bare_len);
    a_dosq[dosq_len] = '\0';
    g_mount_a_dosq = a_dosq;
    g_mount_a_dosq_len = dosq_len;
  }

  /* Wide bare. */
  WCHAR* w_bare = (WCHAR*)malloc((bare_len + 1) * sizeof(WCHAR));
  if (w_bare == NULL) return;
  for (size_t i = 0; i < bare_len; ++i) {
    w_bare[i] = (WCHAR)(unsigned char)a_bare[i];
  }
  w_bare[bare_len] = 0;
  g_mount_w_bare = w_bare;
  g_mount_w_bare_chars = bare_len;

  /* "\\??\\Z:\\<...>" wide */
  size_t nt_chars = 4 + bare_len;
  WCHAR* w_nt = (WCHAR*)malloc((nt_chars + 1) * sizeof(WCHAR));
  if (w_nt != NULL) {
    w_nt[0] = L'\\';
    w_nt[1] = L'?';
    w_nt[2] = L'?';
    w_nt[3] = L'\\';
    for (size_t i = 0; i < bare_len; ++i) w_nt[4 + i] = w_bare[i];
    w_nt[nt_chars] = 0;
    g_mount_w_nt = w_nt;
    g_mount_w_nt_chars = nt_chars;
  }

  /* "\\\\?\\Z:\\<...>" wide */
  size_t dosq_chars = 4 + bare_len;
  WCHAR* w_dosq = (WCHAR*)malloc((dosq_chars + 1) * sizeof(WCHAR));
  if (w_dosq != NULL) {
    w_dosq[0] = L'\\';
    w_dosq[1] = L'\\';
    w_dosq[2] = L'?';
    w_dosq[3] = L'\\';
    for (size_t i = 0; i < bare_len; ++i) w_dosq[4 + i] = w_bare[i];
    w_dosq[dosq_chars] = 0;
    g_mount_w_dosq = w_dosq;
    g_mount_w_dosq_chars = dosq_chars;
  }
}

static int us_under_mount(PUNICODE_STRING_FL us)
{
  if (us == NULL || us->Buffer == NULL) return 0;
  size_t chars = (size_t)(us->Length / sizeof(WCHAR));
  return wide_match_prefix(us->Buffer, chars, g_mount_w_nt,
                           g_mount_w_nt_chars) ||
         wide_match_prefix(us->Buffer, chars, g_mount_w_dosq,
                           g_mount_w_dosq_chars) ||
         wide_match_prefix(us->Buffer, chars, g_mount_w_bare,
                           g_mount_w_bare_chars);
}

static int wide_under_mount(LPCWSTR path)
{
  if (path == NULL) return 0;
  size_t chars = 0;
  while (path[chars] != 0) ++chars;
  return wide_match_prefix(path, chars, g_mount_w_nt, g_mount_w_nt_chars) ||
         wide_match_prefix(path, chars, g_mount_w_dosq,
                           g_mount_w_dosq_chars) ||
         wide_match_prefix(path, chars, g_mount_w_bare,
                           g_mount_w_bare_chars);
}

static int ansi_under_mount(LPCSTR path)
{
  if (path == NULL) return 0;
  size_t plen = strlen(path);
  return byte_match_prefix(path, plen, g_mount_a_bare,
                           g_mount_a_bare_len) ||
         byte_match_prefix(path, plen, g_mount_a_dosq,
                           g_mount_a_dosq_len);
}

static WCHAR* utf8_to_wide_n(const char* s, size_t n)
{
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
  if (wlen <= 0) return NULL;
  WCHAR* out = (WCHAR*)malloc(((size_t)wlen + 1) * sizeof(WCHAR));
  if (out == NULL) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, s, (int)n, out, wlen);
  out[wlen] = 0;
  return out;
}

static unsigned long long json_u64_field(const char* line, const char* key)
{
  char needle[96];
  DWORD pos = 0;
  pos = copy_str(needle, pos, sizeof(needle), "\"");
  pos = copy_str(needle, pos, sizeof(needle), key);
  pos = copy_str(needle, pos, sizeof(needle), "\":");
  needle[pos] = '\0';

  const char* p = strstr(line, needle);
  if (p == NULL) return 0;
  p += strlen(needle);
  while (*p == ' ') ++p;
  unsigned long long v = 0;
  while (*p >= '0' && *p <= '9') {
    v = (v * 10ULL) + (unsigned long long)(*p - '0');
    ++p;
  }
  return v;
}

static FILETIME filetime_from_unix_ns(unsigned long long ns)
{
  unsigned long long ft = 116444736000000000ULL + (ns / 100ULL);
  FILETIME out;
  out.dwLowDateTime = (DWORD)(ft & 0xffffffffULL);
  out.dwHighDateTime = (DWORD)(ft >> 32);
  return out;
}

static unsigned long hash_str(const char* s)
{
  unsigned long h = 2166136261u;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 16777619u;
  }
  return h;
}

static DirBucket* get_dir_bucket(const char* key, int create)
{
  if (g_dir_hash == NULL) {
    if (!create) return NULL;
    g_dir_hash = (DirBucket**)calloc(65536, sizeof(DirBucket*));
    if (g_dir_hash == NULL) return NULL;
  }
  unsigned long slot = hash_str(key) & 65535u;
  for (DirBucket* b = g_dir_hash[slot]; b != NULL; b = b->next) {
    if (strcmp(b->key, key) == 0) return b;
  }
  if (!create) return NULL;

  DirBucket* b = (DirBucket*)calloc(1, sizeof(DirBucket));
  if (b == NULL) return NULL;
  b->key = heap_strndup(key, strlen(key));
  if (b->key == NULL) {
    free(b);
    return NULL;
  }
  b->next = g_dir_hash[slot];
  g_dir_hash[slot] = b;
  ++g_dir_count;
  return b;
}

static int child_cmp(const void* a, const void* b)
{
  const ChildEntry* ca = (const ChildEntry*)a;
  const ChildEntry* cb = (const ChildEntry*)b;
  return strcmp(ca->key, cb->key);
}

static void sort_dir_children(void)
{
  if (g_dir_hash == NULL) return;
  for (size_t i = 0; i < 65536; ++i) {
    for (DirBucket* b = g_dir_hash[i]; b != NULL; b = b->next) {
      if (b->child_count > 1) {
        qsort(b->children, b->child_count, sizeof(ChildEntry), child_cmp);
      }
    }
  }
}

static int bucket_has_child(DirBucket* b, const char* child_key)
{
  for (size_t i = 0; i < b->child_count; ++i) {
    if (strcmp(b->children[i].key, child_key) == 0) return 1;
  }
  return 0;
}

static int add_child(const char* parent_key, const char* child_key,
                     const char* name, size_t name_len, ULONG attrs,
                     unsigned long long size, FILETIME mtime)
{
  DirBucket* b = get_dir_bucket(parent_key, 1);
  if (b == NULL) return 0;
  if (bucket_has_child(b, child_key)) return 1;

  if (b->child_count == b->child_cap) {
    size_t next_cap = b->child_cap ? b->child_cap * 2 : 8;
    ChildEntry* next =
        (ChildEntry*)realloc(b->children, next_cap * sizeof(ChildEntry));
    if (next == NULL) return 0;
    b->children = next;
    b->child_cap = next_cap;
  }

  ChildEntry* e = &b->children[b->child_count];
  memset(e, 0, sizeof(*e));
  e->key = heap_strndup(child_key, strlen(child_key));
  e->name = utf8_to_wide_n(name, name_len);
  if (e->key == NULL || e->name == NULL) {
    if (e->key != NULL) free(e->key);
    if (e->name != NULL) free(e->name);
    memset(e, 0, sizeof(*e));
    return 0;
  }
  e->attrs = attrs;
  e->size = size;
  e->mtime = mtime;
  ++b->child_count;
  ++g_child_count;
  return 1;
}

static void append_segment_key(char* dst, size_t cap, const char* parent,
                               const char* seg)
{
  if (parent[0] != '\0') {
    snprintf(dst, cap, "%s/%s", parent, seg);
  } else {
    snprintf(dst, cap, "%s", seg);
  }
  dst[cap - 1] = '\0';
}

static void add_path_to_dirs(const char* original_path,
                             unsigned long long size,
                             FILETIME mtime)
{
  char parent[2048];
  parent[0] = '\0';
  get_dir_bucket("", 1);

  const char* p = original_path;
  while (*p == '/' || *p == '\\') ++p;
  while (*p) {
    const char* start = p;
    while (*p && *p != '/' && *p != '\\') ++p;
    size_t n = (size_t)(p - start);
    while (*p == '/' || *p == '\\') ++p;
    if (n == 0) continue;

    char* seg = heap_strndup(start, n);
    if (seg == NULL) return;
    normalize_utf8_path(seg);

    char child_key[2048];
    append_segment_key(child_key, sizeof(child_key), parent, seg);
    int is_file = (*p == '\0');
    add_child(parent, seg, start, n,
              is_file ? FILE_ATTRIBUTE_ARCHIVE : FILE_ATTRIBUTE_DIRECTORY,
              is_file ? size : 0, is_file ? mtime : (FILETIME){0, 0});
    if (!is_file) {
      get_dir_bucket(child_key, 1);
      strncpy(parent, child_key, sizeof(parent) - 1);
      parent[sizeof(parent) - 1] = '\0';
    }
    free(seg);
  }
}

static int entry_cmp(const void* a, const void* b)
{
  const VfsEntry* ea = (const VfsEntry*)a;
  const VfsEntry* eb = (const VfsEntry*)b;
  return strcmp(ea->key, eb->key);
}

static VfsEntry* lookup_entry(const char* key)
{
  size_t lo = 0;
  size_t hi = g_entry_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int cmp = strcmp(key, g_entries[mid].key);
    if (cmp == 0) return &g_entries[mid];
    if (cmp < 0) hi = mid;
    else lo = mid + 1;
  }
  return NULL;
}

static void append_log(const char* buf, DWORD len)
{
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err != NULL && err != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    WriteFile(err, buf, len, &wrote, NULL);
  }

  HANDLE f = CreateFileA("Z:\\tmp\\fluorine_vfs_appinit.log",
                         FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE,
                         NULL,
                         OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    WriteFile(f, buf, len, &wrote, NULL);
    CloseHandle(f);
  }
}

static DWORD copy_str(char* dst, DWORD pos, DWORD cap, const char* src)
{
  while (*src && pos + 1 < cap) dst[pos++] = *src++;
  return pos;
}

static DWORD copy_u64(char* dst, DWORD pos, DWORD cap, unsigned long long v)
{
  char rev[32];
  int n = 0;
  if (v == 0) {
    rev[n++] = '0';
  } else {
    while (v > 0 && n < (int)sizeof(rev)) {
      rev[n++] = (char)('0' + (v % 10));
      v /= 10;
    }
  }
  while (n > 0 && pos + 1 < cap) dst[pos++] = rev[--n];
  return pos;
}

static void log_msg(const char* msg)
{
  char line[512];
  DWORD pos = 0;
  pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] ");
  pos = copy_str(line, pos, sizeof(line), msg);
  pos = copy_str(line, pos, sizeof(line), "\n");
  append_log(line, pos);
}

static void log_hook_status(const char* name, MH_STATUS st)
{
  char line[512];
  DWORD pos = 0;
  pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] hook ");
  pos = copy_str(line, pos, sizeof(line), name);
  pos = copy_str(line, pos, sizeof(line), " status=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)(int)st);
  pos = copy_str(line, pos, sizeof(line), "\n");
  append_log(line, pos);
}

static void log_phase(const char* name)
{
  ULONGLONG now = GetTickCount64();
  ULONGLONG start = g_phase_start_tick;
  char line[512];
  DWORD pos = 0;
  pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] phase ");
  pos = copy_str(line, pos, sizeof(line), name);
  pos = copy_str(line, pos, sizeof(line), " tick_ms=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)now);
  if (start != 0) {
    pos = copy_str(line, pos, sizeof(line), " since_hook_worker_ms=");
    pos = copy_u64(line, pos, sizeof(line),
                   (unsigned long long)(now - start));
  }
  pos = copy_str(line, pos, sizeof(line), " tid=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)GetCurrentThreadId());
  pos = copy_str(line, pos, sizeof(line), "\n");
  append_log(line, pos);
}

static void log_stats(void)
{
  char line[512];
  DWORD pos = 0;
  pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] nt_stats create=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_create);
  pos = copy_str(line, pos, sizeof(line), " open=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_open);
  pos = copy_str(line, pos, sizeof(line), " close=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_close);
  pos = copy_str(line, pos, sizeof(line), " qdf=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_qdf);
  pos = copy_str(line, pos, sizeof(line), " qdfex=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_qdfex);
  pos = copy_str(line, pos, sizeof(line), " qattr=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_qattr);
  pos = copy_str(line, pos, sizeof(line), " qfullattr=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_nt_qfullattr);
  pos = copy_str(line, pos, sizeof(line), " redir_file=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_redirect_file);
  pos = copy_str(line, pos, sizeof(line), " redir_miss=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_redirect_miss);
  pos = copy_str(line, pos, sizeof(line), " redir_skip=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_redirect_skip);
  pos = copy_str(line, pos, sizeof(line), " redir_dir=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_redirect_dir);
  pos = copy_str(line, pos, sizeof(line), " dir_served=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_dir_query_served);
  pos = copy_str(line, pos, sizeof(line), " dir_empty=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_dir_query_empty);
  pos = copy_str(line, pos, sizeof(line), " dir_bypass=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_dir_query_bypass);
  pos = copy_str(line, pos, sizeof(line), " attr_hit=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_attr_hit);
  pos = copy_str(line, pos, sizeof(line), " attr_miss=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_attr_miss);
  pos = copy_str(line, pos, sizeof(line), " attr_neg=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_attr_neg);
  pos = copy_str(line, pos, sizeof(line), " attr_pass=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_attr_passthru);
  pos = copy_str(line, pos, sizeof(line), " cache_hit=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_attr_cache_hit);
  pos = copy_str(line, pos, sizeof(line), " cache_ins=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_attr_cache_insert);
  pos = copy_str(line, pos, sizeof(line), " cache_full=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_attr_cache_full);
  pos = copy_str(line, pos, sizeof(line), " k32w=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_gfaexw);
  pos = copy_str(line, pos, sizeof(line), " k32a=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_gfaexa);
  pos = copy_str(line, pos, sizeof(line), " k32_hit=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_attr_hit);
  pos = copy_str(line, pos, sizeof(line), " k32_cache=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_cache_hit);
  pos = copy_str(line, pos, sizeof(line), " k32_pass=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_passthru);
  pos = copy_str(line, pos, sizeof(line), " k32_neg=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_neg);
  pos = copy_str(line, pos, sizeof(line), " k32_fast=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_fast_path);
  pos = copy_str(line, pos, sizeof(line), " k32_full=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_k32_full_path);
  pos = copy_str(line, pos, sizeof(line), " live_ins=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_live_insert);
  pos = copy_str(line, pos, sizeof(line), " open_neg=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_open_neg);
  pos = copy_str(line, pos, sizeof(line), " mm_att=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_attached);
  pos = copy_str(line, pos, sizeof(line), " mm_det=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_detached);
  pos = copy_str(line, pos, sizeof(line), " mm_rd=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_reads);
  pos = copy_str(line, pos, sizeof(line), " mm_by=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_bytes);
  pos = copy_str(line, pos, sizeof(line), " mm_pt=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_passthru);
  pos = copy_str(line, pos, sizeof(line), " mm_skz=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_skip_size);
  pos = copy_str(line, pos, sizeof(line), " mm_mf=");
  pos = copy_u64(line, pos, sizeof(line),
                 (unsigned long long)g_mmap_map_failed);
  pos = copy_str(line, pos, sizeof(line), "\n");
  append_log(line, pos);
}

static DWORD WINAPI stats_worker(void* unused)
{
  (void)unused;
  for (int i = 0; i < 120; ++i) {
    Sleep(1000);
    if (g_installed) log_stats();
  }
  log_phase("stats_worker_exit");
  return 0;
}

static void start_stats_worker(void)
{
  if (InterlockedExchange(&g_stats_started, 1) != 0) return;
  HANDLE th = CreateThread(NULL, 0, stats_worker, NULL, 0, NULL);
  if (th != NULL) CloseHandle(th);
}

static void log_index_loaded(void)
{
  char line[256];
  DWORD pos = 0;
  pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] index files=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_entry_count);
  pos = copy_str(line, pos, sizeof(line), " dirs=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_dir_count);
  pos = copy_str(line, pos, sizeof(line), " children=");
  pos = copy_u64(line, pos, sizeof(line), (unsigned long long)g_child_count);
  pos = copy_str(line, pos, sizeof(line), "\n");
  append_log(line, pos);
}

static int read_file_all(const char* path, char** out, size_t* out_len)
{
  *out = NULL;
  *out_len = 0;
  HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 ||
      sz.QuadPart > 512LL * 1024LL * 1024LL) {
    CloseHandle(f);
    return 0;
  }

  size_t len = (size_t)sz.QuadPart;
  char* buf = (char*)malloc(len + 1);
  if (buf == NULL) {
    CloseHandle(f);
    return 0;
  }

  size_t off = 0;
  while (off < len) {
    DWORD want = (DWORD)((len - off) > (1024 * 1024) ? (1024 * 1024) : (len - off));
    DWORD got = 0;
    if (!ReadFile(f, buf + off, want, &got, NULL) || got == 0) {
      free(buf);
      CloseHandle(f);
      return 0;
    }
    off += got;
  }
  CloseHandle(f);
  buf[len] = '\0';
  *out = buf;
  *out_len = len;
  return 1;
}

static int append_entry(char* key, WCHAR* real_nt, ULONG attrs,
                        unsigned long long size, FILETIME mtime)
{
  VfsEntry* next =
      (VfsEntry*)realloc(g_entries, (g_entry_count + 1) * sizeof(VfsEntry));
  if (next == NULL) return 0;
  g_entries = next;
  g_entries[g_entry_count].key = key;
  g_entries[g_entry_count].real_nt = real_nt;
  g_entries[g_entry_count].attrs = attrs;
  g_entries[g_entry_count].size = size;
  g_entries[g_entry_count].mtime = mtime;
  ++g_entry_count;
  return 1;
}

static int load_index(void)
{
  char index_path[1024];
  DWORD n = GetEnvironmentVariableA("FLUORINE_VFS_INDEX", index_path,
                                    (DWORD)sizeof(index_path));
  if (n == 0 || n >= sizeof(index_path)) {
    log_msg("index load skipped: FLUORINE_VFS_INDEX unset");
    return 0;
  }

  char mount[1024];
  n = GetEnvironmentVariableA("FLUORINE_VFS_MOUNT", mount, (DWORD)sizeof(mount));
  if (n == 0 || n >= sizeof(mount)) {
    log_msg("index load skipped: FLUORINE_VFS_MOUNT unset");
    return 0;
  }
  g_mount = heap_strndup(mount, strlen(mount));
  if (g_mount == NULL) return 0;
  normalize_utf8_path(g_mount);
  build_mount_prefixes();

  char* file = NULL;
  size_t file_len = 0;
  if (!read_file_all(index_path, &file, &file_len)) {
    log_msg("index read failed");
    return 0;
  }

  char* line = file;
  char* end = file + file_len;
  while (line < end) {
    char* nl = (char*)memchr(line, '\n', (size_t)(end - line));
    if (nl != NULL) *nl = '\0';
    if (strstr(line, "\"record\":\"entry\"") != NULL) {
      char* type = json_string_field(line, "type");
      if (type != NULL && ascii_eq_ci(type, "file")) {
        char* vp = json_string_field(line, "virtual_path");
        char* rp = json_string_field(line, "real_path");
        /* Accept both absolute (mod / Overwrite) and backing-relative
         * (vanilla data dir) entries. Backing entries get real_nt=NULL
         * — they exist for query_index_attrs_key and the negcache
         * decision, but Hook_NtCreateFile skips redirect for them and
         * falls through to Real_, which serves them via the FUSE
         * mount path. Without this, every base-game file open without
         * a preceding GetFileAttributes would hit the negcache and
         * return STATUS_OBJECT_NAME_NOT_FOUND. */
        if (vp != NULL && rp != NULL) {
          unsigned long long size = json_u64_field(line, "size");
          FILETIME mtime =
              filetime_from_unix_ns(json_u64_field(line, "mtime_ns"));
          add_path_to_dirs(vp, size, mtime);
          normalize_utf8_path(vp);
          WCHAR* real_nt = (rp[0] == '/') ? unix_path_to_nt_wide(rp) : NULL;
          int unix_alloc_failed = (rp[0] == '/' && real_nt == NULL);
          if (unix_alloc_failed ||
              !append_entry(vp, real_nt, FILE_ATTRIBUTE_ARCHIVE, size,
                            mtime)) {
            free(vp);
            if (real_nt != NULL) free(real_nt);
          }
          vp = NULL;
        }
        if (vp != NULL) free(vp);
        if (rp != NULL) free(rp);
      }
      if (type != NULL) free(type);
    }
    if (nl == NULL) break;
    line = nl + 1;
  }
  free(file);

  if (g_entry_count == 0) {
    log_msg("index load failed: no files");
    return 0;
  }
  qsort(g_entries, g_entry_count, sizeof(VfsEntry), entry_cmp);
  sort_dir_children();
  InterlockedExchange(&g_index_ready, 1);
  log_index_loaded();
  return 1;
}

static int nt_path_to_key(PUNICODE_STRING_FL us, char* out, size_t cap)
{
  if (us == NULL || us->Buffer == NULL || cap == 0) return 0;
  int chars = us->Length / sizeof(WCHAR);
  if (chars <= 0) return 0;
  int need = WideCharToMultiByte(CP_UTF8, 0, us->Buffer, chars, NULL, 0,
                                NULL, NULL);
  if (need <= 0 || (size_t)need + 1 >= cap) return 0;
  WideCharToMultiByte(CP_UTF8, 0, us->Buffer, chars, out, (int)cap, NULL,
                      NULL);
  out[need] = '\0';

  char* p = out;
  if (strncmp(p, "\\??\\", 4) == 0 || strncmp(p, "\\\\?\\", 4) == 0) p += 4;
  if ((p[0] == 'Z' || p[0] == 'z') && p[1] == ':') {
    p += 2;
    while (*p == '\\' || *p == '/') ++p;
    memmove(out, "/", 1);
    memmove(out + 1, p, strlen(p) + 1);
  } else {
    if (p != out) memmove(out, p, strlen(p) + 1);
  }
  for (char* q = out; *q; ++q) {
    if (*q == '\\') *q = '/';
    else *q = ascii_lower(*q);
  }
  while (strlen(out) > 1 && out[strlen(out) - 1] == '/') {
    out[strlen(out) - 1] = '\0';
  }

  const char* cmp = out;
  if (g_mount != NULL && g_mount[0] != '/' && cmp[0] == '/') ++cmp;

  size_t ml = g_mount ? strlen(g_mount) : 0;
  if (ml == 0 || strncmp(cmp, g_mount, ml) != 0) return 0;
  if (cmp[ml] == '\0') {
    out[0] = '\0';
    return 1;
  }
  if (cmp[ml] != '/') return 0;
  memmove(out, cmp + ml + 1, strlen(cmp + ml + 1) + 1);
  return out[0] != '\0';
}

static int dos_wide_path_to_key(LPCWSTR path, char* out, size_t cap)
{
  if (path == NULL || out == NULL || cap == 0 || !g_index_ready) return 0;
  out[0] = '\0';

  int raw_bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0,
                                      NULL, NULL);
  if (raw_bytes > 0 && (size_t)raw_bytes < cap) {
    WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)cap, NULL, NULL);

    char* p = out;
    if (strncmp(p, "\\\\?\\", 4) == 0 || strncmp(p, "\\??\\", 4) == 0) p += 4;
    if ((p[0] == 'Z' || p[0] == 'z') && p[1] == ':') {
      p += 2;
      while (*p == '\\' || *p == '/') ++p;
      memmove(out, "/", 1);
      memmove(out + 1, p, strlen(p) + 1);
    } else if (p != out) {
      memmove(out, p, strlen(p) + 1);
    }

    for (char* q = out; *q; ++q) {
      if (*q == '\\') *q = '/';
      else *q = ascii_lower(*q);
    }
    while (strlen(out) > 1 && out[strlen(out) - 1] == '/') {
      out[strlen(out) - 1] = '\0';
    }

    const char* cmp = out;
    if (g_mount != NULL && g_mount[0] != '/' && cmp[0] == '/') ++cmp;
    size_t ml = g_mount ? strlen(g_mount) : 0;
    if (ml != 0 && strncmp(cmp, g_mount, ml) == 0) {
      if (cmp[ml] == '\0') {
        out[0] = '\0';
        return 1;
      }
      if (cmp[ml] == '/') {
        memmove(out, cmp + ml + 1, strlen(cmp + ml + 1) + 1);
        if (out[0] != '\0') {
          InterlockedIncrement64(&g_k32_fast_path);
          return 1;
        }
      }
    }

    if (strncmp(out, "data/", 5) == 0 && out[5] != '\0') {
      memmove(out, out + 5, strlen(out + 5) + 1);
      InterlockedIncrement64(&g_k32_fast_path);
      return 1;
    }
  }

  DWORD need = GetFullPathNameW(path, 0, NULL, NULL);
  if (need == 0 || need > 32760) return 0;

  WCHAR* full = (WCHAR*)malloc(((size_t)need + 1) * sizeof(WCHAR));
  if (full == NULL) return 0;
  DWORD got = GetFullPathNameW(path, need + 1, full, NULL);
  if (got == 0 || got >= need + 1) {
    free(full);
    return 0;
  }

  int bytes = WideCharToMultiByte(CP_UTF8, 0, full, -1, NULL, 0, NULL, NULL);
  if (bytes <= 0 || (size_t)bytes >= cap) {
    free(full);
    return 0;
  }
  WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int)cap, NULL, NULL);
  free(full);

  char* p = out;
  if (strncmp(p, "\\\\?\\", 4) == 0) p += 4;
  if ((p[0] == 'Z' || p[0] == 'z') && p[1] == ':') {
    p += 2;
    while (*p == '\\' || *p == '/') ++p;
    memmove(out, "/", 1);
    memmove(out + 1, p, strlen(p) + 1);
  } else {
    if (p != out) memmove(out, p, strlen(p) + 1);
  }
  for (char* q = out; *q; ++q) {
    if (*q == '\\') *q = '/';
    else *q = ascii_lower(*q);
  }
  while (strlen(out) > 1 && out[strlen(out) - 1] == '/') {
    out[strlen(out) - 1] = '\0';
  }

  const char* cmp = out;
  if (g_mount != NULL && g_mount[0] != '/' && cmp[0] == '/') ++cmp;
  size_t ml = g_mount ? strlen(g_mount) : 0;
  if (ml == 0 || strncmp(cmp, g_mount, ml) != 0) return 0;
  if (cmp[ml] == '\0') {
    out[0] = '\0';
    return 1;
  }
  if (cmp[ml] != '/') return 0;
  memmove(out, cmp + ml + 1, strlen(cmp + ml + 1) + 1);
  InterlockedIncrement64(&g_k32_full_path);
  return out[0] != '\0';
}

/* Module-handle-sensitive: Windows registers loaded PE modules under
 * the string used to open them. Redirecting those breaks
 * GetModuleHandleW lookups, script-extender trampoline systems, and
 * compiled-script (.pex) version probes. Archives (BSA/BA2/ESP/ESM/
 * ESL) carry no path-string semantics on Linux because reads stream
 * from the resulting file handle, so they redirect freely. */
static int protected_ext_module(const char* key)
{
  const char* dot = strrchr(key, '.');
  if (dot == NULL || dot[1] == '\0') return 0;
  ++dot;
  return strcmp(dot, "dll") == 0 || strcmp(dot, "exe") == 0 ||
         strcmp(dot, "pex") == 0;
}

static int build_redirect_oa(POBJECT_ATTRIBUTES_FL src,
                             UNICODE_STRING_FL* out_us,
                             OBJECT_ATTRIBUTES_FL* out_oa,
                             const WCHAR* real_nt)
{
  size_t len = lstrlenW(real_nt);
  if (len == 0 || len >= (sizeof(t_redirect_path) / sizeof(t_redirect_path[0])))
    return 0;
  memcpy(t_redirect_path, real_nt, (len + 1) * sizeof(WCHAR));
  out_us->Buffer = t_redirect_path;
  out_us->Length = (USHORT)(len * sizeof(WCHAR));
  out_us->MaximumLength = out_us->Length;
  *out_oa = *src;
  out_oa->RootDirectory = NULL;
  out_oa->ObjectName = out_us;
  return 1;
}

static int ensure_dummy_dir(void)
{
  if (g_dummy_dir != INVALID_HANDLE_VALUE) return 1;
  g_dummy_dir = CreateFileW(L"Z:\\tmp", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (g_dummy_dir == INVALID_HANDLE_VALUE) {
    log_msg("dummy dir open failed");
    return 0;
  }
  return 1;
}

static int duplicate_dummy_dir(PHANDLE out)
{
  if (out == NULL || !ensure_dummy_dir()) return 0;
  HANDLE dup = INVALID_HANDLE_VALUE;
  if (!DuplicateHandle(GetCurrentProcess(), g_dummy_dir, GetCurrentProcess(),
                       &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return 0;
  }
  *out = dup;
  return 1;
}

static int is_write_or_create(ACCESS_MASK access, ULONG disp, int is_create)
{
  if (is_create && disp != 1u) return 1;
  return (access & (0x40000000u | 0x00010000u | 0x00000002u |
                    0x00000004u | 0x00000100u)) != 0;
}

static int register_dir_handle(HANDLE h, DirBucket* bucket)
{
  if (h == NULL || h == INVALID_HANDLE_VALUE || bucket == NULL ||
      !g_dir_lock_ready) {
    return 0;
  }
  EnterCriticalSection(&g_dir_lock);
  if (g_dir_handle_count == g_dir_handle_cap) {
    size_t next_cap = g_dir_handle_cap ? g_dir_handle_cap * 2 : 64;
    DirHandleState* next =
        (DirHandleState*)realloc(g_dir_handles,
                                 next_cap * sizeof(DirHandleState));
    if (next == NULL) {
      LeaveCriticalSection(&g_dir_lock);
      return 0;
    }
    g_dir_handles = next;
    g_dir_handle_cap = next_cap;
  }
  DirHandleState* st = &g_dir_handles[g_dir_handle_count++];
  memset(st, 0, sizeof(*st));
  st->handle = h;
  st->bucket = bucket;
  LeaveCriticalSection(&g_dir_lock);
  return 1;
}

static void unregister_dir_handle(HANDLE h)
{
  if (!g_dir_lock_ready || h == NULL || h == INVALID_HANDLE_VALUE) return;
  EnterCriticalSection(&g_dir_lock);
  for (size_t i = 0; i < g_dir_handle_count; ++i) {
    if (g_dir_handles[i].handle == h) {
      if (g_dir_handles[i].filter != NULL) free(g_dir_handles[i].filter);
      g_dir_handles[i] = g_dir_handles[g_dir_handle_count - 1];
      --g_dir_handle_count;
      break;
    }
  }
  LeaveCriticalSection(&g_dir_lock);
}

static DirHandleState* find_dir_handle_locked(HANDLE h)
{
  if (h == NULL || h == INVALID_HANDLE_VALUE) return NULL;
  for (size_t i = 0; i < g_dir_handle_count; ++i) {
    if (g_dir_handles[i].handle == h) return &g_dir_handles[i];
  }
  return NULL;
}

static WCHAR wlower(WCHAR c)
{
  return (c >= L'A' && c <= L'Z') ? (WCHAR)(c + 32) : c;
}

static int wildcard_match_w(const WCHAR* name, size_t nlen,
                            const WCHAR* pat, size_t plen)
{
  size_t i = 0, j = 0;
  size_t star_i = (size_t)-1, star_j = 0;
  while (i < nlen) {
    if (j < plen) {
      WCHAR p = pat[j];
      if (p == L'<') p = L'*';
      else if (p == L'>') p = L'?';
      else if (p == L'"') p = L'.';
      if (p == L'*') {
        star_j = j + 1;
        star_i = i;
        ++j;
        continue;
      }
      if (p == L'?' || wlower(name[i]) == p) {
        ++i;
        ++j;
        continue;
      }
    }
    if (star_i != (size_t)-1) {
      j = star_j;
      ++star_i;
      i = star_i;
      continue;
    }
    return 0;
  }
  while (j < plen) {
    WCHAR p = pat[j++];
    if (p == L'<') p = L'*';
    if (p != L'*') return 0;
  }
  return 1;
}

static int child_matches_filter(DirHandleState* st, const ChildEntry* e)
{
  if (st->filter == NULL || st->filter_len == 0 ||
      (st->filter_len == 1 && st->filter[0] == L'*')) {
    return 1;
  }
  size_t n = lstrlenW(e->name);
  return wildcard_match_w(e->name, n, st->filter, st->filter_len);
}

static size_t dir_info_header_size(ULONG info_class)
{
  switch (info_class) {
  case FL_FileDirectoryInformation:
    return offsetof(FL_FILE_DIRECTORY_INFORMATION, FileName);
  case FL_FileFullDirectoryInformation:
    return offsetof(FL_FILE_FULL_DIR_INFORMATION, FileName);
  case FL_FileBothDirectoryInformation:
    return offsetof(FL_FILE_BOTH_DIR_INFORMATION, FileName);
  case FL_FileIdBothDirectoryInformation:
    return offsetof(FL_FILE_ID_BOTH_DIR_INFORMATION, FileName);
  case FL_FileIdFullDirectoryInformation:
    return offsetof(FL_FILE_ID_FULL_DIR_INFORMATION, FileName);
  case FL_FileNamesInformation:
    return offsetof(FL_FILE_NAMES_INFORMATION, FileName);
  default:
    return 0;
  }
}

static size_t write_dir_entry(unsigned char* buf, size_t cap, size_t off,
                              ULONG info_class, const ChildEntry* e)
{
  size_t hdr = dir_info_header_size(info_class);
  if (hdr == 0) return 0;
  size_t name_chars = lstrlenW(e->name);
  size_t name_bytes = name_chars * sizeof(WCHAR);
  size_t total_raw = hdr + name_bytes;
  size_t total = (total_raw + 7u) & ~(size_t)7u;
  if (off + total_raw > cap) return 0;

  unsigned char* p = buf + off;
  memset(p, 0, total);
  if (info_class != FL_FileNamesInformation) {
    FL_FILE_DIRECTORY_INFORMATION* base =
        (FL_FILE_DIRECTORY_INFORMATION*)p;
    base->CreationTime.LowPart = e->mtime.dwLowDateTime;
    base->CreationTime.HighPart = (LONG)e->mtime.dwHighDateTime;
    base->LastAccessTime = base->CreationTime;
    base->LastWriteTime = base->CreationTime;
    base->ChangeTime = base->CreationTime;
    base->EndOfFile.QuadPart = (LONGLONG)e->size;
    base->AllocationSize.QuadPart =
        (LONGLONG)((e->size + 4095ULL) & ~4095ULL);
    base->FileAttributes = e->attrs;
    base->FileNameLength = (ULONG)name_bytes;
  } else {
    FL_FILE_NAMES_INFORMATION* base = (FL_FILE_NAMES_INFORMATION*)p;
    base->FileNameLength = (ULONG)name_bytes;
  }
  memcpy(p + hdr, e->name, name_bytes);
  return total;
}

static void patch_next(unsigned char* buf, size_t prev, ULONG delta)
{
  memcpy(buf + prev, &delta, sizeof(delta));
}

static void fill_basic_info(FL_FILE_BASIC_INFORMATION* out, ULONG attrs,
                            FILETIME mtime)
{
  memset(out, 0, sizeof(*out));
  out->CreationTime.LowPart = mtime.dwLowDateTime;
  out->CreationTime.HighPart = (LONG)mtime.dwHighDateTime;
  out->LastAccessTime = out->CreationTime;
  out->LastWriteTime = out->CreationTime;
  out->ChangeTime = out->CreationTime;
  out->FileAttributes = attrs;
}

static void fill_network_info(FL_FILE_NETWORK_OPEN_INFORMATION* out,
                              ULONG attrs, unsigned long long size,
                              FILETIME mtime)
{
  memset(out, 0, sizeof(*out));
  out->CreationTime.LowPart = mtime.dwLowDateTime;
  out->CreationTime.HighPart = (LONG)mtime.dwHighDateTime;
  out->LastAccessTime = out->CreationTime;
  out->LastWriteTime = out->CreationTime;
  out->ChangeTime = out->CreationTime;
  out->EndOfFile.QuadPart = (LONGLONG)size;
  out->AllocationSize.QuadPart = (LONGLONG)((size + 4095ULL) & ~4095ULL);
  out->FileAttributes = attrs;
}

static void fill_win32_attr_data(WIN32_FILE_ATTRIBUTE_DATA* out, ULONG attrs,
                                 unsigned long long size, FILETIME mtime)
{
  memset(out, 0, sizeof(*out));
  out->dwFileAttributes = attrs;
  out->ftCreationTime = mtime;
  out->ftLastAccessTime = mtime;
  out->ftLastWriteTime = mtime;
  out->nFileSizeHigh = (DWORD)(size >> 32);
  out->nFileSizeLow = (DWORD)(size & 0xffffffffULL);
}

static int attr_path_key(POBJECT_ATTRIBUTES_FL oa, char* key, size_t key_cap)
{
  if (oa == NULL || oa->ObjectName == NULL || oa->RootDirectory != NULL ||
      !g_index_ready) {
    return 0;
  }
  return nt_path_to_key(oa->ObjectName, key, key_cap);
}

static int query_index_attrs_key(const char* key, ULONG* attrs,
                                 unsigned long long* size, FILETIME* mtime)
{
  VfsEntry* e = lookup_entry(key);
  if (e != NULL) {
    *attrs = e->attrs;
    *size = e->size;
    *mtime = e->mtime;
    return 1;
  }

  DirBucket* b = get_dir_bucket(key, 0);
  if (b != NULL) {
    *attrs = FILE_ATTRIBUTE_DIRECTORY;
    *size = 0;
    mtime->dwLowDateTime = 0;
    mtime->dwHighDateTime = 0;
    return 1;
  }
  return 0;
}

static int should_neg_attr_miss(const char* key)
{
  static LONG cached = -1;
  LONG v = cached;
  if (v < 0) {
    char buf[16];
    DWORD n = GetEnvironmentVariableA("FLUORINE_VFS_NEG_ATTR_MISS", buf,
                                      (DWORD)sizeof(buf));
    v = (n > 0 && n < sizeof(buf) && buf[0] == '1') ? 1 : 0;
    InterlockedCompareExchange(&cached, v, -1);
    v = cached;
  }
  if (!v) return 0;
  if (key == NULL || key[0] == '\0') return 0;
  return !protected_ext_module(key);
}

/* ----- Path cache (PathCache equivalent) -----
 *
 * For paths under the VFS mount that miss our static index, the first
 * Real_NtQuery* roundtrip to Wine -> Z: -> FUSE answers definitively
 * (FUSE is authoritative for the live tree, including Overwrite
 * writes). We memoize the answer so subsequent probes for the same
 * path skip the Real_ syscall entirely. Game-agnostic — no subtree
 * carve-outs needed because we always real-probe before answering.
 *
 * Negatives are cached too. Mid-boot writes that would flip a
 * negative entry to positive are caught by attr_cache_mark_exists(),
 * called from Hook_NtCreateFile after a successful write/create —
 * attr_cache_insert() overwrites any prior entry for the same key. */

typedef struct AttrCacheEntry
{
  char* key;
  ULONG attrs;
  unsigned long long size;
  FILETIME mtime;
  unsigned char exists;
  struct AttrCacheEntry* next;
} AttrCacheEntry;

#define FL_ATTR_CACHE_BUCKETS 8192u
#define FL_ATTR_CACHE_MAX 32768u
static AttrCacheEntry** g_attr_cache = NULL;
static volatile LONG64 g_attr_cache_count = 0;
static CRITICAL_SECTION g_attr_cache_lock;
static volatile LONG g_attr_cache_ready = 0;

static unsigned long key_hash_djb2(const char* s)
{
  unsigned long h = 5381u;
  while (*s) h = (h * 33u) ^ (unsigned char)*s++;
  return h;
}

static void attr_cache_init(void)
{
  if (InterlockedExchange(&g_attr_cache_ready, 1) != 0) return;
  g_attr_cache = (AttrCacheEntry**)calloc(FL_ATTR_CACHE_BUCKETS,
                                          sizeof(AttrCacheEntry*));
  InitializeCriticalSection(&g_attr_cache_lock);
}

static int attr_cache_lookup(const char* key, ULONG* attrs,
                             unsigned long long* size, FILETIME* mtime,
                             int* exists)
{
  if (!g_attr_cache_ready || g_attr_cache == NULL || key == NULL ||
      key[0] == '\0')
    return 0;
  unsigned long h = key_hash_djb2(key) % FL_ATTR_CACHE_BUCKETS;
  EnterCriticalSection(&g_attr_cache_lock);
  for (AttrCacheEntry* e = g_attr_cache[h]; e != NULL; e = e->next) {
    if (strcmp(e->key, key) == 0) {
      *attrs = e->attrs;
      *size = e->size;
      *mtime = e->mtime;
      *exists = e->exists ? 1 : 0;
      LeaveCriticalSection(&g_attr_cache_lock);
      return 1;
    }
  }
  LeaveCriticalSection(&g_attr_cache_lock);
  return 0;
}

static void attr_cache_insert(const char* key, ULONG attrs,
                              unsigned long long size, FILETIME mtime,
                              int exists)
{
  if (!g_attr_cache_ready || g_attr_cache == NULL || key == NULL ||
      key[0] == '\0')
    return;
  if (g_attr_cache_count >= (LONG64)FL_ATTR_CACHE_MAX) {
    InterlockedIncrement64(&g_attr_cache_full);
    return;
  }
  size_t klen = strlen(key);
  unsigned long h = key_hash_djb2(key) % FL_ATTR_CACHE_BUCKETS;
  EnterCriticalSection(&g_attr_cache_lock);
  for (AttrCacheEntry* e = g_attr_cache[h]; e != NULL; e = e->next) {
    if (strcmp(e->key, key) == 0) {
      e->attrs = attrs;
      e->size = size;
      e->mtime = mtime;
      e->exists = (unsigned char)(exists ? 1 : 0);
      LeaveCriticalSection(&g_attr_cache_lock);
      return;
    }
  }
  AttrCacheEntry* ne =
      (AttrCacheEntry*)malloc(sizeof(AttrCacheEntry));
  if (ne == NULL) {
    LeaveCriticalSection(&g_attr_cache_lock);
    return;
  }
  ne->key = heap_strndup(key, klen);
  if (ne->key == NULL) {
    free(ne);
    LeaveCriticalSection(&g_attr_cache_lock);
    return;
  }
  ne->attrs = attrs;
  ne->size = size;
  ne->mtime = mtime;
  ne->exists = (unsigned char)(exists ? 1 : 0);
  ne->next = g_attr_cache[h];
  g_attr_cache[h] = ne;
  InterlockedIncrement64(&g_attr_cache_count);
  InterlockedIncrement64(&g_attr_cache_insert);
  LeaveCriticalSection(&g_attr_cache_lock);
}

/* Promote a path to "exists" after a successful create/write.
 * Uses caller-supplied attrs/size/mtime if available, otherwise
 * stamps a defensive default of FILE_ATTRIBUTE_NORMAL with zero
 * timestamp so subsequent attr probes return STATUS_SUCCESS instead
 * of stale ENOENT. */
static void attr_cache_mark_exists(const char* key)
{
  if (!g_attr_cache_ready || key == NULL || key[0] == '\0') return;
  FILETIME ft = {0, 0};
  attr_cache_insert(key, 0x00000080u /* FILE_ATTRIBUTE_NORMAL */, 0, ft,
                    1);
  InterlockedIncrement64(&g_live_insert);
}

/* ----- mmap shim -----
 *
 * Engine ReadFile loops on plugin/archive loads cost ~20-50us per
 * call in pure Wine syscall overhead, dwarfing the ~50ns memcpy that
 * actually moves the bytes. For redirected handles (which point at a
 * real backing file under the mount) we opportunistically map the
 * whole file into our process address space and serve subsequent
 * NtReadFile calls from memcpy. NtSetInformationFile/
 * NtQueryInformationFile keep the tracked cursor in sync with what
 * the engine sets explicitly.
 *
 * Skipped: files >32 MB (RAM pressure for full map; BSAs handled in
 * a later phase via header-window mapping), zero-byte files,
 * overlapped reads, write-intent opens (write paths never reach
 * mmap_attach because they bail before redirect). */

typedef struct MmapEntry
{
  HANDLE h_file;
  HANDLE h_section;
  void* base;
  LONGLONG size;
  volatile LONGLONG pos;
  struct MmapEntry* next;
} MmapEntry;

#define FL_MMAP_BUCKETS 4096u
/* Bumped to cover DXVK pipeline cache (.bin/.lut) and similar big
 * read-only files. Linux kernel handles physical page eviction; we
 * only consume virtual address space, of which 64-bit has plenty. */
#define FL_MMAP_MAX_SIZE (1024LL * 1024LL * 1024LL)

static MmapEntry** g_mmap_table = NULL;
static SRWLOCK g_mmap_lock = SRWLOCK_INIT;
static volatile LONG g_mmap_ready = 0;

static unsigned long handle_hash(HANDLE h)
{
  uintptr_t v = (uintptr_t)h;
  v ^= v >> 33;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33;
  return (unsigned long)v;
}

static void mmap_init(void)
{
  if (InterlockedExchange(&g_mmap_ready, 1) != 0) return;
  g_mmap_table =
      (MmapEntry**)calloc(FL_MMAP_BUCKETS, sizeof(MmapEntry*));
  /* SRWLOCK already statically initialized via SRWLOCK_INIT. */
}

static MmapEntry* mmap_lookup(HANDLE h)
{
  if (!g_mmap_ready || g_mmap_table == NULL || h == NULL ||
      h == INVALID_HANDLE_VALUE)
    return NULL;
  unsigned long b = handle_hash(h) % FL_MMAP_BUCKETS;
  AcquireSRWLockShared(&g_mmap_lock);
  for (MmapEntry* e = g_mmap_table[b]; e != NULL; e = e->next) {
    if (e->h_file == h) {
      ReleaseSRWLockShared(&g_mmap_lock);
      return e;
    }
  }
  ReleaseSRWLockShared(&g_mmap_lock);
  return NULL;
}

static int mmap_disabled(void)
{
  static volatile LONG cached = -1;
  LONG v = cached;
  if (v < 0) {
    char buf[16];
    DWORD n = GetEnvironmentVariableA("FLUORINE_VFS_NO_MMAP", buf,
                                      (DWORD)sizeof(buf));
    v = (n > 0 && n < sizeof(buf) && buf[0] == '1') ? 1 : 0;
    InterlockedCompareExchange(&cached, v, -1);
    v = cached;
  }
  return v;
}

static void mmap_attach(HANDLE h, const char* key)
{
  if (mmap_disabled()) return;
  if (!g_mmap_ready || h == NULL || h == INVALID_HANDLE_VALUE) return;
  /* Avoid double-attach in unlikely re-open of same kernel handle. */
  if (mmap_lookup(h) != NULL) return;

  IO_STATUS_BLOCK_FL iosb;
  FL_FILE_STANDARD_INFORMATION fsi;
  memset(&fsi, 0, sizeof(fsi));
  if (Real_NtQueryInformationFile == NULL) return;
  NTSTATUS qs =
      Real_NtQueryInformationFile(h, &iosb, &fsi, (ULONG)sizeof(fsi),
                                  FL_FileStandardInformation);
  if (qs != STATUS_SUCCESS) return;
  if (fsi.Directory) return;
  LONGLONG size = fsi.EndOfFile.QuadPart;
  if (size <= 0) {
    InterlockedIncrement64(&g_mmap_skip_zero);
    return;
  }
  if (size > FL_MMAP_MAX_SIZE) {
    InterlockedIncrement64(&g_mmap_skip_size);
    return;
  }

  HANDLE sec = CreateFileMappingW(h, NULL, PAGE_READONLY, 0, 0, NULL);
  if (sec == NULL) {
    InterlockedIncrement64(&g_mmap_map_failed);
    return;
  }
  void* base = MapViewOfFile(sec, FILE_MAP_READ, 0, 0, 0);
  if (base == NULL) {
    CloseHandle(sec);
    InterlockedIncrement64(&g_mmap_view_failed);
    return;
  }

  MmapEntry* ne = (MmapEntry*)malloc(sizeof(MmapEntry));
  if (ne == NULL) {
    UnmapViewOfFile(base);
    CloseHandle(sec);
    return;
  }
  ne->h_file = h;
  ne->h_section = sec;
  ne->base = base;
  ne->size = size;
  ne->pos = 0;
  unsigned long b = handle_hash(h) % FL_MMAP_BUCKETS;
  AcquireSRWLockExclusive(&g_mmap_lock);
  ne->next = g_mmap_table[b];
  g_mmap_table[b] = ne;
  ReleaseSRWLockExclusive(&g_mmap_lock);
  InterlockedIncrement64(&g_mmap_attached);
  if (key != NULL &&
      InterlockedCompareExchange(&g_first_mmap_logged, 1, 0) == 0) {
    log_phase("first_indexed_mmap_attach");
  }
}

static void mmap_detach(HANDLE h)
{
  if (!g_mmap_ready || g_mmap_table == NULL || h == NULL ||
      h == INVALID_HANDLE_VALUE)
    return;
  unsigned long b = handle_hash(h) % FL_MMAP_BUCKETS;
  AcquireSRWLockExclusive(&g_mmap_lock);
  MmapEntry** pp = &g_mmap_table[b];
  while (*pp != NULL) {
    if ((*pp)->h_file == h) {
      MmapEntry* victim = *pp;
      *pp = victim->next;
      ReleaseSRWLockExclusive(&g_mmap_lock);
      UnmapViewOfFile(victim->base);
      CloseHandle(victim->h_section);
      free(victim);
      InterlockedIncrement64(&g_mmap_detached);
      return;
    }
    pp = &(*pp)->next;
  }
  ReleaseSRWLockExclusive(&g_mmap_lock);
}

/* NtReadFile ByteOffset sentinels (per MSDN/ReactOS):
 * If ByteOffset is NULL or its value == FILE_USE_FILE_POINTER_POSITION
 * (-2 == 0xFFFFFFFFFFFFFFFE), reads from current file pointer. */
static int byte_offset_use_pointer(PLARGE_INTEGER bo)
{
  if (bo == NULL) return 1;
  if (bo->QuadPart == (LONGLONG)0xFFFFFFFFFFFFFFFEULL) return 1;
  if (bo->QuadPart < 0) return 1;
  return 0;
}

static NTSTATUS NTAPI Hook_NtReadFile(HANDLE h, HANDLE event,
                                       PIO_APC_ROUTINE_FL apc, PVOID apc_ctx,
                                       PIO_STATUS_BLOCK_FL iosb,
                                       PVOID buf, ULONG len,
                                       PLARGE_INTEGER bo, PULONG key_arg)
{
  /* Pass overlapped reads through; engine plugin loads are sync. */
  if (event != NULL || apc != NULL || apc_ctx != NULL || buf == NULL) {
    return Real_NtReadFile(h, event, apc, apc_ctx, iosb, buf, len, bo,
                           key_arg);
  }
  MmapEntry* e = mmap_lookup(h);
  if (e == NULL) {
    return Real_NtReadFile(h, event, apc, apc_ctx, iosb, buf, len, bo,
                           key_arg);
  }
  LONGLONG start;
  int advance_pos;
  if (byte_offset_use_pointer(bo)) {
    start = e->pos;
    advance_pos = 1;
  } else {
    start = bo->QuadPart;
    advance_pos = 0;
  }
  if (start >= e->size) {
    /* File may have grown through this handle since we mapped it.
     * Sync Wine's kernel-side file pointer to our tracked position so
     * the fall-through Real_NtReadFile (which uses bo or the kernel
     * pointer) reads from the correct offset. Without this sync, our
     * mmap-served reads advance e->pos but leave the kernel's pointer
     * stale at the position of the last NtSetInformationFile call,
     * and the fall-through reads garbage. */
    if (advance_pos && Real_NtSetInformationFile != NULL) {
      FL_FILE_POSITION_INFORMATION pi;
      pi.CurrentByteOffset.QuadPart = e->pos;
      IO_STATUS_BLOCK_FL temp_iosb;
      Real_NtSetInformationFile(h, &temp_iosb, &pi,
                                (ULONG)sizeof(pi),
                                FL_FilePositionInformation);
    }
    InterlockedIncrement64(&g_mmap_passthru);
    return Real_NtReadFile(h, event, apc, apc_ctx, iosb, buf, len, bo,
                           key_arg);
  }
  LONGLONG remain = e->size - start;
  ULONG to_copy = (remain < (LONGLONG)len) ? (ULONG)remain : len;
  memcpy(buf, (const char*)e->base + start, to_copy);
  if (advance_pos) {
    e->pos = start + (LONGLONG)to_copy;
  }
  if (iosb != NULL) {
    iosb->u.Status = STATUS_SUCCESS;
    iosb->Information = to_copy;
  }
  InterlockedIncrement64(&g_mmap_reads);
  InterlockedAdd64(&g_mmap_bytes, (LONG64)to_copy);
  return STATUS_SUCCESS;
}

static NTSTATUS NTAPI Hook_NtSetInformationFile(HANDLE h,
                                                PIO_STATUS_BLOCK_FL iosb,
                                                PVOID info, ULONG len,
                                                ULONG info_class)
{
  NTSTATUS rs = Real_NtSetInformationFile(h, iosb, info, len, info_class);
  if (rs == STATUS_SUCCESS && info_class == FL_FilePositionInformation &&
      info != NULL && len >= sizeof(FL_FILE_POSITION_INFORMATION)) {
    MmapEntry* e = mmap_lookup(h);
    if (e != NULL) {
      FL_FILE_POSITION_INFORMATION* pi =
          (FL_FILE_POSITION_INFORMATION*)info;
      e->pos = pi->CurrentByteOffset.QuadPart;
    }
  }
  return rs;
}

static NTSTATUS NTAPI Hook_NtQueryInformationFile(HANDLE h,
                                                  PIO_STATUS_BLOCK_FL iosb,
                                                  PVOID info, ULONG len,
                                                  ULONG info_class)
{
  /* Most callers use this for size/position. Serve from our state to
   * avoid a redundant kernel transition when we have it. */
  MmapEntry* e = mmap_lookup(h);
  if (e != NULL && info != NULL) {
    if (info_class == FL_FileStandardInformation &&
        len >= sizeof(FL_FILE_STANDARD_INFORMATION)) {
      FL_FILE_STANDARD_INFORMATION* fsi =
          (FL_FILE_STANDARD_INFORMATION*)info;
      memset(fsi, 0, sizeof(*fsi));
      fsi->EndOfFile.QuadPart = e->size;
      fsi->AllocationSize.QuadPart =
          (e->size + 4095LL) & ~4095LL;
      fsi->NumberOfLinks = 1;
      if (iosb != NULL) {
        iosb->u.Status = STATUS_SUCCESS;
        iosb->Information = sizeof(*fsi);
      }
      return STATUS_SUCCESS;
    }
    if (info_class == FL_FilePositionInformation &&
        len >= sizeof(FL_FILE_POSITION_INFORMATION)) {
      FL_FILE_POSITION_INFORMATION* pi =
          (FL_FILE_POSITION_INFORMATION*)info;
      pi->CurrentByteOffset.QuadPart = e->pos;
      if (iosb != NULL) {
        iosb->u.Status = STATUS_SUCCESS;
        iosb->Information = sizeof(*pi);
      }
      return STATUS_SUCCESS;
    }
  }
  return Real_NtQueryInformationFile(h, iosb, info, len, info_class);
}

static NTSTATUS serve_dir_query_locked(DirHandleState* st, PVOID info,
                                       ULONG len, ULONG info_class,
                                       int restart, int single,
                                       PUNICODE_STRING_FL file_name,
                                       PIO_STATUS_BLOCK_FL iosb)
{
  if (iosb == NULL) return STATUS_INFO_LENGTH_MISMATCH;
  if (restart) {
    st->cursor = 0;
    st->filter_set = 0;
    if (st->filter != NULL) {
      free(st->filter);
      st->filter = NULL;
    }
    st->filter_len = 0;
  }
  if (!st->filter_set) {
    if (file_name != NULL && file_name->Buffer != NULL &&
        file_name->Length > 0) {
      st->filter_len = file_name->Length / sizeof(WCHAR);
      st->filter =
          (WCHAR*)malloc((st->filter_len + 1) * sizeof(WCHAR));
      if (st->filter != NULL) {
        for (size_t i = 0; i < st->filter_len; ++i) {
          st->filter[i] = wlower(file_name->Buffer[i]);
        }
        st->filter[st->filter_len] = 0;
      } else {
        st->filter_len = 0;
      }
    }
    st->filter_set = 1;
  }

  iosb->u.Status = STATUS_SUCCESS;
  iosb->Information = 0;
  if (info == NULL || len < 16) {
    iosb->u.Status = STATUS_INFO_LENGTH_MISMATCH;
    return STATUS_INFO_LENGTH_MISMATCH;
  }

  unsigned char* out = (unsigned char*)info;
  size_t off = 0;
  size_t prev = (size_t)-1;
  size_t emitted = 0;
  while (st->cursor < st->bucket->child_count) {
    ChildEntry* e = &st->bucket->children[st->cursor];
    if (!child_matches_filter(st, e)) {
      ++st->cursor;
      continue;
    }
    size_t wrote = write_dir_entry(out, len, off, info_class, e);
    if (wrote == 0) {
      if (emitted > 0) break;
      iosb->u.Status = STATUS_BUFFER_OVERFLOW;
      return STATUS_BUFFER_OVERFLOW;
    }
    if (prev != (size_t)-1) patch_next(out, prev, (ULONG)(off - prev));
    prev = off;
    off += wrote;
    ++emitted;
    ++st->cursor;
    if (single) break;
  }

  if (emitted == 0) {
    iosb->u.Status = STATUS_NO_MORE_FILES;
    iosb->Information = 0;
    InterlockedIncrement64(&g_dir_query_empty);
    return STATUS_NO_MORE_FILES;
  }
  iosb->u.Status = STATUS_SUCCESS;
  iosb->Information = off;
  InterlockedIncrement64(&g_dir_query_served);
  return STATUS_SUCCESS;
}

static int should_skip_redirect(ACCESS_MASK access, ULONG disp, ULONG opts,
                                int is_create)
{
  if (t_inside_redirect > 0) return 1;
  if (!g_index_ready) return 1;
  if (is_create && disp != 1u) return 1;
  if (opts & 0x00000001u) return 1; /* FILE_DIRECTORY_FILE */
  if (access & (0x40000000u | 0x10000000u | 0x00010000u |
                0x00000002u | 0x00000004u | 0x00000010u |
                0x00000100u)) {
    return 1;
  }
  return 0;
}

static NTSTATUS NTAPI Hook_NtCreateFile(PHANDLE out, ACCESS_MASK access,
                                        POBJECT_ATTRIBUTES_FL oa,
                                        PIO_STATUS_BLOCK_FL iosb,
                                        PLARGE_INTEGER alloc_sz,
                                        ULONG file_attrs, ULONG share,
                                        ULONG disp, ULONG opts, PVOID ea,
                                        ULONG ea_len)
{
  InterlockedIncrement64(&g_nt_create);
  /* Mount-scope gate: anything outside the FUSE mount is invisible to the
   * bridge. Pass through immediately, no normalization, no cache. */
  if (g_index_ready && t_inside_redirect == 0 && oa != NULL &&
      oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      !us_under_mount(oa->ObjectName)) {
    return Real_NtCreateFile(out, access, oa, iosb, alloc_sz, file_attrs,
                             share, disp, opts, ea, ea_len);
  }
  char key[2048];
  key[0] = '\0';
  int have_key = 0;
  int write_intent = 0;
  if (oa != NULL && oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      t_inside_redirect == 0 && g_index_ready) {
    if (nt_path_to_key(oa->ObjectName, key, sizeof(key))) {
      have_key = 1;
      if (InterlockedCompareExchange(&g_first_create_logged, 1, 0) == 0) {
        log_phase("first_vfs_ntcreatefile");
      }
      write_intent = is_write_or_create(access, disp, 1);
      if (!write_intent) {
        DirBucket* dir = get_dir_bucket(key, 0);
        if (dir != NULL && duplicate_dummy_dir(out)) {
          if (iosb != NULL) {
            iosb->u.Status = STATUS_SUCCESS;
            iosb->Information = 1;
          }
          register_dir_handle(out ? *out : INVALID_HANDLE_VALUE, dir);
          InterlockedIncrement64(&g_redirect_dir);
          return STATUS_SUCCESS;
        }
      }

      if (!should_skip_redirect(access, disp, opts, 1) &&
          !protected_ext_module(key)) {
        VfsEntry* e = lookup_entry(key);
        if (e != NULL && e->real_nt != NULL) {
          UNICODE_STRING_FL new_us;
          OBJECT_ATTRIBUTES_FL new_oa;
          if (build_redirect_oa(oa, &new_us, &new_oa, e->real_nt)) {
            ++t_inside_redirect;
            NTSTATUS st = Real_NtCreateFile(out, access, &new_oa, iosb,
                                            alloc_sz, file_attrs, share, disp,
                                            opts, ea, ea_len);
            --t_inside_redirect;
            InterlockedIncrement64(&g_redirect_file);
            if (st == STATUS_SUCCESS && out != NULL && *out != NULL &&
                *out != INVALID_HANDLE_VALUE) {
              mmap_attach(*out, key);
            }
            return st;
          }
        } else if (e == NULL) {
          /* Index miss. Only return STATUS_OBJECT_NAME_NOT_FOUND when
           * attr_cache has a *confirmed* negative for this key — that
           * is, a previous Real_NtQueryAttributesFile probe returned
           * ENOENT. A pure cache miss falls through to Real_, because
           * the bridge index is not authoritative: backing-relative
           * entries can carry real_nt=NULL, runtime-created files
           * never reach the index, and other-process writes bypass
           * our hooks entirely. Mirrors try_kernel32_attr_hit. */
          InterlockedIncrement64(&g_redirect_miss);
          ULONG cattrs = 0;
          unsigned long long csize = 0;
          FILETIME cmtime;
          int cexists = 0;
          if (attr_cache_lookup(key, &cattrs, &csize, &cmtime, &cexists)
              && !cexists) {
            if (iosb != NULL) {
              iosb->u.Status = STATUS_OBJECT_NAME_NOT_FOUND;
              iosb->Information = 0;
            }
            InterlockedIncrement64(&g_open_neg);
            return STATUS_OBJECT_NAME_NOT_FOUND;
          }
        }
        /* else: entry in index but real_nt==NULL (backing file). Fall
         * through to Real_NtCreateFile so Wine + FUSE serve it. */
      } else {
        InterlockedIncrement64(&g_redirect_skip);
      }
    } else {
      InterlockedIncrement64(&g_redirect_skip);
    }
  }
  NTSTATUS rs = Real_NtCreateFile(out, access, oa, iosb, alloc_sz,
                                  file_attrs, share, disp, opts, ea, ea_len);
  int rs_write_intent = is_write_or_create(access, disp, 1);
  if (have_key && rs_write_intent && rs == STATUS_SUCCESS) {
    attr_cache_mark_exists(key);
  }
  if (rs == STATUS_SUCCESS && out != NULL && *out != NULL &&
      *out != INVALID_HANDLE_VALUE && !rs_write_intent &&
      !(opts & 0x00000001u /* FILE_DIRECTORY_FILE */)) {
    mmap_attach(*out, have_key ? key : NULL);
  }
  return rs;
}

static NTSTATUS NTAPI Hook_NtOpenFile(PHANDLE out, ACCESS_MASK access,
                                      POBJECT_ATTRIBUTES_FL oa,
                                      PIO_STATUS_BLOCK_FL iosb, ULONG share,
                                      ULONG opts)
{
  InterlockedIncrement64(&g_nt_open);
  if (g_index_ready && t_inside_redirect == 0 && oa != NULL &&
      oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      !us_under_mount(oa->ObjectName)) {
    return Real_NtOpenFile(out, access, oa, iosb, share, opts);
  }
  char ofkey[2048];
  ofkey[0] = '\0';
  int of_have_key = 0;
  int of_write_intent = is_write_or_create(access, 1u, 0);
  if (oa != NULL && oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      t_inside_redirect == 0 && g_index_ready) {
    char* key = ofkey;
    if (nt_path_to_key(oa->ObjectName, key, sizeof(ofkey))) {
      of_have_key = 1;
      if (InterlockedCompareExchange(&g_first_open_logged, 1, 0) == 0) {
        log_phase("first_vfs_ntopenfile");
      }
      if (!of_write_intent) {
        DirBucket* dir = get_dir_bucket(key, 0);
        if (dir != NULL && duplicate_dummy_dir(out)) {
          if (iosb != NULL) {
            iosb->u.Status = STATUS_SUCCESS;
            iosb->Information = 1;
          }
          register_dir_handle(out ? *out : INVALID_HANDLE_VALUE, dir);
          InterlockedIncrement64(&g_redirect_dir);
          return STATUS_SUCCESS;
        }
      }

      if (!should_skip_redirect(access, 1u, opts, 0) &&
          !protected_ext_module(key)) {
        VfsEntry* e = lookup_entry(key);
        if (e != NULL && e->real_nt != NULL) {
          UNICODE_STRING_FL new_us;
          OBJECT_ATTRIBUTES_FL new_oa;
          if (build_redirect_oa(oa, &new_us, &new_oa, e->real_nt)) {
            ++t_inside_redirect;
            NTSTATUS st =
                Real_NtOpenFile(out, access, &new_oa, iosb, share, opts);
            --t_inside_redirect;
            InterlockedIncrement64(&g_redirect_file);
            if (st == STATUS_SUCCESS && out != NULL && *out != NULL &&
                *out != INVALID_HANDLE_VALUE) {
              mmap_attach(*out, key);
            }
            return st;
          }
        } else if (e == NULL) {
          /* Index miss — only ENOENT on confirmed-negative attr_cache.
           * See Hook_NtCreateFile for the full rationale. */
          InterlockedIncrement64(&g_redirect_miss);
          ULONG cattrs = 0;
          unsigned long long csize = 0;
          FILETIME cmtime;
          int cexists = 0;
          if (attr_cache_lookup(key, &cattrs, &csize, &cmtime, &cexists)
              && !cexists) {
            if (iosb != NULL) {
              iosb->u.Status = STATUS_OBJECT_NAME_NOT_FOUND;
              iosb->Information = 0;
            }
            InterlockedIncrement64(&g_open_neg);
            return STATUS_OBJECT_NAME_NOT_FOUND;
          }
        }
        /* else: entry in index, real_nt==NULL (backing file). Fall
         * through to Real_NtOpenFile. */
      } else {
        InterlockedIncrement64(&g_redirect_skip);
      }
    } else {
      InterlockedIncrement64(&g_redirect_skip);
    }
  }
  NTSTATUS rs = Real_NtOpenFile(out, access, oa, iosb, share, opts);
  if (of_have_key && of_write_intent && rs == STATUS_SUCCESS) {
    attr_cache_mark_exists(ofkey);
  }
  if (rs == STATUS_SUCCESS && out != NULL && *out != NULL &&
      *out != INVALID_HANDLE_VALUE && !of_write_intent &&
      !(opts & 0x00000001u /* FILE_DIRECTORY_FILE */)) {
    mmap_attach(*out, of_have_key ? ofkey : NULL);
  }
  return rs;
}

static NTSTATUS NTAPI Hook_NtClose(HANDLE h)
{
  InterlockedIncrement64(&g_nt_close);
  unregister_dir_handle(h);
  mmap_detach(h);
  return Real_NtClose(h);
}

static NTSTATUS NTAPI Hook_NtQueryDirectoryFile(
    HANDLE h, HANDLE ev, PIO_APC_ROUTINE_FL apc, PVOID apc_ctx,
    PIO_STATUS_BLOCK_FL iosb, PVOID info, ULONG len, ULONG info_class,
    BOOLEAN single, PUNICODE_STRING_FL file_name, BOOLEAN restart)
{
  InterlockedIncrement64(&g_nt_qdf);
  if (g_dir_lock_ready) {
    EnterCriticalSection(&g_dir_lock);
    DirHandleState* st = find_dir_handle_locked(h);
    if (st != NULL) {
      NTSTATUS r = serve_dir_query_locked(st, info, len, info_class,
                                          restart ? 1 : 0,
                                          single ? 1 : 0, file_name, iosb);
      LeaveCriticalSection(&g_dir_lock);
      return r;
    }
    LeaveCriticalSection(&g_dir_lock);
  }
  InterlockedIncrement64(&g_dir_query_bypass);
  return Real_NtQueryDirectoryFile(h, ev, apc, apc_ctx, iosb, info, len,
                                   info_class, single, file_name, restart);
}

static NTSTATUS NTAPI Hook_NtQueryDirectoryFileEx(
    HANDLE h, HANDLE ev, PIO_APC_ROUTINE_FL apc, PVOID apc_ctx,
    PIO_STATUS_BLOCK_FL iosb, PVOID info, ULONG len, ULONG info_class,
    ULONG query_flags, PUNICODE_STRING_FL file_name)
{
  InterlockedIncrement64(&g_nt_qdfex);
  if (g_dir_lock_ready) {
    EnterCriticalSection(&g_dir_lock);
    DirHandleState* st = find_dir_handle_locked(h);
    if (st != NULL) {
      NTSTATUS r = serve_dir_query_locked(
          st, info, len, info_class, (query_flags & FL_SL_RESTART_SCAN) != 0,
          (query_flags & FL_SL_RETURN_SINGLE_ENTRY) != 0, file_name, iosb);
      LeaveCriticalSection(&g_dir_lock);
      return r;
    }
    LeaveCriticalSection(&g_dir_lock);
  }
  InterlockedIncrement64(&g_dir_query_bypass);
  return Real_NtQueryDirectoryFileEx(h, ev, apc, apc_ctx, iosb, info, len,
                                     info_class, query_flags, file_name);
}

static NTSTATUS NTAPI Hook_NtQueryAttributesFile(POBJECT_ATTRIBUTES_FL oa,
                                                 PVOID info)
{
  InterlockedIncrement64(&g_nt_qattr);
  if (g_index_ready && t_inside_redirect == 0 && oa != NULL &&
      oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      !us_under_mount(oa->ObjectName)) {
    return Real_NtQueryAttributesFile(oa, info);
  }
  if (info != NULL && t_inside_redirect == 0) {
    ULONG attrs = 0;
    unsigned long long size = 0;
    FILETIME mtime;
    char key[2048];
    key[0] = '\0';
    if (attr_path_key(oa, key, sizeof(key)) &&
        query_index_attrs_key(key, &attrs, &size, &mtime)) {
      (void)size;
      fill_basic_info((FL_FILE_BASIC_INFORMATION*)info, attrs, mtime);
      InterlockedIncrement64(&g_attr_hit);
      return STATUS_SUCCESS;
    }
    if (key[0] != '\0') {
      ULONG cattrs = 0;
      unsigned long long csize = 0;
      FILETIME cmtime;
      int cexists = 0;
      if (attr_cache_lookup(key, &cattrs, &csize, &cmtime, &cexists)) {
        InterlockedIncrement64(&g_attr_cache_hit);
        if (cexists) {
          fill_basic_info((FL_FILE_BASIC_INFORMATION*)info, cattrs,
                          cmtime);
          return STATUS_SUCCESS;
        }
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      if (should_neg_attr_miss(key)) {
        InterlockedIncrement64(&g_attr_miss);
        InterlockedIncrement64(&g_attr_neg);
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      InterlockedIncrement64(&g_attr_passthru);
      NTSTATUS rs = Real_NtQueryAttributesFile(oa, info);
      if (rs == STATUS_SUCCESS) {
        FL_FILE_BASIC_INFORMATION* bi =
            (FL_FILE_BASIC_INFORMATION*)info;
        FILETIME ft;
        ft.dwLowDateTime = (DWORD)bi->LastWriteTime.LowPart;
        ft.dwHighDateTime = (DWORD)bi->LastWriteTime.HighPart;
        attr_cache_insert(key, bi->FileAttributes, 0, ft, 1);
      } else if (rs == STATUS_OBJECT_NAME_NOT_FOUND) {
        FILETIME zft = {0, 0};
        attr_cache_insert(key, 0, 0, zft, 0);
      }
      return rs;
    }
    InterlockedIncrement64(&g_attr_passthru);
  }
  return Real_NtQueryAttributesFile(oa, info);
}

static NTSTATUS NTAPI Hook_NtQueryFullAttributesFile(POBJECT_ATTRIBUTES_FL oa,
                                                     PVOID info)
{
  InterlockedIncrement64(&g_nt_qfullattr);
  if (g_index_ready && t_inside_redirect == 0 && oa != NULL &&
      oa->ObjectName != NULL && oa->RootDirectory == NULL &&
      !us_under_mount(oa->ObjectName)) {
    return Real_NtQueryFullAttributesFile(oa, info);
  }
  if (info != NULL && t_inside_redirect == 0) {
    ULONG attrs = 0;
    unsigned long long size = 0;
    FILETIME mtime;
    char key[2048];
    key[0] = '\0';
    if (attr_path_key(oa, key, sizeof(key)) &&
        query_index_attrs_key(key, &attrs, &size, &mtime)) {
      fill_network_info((FL_FILE_NETWORK_OPEN_INFORMATION*)info, attrs, size,
                        mtime);
      InterlockedIncrement64(&g_attr_hit);
      return STATUS_SUCCESS;
    }
    if (key[0] != '\0') {
      ULONG cattrs = 0;
      unsigned long long csize = 0;
      FILETIME cmtime;
      int cexists = 0;
      if (attr_cache_lookup(key, &cattrs, &csize, &cmtime, &cexists)) {
        InterlockedIncrement64(&g_attr_cache_hit);
        if (cexists) {
          fill_network_info(
              (FL_FILE_NETWORK_OPEN_INFORMATION*)info, cattrs, csize,
              cmtime);
          return STATUS_SUCCESS;
        }
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      if (should_neg_attr_miss(key)) {
        InterlockedIncrement64(&g_attr_miss);
        InterlockedIncrement64(&g_attr_neg);
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      InterlockedIncrement64(&g_attr_passthru);
      NTSTATUS rs = Real_NtQueryFullAttributesFile(oa, info);
      if (rs == STATUS_SUCCESS) {
        FL_FILE_NETWORK_OPEN_INFORMATION* ni =
            (FL_FILE_NETWORK_OPEN_INFORMATION*)info;
        FILETIME ft;
        ft.dwLowDateTime = (DWORD)ni->LastWriteTime.LowPart;
        ft.dwHighDateTime = (DWORD)ni->LastWriteTime.HighPart;
        attr_cache_insert(key, ni->FileAttributes,
                          (unsigned long long)ni->EndOfFile.QuadPart, ft,
                          1);
      } else if (rs == STATUS_OBJECT_NAME_NOT_FOUND) {
        FILETIME zft = {0, 0};
        attr_cache_insert(key, 0, 0, zft, 0);
      }
      return rs;
    }
    InterlockedIncrement64(&g_attr_passthru);
  }
  return Real_NtQueryFullAttributesFile(oa, info);
}

static int try_kernel32_attr_hit(LPCWSTR path, GET_FILEEX_INFO_LEVELS level,
                                 LPVOID info)
{
  if (info == NULL || level != GetFileExInfoStandard ||
      t_inside_redirect != 0 || !g_index_ready) {
    return 0;
  }

  char key[2048];
  key[0] = '\0';
  if (!dos_wide_path_to_key(path, key, sizeof(key)) || key[0] == '\0') {
    return 0;
  }

  ULONG attrs = 0;
  unsigned long long size = 0;
  FILETIME mtime;
  if (query_index_attrs_key(key, &attrs, &size, &mtime)) {
    fill_win32_attr_data((WIN32_FILE_ATTRIBUTE_DATA*)info, attrs, size,
                         mtime);
    InterlockedIncrement64(&g_k32_attr_hit);
    return 1;
  }

  ULONG cattrs = 0;
  unsigned long long csize = 0;
  FILETIME cmtime;
  int cexists = 0;
  if (attr_cache_lookup(key, &cattrs, &csize, &cmtime, &cexists)) {
    InterlockedIncrement64(&g_k32_cache_hit);
    if (cexists) {
      fill_win32_attr_data((WIN32_FILE_ATTRIBUTE_DATA*)info, cattrs, csize,
                           cmtime);
      return 1;
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    InterlockedIncrement64(&g_k32_neg);
    return -1;
  }

  return 0;
}

static BOOL WINAPI Hook_GetFileAttributesExW(LPCWSTR path,
                                             GET_FILEEX_INFO_LEVELS level,
                                             LPVOID info)
{
  InterlockedIncrement64(&g_k32_gfaexw);
  if (g_index_ready && t_inside_redirect == 0 && path != NULL &&
      !wide_under_mount(path)) {
    InterlockedIncrement64(&g_k32_passthru);
    return Real_GetFileAttributesExW(path, level, info);
  }
  int hit = try_kernel32_attr_hit(path, level, info);
  if (hit > 0) return TRUE;
  if (hit < 0) return FALSE;
  InterlockedIncrement64(&g_k32_passthru);
  return Real_GetFileAttributesExW(path, level, info);
}

static BOOL WINAPI Hook_GetFileAttributesExA(LPCSTR path,
                                             GET_FILEEX_INFO_LEVELS level,
                                             LPVOID info)
{
  InterlockedIncrement64(&g_k32_gfaexa);
  if (g_index_ready && t_inside_redirect == 0 && path != NULL &&
      !ansi_under_mount(path)) {
    InterlockedIncrement64(&g_k32_passthru);
    return Real_GetFileAttributesExA(path, level, info);
  }
  if (path != NULL && info != NULL && level == GetFileExInfoStandard &&
      t_inside_redirect == 0 && g_index_ready) {
    int wlen = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    if (wlen > 0 && wlen < 32760) {
      WCHAR* wpath = (WCHAR*)malloc((size_t)wlen * sizeof(WCHAR));
      if (wpath != NULL) {
        MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, wlen);
        int hit = try_kernel32_attr_hit(wpath, level, info);
        free(wpath);
        if (hit > 0) return TRUE;
        if (hit < 0) return FALSE;
      }
    }
  }
  InterlockedIncrement64(&g_k32_passthru);
  return Real_GetFileAttributesExA(path, level, info);
}

static int hook_api(const wchar_t* module, const char* name, void* hook,
                    void** real, int required)
{
  MH_STATUS st = MH_CreateHookApi(module, name, hook, real);
  log_hook_status(name, st);
  if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED) {
    if (!required) return 1;
    return 0;
  }
  return 1;
}

static DWORD WINAPI hook_worker(void* unused)
{
  (void)unused;
  g_phase_start_tick = GetTickCount64();
  log_phase("hook_worker_entry");
  if (!load_index()) {
    log_msg("ntdll hooks skipped: index unavailable");
    return 0;
  }
  log_phase("index_loaded");
  InitializeCriticalSection(&g_dir_lock);
  g_dir_lock_ready = 1;
  attr_cache_init();
  mmap_init();
  ensure_dummy_dir();

  MH_STATUS st = MH_Initialize();
  if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
    log_msg("MinHook init failed");
    return 0;
  }

  int ok = 1;
  ok &= hook_api(L"ntdll.dll", "NtCreateFile", Hook_NtCreateFile,
                 (void**)&Real_NtCreateFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtOpenFile", Hook_NtOpenFile,
                 (void**)&Real_NtOpenFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtClose", Hook_NtClose,
                 (void**)&Real_NtClose, 1);
  ok &= hook_api(L"ntdll.dll", "NtQueryDirectoryFile",
                 Hook_NtQueryDirectoryFile,
                 (void**)&Real_NtQueryDirectoryFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtQueryDirectoryFileEx",
                 Hook_NtQueryDirectoryFileEx,
                 (void**)&Real_NtQueryDirectoryFileEx, 0);
  ok &= hook_api(L"ntdll.dll", "NtQueryAttributesFile",
                 Hook_NtQueryAttributesFile,
                 (void**)&Real_NtQueryAttributesFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtQueryFullAttributesFile",
                 Hook_NtQueryFullAttributesFile,
                 (void**)&Real_NtQueryFullAttributesFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtReadFile", Hook_NtReadFile,
                 (void**)&Real_NtReadFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtSetInformationFile",
                 Hook_NtSetInformationFile,
                 (void**)&Real_NtSetInformationFile, 1);
  ok &= hook_api(L"ntdll.dll", "NtQueryInformationFile",
                 Hook_NtQueryInformationFile,
                 (void**)&Real_NtQueryInformationFile, 1);
  ok &= hook_api(L"kernel32.dll", "GetFileAttributesExW",
                 Hook_GetFileAttributesExW,
                 (void**)&Real_GetFileAttributesExW, 0);
  ok &= hook_api(L"kernel32.dll", "GetFileAttributesExA",
                 Hook_GetFileAttributesExA,
                 (void**)&Real_GetFileAttributesExA, 0);
  if (!ok) {
    log_msg("ntdll hook create failed");
    return 0;
  }

  st = MH_EnableHook(MH_ALL_HOOKS);
  if (st != MH_OK) {
    log_msg("ntdll hook enable failed");
    return 0;
  }
  log_phase("hooks_enabled");

  InterlockedExchange(&g_installed, 1);
  log_msg("ntdll hooks installed (file+dir+attr redirect)");
  start_stats_worker();
  return 0;
}

void fluorine_vfs_start_hooks(void)
{
  if (InterlockedExchange(&g_started, 1) != 0) return;
  HANDLE th = CreateThread(NULL, 0, hook_worker, NULL, 0, NULL);
  if (th != NULL) CloseHandle(th);
}

void fluorine_vfs_report_hooks(void)
{
  if (g_installed) log_stats();
}
