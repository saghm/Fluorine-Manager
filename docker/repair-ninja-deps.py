#!/usr/bin/env python3
"""Rebuild compiler objects whose Ninja header dependencies have been lost."""
import pathlib
import re
import subprocess
import sys


def repair_objects(build_dir, dependency_log):
    root = pathlib.Path(build_dir).resolve()
    repaired = []
    for line in dependency_log.splitlines():
        match = re.fullmatch(r"(.+\.o): #deps 0,.*", line)
        if not match:
            continue
        target = (root / match[1]).resolve()
        if not target.is_relative_to(root):
            raise ValueError(f"Object is outside the build directory: {target}")
        if target.is_file():
            target.unlink()
            repaired.append(match[1])
    return repaired


if __name__ == "__main__":
    build_dir = sys.argv[1]
    result = subprocess.run(["ninja", "-C", build_dir, "-t", "deps"],
                            capture_output=True, text=True, check=True)
    repaired = repair_objects(build_dir, result.stdout)
    if repaired:
        print(f"Rebuilding {len(repaired)} object(s) with missing header dependencies:")
        for name in repaired:
            print(f"  {name}")
