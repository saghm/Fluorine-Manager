#ifndef STEAMUTILS_H
#define STEAMUTILS_H

#include <QHash>
#include <QString>

// Scans all Steam library folders and returns a map of
// Steam App ID -> installation path
QHash<int, QString> findSteamGames();

// Find the installation path for a specific Steam app ID
QString findSteamGamePath(int appId);

#endif  // STEAMUTILS_H
