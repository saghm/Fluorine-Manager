#ifndef MO2_STEAM_APPINFO_FFI_H
#define MO2_STEAM_APPINFO_FFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Callback invoked once per app in appinfo.vdf. Strings are valid for the
/// duration of the call only — copy if you want to keep them.
typedef void (*SteamAppInfoCallback)(void* user, uint32_t appid,
                                     const char* type, const char* name);

/// Parse Steam's appinfo.vdf at `path` and invoke `cb` for every app.
/// Returns 0 on success, negative on error.
int32_t steam_appinfo_parse(const char* path, void* user,
                            SteamAppInfoCallback cb);

#ifdef __cplusplus
}
#endif

#endif  // MO2_STEAM_APPINFO_FFI_H
