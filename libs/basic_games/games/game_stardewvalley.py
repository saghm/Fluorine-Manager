import sys

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class StardewValleyModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        for e in filetree:
            if isinstance(e, mobase.IFileTree) and e.exists(
                "manifest.json", mobase.IFileTree.FILE
            ):
                return mobase.ModDataChecker.VALID

        return mobase.ModDataChecker.INVALID


def _has_native_linux_install(game_dir: QDir) -> bool:
    # Native Linux Stardew ships a `StardewValley` shell-script launcher and
    # has no `Stardew Valley.exe`.  Use that to distinguish from a Windows
    # install dropped under a Wine prefix.
    if sys.platform != "linux":
        return False
    if QFileInfo(game_dir, "Stardew Valley.exe").exists():
        return False
    return QFileInfo(game_dir, "StardewValley").exists()


class StardewValleyGame(BasicGame):
    Name = "Stardew Valley Support Plugin"
    Author = "Syer10"
    Version = "0.1.0a"

    GameName = "Stardew Valley"
    GameShortName = "stardewvalley"
    GameNexusName = "stardewvalley"
    GameNexusId = 1303
    GameSteamId = 413150
    GameGogId = 1453375253
    GameBinary = "Stardew Valley.exe"
    GameDataPath = "Mods"
    GameDocumentsDirectory = "%DOCUMENTS%/StardewValley"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Saves"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Stardew-Valley"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(StardewValleyModDataChecker())
        return True

    def isNativeLinux(self) -> bool:
        return _has_native_linux_install(self.gameDirectory())

    def binaryName(self) -> str:
        if self.isNativeLinux():
            return "StardewValley"
        return super().binaryName()

    def executables(self):
        if self.isNativeLinux():
            return [
                mobase.ExecutableInfo(
                    "SMAPI",
                    QFileInfo(self.gameDirectory(), "StardewModdingAPI"),
                ),
                mobase.ExecutableInfo(
                    "Stardew Valley",
                    QFileInfo(self.gameDirectory(), "StardewValley"),
                ),
            ]
        return [
            mobase.ExecutableInfo(
                "SMAPI", QFileInfo(self.gameDirectory(), "StardewModdingAPI.exe")
            ),
            mobase.ExecutableInfo(
                "Stardew Valley", QFileInfo(self.gameDirectory(), "Stardew Valley.exe")
            ),
        ]
