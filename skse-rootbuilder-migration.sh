#!/usr/bin/env bash
#
# Move the Skyrim Script Extender install under MO2/Fluorine's control.
#
# Before: SKSE 2.3.1's binaries sat loose in the game root and a separate,
#         stale 2.2.6 mod folder shadowed Data/Scripts. The update check read
#         the stale folder and reported "2.2.6 installed, 2.3.1 available".
# After:  one mod folder owns the whole install. Root/ is deployed into the
#         game directory by the VFS Root Builder; Scripts/ is served by the VFS.
#
# Requires "Enable VFS Root Builder" to be on in the instance setup.
#
# Close Fluorine before running this.

set -euo pipefail

GAME="/home/saghm/.games/frost/stock game"
MOD="/home/saghm/.games/frost/mods/Skyrim Script Extender"

# Nexus file id of the SKSE 2.3.1 upload.
FILE_ID_2_3_1=795992

# ---------------------------------------------------------------- preflight --
for path in "$GAME" "$MOD" "$MOD/meta.ini"; do
  if [ ! -e "$path" ]; then
    echo "error: expected $path to exist; wrong instance or layout?" >&2
    exit 1
  fi
done

echo "== before =="
echo "game root SKSE binaries:"
ls -1 "$GAME"/skse64* 2>/dev/null || echo "  (none)"
echo "game root Data/Scripts: $(ls -1 "$GAME/Data/Scripts" 2>/dev/null | wc -l) entries"
echo "mod Scripts:             $(ls -1 "$MOD/Scripts" 2>/dev/null | wc -l) entries"

# ------------------------------------------------------------------- move ----
# Binaries: game root -> the mod's Root/. Moved, not copied, so a single
# deployment can never drift out of sync with the mod.
mkdir -p "$MOD/Root"
mv "$GAME/skse64_1_7_104.dll" "$GAME/skse64_loader.exe" "$MOD/Root/"

# Scripts: discard the 2.2.6 payload, adopt the real 2.3.1 one.
rm -rf "$MOD/Scripts"
mv "$GAME/Data/Scripts" "$MOD/Scripts"

# ------------------------------------------------------------------- meta ----
# Point the mod at 2.3.1 and clear the evidence that described 2.2.6. The
# evidence is re-derived on the next check; zeroing it means the verdict can
# never be carried over from the file that is no longer installed.
sed -i \
  -e 's|^version=.*|version=2.3.1.0|' \
  -e 's|^installedFileUpdate=.*|installedFileUpdate=0|' \
  -e 's|^latestFileUpdate=.*|latestFileUpdate=0|' \
  -e 's|^newestFileId=.*|newestFileId=0|' \
  -e 's|^nexusFileStatus=.*|nexusFileStatus=1|' \
  -e 's|^updateChainFileId=.*|updateChainFileId=0|' \
  -e "s|^1.fileid=.*|1\\\\fileid=$FILE_ID_2_3_1|" \
  "$MOD/meta.ini"

# ---------------------------------------------------------------- verify ----
echo
echo "== after =="
echo "mod layout:"
find "$MOD" -maxdepth 1 -mindepth 1 | sort | sed 's|^|  |'

echo "Root/ (deployed into $GAME by the root builder):"
ls -1 "$MOD/Root" | sed 's|^|  |'

printf 'Scripts/: %s .pex, %s .psc\n' \
  "$(ls -1 "$MOD/Scripts"/*.pex 2>/dev/null | wc -l)" \
  "$(ls -1 "$MOD/Scripts/Source"/*.psc 2>/dev/null | wc -l)"

echo "game root Data/Scripts: $(ls -1 "$GAME/Data/Scripts" 2>/dev/null | wc -l) entries (0 = supplied by the VFS)"

echo
echo "meta:"
grep -E '^(version|newestVersion|modid|nexusFileStatus|updateChainFileId|installedFileUpdate|latestFileUpdate|newestFileId)=' \
  "$MOD/meta.ini" | sed 's|^|  |'
sed -n '/^\[installedFiles\]/,$p' "$MOD/meta.ini" | sed 's|^|  |'

cat <<'NEXT'

== next steps ==

1. Launch Fluorine. The root builder deploys Root/ on mount, so
   skse64_1_7_104.dll and skse64_loader.exe should reappear in the game root.
   Confirm they came back.

2. Right-click the mod -> "Force-check updates". Per-mod path, so it skips
   the 5-minute cooldown and the bulk/priming machinery. The log should record
   "verdict=none" and the version column should go green.

3. In game, run:  [i]getskseversion      -> expect 2.3.1

4. Worth testing: disable the mod, confirm the binaries are backed out of the
   game root, re-enable, confirm they return. That is the durability property
   this arrangement is for.

== known rough edges ==

* installationFile still names the 2.2.6 archive. Harmless while
  [installedFiles] holds exactly one entry, because the recorded file id wins.
  If a second file is ever installed for this mod the set becomes size 2, the
  archive-name fallback engages, and the stale name will not match anything on
  Nexus -- the mod then goes unresolved and falls back to the mod-info path.

* Data/Scripts is now empty in the game root and supplied by the VFS.
  Disabling the mod removes SKSE's scripts from the game. That is the
  intended MO2 semantic, but it is a change from loose files that always
  worked.
NEXT
