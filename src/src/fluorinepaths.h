#ifndef FLUORINEPATHS_H
#define FLUORINEPATHS_H

#include <QString>

/// Returns the Fluorine data directory: ~/.local/share/fluorine
QString fluorineDataDir();

/// One-time migration from ~/.var/app/com.fluorine.manager/ to
/// ~/.local/share/fluorine/. Call before initLogging().
void fluorineMigrateDataDir();

#endif  // FLUORINEPATHS_H
