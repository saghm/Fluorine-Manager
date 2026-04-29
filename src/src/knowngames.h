#ifndef KNOWNGAMES_H
#define KNOWNGAMES_H

#include <QString>
#include <QVector>
#include <optional>

struct KnownGame {
  const char* name;
  const char* steam_app_id;
  const char* gog_app_id;          // nullptr if not on GOG
  const char* epic_app_id;         // nullptr if not on Epic
  const char* my_games_folder;     // nullptr if not applicable
  const char* appdata_local_folder;// nullptr if not applicable
  const char* appdata_roaming_folder; // nullptr if not applicable
  const char* registry_path;       // under HKLM\Software
  const char* registry_value;      // value name for install path
  const char* steam_folder;        // expected folder in steamapps/common/
};

// Static list of all known games.
inline constexpr KnownGame KNOWN_GAMES[] = {
    // Bethesda Games
    {"Enderal", "933480", "1708684988", nullptr,
     "Enderal", nullptr, nullptr,
     R"(Software\SureAI\Enderal)", "Install_Path", "Enderal"},
    {"Enderal Special Edition", "976620", nullptr, nullptr,
     "Enderal Special Edition", nullptr, nullptr,
     R"(Software\SureAI\Enderal SE)", "installed path", "Enderal Special Edition"},
    {"Fallout 3", "22300", "1454315831", "adeae8bbfc94427db57c7dfecce3f1d4",
     "Fallout3", "Fallout3", nullptr,
     R"(Software\Bethesda Softworks\Fallout3)", "installed path", "Fallout 3"},
    {"Fallout 4", "377160", "1998527297", "61d52ce4d09d41e48800c22784d13ae8",
     "Fallout4", "Fallout4", nullptr,
     R"(Software\Bethesda Softworks\Fallout4)", "installed path", "Fallout 4"},
    {"Fallout 4 VR", "611660", nullptr, nullptr,
     "Fallout4VR", nullptr, nullptr,
     R"(Software\Bethesda Softworks\Fallout 4 VR)", "installed path", "Fallout 4 VR"},
    {"Fallout New Vegas", "22380", "1454587428", "5daeb974a22a435988892319b3a4f476",
     "FalloutNV", "FalloutNV", nullptr,
     R"(Software\Bethesda Softworks\FalloutNV)", "installed path", "Fallout New Vegas"},
    {"Morrowind", "22320", "1440163901", nullptr,
     "Morrowind", nullptr, nullptr,
     R"(Software\Bethesda Softworks\Morrowind)", "installed path", "Morrowind"},
    {"Oblivion", "22330", "1458058109", nullptr,
     "Oblivion", "Oblivion", nullptr,
     R"(Software\Bethesda Softworks\Oblivion)", "installed path", "Oblivion"},
    {"Skyrim", "72850", nullptr, nullptr,
     "Skyrim", "Skyrim", nullptr,
     R"(Software\Bethesda Softworks\Skyrim)", "installed path", "Skyrim"},
    {"Skyrim Special Edition", "489830", "1711230643", "ac82db5035584c7f8a2c548d98c86b2c",
     "Skyrim Special Edition", "Skyrim Special Edition", nullptr,
     R"(Software\Bethesda Softworks\Skyrim Special Edition)", "installed path",
     "Skyrim Special Edition"},
    {"Skyrim VR", "611670", nullptr, nullptr,
     "Skyrim VR", nullptr, nullptr,
     R"(Software\Bethesda Softworks\Skyrim VR)", "installed path", "Skyrim VR"},
    {"Starfield", "1716740", nullptr, nullptr,
     "Starfield", nullptr, nullptr,
     R"(Software\Bethesda Softworks\Starfield)", "installed path", "Starfield"},
    // CD Projekt RED Games
    {"The Witcher 3", "292030", "1495134320", nullptr,
     "The Witcher 3", nullptr, nullptr,
     R"(Software\CD Projekt Red\The Witcher 3)", "InstallFolder",
     "The Witcher 3 Wild Hunt"},
    {"Cyberpunk 2077", "1091500", "1423049311", nullptr,
     nullptr, "CD Projekt Red/Cyberpunk 2077", nullptr,
     R"(Software\CD Projekt Red\Cyberpunk 2077)", "InstallFolder", "Cyberpunk 2077"},
    // Other popular moddable games
    {"Baldur's Gate 3", "1086940", "1456460669", nullptr,
     nullptr, "Larian Studios/Baldur's Gate 3", nullptr,
     R"(Software\Larian Studios\Baldur's Gate 3)", "InstallDir", "Baldurs Gate 3"},
};

inline constexpr int KNOWN_GAMES_COUNT =
    static_cast<int>(sizeof(KNOWN_GAMES) / sizeof(KNOWN_GAMES[0]));

// GOG ID aliases: (alias_id, primary_id)
inline constexpr std::pair<const char*, const char*> GOG_ID_ALIASES[] = {
    {"1435828767", "1440163901"},  // Morrowind alternate GOG ID
    {"1801825368", "1711230643"},  // Skyrim Anniversary Edition → Skyrim SE
};

inline constexpr int GOG_ID_ALIASES_COUNT =
    static_cast<int>(sizeof(GOG_ID_ALIASES) / sizeof(GOG_ID_ALIASES[0]));

// Normalize Steam App ID (e.g. Fallout 3 GOTY 22370 → 22300).
inline QString normalizeSteamId(const QString& appId)
{
  if (appId == QLatin1String("22370"))
    return QStringLiteral("22300");
  return appId;
}

inline const KnownGame* findKnownGameBySteamId(const QString& appId)
{
  const QString normalized = normalizeSteamId(appId);
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    if (normalized == QLatin1String(KNOWN_GAMES[i].steam_app_id))
      return &KNOWN_GAMES[i];
  }
  return nullptr;
}

inline const KnownGame* findKnownGameByGogId(const QString& appId)
{
  // Direct match.
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    if (KNOWN_GAMES[i].gog_app_id && appId == QLatin1String(KNOWN_GAMES[i].gog_app_id))
      return &KNOWN_GAMES[i];
  }
  // Check aliases.
  for (int i = 0; i < GOG_ID_ALIASES_COUNT; ++i) {
    if (appId == QLatin1String(GOG_ID_ALIASES[i].first)) {
      const char* primary = GOG_ID_ALIASES[i].second;
      for (int j = 0; j < KNOWN_GAMES_COUNT; ++j) {
        if (KNOWN_GAMES[j].gog_app_id &&
            QLatin1String(KNOWN_GAMES[j].gog_app_id) == QLatin1String(primary))
          return &KNOWN_GAMES[j];
      }
    }
  }
  return nullptr;
}

inline const KnownGame* findKnownGameByEpicId(const QString& appId)
{
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    if (KNOWN_GAMES[i].epic_app_id && appId == QLatin1String(KNOWN_GAMES[i].epic_app_id))
      return &KNOWN_GAMES[i];
  }
  return nullptr;
}

inline const KnownGame* findKnownGameByName(const QString& name)
{
  const QString lower = name.toLower();
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i) {
    if (lower == QString::fromLatin1(KNOWN_GAMES[i].name).toLower())
      return &KNOWN_GAMES[i];
  }
  return nullptr;
}

/// Fuzzy title matching — strips punctuation, tries containment, colon-split.
inline const KnownGame* findKnownGameByTitle(const QString& title)
{
  const QString titleLower = title.toLower();

  // Exact match first.
  if (auto* g = findKnownGameByName(title))
    return g;

  // Sort by name length descending so "Skyrim Special Edition" beats "Skyrim".
  QVector<const KnownGame*> sorted;
  sorted.reserve(KNOWN_GAMES_COUNT);
  for (int i = 0; i < KNOWN_GAMES_COUNT; ++i)
    sorted.append(&KNOWN_GAMES[i]);
  std::sort(sorted.begin(), sorted.end(), [](const KnownGame* a, const KnownGame* b) {
    return qstrlen(a->name) > qstrlen(b->name);
  });

  // Helper: strip non-alphanumeric, collapse whitespace.
  auto normalize = [](const QString& s) -> QString {
    QString out;
    for (QChar const c : s) {
      out += (c.isLetterOrNumber() || c == ' ') ? c : QChar(' ');
    }
    return out.simplified();
  };

  const QString titleNorm = normalize(titleLower);

  for (const KnownGame* g : sorted) {
    const QString gameLower = QString::fromLatin1(g->name).toLower();
    const QString gameNorm  = normalize(gameLower);

    if (titleLower.contains(gameLower))
      return g;
    if (titleNorm.contains(gameNorm))
      return g;

    // After-colon match: "The Elder Scrolls III: Morrowind" → "Morrowind"
    const int colon = titleLower.indexOf(':');
    if (colon >= 0) {
      const QString afterColon = titleLower.mid(colon + 1).trimmed();
      if (afterColon.startsWith(gameLower))
        return g;
    }
  }
  return nullptr;
}

#endif // KNOWNGAMES_H
