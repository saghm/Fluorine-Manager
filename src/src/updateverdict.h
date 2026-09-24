#ifndef UPDATEVERDICT_H
#define UPDATEVERDICT_H

#include <QString>

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
};

// File-level evidence captured from the per-mod Nexus "files" response and
// persisted to meta.ini through ModInfoRegular::setUpdateEvidence().
struct Evidence
{
  // Upload timestamp (unix seconds) of the installed file and of the newest
  // primary/MAIN (fallback: newest active) file. 0 means "unknown".
  qint64 installedFileUpdate = 0;
  qint64 latestFileUpdate    = 0;

  // Terminal successor of the installed file in the file_updates chain,
  // 0 when the installed file was not superseded by the author.
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

// Ordered rule chain: ignored -> chain successor -> installed file status ->
// upload timestamps -> guarded normalized version comparison. Evidence that
// is present always outranks version strings.
Verdict compute(const Evidence& evidence, const VersionEvidence& versions);

// Comparison-time-only version normalization (B.1).
//
// Returns < 0 when `installed` is older than `newest` (update), > 0 when it is
// newer (downgrade) and 0 for "equal or cannot be ordered safely". Every
// ambiguity is biased towards 0, i.e. towards not raising a flag.
int compareNormalized(const QString& installed, const QString& newest);

// True when the value is shaped like a date; date-shaped versions are not
// orderable and therefore compare as equal. Exposed for tests.
bool isDateVersion(const QString& value);

// Nexus file categories that still count as "current" for the installed file
// (MAIN, UPDATE, OPTIONAL, MISC). Keep in sync with
// NexusInterface::isActiveFileStatus() in nexusinterface.h. Exposed for tests.
bool isActiveFileStatus(int status);

}  // namespace UpdateVerdict

#endif  // UPDATEVERDICT_H
