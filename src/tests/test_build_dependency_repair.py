import importlib.util
import pathlib
import tempfile
import unittest

script = pathlib.Path(__file__).resolve().parents[2] / "docker/repair-ninja-deps.py"
spec = importlib.util.spec_from_file_location("repair_ninja_deps", script)
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)


class DependencyRepairTests(unittest.TestCase):
    def test_discards_only_objects_with_empty_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("stale.o", "valid.o", "notes.txt"):
                (root / name).write_text("keep unless stale")
            log = ("stale.o: #deps 0, deps mtime 123 (VALID)\n"
                   "valid.o: #deps 2, deps mtime 456 (VALID)\n"
                   "    source.cpp\n    header.h\n"
                   "notes.txt: #deps 0, deps mtime 123 (VALID)\n")
            self.assertEqual(repair.repair_objects(root, log), ["stale.o"])
            self.assertFalse((root / "stale.o").exists())
            self.assertTrue((root / "valid.o").exists())
            self.assertTrue((root / "notes.txt").exists())
            self.assertEqual(repair.repair_objects(root, log), [])

    def test_keeps_files_outside_build_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "build").mkdir()
            (root / "external.o").write_text("keep")
            with self.assertRaises(ValueError):
                repair.repair_objects(root / "build", "../external.o: #deps 0, invalid")
            self.assertTrue((root / "external.o").exists())


if __name__ == "__main__":
    unittest.main()
