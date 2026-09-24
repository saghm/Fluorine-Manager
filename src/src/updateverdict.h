#ifndef UPDATEVERDICT_H
#define UPDATEVERDICT_H

#include <QString>
#include <QStringList>

// Evidence-based Nexus update verdict.
//
// The on-screen verdict must never depend on how a version string happens to
// be formatted, so the ordered rule chain in compute() uses file-level
// evidence first (chain succession, installed-file status, upload timestamps)
// and only falls back to a guarded version comparison when that evidence
// cannot decide. The fallback normalizes both strings at comparison time only
// (compareNormalized(), section B.1 of the update-check fix plan); the stored
// version strings themselves are never rewritten.
namespace UpdateVerdict
{

enum class Verdict
{
  None,
  Update,
  Downgrade,
  // The check ran, but nothing could decide the question: no file-level
  // evidence *and* the version pair is not orderable (a side is missing or
  // unparsable, a date-shaped version, or a B.1 ambiguity guard fired).
  // Distinct from None: None asserts "no update", Unknown asserts "not
  // known", and only the former may be rendered as "up to date".
  Unknown,
};

// File-level evidence captured from the Nexus file list of the mod's page and
// persisted to meta.ini through ModInfoRegular::setUpdateEvidence().
struct Evidence
{
  // Upload timestamp (unix seconds) of the installed file and of the file the
  // verdict is decided against ("the anchor"). 0 means "unknown".
  //
  // While the installed file is still offered by Nexus the anchor is the
  // newest active file *of the same category*, so a later upload of some other
  // file on the page — a sibling platform variant, a different optional — can
  // never read as an update. Once the installed file is no longer offered
  // there is no same-category file left to compare against, so the anchor
  // falls back to the newest primary/MAIN file on the page and the question
  // becomes "has the whole page moved backwards past me?", which is what the
  // downgrade branch answers.
  qint64 installedFileUpdate = 0;
  qint64 latestFileUpdate    = 0;

  // Author-supplied version labels of those same two files exactly as Nexus
  // returned them ("4.1.0"). Deliberately *not* version= / newestVersion:
  // those describe the mod folder, which the user can edit, and newestVersion
  // is derived from the update chain rather than from the anchor. Empty means
  // unknown, and an unknown label can only leave a date-driven Update in
  // place — it can never clear one (see the veto in compute()).
  QString installedFileVersion;
  QString latestFileVersion;

  // True when the two labels above were recorded by a real file-list fetch.
  // A meta written before these fields existed carries two empty strings
  // that mean "never sampled", not "Nexus returned nothing", and the two cases
  // must not be confused: when they were never sampled rule 4 has no veto to
  // run, so its date-driven Update is unjustified and it answers Unknown
  // instead; when they were sampled and really came back empty, the veto
  // legitimately cannot fire and the dates stand on their own.
  bool fileVersionSampled = false;

  // Terminal successor of the installed file in its version chain — the file
  // with the highest position after it in the same mod_file_id group — 0 when
  // the installed file was not superseded by the author.
  int chainSuccessorFileId = 0;

  // Nexus category id of the installed file (meta key nexusFileStatus).
  int installedFileStatus = 1;
};

// Version strings, only consulted when the file evidence cannot decide.
struct VersionEvidence
{
  QString installed;
  QString newest;
  // "Ignore this update" state: ignoredVersion == newestVersion.
  bool ignored = false;
};

// Ordered rule chain: ignored -> chain successor -> installed file no longer
// offered (with the rollback exception and a known upload date) -> upload
// timestamps (gated by the two files' own version labels) -> guarded
// normalized version comparison. Evidence that is present always outranks the
// mod folder's version= / newestVersion strings.
Verdict compute(const Evidence& evidence, const VersionEvidence& versions);

// Names of the evidence groups compute() consumes, and therefore the names a
// meta.ini records in its `updateEvidenceSampled` key once a file-list fetch
// has written them.
//
// This list is the update check's only backfill trigger: a meta missing any
// name here is "pending", and ModInfo::filteredMods() puts pending mods in the
// set that gets a real file list. Nothing keys off a version number, so
// **adding a name here is all that is needed to backfill every meta written
// before it** — the name is absent from all of them, so the same mechanism
// fills it in. A name added here must also be written by
// ModInfoRegular::setUpdateEvidence(), or those metas stay pending forever;
// requiredEvidenceFields() is pinned by a test for exactly that reason.
QStringList requiredEvidenceFields();

/**
 * @return true when @p sampled covers every name in requiredEvidenceFields().
 */
bool evidenceFieldsSampled(const QStringList& sampled);

/**
 * @return true when @p sampled covers the per-file version labels, i.e. the
 *         installedFileVersion / latestFileVersion strings in the meta were
 *         written by a fetch rather than defaulted by saveMeta().
 */
bool fileVersionLabelsSampled(const QStringList& sampled);

// Result of the guarded comparison, separating "these are the same version"
// from "we could not tell".
struct Comparison
{
  // < 0 installed is older than newest (update), > 0 installed is newer
  // (downgrade), 0 equal *or* not orderable — see indeterminate.
  int cmp = 0;

  // True when cmp == 0 only because the two sides could not be ordered
  // safely: a side was empty or unparsable, a side was date-shaped, or a B.1
  // ambiguity guard fired. False when the numeric comparison actually ran and
  // found them equal (including the "2.0b" vs "2.0" case, where the numeric
  // core is identical and only the display suffix differs).
  bool indeterminate = false;
};

// Comparison-time-only version normalization (B.1).
//
// Every ambiguity is biased towards not raising a flag (the plan's B.1
// guardrails), but the bias is recorded in `indeterminate` so the caller can
// distinguish "no update" from "cannot tell" instead of painting the second as
// the first.
Comparison compareNormalized(const QString& installed, const QString& newest);

// True when the value is shaped like a date; date-shaped versions are not
// orderable and therefore compare as indeterminate. Exposed for tests.
bool isDateVersion(const QString& value);

// Nexus file categories that still count as "current" for the installed file
// (MAIN, UPDATE, OPTIONAL, MISC).
//
// This is a deliberate copy of NexusInterface::isActiveFileStatus()
// (nexusinterface.h:193) using literal category ids rather than
// NexusInterface::FileStatus constants: this translation unit stays free of
// the QtNetwork/UIBAse includes so it can be unit tested standalone. **If a
// file category is added or reclassified upstream, the switch below and
// nexusinterface.h must change together** — they are not linked, so nothing
// will catch the divergence at build time. Exposed for tests.
bool isActiveFileStatus(int status);

}  // namespace UpdateVerdict

#endif  // UPDATEVERDICT_H
