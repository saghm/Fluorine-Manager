from __future__ import annotations

import shutil
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo, qInfo, qWarning

import mobase

from ..basic_game import BasicGame


class RedDeadRedemption2Game(BasicGame):
    Name = "Red Dead Redemption 2 Support Plugin"
    Author = "KrebKrebou, Fluorine"
    Version = "1.1.0"

    GameName = "Red Dead Redemption 2"
    GameShortName = "reddeadredemption2"
    GameNexusName = "reddeadredemption2"
    GameNexusId = 3024
    GameSteamId = 1174180
    GameBinary = "RDR2.exe"
    GameLauncher = "PlayRDR2.exe"
    GameDataPath = ""

    _binary_candidates = ("RDR2.exe", "PlayRDR2.exe")

    def init(self, organizer: mobase.IOrganizer) -> bool:
        if not super().init(organizer):
            return False
        return organizer.onAboutToRun(self._on_about_to_run)

    def settings(self) -> list[mobase.PluginSetting]:
        settings = super().settings()
        settings.append(
            mobase.PluginSetting(
                "cleanup_overwrite_scripts",
                (
                    "Delete overwrite/scripts before launching RDR2. This works around "
                    "ScriptHookRDR2 launch failures caused by a stale generated scripts "
                    "folder in overwrite."
                ),
                True,
            )
        )
        return settings

    def binaryName(self) -> str:
        game_dir = self.gameDirectory()
        for candidate in self._binary_candidates:
            if game_dir.exists(candidate):
                return candidate
        return super().binaryName()

    def looksValid(self, directory: QDir) -> bool:
        return any(directory.exists(candidate) for candidate in self._binary_candidates)

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_dir = self.gameDirectory()
        executables: list[mobase.ExecutableInfo] = []

        for candidate in self._binary_candidates:
            if game_dir.exists(candidate) or not executables:
                label = self.gameName()
                if candidate != self.binaryName():
                    label = f"{self.gameName()} ({candidate})"
                executables.append(
                    mobase.ExecutableInfo(label, QFileInfo(game_dir, candidate))
                )

        return executables

    def _on_about_to_run(self, app_path: str, *args: object) -> bool:
        if not self._is_rdr2_executable(app_path):
            return True

        cleanup_scripts = self._organizer.pluginSetting(
            self.name(), "cleanup_overwrite_scripts"
        )
        if cleanup_scripts is False:
            return True

        overwrite_scripts = Path(self._organizer.overwritePath()) / "scripts"
        try:
            if overwrite_scripts.is_dir():
                shutil.rmtree(overwrite_scripts)
                qInfo(f"Removed stale RDR2 ScriptHook folder: {overwrite_scripts}")
            elif overwrite_scripts.exists():
                overwrite_scripts.unlink()
                qInfo(f"Removed stale RDR2 ScriptHook path: {overwrite_scripts}")
        except OSError as err:
            qWarning(f"Failed to remove '{overwrite_scripts}': {err}")

        return True

    def _is_rdr2_executable(self, app_path: str) -> bool:
        executable_name = Path(app_path.replace("\\", "/")).name.lower()
        return executable_name in {name.lower() for name in self._binary_candidates}
