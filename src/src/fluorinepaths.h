#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns the Fluorine data directory: ~/.local/share/fluorine
QString fluorineDataDir();

/// Returns the VFS scan-cache directory: ~/.local/share/fluorine/vfs_cache
/// Created on demand by the cache writer.
QString fluorineVfsCacheDir();

/// Returns the VFS bridge index directory: ~/.local/share/fluorine/vfs_bridge
/// Created on demand by the bridge index writer.
QString fluorineVfsBridgeDir();

/// Returns the path to the bundled PE-side VFS injector
/// (`fluorine_vfs.dll`), or an empty string if it was not built (e.g.
/// mingw missing in the build image).  The DLL is staged to the
/// prefix's `c:\\windows\\system32\\` and registered in AppInit_DLLs at
/// prefix init.
QString fluorineVfsInjectDllPath();

/// Returns the bundled low-collision `hid.dll` game-directory proxy for
/// Wine builds that ignore AppInit_DLLs, or an empty string if it was not built.
QString fluorineVfsHidProxyDllPath();

/// One-time migration from ~/.var/app/com.fluorine.manager/ to
/// ~/.local/share/fluorine/. Call before initLogging().
void fluorineMigrateDataDir();

#endif  // FLUORINEPATHS_H
