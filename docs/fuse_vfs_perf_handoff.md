# FUSE VFS Performance Handoff (SkyrimSE / MO2 Linux Port)

## Context
- Project: MO2 Linux Port with C
- Test instance: `/home/luke/Games/ElderTeej`
- Symptom: VFS tree build is very fast (`~300ms`), but Skyrim startup can stall for many minutes.
- Baseline expectation from user: startup should be around `2-3 minutes`.

## Core Diagnosis
The slow path is **not** initial VFS construction.  
The bottleneck is sustained runtime metadata traffic (Wine/game probing), especially:
- `getattr`
- `readdir`
- (previously) `ioctl` probes

Observed pattern during bad runs:
- Very high `getattr/readdir/ioctl`
- Very low `read`
- Process appears mostly idle in btop (low MB read/write), yet VFS op counters climb rapidly

## Why This Happens
FUSE builds a tree once, but then serves kernel/user-space requests continuously.  
Games + Wine often do heavy existence/stat/directory probing before and during menu load.

So startup cost can be dominated by metadata round-trips even if actual file reads are small.

## Major Evidence from Logs
- Early problematic runs showed huge ioctl growth (millions) with low reads.
- After fixes, `ioctl` can be driven to `0`.
- Even with `ioctl=0`, `readdir/getattr` remained very high in some phases:
  - root (`""`)
  - `ShaderCache/*`
  - `meshes/actors/character`
  - `meshes/actors/character/facegenmorphs`
  - `Seq`

## Implemented Changes (Chronological Summary)

### 1) Persistent file handles for open files (big fix)
- Reworked open/read/write/release to use handle state.
- Avoided reopening backing files on every read.
- `write` path moved to `pwrite`.
- `release` now closes tracked FDs.

Impact:
- Reduced repeated open/close overhead for active handles.

---

### 2) Added ioctl handler (then corrected behavior)
- Implemented `mo2_ioctl`.
- Returned `ENOTTY` for known probe-heavy commands (`VFAT_IOCTL_READDIR_BOTH`, `FS_IOC_GETFLAGS`).
- Added ioctl hotloop diagnostics.

Impact:
- Helped identify ioctl storms.

---

### 3) Enabled directory operation caching primitives
- Added `opendir` / `releasedir`.
- Added directory handle cache for entries.
- Added `readdirplus` callback and capability flags initially.
- Added negative lookup caching (`fuse_reply_entry` with `ino=0`, TTL).
- Increased entry/attr TTL values in replies.

Impact:
- Reduced some repeated rebuild work, but did not fully eliminate metadata storm.

---

### 4) Added instrumentation for hotspot visibility
- Global op counters (`lookup/getattr/readdir/open/read/ioctl`).
- Top hot paths logging for lookup hit/miss/getattr/readdir.
- Sampling mechanism to keep logs manageable.

Impact:
- Confirmed dominant hotspots and phase shifts.

---

### 5) Stale mount cleanup hardening
- Added proactive cleanup for stale `mo2linux` mounts (including trash paths).

Impact:
- Stability/cleanup improvement, not direct startup speedup.

---

### 6) Stabilized synthetic directory timestamps
- Virtual dir stat times changed from `now` per request to fixed constant.

Impact:
- Prevents needless cache invalidation behavior due to changing timestamps.

---

### 7) Lock/contention reduction in inode path
- Switched inode mutex to shared mutex.
- Added inode fast lookup (`get`) separate from `getOrCreate`.
- Reduced exclusive lock pressure on read-heavy paths.

Impact:
- Better concurrency under FUSE multithread loop.

---

### 8) Per-handle path retention in readdir path
- Stored directory path on open dir handles to avoid repeated inode-to-path resolution.

Impact:
- Reduced overhead in hot `readdir` loop.

---

### 9) Prebuilt encoded directory blobs
- Built and reused encoded `readdir` and `readdirplus` blobs.
- Added global per-path blob caches.

Impact:
- Makes repeated directory listing replies cheaper.

---

### 10) FD exhaustion fix (critical stability fix)
- Runtime crash seen:
  - `GLib-ERROR: Creating pipes for GWakeup: Too many open files`
- Cause: too many retained open handles.
- Fix:
  - Do **not** keep persistent FD for read-only handles.
  - Open temp FD in read path when needed, close immediately.
  - Keep persistent FD only for writable handles.
  - Added `open_handles` counter in log line.

Impact:
- Stabilized handle growth behavior and avoided EMFILE crash path.

---

### 11) Disabled custom ioctl callback in ops table
- Stopped wiring `ops->ioctl`.

Impact:
- `ioctl` count dropped to `0` in user logs.
- Removed large class of user-space ioctl churn.

---

### 12) Removed `default_permissions` mount option (experiment)
- Tested to reduce permission-check-driven metadata round-trips.

Impact:
- Mixed/limited; metadata storm still exists in hot phases.

---

### 13) Disabled `readdirplus` capability/callback (experiment)
- Stopped advertising/using readdirplus.

Impact:
- Profile changed; in latest successful run startup reached `2m40s`.

---

## Current State (Most Important)
- User reported successful startup in **~2 minutes 40 seconds**.
- `ioctl=0`.
- `open_handles` still can grow into the thousands in long runs, but EMFILE crash path improved substantially after RO FD change.
- Remaining hot metadata phases are path-specific (shader cache and facegen/character meshes).

## Remaining Known Issues
- Menu/runtime phases still generate heavy `getattr/readdir` on certain directories.
- `open_handles` can still increase significantly depending on workload phase.

## What Appears to Have Helped Most
1. Removing ioctl churn (`ioctl=0`).
2. Avoiding persistent RO file FDs (stability fix).
3. Directory response caching plus reduced overhead paths.
4. Readdirplus/permissions experiments likely affected metadata profile depending on phase.

## Suggested Next Experiments (if further optimization is desired)

1. Add small in-process `getattr` LRU cache (short TTL, e.g. 0.5-1.0s) keyed by inode/path.
2. Introduce targeted caches for hottest directories:
   - `meshes/actors/character`
   - `meshes/actors/character/facegenmorphs`
   - `ShaderCache/*`
   - `Seq`
3. Add optional “performance mode” config flag:
   - less verbose logging
   - larger TTLs
   - aggressive directory blob reuse
4. Track `release`/`releasedir` deltas in logs to verify handle churn balance.
5. Validate `ulimit -n` at launch and warn if too low.

## Useful Log Fields for Future Triage
- `ops lookup=... getattr=... readdir=... open=... read=... ioctl=... open_handles=...`
- `hot readdir: ...`
- `hot getattr: ...`
- `hot lookup_hit/lookup_miss: ...`

## Bottom Line
The root misunderstanding was “fast VFS build should imply fast startup.”  
In practice, startup was dominated by post-build metadata probing.  
After multiple fixes, the system reached user-acceptable startup (`~2m40s`) with much better stability and no ioctl storm.
