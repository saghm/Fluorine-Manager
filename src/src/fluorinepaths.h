#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns $XDG_DATA_HOME/fluorine (default: ~/.local/share/fluorine).
QString fluorineDataDir();

/// Returns the XDG credentials path, copying legacy credentials if needed.
QString fluorineCredentialsPath();

/// Returns the VFS scan-cache directory under fluorineDataDir().
/// Created on demand by the cache writer.
QString fluorineVfsCacheDir();

/// One-time migration from ~/.var/app/com.fluorine.manager/ to
/// fluorineDataDir(). Call before initLogging().
void fluorineMigrateDataDir();

#endif  // FLUORINEPATHS_H
