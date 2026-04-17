#ifndef STEAMAPPINFO_H
#define STEAMAPPINFO_H

#include <QHash>
#include <QString>

struct SteamAppInfo {
  quint32 appid = 0;
  QString type;  // "Game", "Tool", "Application", "Demo", "Music", "Config", ...
  QString name;
};

/// Parse ~/.steam/steam/appcache/appinfo.vdf (binary v29) and return a map of
/// appid -> SteamAppInfo with just the fields we need (common.type, common.name).
/// Parses lazily on first call and caches in-memory for the process lifetime.
/// Returns an empty hash on any parse/IO failure — callers should treat missing
/// entries as "unknown type".
const QHash<quint32, SteamAppInfo>& loadSteamAppInfo(const QString& steamPath);

/// Rank for sorting candidate prefixes when multiple games have the same
/// My Games subfolder: managed-game's own appid wins, then Games, then
/// Applications, then unknown, then Tools/Demos/Config last.
/// Lower = higher priority.
int steamAppTypeRank(const QString& type);

#endif  // STEAMAPPINFO_H
