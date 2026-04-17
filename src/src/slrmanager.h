#ifndef SLRMANAGER_H
#define SLRMANAGER_H

#include <QString>
#include <functional>

/// Returns true if the SLR `run` script is present and executable.
__attribute__((visibility("default"))) bool isSlrInstalled();

/// Returns the path to the SLR `run` script, or empty if not installed.
__attribute__((visibility("default"))) QString getSlrRunScript();

/// Download and install SteamLinuxRuntime_4 (steamrt4, ~200 MB).
/// Skips if already at the latest version (BUILD_ID check).
/// Returns empty string on success, or an error message.
__attribute__((visibility("default")))
QString downloadSlr(const std::function<void(float)>& progressCb,
                    const std::function<void(const QString&)>& statusCb,
                    const int* cancelFlag);

#endif // SLRMANAGER_H
