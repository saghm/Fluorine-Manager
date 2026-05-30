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

## [0.2.3] - 2026-05-30

### Changed
- Made the virtual filesystem faster and more reliable for large mod lists,
  file moves, and frequent file changes.
- Updated the bundled FUSE support and added an optional `io_uring` setting.
- Made Proton launches and prefix setup use the expected Steam Linux Runtime
  environment more consistently.

### Added
- Added a Nexus settings button to remove Fluorine's download link association.

### Fixed
- Fixed download link association on Linux so old MO2 or Vortex handlers are
  less likely to keep taking priority after moving to Fluorine Manager.

## [0.2.2] - 2026-05-24

### Changed
- Updated the embedded MO2 integration through upstream 2.5.3 Beta 12.
- Stable release publishing now preserves existing draft `v*` releases while
  forcing published stable releases out of prerelease state and marking them
  as latest.

### Added
- Clearer prefix setup progress and failure logs, including installer exit
  code descriptions when dependency setup fails.

### Fixed
- Native Stardew Valley launch path detection.
- Invalid instance recovery messaging during startup.
- Prefix dependency setup and related VFS handling.
- Preview NIF third-party warning noise during builds.

## [0.1.3]

- First versioned Fluorine Manager release (baseline pre-CHANGELOG).
  See git history for earlier work.
