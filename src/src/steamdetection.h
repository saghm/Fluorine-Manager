#ifndef STEAMDETECTION_H
#define STEAMDETECTION_H

#include <QString>
#include <QStringList>
#include <QVector>

/// Information about an installed Proton version.
struct SteamProtonInfo {
  QString name;
  QString config_name;
  QString path;
  bool is_steam_proton  = false;
  bool is_experimental  = false;

  /// Get the path to the wine binary (files/bin/wine or dist/bin/wine).
  QString wineBinary() const;

  /// Get the path to the wineserver binary.
  QString wineserverBinary() const;

  /// Get the bin directory containing wine executables.
  QString binDir() const;
};

/// Find the Steam installation path (returns empty string if not found).
__attribute__((visibility("default"))) QString findSteamPath();

/// Find all Steam library root paths, including secondary libraries.
__attribute__((visibility("default"))) QStringList findSteamLibraryPaths();

/// Find all installed Proton versions (Proton 10+ only, sorted newest first).
__attribute__((visibility("default"))) QVector<SteamProtonInfo> findSteamProtons();

#endif // STEAMDETECTION_H
