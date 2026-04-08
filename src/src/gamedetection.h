#ifndef GAMEDETECTION_H
#define GAMEDETECTION_H

#include <QString>
#include <QStringList>
#include <QVector>

/// A detected game installation.
struct DetectedGame {
  QString name;
  QString app_id;
  QString install_path;
  QString prefix_path;              // empty if no prefix
  QString launcher;                 // display string: "Steam", "Heroic (GOG)", etc.
  QString my_games_folder;          // empty if n/a
  QString appdata_local_folder;     // empty if n/a
  QString appdata_roaming_folder;   // empty if n/a
  QString registry_path;            // empty if n/a
  QString registry_value;           // empty if n/a
};

/// Results of a full game scan.
struct GameScanResult {
  QVector<DetectedGame> games;
  int steam_count   = 0;
  int heroic_count  = 0;
  int bottles_count = 0;
};

/// Detect all installed games across Steam, Heroic (GOG + Epic), and Bottles.
__attribute__((visibility("default"))) GameScanResult detectAllGames();

#endif // GAMEDETECTION_H
