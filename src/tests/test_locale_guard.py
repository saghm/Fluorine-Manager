"""Exercise the actual LD_PRELOAD interposer, including selection of the matching ELF class."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

GUARD_DIR = Path(sys.argv.pop()).resolve()


class LocaleGuardTests(unittest.TestCase):
    def run_probe(self, enabled):
        env = dict(os.environ, LD_PRELOAD="libfluorine_locale.so", LC_ALL="C.UTF-8")
        env["LD_LIBRARY_PATH"] = ":".join([str(GUARD_DIR / "x86_64"),
                                           str(GUARD_DIR / "i386"),
                                           os.environ.get("LD_LIBRARY_PATH", "")]).rstrip(":")
        env.pop("FLUORINE_WINE_UTF8", None)
        if enabled:
            env["FLUORINE_WINE_UTF8"] = "C.UTF-8"
        code = r'''
import ctypes
libc = ctypes.CDLL(None)
libc.getenv.restype = ctypes.c_char_p
def value(key): return libc.getenv(key).decode()
libc.setenv(b"LC_ALL", b"C", 1)
print(value(b"LC_ALL"))
assignment = ctypes.create_string_buffer(b"LC_ALL=POSIX")
libc.putenv(assignment)
print(value(b"LC_ALL"))
libc.setenv(b"OTHER", b"C", 1)
libc.setenv(b"OTHER", b"replacement", 0)
print(value(b"OTHER"))
libc.setenv(b"LC_ALL", b"ja_JP.UTF-8", 1)
libc.setenv(b"LC_ALL", b"C", 0)
print(value(b"LC_ALL"))
mutable = ctypes.create_string_buffer(b"UNRELATED=old", 32)
libc.putenv(mutable)
mutable.value = b"UNRELATED=new"
print(value(b"UNRELATED"))
'''
        result = subprocess.run([sys.executable, "-c", code], env=env,
                                capture_output=True, text=True, check=True)
        self.assertNotIn("cannot be preloaded", result.stderr)
        return result.stdout.splitlines()

    def test_late_ascii_overrides_are_replaced(self):
        self.assertEqual(self.run_probe(True),
                         ["C.UTF-8", "C.UTF-8", "C", "ja_JP.UTF-8", "new"])

    def test_no_marker_leaves_environment_semantics_unchanged(self):
        self.assertEqual(self.run_probe(False),
                         ["C", "POSIX", "C", "ja_JP.UTF-8", "new"])


if __name__ == "__main__":
    unittest.main()
