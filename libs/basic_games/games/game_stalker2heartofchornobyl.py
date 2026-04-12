from typing import List, Tuple
from PyQt6.QtCore import QFileInfo
import mobase
from ..basic_game import BasicGame
from ..basic_features import BasicModDataChecker, GlobPatterns, BasicLocalSavegames


class S2HoCGame(BasicGame, mobase.IPluginFileMapper):

    Name = "Stalker 2: Heart of Chornobyl Plugin"
    Author = "Archon"
    Version = "0.1.0a"
    GameName = "Stalker 2: Heart of Chornobyl"
    GameShortName = "stalker2heartofchornobyl"
    GameNexusName = "stalker2heartofchornobyl"
    GameDocumentsDirectory = "%USERPROFILE%/AppData/Local/Stalker2"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Saved/Steam/SaveGames/Data"
    GameSaveExtension = "sav"
    GameNexusId = 6944
    GameSteamId = 1643320
    GameGogId = 1529799785
    GameBinary = "Stalker2.exe"
    GameDataPath = "%GAME_PATH%/Stalker2"
    GameIniFiles = [
        "%GAME_DOCUMENTS%/Saved/Config/Windows/Game.ini",
        "%GAME_DOCUMENTS%/Saved/Config/Windows/GameUserSettings.ini",
        "%GAME_DOCUMENTS%/Saved/Config/Windows/Engine.ini"
    ]

    def __init__(self):
        super().__init__()
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        if hasattr(self, '_featureMap'):
            self._featureMap[mobase.ModDataChecker] = S2HoCModDataChecker()
        else:
            self._register_feature(S2HoCModDataChecker())
        return True

    def mappings(self) -> list[mobase.Mapping]:
        return []

class S2HoCModDataChecker(BasicModDataChecker):
    def __init__(self, patterns: GlobPatterns = GlobPatterns()):
        super().__init__(
            GlobPatterns(
                valid=["Content"],
                move={"*.pak": "Content/Paks/~mods/", "*.utoc": "Content/Paks/~mods/", "*.ucas": "Content/Paks/~mods/"},
            ).merge(patterns),
        )
