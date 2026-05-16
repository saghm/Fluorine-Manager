from ..basic_game import BasicGame


# Ready or Not ships two different binary names depending on the store:
#   - Steam build → ReadyOrNotSteam-Win64-Shipping.exe
#   - non-Steam (Epic / others) → ReadyOrNot-Win64-Shipping.exe
# Override looksValid() to accept either so both work out of the box.
_RON_BINARY_DIR = "ReadyOrNot/Binaries/Win64"
_RON_BINARY_STEAM = "ReadyOrNotSteam-Win64-Shipping.exe"
_RON_BINARY_OTHER = "ReadyOrNot-Win64-Shipping.exe"


class ReadyOrNotGame(BasicGame):
    Name = "Ready or Not Support Plugin"
    Author = "Ra2-IFV"
    Version = "0.0.0.2"

    GameName = "Ready or Not"
    GameShortName = "readyornot"
    GameNexusName = "readyornot"
    GameValidShortNames = ["ron"]
    # GameNexusId = "readyornot"
    # Default to the Steam binary since Fluorine is Steam/Proton-oriented; Epic
    # users will still be detected via the looksValid() override below.
    GameBinary = _RON_BINARY_DIR + "/" + _RON_BINARY_STEAM
    GameLauncher = "ReadyOrNot.exe"
    GameDataPath = "ReadyOrNot/Content/Paks"
    GameDocumentsDirectory = "%USERPROFILE%/AppData/Local/ReadyOrNot"
    GameIniFiles = [
        "%GAME_DOCUMENTS%/Saved/Config/Windows/Game.ini",
        "%GAME_DOCUMENTS%/Saved/Config/Windows/GameUserSettings.ini",
    ]
    GameSavesDirectory = "%USERPROFILE%/AppData/Local/ReadyOrNot/Saved/SaveGames"
    GameSaveExtension = "sav"
    GameSteamId = 1144200

    def looksValid(self, directory):
        return directory.exists(_RON_BINARY_DIR + "/" + _RON_BINARY_STEAM) or \
               directory.exists(_RON_BINARY_DIR + "/" + _RON_BINARY_OTHER)
