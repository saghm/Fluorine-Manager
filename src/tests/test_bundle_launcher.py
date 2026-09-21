"""Exercise the generated launcher using a fake bundle and an isolated data root."""

import os
from pathlib import Path
import shlex
import subprocess
import tarfile
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
        self.env.pop("XDG_DATA_HOME", None)
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
        for name in ["libgbm.so.1", "libssl.so.3", "libcrypto.so.3",
                     "libnss3.so", "libnssutil3.so", "libsmime3.so", "libssl3.so",
                     "libnspr4.so", "libplc4.so", "libplds4.so", "libsoftokn3.so",
                     "libfreebl3.so", "libfreeblpriv3.so", "libnssckbi.so",
                     "libnssdbm3.so"]:
            (lib / name).write_text("obsolete bundled runtime")
        (lib / "libgbm.so").symlink_to("libgbm.so.1")
        (lib / "custom-plugin.so").write_text("user file")
        for arch in ["x86_64", "i386"]:
            locale = self.installed / "locale" / arch
            locale.mkdir(parents=True, exist_ok=True)
            (locale / "libfluorine_locale.so").write_text("retired interposer")
            (locale / "custom-file").write_text("user file")

    def assert_clean(self):
        lib = self.installed / "lib"
        for pattern in ["libgbm.so*", "libssl.so*", "libcrypto.so*",
                        "libnss*.so*", "libsmime3.so*", "libssl3.so*", "libnspr4.so*",
                        "libplc4.so*", "libplds4.so*", "libsoftokn3.so*",
                        "libfreebl*.so*"]:
            self.assertEqual(list(lib.glob(pattern)), [])
        self.assertEqual((lib / "custom-plugin.so").read_text(), "user file")
        for arch in ["x86_64", "i386"]:
            locale = self.installed / "locale" / arch
            self.assertFalse((locale / "libfluorine_locale.so").exists())
            self.assertEqual((locale / "custom-file").read_text(), "user file")

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

    def test_custom_data_home_installs_bundle_icons_and_desktop_entry(self):
        data = self.root / "custom data"
        self.env["XDG_DATA_HOME"] = str(data)
        self.installed = data / "fluorine/bin"
        icons = self.bundle / "icons"
        icons.mkdir()
        (icons / "com.fluorine.manager.png").write_bytes(b"test icon")
        (icons / "com.fluorine.manager.desktop").write_text(
            "[Desktop Entry]\nType=Application\nExec=fluorine-manager %u\n"
        )
        with (self.bundle / "fluorine-manifest.txt").open("a") as manifest:
            manifest.write("icons\n")
        self.seed_legacy_files()
        self.launch(self.bundle / "fluorine-manager")
        self.assert_clean()
        desktop = data / "applications/com.fluorine.manager.desktop"
        self.assertIn(f'Exec="{self.installed}/fluorine-manager" %u', desktop.read_text())
        self.assertTrue((data / "icons/hicolor/256x256/apps/com.fluorine.manager.png").exists())
        self.assertFalse((self.test_home / ".local/share").exists())
        self.launch(self.installed / "fluorine-manager")

    def test_empty_and_relative_data_home_use_default(self):
        for value in ("", "relative/path"):
            with self.subTest(value=value):
                self.env["XDG_DATA_HOME"] = value
                self.launch(self.bundle / "fluorine-manager")
                self.assertTrue((self.installed / "ModOrganizer-core").exists())

    def test_installer_uses_same_data_root(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        header = source.split("<<'INSTALLER_HEADER'\n", 1)[1].split("\necho", 1)[0]
        for value in (None, "", "relative/path", str(self.root / "custom data")):
            with self.subTest(value=value):
                env = self.env.copy()
                env["HOME"] = str(self.test_home)
                if value is not None:
                    env["XDG_DATA_HOME"] = value
                script = header + '\nprintf "%s\\n" "$INSTALL_DIR" "$DESKTOP_DIR" "$ICON_DIR"\n'
                result = subprocess.run(["bash", "-c", script], env=env,
                                        capture_output=True, text=True, check=True)
                root = Path(value) if value and value.startswith("/") else self.test_home / ".local/share"
                self.assertEqual(result.stdout.splitlines(), [str(root / suffix) for suffix in
                                 ("fluorine/bin", "applications", "icons/hicolor/256x256/apps")])

    def test_packaging_keeps_entire_nss_stack_on_host(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        block = source.split("# Libraries that MUST come from the host", 1)[1]
        block = block[block.index("SKIP_PATTERN="):].split('echo "Dependencies bundled."', 1)[0]
        host_libs = ("libnss3.so", "libnssutil3.so", "libsmime3.so", "libssl3.so",
                     "libnspr4.so", "libplc4.so", "libplds4.so", "libsoftokn3.so",
                     "libfreebl3.so", "libfreeblpriv3.so", "libnssckbi.so",
                     "libnssdbm3.so", "libnss_files.so.2")
        deps = self.root / "deps"
        deps.mkdir()
        lines = []
        for name in (*host_libs, "libz.so.1"):
            (deps / name).touch()
            lines.append(f"{name} => {deps / name} (0x0)")
        tool_dir = self.root / "tools"
        tool_dir.mkdir()
        self.write_executable(tool_dir / "ldd", "#!/bin/sh\ncat <<'DEPS'\n" + "\n".join(lines) + "\nDEPS\n")
        (self.bundle / "plugins").mkdir()
        env = self.env.copy()
        env["PATH"] = str(tool_dir) + os.pathsep + env["PATH"]
        script = "set -euo pipefail\nOUT_DIR=" + shlex.quote(str(self.bundle)) + "\n" + block
        subprocess.run(["bash", "-c", script], env=env, check=True, capture_output=True)
        self.assertTrue((self.bundle / "lib/libz.so.1").exists())
        for name in host_libs:
            self.assertFalse((self.bundle / "lib" / name).exists(), name)

    def test_installer_extracts_into_custom_data_home(self):
        source = (Path(__file__).resolve().parents[2] / "docker/build-inner.sh").read_text()
        header = source.split("<<'INSTALLER_HEADER'\n", 1)[1].split("\nINSTALLER_HEADER", 1)[0]
        icons = self.bundle / "icons"
        icons.mkdir()
        (icons / "com.fluorine.manager.png").write_bytes(b"test icon")
        payload = self.root / "payload.tar.gz"
        with tarfile.open(payload, "w:gz") as archive:
            archive.add(self.bundle, arcname="fluorine-manager")
        installer = self.root / "installer.bin"
        installer.write_bytes((header + "\n").encode() + payload.read_bytes())
        data = self.root / "custom data"
        env = self.env.copy()
        env.update(HOME=str(self.test_home), XDG_DATA_HOME=str(data))
        subprocess.run(["bash", str(installer)], input="1\nn\n", env=env,
                       capture_output=True, text=True, check=True, timeout=15)
        installed = data / "fluorine/bin"
        self.assertTrue((installed / "ModOrganizer-core").exists())
        desktop = data / "applications/com.fluorine.manager.desktop"
        self.assertIn(f'Exec="{installed}/fluorine-manager" %u', desktop.read_text())
        self.assertTrue((data / "icons/hicolor/256x256/apps/com.fluorine.manager.png").exists())
        self.assertFalse((self.test_home / ".local/share").exists())


if __name__ == "__main__":
    unittest.main()
