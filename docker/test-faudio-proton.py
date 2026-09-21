#!/usr/bin/env python3
"""Run bundled audio probes with one Proton, using only an isolated prefix.

Pass that Proton's Steam Linux Runtime explicitly (sniper for GE 10,
SteamLinuxRuntime_4 for GE 11). Repeat for each runner in the release matrix.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--proton", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--steam", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="New directory for the disposable prefix and logs")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    compat = args.output / "compatdata"
    compat.mkdir()
    env = os.environ.copy()
    env.update(STEAM_COMPAT_CLIENT_INSTALL_PATH=str(args.steam.resolve()),
               STEAM_COMPAT_DATA_PATH=str(compat), SteamAppId="0", SteamGameId="0",
               STEAM_COMPAT_APP_ID="0", UMU_ID="umu-default", PROTON_USE_XALIA="0",
               PROTON_LOG="1", PROTON_LOG_DIR=str(args.output), WINEDEBUG="-all")
    # Probe processes must not inherit an application's prefix or overrides.
    for key in ("WINEPREFIX", "WINEDLLOVERRIDES", "WINEARCH", "WINESERVER", "WINELOADER"):
        env.pop(key, None)
    failed = False
    for variant in ("safe", "latest"):
        for arch, pe in (("i686", "i386-windows"), ("x86_64", "x86_64-windows")):
            stage = args.output / variant / arch
            shutil.copytree(args.bundle / variant / pe, stage)
            names = sorted(path.stem for path in stage.glob("*.dll"))
            env["WINEDLLOVERRIDES"] = ",".join(names) + "=n"
            for probe in ("wma", "smoke"):
                exe = stage / f"{probe}.exe"
                shutil.copy2(args.bundle / "tests" / f"{arch}-{probe}.exe", exe)
                report = stage / f"{probe}.txt"
                report_windows = "Z:" + str(report).replace("/", "\\")
                command = [str(args.runtime.resolve() / "run"), "--",
                           str(args.proton.resolve() / "proton"), "waitforexitandrun", str(exe)]
                if probe == "smoke":
                    command.append("register")
                command.append(report_windows)
                with (stage / f"{probe}-launcher.log").open("w") as log:
                    try:
                        result = subprocess.run(command, env=env, stdout=log,
                                                stderr=subprocess.STDOUT, timeout=120)
                        rc = result.returncode
                    except subprocess.TimeoutExpired:
                        rc = "timeout"
                text = report.read_text(errors="replace") if report.exists() else ""
                expected = "PASS WMA worker threads" if probe == "wma" else "PASS 35 modules;"
                passed = rc == 0 and expected in text
                print(f"{'PASS' if passed else 'FAIL'} {variant} {arch} {probe}: exit={rc}", flush=True)
                failed |= not passed
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
