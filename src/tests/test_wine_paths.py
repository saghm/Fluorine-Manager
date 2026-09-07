from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[2] / "libs/basic_games/wine_paths.py"
spec = importlib.util.spec_from_file_location("wine_paths", SOURCE)
wine_paths = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wine_paths)


class WinePathsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.home = Path(self.temporary.name)
        self.config = self.home / "config/fluorine/config.json"
        self.config.parent.mkdir(parents=True)
        env = patch.dict(os.environ, {
            "HOME": str(self.home), "XDG_CONFIG_HOME": str(self.home / "config"),
        })
        env.start()
        self.addCleanup(env.stop)
        platform = patch.object(wine_paths.platform, "system", return_value="Linux")
        platform.start()
        self.addCleanup(platform.stop)
        self.default = self.make_prefix(".local/share/fluorine/Prefix/pfx")

    def make_prefix(self, relative):
        prefix = self.home / relative
        (prefix / "drive_c/users/steamuser").mkdir(parents=True)
        return prefix

    def user(self, prefix):
        return str(prefix / "drive_c/users/steamuser")

    def test_configured_prefix_wins_over_existing_default(self):
        prefix = self.make_prefix("custom/pfx")
        self.config.write_text(json.dumps({"prefix_path": str(prefix)}))
        self.assertEqual(wine_paths.find_wine_userprofile(), self.user(prefix))

    def test_compatibility_parent_is_normalized(self):
        prefix = self.make_prefix("custom/pfx")
        self.config.write_text(json.dumps({"prefix_path": str(prefix.parent)}))
        self.assertEqual(wine_paths.find_wine_userprofile(), self.user(prefix))

    def test_launcher_prefix_wins_over_global_config_and_default(self):
        configured = self.make_prefix("configured/pfx")
        active = self.make_prefix("instance/pfx")
        self.config.write_text(json.dumps({"prefix_path": str(configured)}))
        organizer = SimpleNamespace(winePrefixPath=lambda: str(active))
        self.assertEqual(wine_paths.find_wine_userprofile(organizer), self.user(active))

    def test_empty_or_missing_active_prefix_never_selects_default(self):
        for prefix in ("", str(self.home / "missing")):
            with self.subTest(prefix=prefix):
                organizer = SimpleNamespace(winePrefixPath=lambda: prefix)
                self.assertIsNone(wine_paths.find_wine_userprofile(organizer))

    def test_older_host_uses_config_fallback(self):
        prefix = self.make_prefix("custom/pfx")
        self.config.write_text(json.dumps({"prefix_path": str(prefix)}))
        self.assertEqual(wine_paths.find_wine_userprofile(object()), self.user(prefix))

    def test_missing_or_invalid_config_uses_default_before_initialization(self):
        self.assertEqual(wine_paths.find_wine_userprofile(), self.user(self.default))
        for config in ('broken', '[]', '{"prefix_path":42}'):
            with self.subTest(config=config):
                self.config.write_text(config)
                self.assertEqual(wine_paths.find_wine_userprofile(), self.user(self.default))

    def test_windows_does_not_resolve_wine_prefix(self):
        with patch.object(wine_paths.platform, "system", return_value="Windows"):
            self.assertIsNone(wine_paths.find_wine_userprofile())


if __name__ == "__main__":
    unittest.main()
