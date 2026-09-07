# Steam Cloud integration status

An **opt-in experimental automatic-sync implementation** is now integrated with
Fluorine's Run workflow for native Steam Cyberpunk launches. A real upload/download
round trip passed on the installed Linux Steam client on 2026-09-06 using explicit
console sync commands. The earlier
`RetryAppSync` test failed; that result did not rule out other client operations.

## Using automatic sync

Launch `Cyberpunk2077.exe` normally from Fluorine and accept the first-use cloud
prompt, or enable **Automatic Steam Cloud sync (Cyberpunk, experimental)** from
the Saves tab's right-click menu. **Steam Cloud status** shows the last result
and latest pre-download backup. No separate Steam login or launch-option wrapper
is used. CLF3 and Nexus authentication are unrelated to this feature.

The first version requires:

- Native Linux Steam, signed in and online, with Steam Cloud enabled for the
  account and Cyberpunk.
- A Steam-installed Cyberpunk game folder and existing Steam Proton prefix.
- The active Fluorine profile, with **profile-specific saves disabled**.
- Launching `Cyberpunk2077.exe` directly. Tools and `REDprelauncher.exe` do not
  trigger these hooks.

If Steam's debugging connection is unavailable, Fluorine offers a graceful
restart with `-cef-enable-debugging`. It refuses while it detects running Steam
games and does not force-kill Steam. Debugging stays enabled for future launches;
restart Steam normally when you want to turn it off.

Before launch, Fluorine takes an exclusive cloud-session lock, verifies the
account and save mapping, backs up the active save files, requests a download,
and waits for Steam's Auto-Cloud watch setup. On first setup, Steam's original
folder is synchronized and compared with the Fluorine collection before the
shared-folder link is made. Differing non-empty collections require a whole-
collection choice; Fluorine never merges or renumbers Cyberpunk save slots.
It then records an upload-pending journal before spawning the game.
The process runner waits through the full process tree and VFS cleanup before
requesting an upload on a clean exit. It also recognizes Steam's automatic exit
upload from the current launch, avoiding a duplicate request and timeout after
Steam has already finished. Upload completion requires Steam's synced file
records to match the complete local collection by path, size, and SHA-1,
including additions and deletions. The progress dialog is cancellable and waits
at most 60 seconds; cancellation retains the pending recovery record.

The account-bound journal and pre-download backups live under
`~/.local/share/fluorine/steam-cloud/` (or the corresponding `XDG_DATA_HOME`).
Backups are retained until manually removed. Existing Steam save folders are
renamed to a unique adjacent `.fluorine-backup-*` folder before creating the
shared-directory symlink. Unrelated existing links are not replaced.

Failures block a cloud-enabled launch, rather than silently launching offline.
An interrupted/abnormal session keeps an upload-pending record. On the next
attempt Fluorine first asks Steam to refresh remote state so Steam can expose a
conflict caused by another device; only then can the user explicitly upload the
remaining local collection. Steam conflict, sync-failure, and pending-on-another-
device states block the launch with instructions to resolve them in Steam. If
Steam skips files during recovery, the file-record check rejects completion and
keeps the journal pending. Account or prefix changes block automatic sync.
Local files differing from Steam's synced-file records also require an explicit
upload decision before downloading, even if no unfinished session was recorded
(for example, after playing with automatic sync disabled).

Disabling the Fluorine option stops Fluorine's requests; it does **not** unlink
the shared directory or disable Steam Cloud itself. Steam can independently
sync that directory, including on game exit or account changes. This first
version is intended for a single account and one shared save collection: do not
switch Steam accounts while expecting the shared local folder to stay isolated.
In particular, Fluorine's abnormal-exit guard cannot undo an upload Steam itself
already performed.

## Successful explicit-sync experiment

With Cyberpunk closed, a real manual save made by launching through Fluorine was
found in its configured prefix. Steam's game-exit log showed it scanning a
different directory: its own `steamapps/compatdata/1091500/pfx` save root.

Both folders were backed up. The following developer-only sequence passed:

1. `SteamClient.Console.ExecCommand("cloud_sync_down 1091500")` started an actual
   `AC Launch,down` job and established Auto-Cloud's watched-file baseline.
2. Steam's Cyberpunk save directory was moved aside and replaced by a symlink to
   Fluorine's Cyberpunk save directory. A duplicate `ManualSave-1` was created
   from the user's new `ManualSave-0` to exercise new-slot uploads too.
3. `cloud_sync_up 1091500` scanned that shared folder and uploaded ten files,
   including the changed existing slots and the new slot. Steam logged successful
   HTTP uploads and `Upload complete, result OK`; cloud change number advanced
   from 43 to 44.
4. The local `ManualSave-1` directory was moved out of the save root, without
   editing `remotecache.vdf`. Another `cloud_sync_down 1091500` fetched its three
   files from Steam Cloud and completed at change number 44.
5. All three restored files matched the uploaded originals byte-for-byte. The
   restored `sav.dat` SHA-256 was
   `495d33ebf78b2c3d29e2605379221c4ca28de4bbcd726401731313d98e4db60e`.

No separate Steam login, Steam launch options, or direct RemoteStorage writes
were used. These are internal commands, so capability checks and a failure path
are still necessary. This proves file transfer on one Steam installation, not
cross-device game loading or production-ready conflict/offline behavior.

The test machine retains the shared save-directory symlink and duplicate test
slot. Original folders are preserved under Fluorine's
`backups/steam-cloud-test-20260906-2355` directory. Steam debugging is turned off
after the experiment. The automatic implementation above now manages equivalent
mapping setup; the manual experiment itself did not exercise the Run hooks.

## Earlier retry-only capability test

Steam was gracefully restarted with `-cef-enable-debugging`, with no game running.
Its `SharedJSContext` exposed `SteamClient.Cloud.RetryAppSync` and
`ResolveAppSyncConflict`. A fresh `RegisterForAppDetails` subscription reported
Cyberpunk (1091500) available, cloud enabled for both the account and game, cloud
status 3, and progress 0.

Calling `RetryAppSync(1091500)` was accepted but produced no new Cyberpunk sync job
in `logs/cloud_log.txt`. Repeating it after adding a temporary text marker to the
Steam prefix's Cyberpunk save directory also produced no new job or status
change. The marker was removed afterward; no existing save was edited or deleted.
This does not establish whether a text marker matches every cloud rule, but the
absence of any sync job means this method has not passed the required gate.

This test does **not** prove a download/upload round trip. In particular, an
accepted retry request and an unchanged "synchronized" status must not be shown
as confirmation that newly written saves have uploaded. The explicit console
operations above subsequently passed the file-transfer gate. Do not replace
Auto-Cloud synchronization with ordinary Steam API writes:
Cyberpunk's existing Auto-Cloud files use the WinSavedGames root, not necessarily
the default API root.

## Reproduce the probe

Use a development Python environment with `websocket-client` installed. Start
Steam with `-cef-enable-debugging`, then run from the repository root:

```sh
python tools/steam-cloud-probe.py 1091500
python tools/steam-cloud-probe.py 1091500 --status
```

The default command inspects capabilities. `--status` reads current client
status. With backups, a correctly mapped save folder, and no game running, use
`--sync-down` before play and `--sync-up` after play to request actual client
sync jobs. These can overwrite local or remote saves. `--retry` only invokes the
UI retry operation and is not reliable for starting a fresh sync. Check Steam's
cloud log and file contents separately. The probe does not restart
Steam, write its metadata, handle credentials, or claim upload completion.
Steam's debugging interface is internal and may change. Exit Steam and restart
normally to turn debugging off after development tests.

## Save-location fix

Launching, save deployment, and Python game plugins now resolve the same active
Wine prefix. Global Fluorine configuration takes precedence over explicit
instance configuration, followed by legacy detected paths, matching the existing
launcher policy. A compatibility-data parent is normalized to its `pfx` directory.
An explicitly configured but missing instance prefix does not silently select
another installation's saves.

`IOrganizer.winePrefixPath()` exposes that resolved prefix to Python plugins.
Before plugin initialization or with older hosts, the Python fallback prefers
Fluorine's configured prefix over its historical default.

Right-click the Saves tab, including its empty area, and use **Open active save
folder** or **Save location details**. The latter shows the prefix, game save
folder, and active folder, and writes those paths to the log. When profile-specific
saves are enabled, the profile directory can be mounted over the prefix directory
during play, hiding files manually copied underneath it.

## Remaining validation and scope limits

Cross-device in-game loading still needs user verification. Automated tests cover
scope, job-log recognition, mapping/backups, account/source journal identity,
and synced-file hash checks. An opt-in `SteamCloudLive` test exercises the C++
pre/post sync implementation against an already running debug-enabled Steam
client without launching the game. It is skipped in normal offline test runs.
On 2026-09-07 this C++ live test passed both with an existing debugging connection
and with the managed restart from ordinary Steam (8.6 seconds for restart plus
pre/post sync). The latter caught and fixed startup placeholder-account and
not-yet-loaded app-data races. A fresh interactive game session through the
packaged Run button remains a user acceptance check.

Flatpak/Snap Steam, games without an existing Steam prefix, profile-specific save
collections, multi-account isolation, and other games are not supported by the
automatic feature yet. Internal Steam APIs/log formats may change; failures
must remain explicit. Full crash, conflict, restart-recovery and cross-device
behavior should be exercised before treating this experimental integration as
a general-purpose production cloud client.

References:

- [Valve Steam Cloud documentation](https://partner.steamgames.com/doc/features/cloud)
- [Steam Cloud File Manager](https://github.com/Fldicoahkiin/SteamCloudFileManager)
- [Inspected Steam UI snapshot](https://github.com/SteamDatabase/SteamTracking/blob/bbc717da8ddd35ac9a091eb87664ae06d64c7145/ClientExtracted/steamui/chunk~2dcc5aaf7.js)
