#ifndef PREFIXSYMLINKS_H
#define PREFIXSYMLINKS_H

#include <QString>

/// Ensure AppData/Local/Temp exists in the Wine prefix.
__attribute__((visibility("default"))) void ensureTempDirectory(const QString& prefixPath);

/// Detect all games and create symlinks from the given prefix to game prefixes.
__attribute__((visibility("default"))) void createGameSymlinksAuto(const QString& prefixPath);

#endif // PREFIXSYMLINKS_H
