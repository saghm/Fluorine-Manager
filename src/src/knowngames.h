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
    {.name="Enderal", .steam_app_id="933480", .gog_app_id="1708684988", .epic_app_id=nullptr,
     .my_games_folder="Enderal", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\SureAI\Enderal)", .registry_value="Install_Path", .steam_folder="Enderal"},
    {.name="Enderal Special Edition", .steam_app_id="976620", .gog_app_id=nullptr, .epic_app_id=nullptr,
     .my_games_folder="Enderal Special Edition", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\SureAI\Enderal SE)", .registry_value="installed path", .steam_folder="Enderal Special Edition"},
    {.name="Fallout 3", .steam_app_id="22300", .gog_app_id="1454315831", .epic_app_id="adeae8bbfc94427db57c7dfecce3f1d4",
     .my_games_folder="Fallout3", .appdata_local_folder="Fallout3", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Fallout3)", .registry_value="installed path", .steam_folder="Fallout 3"},
    {.name="Fallout 4", .steam_app_id="377160", .gog_app_id="1998527297", .epic_app_id="61d52ce4d09d41e48800c22784d13ae8",
     .my_games_folder="Fallout4", .appdata_local_folder="Fallout4", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Fallout4)", .registry_value="installed path", .steam_folder="Fallout 4"},
    {.name="Fallout 4 VR", .steam_app_id="611660", .gog_app_id=nullptr, .epic_app_id=nullptr,
     .my_games_folder="Fallout4VR", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Fallout 4 VR)", .registry_value="installed path", .steam_folder="Fallout 4 VR"},
    {.name="Fallout New Vegas", .steam_app_id="22380", .gog_app_id="1454587428", .epic_app_id="5daeb974a22a435988892319b3a4f476",
     .my_games_folder="FalloutNV", .appdata_local_folder="FalloutNV", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\FalloutNV)", .registry_value="installed path", .steam_folder="Fallout New Vegas"},
    {.name="Morrowind", .steam_app_id="22320", .gog_app_id="1440163901", .epic_app_id=nullptr,
     .my_games_folder="Morrowind", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Morrowind)", .registry_value="installed path", .steam_folder="Morrowind"},
    {.name="Oblivion", .steam_app_id="22330", .gog_app_id="1458058109", .epic_app_id=nullptr,
     .my_games_folder="Oblivion", .appdata_local_folder="Oblivion", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Oblivion)", .registry_value="installed path", .steam_folder="Oblivion"},
    {.name="Skyrim", .steam_app_id="72850", .gog_app_id=nullptr, .epic_app_id=nullptr,
     .my_games_folder="Skyrim", .appdata_local_folder="Skyrim", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Skyrim)", .registry_value="installed path", .steam_folder="Skyrim"},
    {.name="Skyrim Special Edition", .steam_app_id="489830", .gog_app_id="1711230643", .epic_app_id="ac82db5035584c7f8a2c548d98c86b2c",
     .my_games_folder="Skyrim Special Edition", .appdata_local_folder="Skyrim Special Edition", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Skyrim Special Edition)", .registry_value="installed path",
     .steam_folder="Skyrim Special Edition"},
    {.name="Skyrim VR", .steam_app_id="611670", .gog_app_id=nullptr, .epic_app_id=nullptr,
     .my_games_folder="Skyrim VR", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Skyrim VR)", .registry_value="installed path", .steam_folder="Skyrim VR"},
    {.name="Starfield", .steam_app_id="1716740", .gog_app_id=nullptr, .epic_app_id=nullptr,
     .my_games_folder="Starfield", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Bethesda Softworks\Starfield)", .registry_value="installed path", .steam_folder="Starfield"},
    // CD Projekt RED Games
    {.name="The Witcher 3", .steam_app_id="292030", .gog_app_id="1495134320", .epic_app_id=nullptr,
     .my_games_folder="The Witcher 3", .appdata_local_folder=nullptr, .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\CD Projekt Red\The Witcher 3)", .registry_value="InstallFolder",
     .steam_folder="The Witcher 3 Wild Hunt"},
    {.name="Cyberpunk 2077", .steam_app_id="1091500", .gog_app_id="1423049311", .epic_app_id=nullptr,
     .my_games_folder=nullptr, .appdata_local_folder="CD Projekt Red/Cyberpunk 2077", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\CD Projekt Red\Cyberpunk 2077)", .registry_value="InstallFolder", .steam_folder="Cyberpunk 2077"},
    // Other popular moddable games
    {.name="Baldur's Gate 3", .steam_app_id="1086940", .gog_app_id="1456460669", .epic_app_id=nullptr,
     .my_games_folder=nullptr, .appdata_local_folder="Larian Studios/Baldur's Gate 3", .appdata_roaming_folder=nullptr,
     .registry_path=R"(Software\Larian Studios\Baldur's Gate 3)", .registry_value="InstallDir", .steam_folder="Baldurs Gate 3"},
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
  if (const auto* g = findKnownGameByName(title))
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
