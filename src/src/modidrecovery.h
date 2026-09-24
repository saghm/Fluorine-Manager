#ifndef MODIDRECOVERY_H
#define MODIDRECOVERY_H

#include <QString>

// Conservative Nexus mod-id recovery (section D.1 of the update-check fix
// plan), ported from VersionFixer.py.
//
// Recovery only ever *fills in* a missing or invalid mod id: canApply() refuses
// to replace an id that is already valid, so a working mod is never re-pointed
// at a different Nexus entry. Everything in here is pure string handling so it
// can be unit tested without touching the disk; the caller is responsible for
// reading meta.ini / <archive>.meta and for persisting the result.
namespace ModIdRecovery
{

// True when `recovered` may be written over `current`: only an invalid disk
// value (< 1) is ever replaced, and only by a positive recovered id.
bool canApply(int current, int recovered);

// Pick a Nexus mod id from the recorded metadata, in priority order:
//   1. download .meta modid  (explicit value recorded by the downloader)
//   2. meta url             (a nexusmods.com/<game>/mods/<id> link)
//   3. download .meta url
//   4. archive filename heuristics (ISO download name, then the gated token
//      scan)
// Returns 0 when nothing can be recovered safely.
//
// The filename heuristics are skipped when the download's .meta directURL is a
// non-Nexus URL: such an archive was never fetched from Nexus, so its name
// cannot identify a Nexus mod. The explicit meta values above are still
// honoured, mirroring VersionFixer.py's _recover_mod_id().
int recover(const QString& metaUrl, const QString& installationFile,
            const QString& dlModId, const QString& dlUrl, const QString& dlDirectUrl);

// Extract "<id>" from a "nexusmods.com/<game>/mods/<id>" link. 0 if absent.
int extractFromUrl(const QString& url);

// Archive filename heuristics. Returns 0 unless every gate is met: the ISO
// Nexus download pattern, otherwise a '-'-separated token scan that requires
// at least 3 digits, a version-shaped tail, and a single longest winner.
int extractFromFilename(const QString& fileName);

}  // namespace ModIdRecovery

#endif  // MODIDRECOVERY_H
