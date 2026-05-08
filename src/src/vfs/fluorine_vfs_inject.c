// PE-side VFS injector for Wine/Proton.
//
// Loaded into PE game processes via Wine's AppInit_DLLs registry mechanism
// when available, or through the game-directory hid.dll proxy fallback.  The
// hook implementation short-circuits selected NTDLL file, directory, read, and
// metadata calls against Fluorine's exported VFS bridge index.
//
// Design constraints:
//   - Hot path runs in every PE process spawned in the prefix.
//     Keep DllMain cheap; bail early in known non-game host procs.
//   - AppInit_DLLs DLLs are loaded *from inside user32's DllMain*, so
//     the Windows loader lock is held when our DllMain runs.  Calling
//     into user32 (e.g. wsprintfA) or any other DLL whose own DllMain
//     hasn't run yet causes ShellExecuteEx-style "General failure"
//     across every PE process in the prefix.  Stick to kernel32-only
//     APIs and inline-format anything we need to print.
//   - AppInit_DLLs DLLs also load before MSVCRT stdio is initialized.
//     Use Win32 (GetEnvironmentVariableA / WriteFile / GetStdHandle)
//     instead of fprintf/getenv so we don't crash on uninitialized
//     CRT state in early-stage processes.
//   - Talk to Fluorine via env vars set by ProtonLauncher
//     (FLUORINE_VFS_INDEX, FLUORINE_VFS_DATA_DIR, FLUORINE_VFS_MOUNT).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void fluorine_vfs_start_hooks(void);
void fluorine_vfs_report_hooks(void);

static int ascii_eq_ci(const char* a, const char* b)
{
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return 0;
    ++a; ++b;
  }
  return *a == '\0' && *b == '\0';
}

static int env_truthy(const char* name)
{
  char buf[16];
  DWORD n = GetEnvironmentVariableA(name, buf, (DWORD)sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return 0;
  if (buf[0] == '0' && buf[1] == '\0') return 0;
  if (ascii_eq_ci(buf, "false")) return 0;
  if (ascii_eq_ci(buf, "no")) return 0;
  return 1;
}

static DWORD env_get(const char* name, char* out, DWORD cap)
{
  DWORD n = GetEnvironmentVariableA(name, out, cap);
  if (n == 0 || n >= cap) {
    if (cap > 0) out[0] = '\0';
    return 0;
  }
  return n;
}

static const char* basename_of(const char* path)
{
  const char* p = path;
  const char* last = path;
  while (*p) {
    if (*p == '\\' || *p == '/') last = p + 1;
    ++p;
  }
  return last;
}

// Skip non-game host processes that fork/exec dozens of times during a
// Wine/Proton session.  Hooking them costs more than it saves and
// occasionally breaks pipes (services.exe spawns PE helpers inline).
static int is_skipped_host_proc(const char* exe_basename)
{
  static const char* const skip[] = {
      "services.exe", "plugplay.exe", "winedevice.exe", "rpcss.exe",
      "explorer.exe", "svchost.exe",  "conhost.exe",   "wineboot.exe",
      "winemenubuilder.exe", "tabtip.exe", "start.exe", "regedit.exe",
      "rundll32.exe", "msiexec.exe",
      NULL,
  };
  for (size_t i = 0; skip[i] != NULL; ++i) {
    if (ascii_eq_ci(exe_basename, skip[i])) return 1;
  }
  return 0;
}

static void log_line(const char* buf, DWORD len)
{
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  if (h != NULL && h != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    WriteFile(h, buf, len, &wrote, NULL);
  }

  // Stderr can disappear under some Proton launch paths.  Keep this
  // kernel32-only fallback so Phase 1 can distinguish "not loaded" from
  // "loaded, but the launch log swallowed stderr".
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

static DWORD copy_str(char* dst, DWORD dst_pos, DWORD dst_cap, const char* src)
{
  while (*src && dst_pos + 1 < dst_cap) dst[dst_pos++] = *src++;
  return dst_pos;
}

#ifdef FLUORINE_VFS_PROXY_HID
static HMODULE real_hid(void)
{
  static HMODULE mod = NULL;
  if (mod == NULL) {
    mod = LoadLibraryA("C:\\windows\\system32\\hid.dll");
  }
  return mod;
}

static FARPROC real_hid_proc(const char* name)
{
  HMODULE mod = real_hid();
  if (mod == NULL) return NULL;
  return GetProcAddress(mod, name);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_FreePreparsedData(void* preparsed)
{
  typedef BOOLEAN(WINAPI * Fn)(void*);
  Fn fn = (Fn)real_hid_proc("HidD_FreePreparsedData");
  if (fn == NULL) return FALSE;
  return fn(preparsed);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetAttributes(HANDLE file, void* attrs)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*);
  Fn fn = (Fn)real_hid_proc("HidD_GetAttributes");
  if (fn == NULL) return FALSE;
  return fn(file, attrs);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetFeature(HANDLE file,
                                                     void* report,
                                                     ULONG len)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*, ULONG);
  Fn fn = (Fn)real_hid_proc("HidD_GetFeature");
  if (fn == NULL) return FALSE;
  return fn(file, report, len);
}

__declspec(dllexport) void WINAPI HidD_GetHidGuid(GUID* guid)
{
  typedef void(WINAPI * Fn)(GUID*);
  Fn fn = (Fn)real_hid_proc("HidD_GetHidGuid");
  if (fn != NULL) fn(guid);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetManufacturerString(HANDLE file,
                                                                void* buf,
                                                                ULONG len)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*, ULONG);
  Fn fn = (Fn)real_hid_proc("HidD_GetManufacturerString");
  if (fn == NULL) return FALSE;
  return fn(file, buf, len);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetPreparsedData(HANDLE file,
                                                           void** preparsed)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void**);
  Fn fn = (Fn)real_hid_proc("HidD_GetPreparsedData");
  if (fn == NULL) return FALSE;
  return fn(file, preparsed);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetProductString(HANDLE file,
                                                           void* buf,
                                                           ULONG len)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*, ULONG);
  Fn fn = (Fn)real_hid_proc("HidD_GetProductString");
  if (fn == NULL) return FALSE;
  return fn(file, buf, len);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_GetSerialNumberString(HANDLE file,
                                                                void* buf,
                                                                ULONG len)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*, ULONG);
  Fn fn = (Fn)real_hid_proc("HidD_GetSerialNumberString");
  if (fn == NULL) return FALSE;
  return fn(file, buf, len);
}

__declspec(dllexport) BOOLEAN WINAPI HidD_SetFeature(HANDLE file,
                                                     void* report,
                                                     ULONG len)
{
  typedef BOOLEAN(WINAPI * Fn)(HANDLE, void*, ULONG);
  Fn fn = (Fn)real_hid_proc("HidD_SetFeature");
  if (fn == NULL) return FALSE;
  return fn(file, report, len);
}

__declspec(dllexport) LONG WINAPI HidP_GetCaps(void* preparsed, void* caps)
{
  typedef LONG(WINAPI * Fn)(void*, void*);
  Fn fn = (Fn)real_hid_proc("HidP_GetCaps");
  if (fn == NULL) return (LONG)0xc0110001;
  return fn(preparsed, caps);
}

__declspec(dllexport) LONG WINAPI HidP_GetValueCaps(int report_type,
                                                    void* caps,
                                                    ULONG* len,
                                                    void* preparsed)
{
  typedef LONG(WINAPI * Fn)(int, void*, ULONG*, void*);
  Fn fn = (Fn)real_hid_proc("HidP_GetValueCaps");
  if (fn == NULL) return (LONG)0xc0110001;
  return fn(report_type, caps, len, preparsed);
}
#endif

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
  (void)reserved;
  if (reason == DLL_PROCESS_DETACH) {
    fluorine_vfs_report_hooks();
    return TRUE;
  }
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  DisableThreadLibraryCalls(hinst);

  if (env_truthy("FLUORINE_DISABLE_VFS_INJECT")) return TRUE;

  char proc_path[MAX_PATH];
  DWORD pn = GetModuleFileNameA(NULL, proc_path, (DWORD)sizeof(proc_path));
  if (pn == 0 || pn >= sizeof(proc_path)) {
    proc_path[0] = '\0';
  } else {
    proc_path[pn] = '\0';
  }
  const char* exe = basename_of(proc_path);

  if (is_skipped_host_proc(exe)) return TRUE;

  char idx[1024];
  env_get("FLUORINE_VFS_INDEX", idx, (DWORD)sizeof(idx));
  char mount[1024];
  env_get("FLUORINE_VFS_MOUNT", mount, (DWORD)sizeof(mount));

  // Log process/env wiring before hook startup.  This also leaves a marker in
  // /tmp/fluorine_vfs_appinit.log when Proton swallows stderr.
  if (idx[0] != '\0' || env_truthy("FLUORINE_VFS_INJECT_DEBUG")) {
    char line[2048];
    DWORD pos = 0;
    pos = copy_str(line, pos, sizeof(line), "[fluorine_vfs] pid=");
    // Format PID without calling into user32 (wsprintfA) — see header
    // comment about loader-lock constraint when running from
    // AppInit_DLLs.
    char num[16];
    DWORD pid = GetCurrentProcessId();
    int ndigits = 0;
    if (pid == 0) {
      num[ndigits++] = '0';
    } else {
      char rev[16];
      int rn = 0;
      while (pid > 0 && rn < (int)sizeof(rev)) {
        rev[rn++] = (char)('0' + (pid % 10));
        pid /= 10;
      }
      while (rn > 0) num[ndigits++] = rev[--rn];
    }
    for (int i = 0; i < ndigits && pos + 1 < sizeof(line); ++i) line[pos++] = num[i];
    pos = copy_str(line, pos, sizeof(line), " proc='");
    pos = copy_str(line, pos, sizeof(line), exe[0] ? exe : "?");
    pos = copy_str(line, pos, sizeof(line), "' index='");
    pos = copy_str(line, pos, sizeof(line), idx[0] ? idx : "<unset>");
    pos = copy_str(line, pos, sizeof(line), "' mount='");
    pos = copy_str(line, pos, sizeof(line), mount[0] ? mount : "<unset>");
    pos = copy_str(line, pos, sizeof(line), "'\n");
    log_line(line, pos);
  }

  if (idx[0] != '\0') {
    fluorine_vfs_start_hooks();
  }

  return TRUE;
}
