# Fluorine Manager — Changelog

All notable user-facing changes to Fluorine Manager land here. Versioning
follows SemVer (MAJOR.MINOR.PATCH). Two distribution channels:

- **stable** — tagged `v*` releases. Each tag cuts a normal GitHub release.
- **beta** — rolling release under the `beta` tag. Every push to `main` that
  builds successfully replaces the assets on that one release — no new tag
  per build. Beta builds embed a `B<timestamp>` suffix in their version
  string (e.g. `0.1.4B202604231800`) and a commit hash, so the in-app
  updater can tell whether the build you're running already matches the
  current rolling release.

## [Unreleased]

### Added
- In-app self-update checker with channel toggle (stable / beta) in
  Settings → General. Runs on startup (when "Check for updates" is on) and
  surfaces new releases without auto-installing.
- Fluorine version is now distinct from the embedded MO2 engine version.
  About dialog shows `Fluorine Manager <version>` with the MO2 engine
  version and build commit on the revision line.
- CI publishes a rolling `beta` GitHub release on each push to `main`,
  including a machine-parseable `fluorine-meta` block (timestamp, commit)
  that the updater reads to detect matching builds.
- FUSE VFS now handles `rmdir`, unblocking tools like Wrye Bash that tear
  down temp directories inside the virtual Data folder.

### Fixed
- Bulk hide from Conflict tab (#54) no longer fails with a misleading
  "Input/output error" when the DirectoryEntry tree has lowercased a
  parent directory. `FileRenamer` now resolves case mismatches against the
  real filesystem before giving up, and the rename path surfaces the
  actual errno text instead of conflating the Windows error code 5 with
  errno 5 (EIO).
- Wrye Bash `NotADirectoryError: [WinError 267]` during Data-folder
  initialization (#47). Rooted in the missing FUSE `rmdir` op.

## [0.1.3]

- First versioned Fluorine Manager release (baseline pre-CHANGELOG).
  See git history for earlier work.
