#include "gamedefs.h"

// clang-format off
const std::vector<GameDefinition>& allGameDefinitions()
{
  static const std::vector<GameDefinition> defs = {
    // 1. Batman: Arkham City
    {
      "Batman: Arkham City Plugin", "Paynamia", "0.5.3",
      "Batman: Arkham City", "batmanarkhamcity", {},
      {200260}, {1260066469}, {"Egret"},
      "Binaries/Win32/BatmanAC.exe", "Binaries/Win32/BmLauncher.exe",
      "", 372,
      "BmGame",
      "%DOCUMENTS%/WB Games/Batman Arkham City GOTY/BmGame/Config",
      "", "sgd",
      {"UserEngine.ini", "UserGame.ini", "UserInput.ini"},
      "", {}, {}, {}, {}
    },
    // 2. Assetto Corsa
    {
      "Assetto Corsa Support Plugin", "Deorder", "0.0.1",
      "Assetto Corsa", "ac", {},
      {244210}, {}, {},
      "AssettoCorsa.exe", "",
      "", 0,
      "",
      "%DOCUMENTS%/Assetto Corsa",
      "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Assetto-Corsa",
      {}, {}, {}, {}
    },
    // 3. Baldur's Gate 3
    {
      "Baldur's Gate 3 Plugin", "daescha", "0.1.0",
      "Baldur's Gate 3", "baldursgate3", {"bg3"},
      {1086940}, {1456460669}, {},
      "bin/bg3.exe", "Launcher/LariLauncher.exe",
      "baldursgate3", 3474,
      "",
      "%USERPROFILE%/AppData/Local/Larian Studios/Baldur's Gate 3",
      "%GAME_DOCUMENTS%/PlayerProfiles/Public/Savegames/Story", "lsv",
      {},
      "",
      MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt,
      MOBase::IPluginGame::SortMechanism::NONE,
      {}, {}
    },
    // 4. Black & White 2
    {
      "Black & White 2 Support Plugin", "Ilyu", "1.0.1",
      "Black & White 2", "BW2", {},
      {}, {}, {},
      "white.exe", "",
      "blackandwhite2", 0,
      "%GAME_PATH%",
      "%DOCUMENTS%/Black & White 2",
      "%GAME_DOCUMENTS%/Profiles", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Black-&-White-2",
      {}, {}, {}, {}
    },
    // 5. Black & White 2 Battle of the Gods
    {
      "Black & White 2 Battle of the Gods Support Plugin", "Ilyu", "1.0.1",
      "Black & White 2 Battle of the Gods", "BOTG", {},
      {}, {}, {},
      "BattleOfTheGods.exe", "",
      "blackandwhite2", 0,
      "%GAME_PATH%",
      "%DOCUMENTS%/Black & White 2 - Battle of the Gods",
      "%GAME_DOCUMENTS%/Profiles", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Black-&-White-2",
      {}, {}, {}, {}
    },
    // 6. Blade & Sorcery
    {
      "Blade & Sorcery Plugin", "R3z Shark & Silarn & Jonny_Bro", "0.5.1",
      "Blade & Sorcery", "bladeandsorcery", {},
      {629730}, {}, {},
      "BladeAndSorcery.exe", "",
      "", 0,
      "BladeAndSorcery_Data/StreamingAssets/Mods",
      "%DOCUMENTS%/My Games/BladeAndSorcery",
      "%GAME_DOCUMENTS%/Saves/Default", "chr",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Blade-&-Sorcery",
      {}, {}, {}, {}
    },
    // 7. Borderlands
    {
      "Borderlands 1 Support Plugin", "Miner Of Worlds, RedxYeti, mopioid", "1.0.0",
      "Borderlands", "Borderlands", {},
      {8980}, {}, {},
      "Binaries/Borderlands.exe", "",
      "Borderlands GOTY", 0,
      ".",
      "%DOCUMENTS%/My Games/Borderlands",
      "%GAME_DOCUMENTS%/savedata", "sav",
      {},
      "", {}, {}, {}, {}
    },
    // 8. Control
    {
      "Control Support Plugin", "Zash", "1.0.0",
      "Control", "control", {},
      {870780}, {2049187585}, {"calluna"},
      "Control.exe", "",
      "", 2936,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 9. Cyberpunk 2077
    {
      "Cyberpunk 2077 Support Plugin", "6788, Zash", "3.0.1",
      "Cyberpunk 2077", "cyberpunk2077", {},
      {1091500}, {1423049311}, {"77f2b98e2cef40c8a7437518bf420e47"},
      "bin/x64/Cyberpunk2077.exe", "REDprelauncher.exe",
      "", 0,
      "%GAME_PATH%",
      "%USERPROFILE%/AppData/Local/CD Projekt Red/Cyberpunk 2077",
      "%USERPROFILE%/Saved Games/CD Projekt Red/Cyberpunk 2077", "dat",
      {"UserSettings.json"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Cyberpunk-2077",
      {}, {}, {}, {}
    },
    // 10. Dragon Age 2
    {
      "Dragon Age 2 Support Plugin", "Patchier", "1.0.1",
      "Dragon Age 2", "dragonage2", {},
      {1238040}, {}, {},
      "bin_ship/DragonAge2.exe", "",
      "", 0,
      "%DOCUMENTS%/BioWare/Dragon Age 2/packages/core/override",
      "",
      "%DOCUMENTS%/BioWare/Dragon Age 2/Characters", "das",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dragon-Age-II",
      {}, {}, {}, {}
    },
    // 11. Daggerfall Unity
    {
      "Daggerfall Unity Support Plugin", "HomerSimpleton", "1.0.0",
      "Daggerfall Unity", "daggerfallunity", {},
      {}, {}, {},
      "DaggerfallUnity.exe", "DaggerfallUnity.exe",
      "", 0,
      "%GAME_PATH%/DaggerfallUnity_Data/StreamingAssets",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Daggerfall-Unity",
      {}, {}, {}, {}
    },
    // 12. Dragon Age: Origins
    {
      "Dragon Age Origins Support Plugin", "Patchier", "1.1.1",
      "Dragon Age: Origins", "dragonage", {},
      {17450, 47810}, {1949616134}, {},
      "bin_ship/DAOrigins.exe", "",
      "", 0,
      "%DOCUMENTS%/BioWare/Dragon Age/packages/core/override",
      "",
      "%DOCUMENTS%/BioWare/Dragon Age/Characters", "das",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dragon-Age:-Origins",
      {}, {}, {}, {}
    },
    // 13. Darkest Dungeon
    {
      "DarkestDungeon", "erri120", "0.2.0",
      "Darkest Dungeon", "darkestdungeon", {},
      {262060}, {1719198803}, {},
      "_windows/win64/Darkest.exe", "",
      "darkestdungeon", 804,
      "",
      "%DOCUMENTS%/Darkest",
      "%GAME_DOCUMENTS%", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Darkest-Dungeon",
      {}, {}, {}, {}
    },
    // 14. Dark Messiah of Might & Magic
    {
      "Dark Messiah of Might and Magic Support Plugin", "Holt59", "0.1.0",
      "Dark Messiah of Might & Magic", "darkmessiahofmightandmagic", {},
      {2100}, {}, {},
      "mm.exe", "",
      "darkmessiahofmightandmagic", 628,
      "mm",
      "%GAME_PATH%/mm",
      "%GAME_PATH%/mm/SAVE", "sav",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dark-Messiah-of-Might-&-Magic",
      {}, {}, {}, {}
    },
    // 15. Dark Souls II: Scholar of the First Sin
    {
      "DarkSouls2Sotfs", "raehik", "0.1.0",
      "Dark Souls II: Scholar of the First Sin", "darksouls2sotfs", {},
      {335300}, {}, {},
      "Game/DarkSoulsII.exe", "",
      "darksouls2", 482,
      "Game",
      "%USERPROFILE%/AppData/Roaming/DarkSoulsII",
      "%USERPROFILE%/AppData/Roaming/DarkSoulsII", "sl2",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dark-Souls-2-Sotfs",
      {}, {}, {}, {}
    },
    // 16. Dark Souls
    {
      "DarkSouls", "Holt59", "0.1.0",
      "Dark Souls", "darksouls", {},
      {211420}, {}, {},
      "DATA/DARKSOULS.exe", "",
      "darksouls", 162,
      "DATA",
      "%DOCUMENTS%/NBGI/DarkSouls",
      "%DOCUMENTS%/NBGI/DarkSouls", "sl2",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dark-Souls",
      {}, {}, {}, {}
    },
    // 17. Dispatch
    {
      "Dispatch Support Plugin", "Syer10", "0.1.0",
      "Dispatch", "dispatch", {},
      {2592160}, {}, {},
      "Dispatch/Binaries/Win64/Dispatch-Win64-Shipping.exe", "",
      "dispatch", 0,
      "Dispatch/Content/",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dispatch",
      {}, {}, {}, {}
    },
    // 18. Divinity: Original Sin (Enhanced Edition)
    {
      "Divinity: Original Sin (Enhanced Edition) Support Plugin", "LostDragonist", "1.0.0",
      "Divinity: Original Sin (Enhanced Edition)", "divinityoriginalsinenhancededition",
      {"divinityoriginalsin"},
      {373420}, {1445516929, 1445524575}, {},
      "Shipping/EoCApp.exe", "",
      "divinityoriginalsinenhancededition", 1995,
      "Data",
      "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin Enhanced Edition",
      "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin Enhanced Edition/PlayerProfiles",
      "lsv",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Divinity:-Original-Sin",
      {}, {}, {}, {}
    },
    // 19. Divinity: Original Sin (Classic)
    {
      "Divinity: Original Sin (Classic) Support Plugin", "LostDragonist", "1.0.0",
      "Divinity: Original Sin (Classic)", "divinityoriginalsin",
      {"divinityoriginalsin"},
      {230230}, {}, {},
      "Shipping/EoCApp.exe", "",
      "divinityoriginalsin", 573,
      "Data",
      "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin",
      "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin/PlayerProfiles",
      "lsv",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Divinity:-Original-Sin",
      {}, {}, {}, {}
    },
    // 20. Dragon's Dogma: Dark Arisen
    {
      "Dragon's Dogma: Dark Arisen Support Plugin", "Luca/EzioTheDeadPoet", "1.0.0",
      "Dragon's Dogma: Dark Arisen", "dragonsdogma", {},
      {367500}, {1242384383}, {},
      "DDDA.exe", "",
      "dragonsdogma", 0,
      "nativePC",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dragon's-Dogma:-Dark-Arisen",
      {}, {}, {}, {}
    },
    // 21. Dungeon Siege I
    {
      "Dungeon Siege I", "mrudat", "0.0.1",
      "Dungeon Siege I", "dungeonsiege1", {},
      {39190}, {1142020247}, {},
      "DungeonSiege.exe", "",
      "dungeonsiege1", 541,
      "",
      "%DOCUMENTS%/Dungeon Siege",
      "%GAME_DOCUMENTS%/Save", "dssave",
      {"DungeonSiege.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dungeon-Siege-I",
      {}, {}, {}, {}
    },
    // 22. Dungeon Siege II
    {
      "Dungeon Siege II", "Holt59", "0.1.1",
      "Dungeon Siege II", "dungeonsiegeii", {},
      {39200}, {1142020247}, {},
      "DungeonSiege2.exe", "",
      "dungeonsiegeii", 2078,
      "",
      "%DOCUMENTS%/My Games/Dungeon Siege 2",
      "%GAME_DOCUMENTS%/Save", "ds2party",
      {"DungeonSiege2.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Dungeon-Siege-II",
      {}, {}, {}, {}
    },
    // 23. F1 23
    {
      "F1 23 Support Plugin", "ju5tA1ex", "1.0.0",
      "F1 23", "F1 23", {},
      {2108330}, {}, {},
      "F1_23.exe", "",
      "", 0,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 24. Fantasy Life I
    {
      "Fantasy Life I Support Plugin", "AmeliaCute", "0.2.2",
      "FANTASY LIFE i", "fantasylifei", {"fli"},
      {2993780}, {}, {},
      "Game/Binaries/Win64/NFL1-Win64-Shipping.exe", "",
      "fantasylifeithegirlwhostealstime", 0,
      "Game/Content/",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Fantasy-Life-I:-The-Girl-Who-Steals-Time",
      {}, {}, {}, {}
    },
    // 25. Final Fantasy VII Rebirth
    {
      "Final Fantasy 7 Rebirth Support Plugin", "diegofesanto, TheUnlocked", "0.0.1",
      "Final Fantasy VII Rebirth", "finalfantasy7rebirth", {},
      {2909400}, {}, {},
      "End/Binaries/Win64/ff7rebirth_.exe", "",
      "finalfantasy7rebirth", 0,
      "_ROOT",
      "", "", "sav",
      {},
      "", {}, {}, {}, {}
    },
    // 26. Final Fantasy VII Remake
    {
      "Final Fantasy VII Remake Support Plugin", "TheUnlocked", "1.0.0",
      "Final Fantasy VII Remake", "finalfantasy7remake", {},
      {1462040}, {}, {},
      "ff7remake.exe", "",
      "finalfantasy7remake", 0,
      "_ROOT",
      "", "", "sav",
      {},
      "", {}, {}, {}, {}
    },
    // 27. GTA III - Definitive Edition
    {
      "Grand Theft Auto III - Definitive Edition Support Plugin", "dekart811", "1.0",
      "GTA III - Definitive Edition", "grandtheftautothetrilogy", {},
      {}, {}, {},
      "Gameface/Binaries/Win64/LibertyCity.exe", "",
      "grandtheftautothetrilogy", 0,
      "Gameface/Content/Paks/~mods",
      "%USERPROFILE%/Documents/Rockstar Games/GTA III Definitive Edition/Config/WindowsNoEditor",
      "%GAME_DOCUMENTS%/../../SaveGames", "sav",
      {"GameUserSettings.ini", "CustomSettings.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Grand-Theft-Auto:-The-Trilogy",
      {}, {}, {}, {}
    },
    // 28. GTA: San Andreas - Definitive Edition
    {
      "Grand Theft Auto: San Andreas - Definitive Edition Support Plugin", "dekart811", "1.0",
      "GTA: San Andreas - Definitive Edition", "grandtheftautothetrilogy", {},
      {}, {}, {},
      "Gameface/Binaries/Win64/SanAndreas.exe", "",
      "grandtheftautothetrilogy", 0,
      "Gameface/Content/Paks/~mods",
      "%USERPROFILE%/Documents/Rockstar Games/GTA San Andreas Definitive Edition/Config/WindowsNoEditor",
      "%GAME_DOCUMENTS%/../../SaveGames", "sav",
      {"GameUserSettings.ini", "CustomSettings.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Grand-Theft-Auto:-The-Trilogy",
      {}, {}, {}, {}
    },
    // 29. GTA: Vice City - Definitive Edition
    {
      "Grand Theft Auto: Vice City - Definitive Edition Support Plugin", "dekart811", "1.0",
      "GTA: Vice City - Definitive Edition", "grandtheftautothetrilogy", {},
      {}, {}, {},
      "Gameface/Binaries/Win64/ViceCity.exe", "",
      "grandtheftautothetrilogy", 0,
      "Gameface/Content/Paks/~mods",
      "%USERPROFILE%/Documents/Rockstar Games/GTA Vice City Definitive Edition/Config/WindowsNoEditor",
      "%GAME_DOCUMENTS%/../../SaveGames", "sav",
      {"GameUserSettings.ini", "CustomSettings.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Grand-Theft-Auto:-The-Trilogy",
      {}, {}, {}, {}
    },
    // 30. Kerbal Space Program
    {
      "Kerbal Space Program Support Plugin", "LaughingHyena", "1.0.0",
      "Kerbal Space Program", "kerbalspaceprogram", {},
      {220200, 283740, 982970}, {}, {},
      "KSP_x64.exe", "",
      "kerbalspaceprogram", 0,
      "GameData",
      "",
      "%GAME_PATH%/saves", "sfs",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Kerbal-Space-Program",
      {}, {}, {}, {}
    },
    // 31. Kingdom Come: Deliverance
    {
      "Kingdom Come Deliverance Support Plugin", "Silencer711", "1.0.0",
      "Kingdom Come: Deliverance", "kingdomcomedeliverance", {},
      {379430}, {1719198803}, {"Eel"},
      "bin/Win64/KingdomCome.exe", "",
      "kingdomcomedeliverance", 2298,
      "mods",
      "%GAME_PATH%",
      "%USERPROFILE%/Saved Games/kingdomcome/saves", "whs",
      {"custom.cfg", "system.cfg", "user.cfg"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Kingdom-Come:-Deliverance",
      {}, {}, {}, {}
    },
    // 32. Yu-Gi-Oh! Master Duel
    {
      "Yu-Gi-Oh! Master Duel Support Plugin", "The Conceptionist & uwx", "1.0.2",
      "Yu-Gi-Oh! Master Duel", "masterduel", {},
      {1449850}, {}, {},
      "masterduel.exe", "",
      "yugiohmasterduel", 4272,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 33. METAL GEAR SOLID 2: Sons of Liberty - MC
    {
      "METAL GEAR SOLID 2: Sons of Liberty - Master Collection Version Support Plugin",
      "AkiraJkr", "1.0.0",
      "METAL GEAR SOLID 2: Sons of Liberty - Master Collection Version",
      "metalgearsolid2mc", {},
      {2131640}, {}, {},
      "METAL GEAR SOLID2.exe", "launcher.exe",
      "metalgearsolid2mc", 0,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 34. METAL GEAR SOLID 3: Snake Eater - MC
    {
      "METAL GEAR SOLID 3: Snake Eater - Master Collection Version Support Plugin",
      "AkiraJkr", "1.0.0",
      "METAL GEAR SOLID 3: Snake Eater - Master Collection Version",
      "metalgearsolid3mc", {},
      {2131650}, {}, {},
      "METAL GEAR SOLID3.exe", "launcher.exe",
      "metalgearsolid3mc", 0,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 35. Mirror's Edge
    {
      "Mirror's Edge Support Plugin", "Luca/EzioTheDeadPoet", "1.0.0",
      "Mirror's Edge", "mirrorsedge", {},
      {17410}, {1893001152}, {},
      "Binaries/MirrorsEdge.exe", "",
      "", 0,
      "TdGame",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Mirror's-Edge",
      {}, {}, {}, {}
    },
    // 36. Monster Hunter: Rise
    {
      "Monster Hunter: Rise Support Plugin", "RodolfoFigueroa", "1.0.0",
      "Monster Hunter: Rise", "monsterhunterrise", {},
      {1446780}, {}, {},
      "MonsterHunterRise.exe", "",
      "", 4095,
      "%GAME_PATH%",
      "", "", "bin",
      {},
      "", {}, {}, {}, {}
    },
    // 37. Monster Hunter: Wilds
    {
      "Monster Hunter: Wilds Support Plugin", "AbyssDragnonModding", "1.0.0",
      "Monster Hunter: Wilds", "monsterhunterwilds", {},
      {2246340}, {}, {},
      "MonsterHunterWilds.exe", "",
      "", 6993,
      "%GAME_PATH%",
      "", "", "bin",
      {},
      "", {}, {}, {}, {}
    },
    // 38. Monster Hunter: World
    {
      "Monster Hunter: World Support Plugin", "prz", "1.0.0",
      "Monster Hunter: World", "monsterhunterworld", {},
      {582010}, {}, {},
      "MonsterHunterWorld.exe", "MonsterHunterWorld.exe",
      "monsterhunterworld", 2531,
      "%GAME_PATH%",
      "", "", "dat",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Monster-Hunter:-World",
      {}, {}, {}, {}
    },
    // 39. Mount & Blade II: Bannerlord
    {
      "Mount & Blade II: Bannerlord", "Holt59", "0.1.1",
      "Mount & Blade II: Bannerlord", "mountandblade2bannerlord", {},
      {261550}, {}, {},
      "bin/Win64_Shipping_Client/TaleWorlds.MountAndBlade.Launcher.exe", "",
      "", 3174,
      "Modules",
      "%DOCUMENTS%/Mount and Blade II Bannerlord/Configs",
      "%DOCUMENTS%/Mount and Blade II Bannerlord/Game Saves", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Mount-&-Blade-II:-Bannerlord",
      {}, {}, {}, {}
    },
    // 40. Microsoft Flight Simulator 2020
    {
      "Microsoft Flight Simulator 2020 Support Plugin", "Deorder", "0.0.1",
      "Microsoft Flight Simulator 2020", "msfs2020", {},
      {1250410}, {}, {},
      "FlightSimulator.exe", "",
      "", 0,
      "",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Microsoft-Flight-Simulator-(2020)",
      {}, {}, {}, {}
    },
    // 41. Need for Speed: High Stakes
    {
      "Need for Speed: High Stakes Support Plugin", "uwx", "1.0.0",
      "Need for Speed: High Stakes", "nfshs", {},
      {}, {}, {},
      "nfshs.exe", "",
      "needforspeedhighstakes", 6032,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 42. NieR:Automata
    {
      "NieR:Automata Support Plugin", "Luca/EzioTheDeadPoet", "1.0.0",
      "NieR:Automata", "nierautomata", {},
      {524220}, {}, {},
      "NieRAutomata.exe", "",
      "nierautomata", 0,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 43. No Man's Sky
    {
      "No Man's Sky Support Plugin", "Luca/EzioTheDeadPoet", "1.0.0",
      "No Man's Sky", "nomanssky", {},
      {275850}, {1446213994}, {},
      "Binaries/NMS.exe", "",
      "nomanssky", 1634,
      "GAMEDATA/MODS",
      "",
      "%USERPROFILE%/AppData/Roaming/HelloGames/NMS", "hg",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-No-Man's-Sky",
      {}, {}, {}, {}
    },
    // 44. Oblivion Remastered
    {
      "Oblivion Remastered Plugin", "Silarn", "0.1.0",
      "Oblivion Remastered", "oblivionremastered", {},
      {2623190}, {}, {},
      "OblivionRemastered.exe", "",
      "", 7587,
      "%GAME_PATH%/OblivionRemastered/Content/Dev/ObvData/Data",
      "%GAME_PATH%/OblivionRemastered/Content/Dev/ObvData",
      "%DOCUMENTS%/My Games/Oblivion Remastered/Saved/SaveGames", "sav",
      {"Oblivion.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Elder-Scrolls-IV:-Oblivion-Remastered",
      MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt,
      MOBase::IPluginGame::SortMechanism::LOOT,
      {"Oblivion.esm", "DLCBattlehornCastle.esp", "DLCFrostcrag.esp",
       "DLCHorseArmor.esp", "DLCMehrunesRazor.esp", "DLCOrrery.esp",
       "DLCShiveringIsles.esp", "DLCSpellTomes.esp", "DLCThievesDen.esp",
       "DLCVileLair.esp", "Knights.esp", "AltarESPMain.esp",
       "AltarDeluxe.esp", "AltarESPLocal.esp"},
      {}
    },
    // 45. Ready or Not
    {
      "Ready or Not Support Plugin", "Ra2-IFV", "0.0.0.1",
      "Ready or Not", "readyornot", {"ron"},
      {1144200}, {}, {},
      "ReadyOrNot/Binaries/Win64/ReadyOrNot-Win64-Shipping.exe", "ReadyOrNot.exe",
      "readyornot", 0,
      "ReadyOrNot/Content/Paks",
      "%USERPROFILE%/AppData/Local/ReadyOrNot",
      "%USERPROFILE%/AppData/Local/ReadyOrNot/Saved/SaveGames", "sav",
      {"%GAME_DOCUMENTS%/Saved/Config/Windows/Game.ini",
       "%GAME_DOCUMENTS%/Saved/Config/Windows/GameUserSettings.ini"},
      "", {}, {}, {}, {}
    },
    // 46. Schedule I
    {
      "Schedule I Support Plugin", "shellbj", "1.0.0",
      "Schedule I", "scheduleI", {"schedule1", "scheduleI"},
      {3164500}, {}, {},
      "Schedule I.exe", "",
      "schedule1", 7381,
      "",
      "",
      "%USERPROFILE%/AppData/LocalLow/TVGS/Schedule I/Saves", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Schedule-I",
      {}, {}, {}, {}
    },
    // 47. Sekiro: Shadows Die Twice
    {
      "Sekiro: Shadows Die Twice Support Plugin", "Kane Dou", "1.0.0",
      "Sekiro: Shadows Die Twice", "sekiro", {},
      {814380}, {}, {},
      "sekiro.exe", "",
      "", 0,
      "mods",
      "", "", "sl2",
      {},
      "", {}, {}, {}, {}
    },
    // 48. Silent Hill 2 Remake
    {
      "Silent Hill 2 Remake Support Plugin", "HomerSimpleton Returns", "1.0",
      "Silent Hill 2 Remake", "silenthill2", {},
      {}, {1225972913, 2051029707}, {},
      "SHProto/Binaries/Win64/SHProto-Win64-Shipping.exe", "SHProto.exe",
      "silenthill2", 0,
      "%GAME_PATH%",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Silent-Hill-2-Remake",
      {}, {}, {}, {}
    },
    // 49. Hollow Knight: Silksong
    {
      "Hollow Knight: Silksong Support Plugin", "Nikirack", "1.0.0",
      "Hollow Knight: Silksong", "hollowknightsilksong", {},
      {1030300}, {}, {},
      "Hollow Knight Silksong.exe", "",
      "hollowknightsilksong", 0,
      "",
      "",
      "%USERPROFILE%/AppData/LocalLow/Team Cherry/Hollow Knight Silksong", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Hollow-Knight-Silksong",
      {}, {}, {}, {}
    },
    // 50. The Sims 4
    {
      "The Sims 4 Support Plugin", "R3z Shark, xieve", "1.0.0",
      "The Sims 4", "thesims4", {},
      {1222670}, {}, {},
      "Game/Bin/TS4_x64.exe", "",
      "", 0,
      "%DOCUMENTS%/Electronic Arts/The Sims 4/Mods",
      "%DOCUMENTS%/Electronic Arts/The Sims 4/Mods",
      "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 51. STALKER Anomaly
    {
      "STALKER Anomaly", "Qudix", "0.5.0",
      "STALKER Anomaly", "stalkeranomaly", {},
      {}, {}, {},
      "AnomalyLauncher.exe", "",
      "stalkeranomaly", 3743,
      "",
      "%GAME_PATH%/appdata",
      "%GAME_DOCUMENTS%/savedgames", "scop",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-S.T.A.L.K.E.R.-Anomaly",
      {}, {}, {}, {}
    },
    // 52. Stardew Valley
    {
      "Stardew Valley Support Plugin", "Syer10", "0.1.0",
      "Stardew Valley", "stardewvalley", {},
      {413150}, {1453375253}, {},
      "Stardew Valley.exe", "",
      "stardewvalley", 1303,
      "mods",
      "%DOCUMENTS%/StardewValley",
      "%GAME_DOCUMENTS%/Saves", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Stardew-Valley",
      {}, {}, {}, {}
    },
    // 53. Starsector
    {
      "Starsector Support Plugin", "ddbb07", "1.0.1",
      "Starsector", "starsector", {},
      {}, {}, {},
      "starsector.exe", "",
      "starsector", 0,
      "mods",
      "",
      "%GAME_PATH%/saves", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Starsector",
      {}, {}, {}, {}
    },
    // 54. STAR WARS Empire at War: Forces of Corruption
    {
      "STAR WARS Empire at War - Force of Corruption", "erri120", "1.0.0",
      "STAR WARS Empire at War: Forces of Corruption", "starwarsempireatwar", {},
      {32470}, {1421404887}, {},
      "corruption/StarWarsG.exe", "",
      "starwarsempireatwar", 453,
      "corruption/Data",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Star-Wars:-Empire-At-War",
      {}, {}, {}, {}
    },
    // 55. STAR WARS Empire at War (base)
    {
      "STAR WARS Empire at War", "erri120", "1.0.0",
      "STAR WARS Empire at War", "starwarsempireatwar", {},
      {32470}, {1421404887}, {},
      "GameData/StarWarsG.exe", "",
      "starwarsempireatwar", 453,
      "GameData/Data",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Star-Wars:-Empire-At-War",
      {}, {}, {}, {}
    },
    // 56. Subnautica: Below Zero
    {
      "Subnautica Below Zero Support Plugin", "dekart811, Zash", "2.3",
      "Subnautica: Below Zero", "subnauticabelowzero", {},
      {848450}, {}, {"foxglove"},
      "SubnauticaZero.exe", "",
      "subnauticabelowzero", 0,
      "_ROOT",
      "%GAME_PATH%",
      "%USERPROFILE%/AppData/LocalLow/Unknown Worlds/Subnautica Below Zero/SubnauticaZero/SavedGames",
      "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Subnautica:-Below-Zero",
      {}, {}, {}, {}
    },
    // 57. Subnautica
    {
      "Subnautica Support Plugin", "dekart811, Zash", "2.3",
      "Subnautica", "subnautica", {},
      {264710}, {}, {"Jaguar"},
      "Subnautica.exe", "",
      "subnautica", 0,
      "_ROOT",
      "%GAME_PATH%",
      "%GAME_PATH%/SNAppData/SavedGames", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Subnautica",
      {}, {}, {}, {}
    },
    // 58. Test Drive Unlimited 2
    {
      "Test Drive Unlimited 2 Support Plugin", "uwx", "1.0.0",
      "Test Drive Unlimited 2", "tdu2", {},
      {9930}, {}, {},
      "UpLauncher.exe", "",
      "testdriveunlimited2", 2353,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 59. Test Drive Unlimited
    {
      "Test Drive Unlimited Support Plugin", "uwx", "1.0.0",
      "Test Drive Unlimited", "tdu", {},
      {}, {}, {},
      "TestDriveUnlimited.exe", "",
      "testdriveunlimited", 4615,
      "",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 60. The Binding of Isaac: Rebirth
    {
      "The Binding of Isaac: Rebirth - Support Plugin", "Luca/EzioTheDeadPoet", "0.1.0",
      "The Binding of Isaac: Rebirth", "thebindingofisaacrebirth", {},
      {250900}, {}, {},
      "isaac-ng.exe", "",
      "thebindingofisaacrebirth", 1293,
      "%DOCUMENTS%/My Games/Binding of Isaac Afterbirth+ Mods",
      "%DOCUMENTS%/My Games/Binding of Isaac Afterbirth+",
      "", "",
      {"options.ini"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-The-Binding-of-Isaac:-Rebirth",
      {}, {}, {}, {}
    },
    // 61. Tony Hawk's Pro Skater 3
    {
      "Tony Hawk's Pro Skater 3 Support Plugin", "uwx", "1.0.0",
      "Tony Hawk's Pro Skater 3", "thps3", {},
      {}, {}, {},
      "Skate3.exe", "",
      "", 0,
      "Data",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 62. Tony Hawk's Pro Skater 4
    {
      "Tony Hawk's Pro Skater 4 Support Plugin", "uwx", "1.0.0",
      "Tony Hawk's Pro Skater 4", "thps4", {},
      {}, {}, {},
      "Skate4.exe", "",
      "", 0,
      "Data",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 63. Tony Hawk's Underground 2
    {
      "Tony Hawk's Underground 2 Support Plugin", "uwx", "1.0.0",
      "Tony Hawk's Underground 2", "thug2", {},
      {}, {}, {},
      "THUG2.exe", "",
      "", 0,
      "Data",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 64. Tony Hawk's Underground
    {
      "Tony Hawk's Underground Support Plugin", "uwx", "1.0.0",
      "Tony Hawk's Underground", "thug", {},
      {}, {}, {},
      "THUG.exe", "",
      "", 0,
      "Data",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 65. Trackmania United Forever
    {
      "Trackmania United Forever Support Plugin", "uwx", "1.0.0",
      "Trackmania United Forever", "tmuf", {},
      {7200}, {}, {},
      "TmForeverLauncher.exe", "",
      "trackmaniaunited", 1500,
      "GameData",
      "", "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 66. Train Simulator
    {
      "Train Simulator Classic Support Plugin", "Ryan Young", "1.1.0",
      "Train Simulator", "railworks", {},
      {24010}, {}, {},
      "RailWorks.exe", "",
      "", 0,
      "",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Train-Simulator-Classic",
      {}, {}, {}, {}
    },
    // 67. Valheim
    {
      "Valheim Support Plugin", "Zash", "1.3",
      "Valheim", "valheim", {},
      {892970, 896660, 1223920}, {}, {},
      "valheim.exe", "",
      "", 3667,
      "",
      "",
      "%USERPROFILE%/AppData/LocalLow/IronGate/Valheim", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Valheim",
      {}, {}, {}, {}
    },
    // 68. Valkyria Chronicles
    {
      "Valkyria Chronicles Support Plugin", "Ketsuban", "1.0.0",
      "Valkyria Chronicles", "vc1", {},
      {294860}, {}, {},
      "Valkyria.exe", "Launcher.exe",
      "", 0,
      "%GAME_PATH%",
      "",
      "%GAME_PATH%/savedata", "",
      {},
      "", {}, {}, {}, {}
    },
    // 69. Vampire - The Masquerade: Bloodlines
    {
      "Vampire - The Masquerade: Bloodlines Support Plugin", "John", "1.0.0",
      "Vampire - The Masquerade: Bloodlines", "vampirebloodlines", {},
      {2600}, {1207659240}, {},
      "vampire.exe", "",
      "vampirebloodlines", 437,
      "vampire",
      "%GAME_PATH%/vampire/cfg",
      "%GAME_PATH%/vampire/SAVE", "sav",
      {"autoexec.cfg", "user.cfg"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Vampire:-The-Masquerade-Bloodlines",
      {}, {}, {}, {}
    },
    // 70. The Witcher: Enhanced Edition
    {
      "Witcher 1 Support Plugin", "erri120", "1.0.0",
      "The Witcher: Enhanced Edition", "witcher", {},
      {20900}, {1207658924}, {},
      "System/witcher.exe", "",
      "witcher", 150,
      "Data",
      "%DOCUMENTS%/The Witcher",
      "%GAME_DOCUMENTS%/saves", "TheWitcherSave",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-The-Witcher",
      {}, {}, {}, {}
    },
    // 71. The Witcher 2: Assassins of Kings
    {
      "Witcher 2 Support Plugin", "DefinitelyNotSade", "1.0.0",
      "The Witcher 2: Assassins of Kings", "witcher2", {},
      {20920}, {1207658930}, {},
      "bin/witcher2.exe", "Launcher.exe",
      "witcher2", 0,
      "CookedPC",
      "%DOCUMENTS%/witcher 2/Config",
      "%GAME_DOCUMENTS%/../gamesaves", "sav",
      {"User.ini", "Rendering.ini", "Community.ini", "UserContent.ini",
       "DIMapping.ini", "Input_QWERTY.ini", "Input_AZERTY.ini", "Input_QWERTZ.ini"},
      "", {}, {}, {}, {}
    },
    // 72. The Witcher 3: Wild Hunt
    {
      "Witcher 3 Support Plugin", "Holt59", "1.0.0",
      "The Witcher 3: Wild Hunt", "witcher3", {},
      {499450, 292030}, {1640424747, 1495134320, 1207664663, 1207664643}, {},
      "bin/x64/witcher3.exe", "",
      "witcher3", 952,
      "Mods",
      "%DOCUMENTS%/The Witcher 3",
      "%GAME_DOCUMENTS%/gamesaves", "sav",
      {"user.settings", "input.settings"},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-The-Witcher-3",
      {}, {}, {}, {}
    },
    // 73. X4: Foundations
    {
      "X4 Foundations Support Plugin", "Twinki,BrandonM4", "0.1.0",
      "X4: Foundations", "x4foundations", {},
      {392160}, {}, {},
      "x4.exe", "",
      "", 2659,
      "extensions",
      "%DOCUMENTS%/Egosoft/X4",
      "", "",
      {},
      "", {}, {}, {}, {}
    },
    // 74. X-Plane 11
    {
      "X-Plane 11 Support Plugin", "Deorder", "0.0.1",
      "X-Plane 11", "xp11", {},
      {}, {}, {},
      "X-Plane.exe", "",
      "", 0,
      "",
      "", "", "",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-X-Plane-11",
      {}, {}, {}, {}
    },
    // 75. Zeus and Poseidon
    {
      "Zeus and Poseidon Support Plugin", "Holt59", "1.0.0",
      "Zeus and Poseidon", "zeusandposeidon", {},
      {566050}, {1207659039}, {},
      "Zeus.exe", "",
      "", 0,
      "Adventures",
      "%GAME_PATH%",
      "%GAME_PATH%/Save", "sav",
      {},
      "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Zeus-Poseidon",
      {}, {}, {}, {}
    },
  };
  // clang-format on

  return defs;
}
