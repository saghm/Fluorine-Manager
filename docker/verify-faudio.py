#!/usr/bin/env python3
"""Reject incomplete, builtin-marked, or dynamically linked FAudio packs."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

root = Path(sys.argv[1])
modules = {f"{family}{n}.dll" for family, numbers in (
    ("xaudio2_", range(10)), ("x3daudio1_", range(8)),
    ("xapofx1_", range(1, 6)), ("xactengine3_", range(8)),
    ("xactengine2_", (0, 4, 7, 9))) for n in numbers}
inventory = {}
for arch, machine, tool in (("i386-windows", 0x14c, "i686"),
                            ("x86_64-windows", 0x8664, "x86_64")):
    files = {p.name: p for p in (root / arch).glob("*.dll")}
    if files.keys() != modules:
        raise SystemExit(f"Incorrect {arch} module set: {files.keys() ^ modules}")
    for name, path in sorted(files.items()):
        data = path.read_bytes()
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if (data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or
                struct.unpack_from("<H", data, pe + 4)[0] != machine):
            raise SystemExit(f"Invalid PE architecture: {path}")
        if b"Wine builtin DLL" in data[64:pe]:
            raise SystemExit(f"Builtin marker would redirect prefix loading: {path}")
        output = subprocess.check_output(
            [f"{tool}-w64-mingw32-objdump", "-p", str(path)], text=True)
        imports = re.findall(r"DLL Name: (\S+)", output)
        if any(re.search(r"faudio|sdl", dll, re.I) for dll in imports):
            raise SystemExit(f"Unexpected external audio dependency: {path}: {imports}")
        inventory[f"{arch}/{name}"] = {
            "sha256": hashlib.sha256(data).hexdigest(), "machine": hex(machine),
            "imports": imports,
        }
(root / "pe-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
print(f"Verified {len(inventory)} ordinary PE audio modules: {root}")
