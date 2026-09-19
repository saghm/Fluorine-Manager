"""Exercise the generated launcher using a fake bundle and an isolated data root."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class BundleLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fluorine-launcher-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.test_home = self.root / "home"
        self.installed = self.test_home / ".local/share/fluorine/bin"
        self.bundle = self.root / "bundle"
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        launcher = source.split("<<'LAUNCH'\n", 1)[1].split("\nLAUNCH", 1)[0]
        # Redirect only the home-directory references; never touch the real install.
        launcher = launcher.replace("${HOME}", "${FLUORINE_TEST_HOME}")
        self.bundle.mkdir()
        (self.bundle / "lib").mkdir()
        (self.bundle / "lib/current-library.txt").write_text("current")
        self.write_executable(self.bundle / "fluorine-manager", launcher)
        self.write_executable(
            self.bundle / "ModOrganizer-core",
            '#!/usr/bin/env bash\nprintf "started" > "$FLUORINE_TEST_CAPTURE"\n',
        )
        (self.bundle / "fluorine-bundle-version.txt").write_text("test-bundle-1")
        (self.bundle / "fluorine-manifest.txt").write_text(
            "ModOrganizer-core\nfluorine-manager\nlib\n"
            "fluorine-bundle-version.txt\nfluorine-manifest.txt\n"
        )
        self.env = os.environ.copy()
        self.env.update(
            FLUORINE_TEST_HOME=str(self.test_home),
            FLUORINE_TEST_CAPTURE=str(self.root / "started"),
            FLUORINE_DISABLE_FONTCONFIG_FIX="1",
        )

    @staticmethod
    def write_executable(path, text):
        path.write_text(text)
        path.chmod(0o755)

    def launch(self, path):
        result = subprocess.run(
            [str(path)], env=self.env, capture_output=True, text=True, timeout=15
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "started").read_text(), "started")

    def seed_legacy_files(self):
        lib = self.installed / "lib"
        lib.mkdir(parents=True, exist_ok=True)
        for name in ["libgbm.so.1", "libssl.so.3", "libcrypto.so.3"]:
            (lib / name).write_text("obsolete bundled runtime")
        (lib / "libgbm.so").symlink_to("libgbm.so.1")
        (lib / "custom-plugin.so").write_text("user file")

    def assert_clean(self):
        lib = self.installed / "lib"
        for pattern in ["libgbm.so*", "libssl.so*", "libcrypto.so*"]:
            self.assertEqual(list(lib.glob(pattern)), [])
        self.assertEqual((lib / "custom-plugin.so").read_text(), "user file")

    def test_upgrade_removes_retired_libraries_and_preserves_user_files(self):
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()

    def test_same_bundle_version_still_cleans_legacy_libraries(self):
        self.launch(self.bundle / "fluorine-manager")
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()

    def test_launching_installed_copy_still_cleans_legacy_libraries(self):
        self.launch(self.bundle / "fluorine-manager")
        self.seed_legacy_files()
        self.launch(self.installed / "fluorine-manager")
        self.assert_clean()


if __name__ == "__main__":
    unittest.main()
