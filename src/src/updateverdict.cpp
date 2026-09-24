#include "updateverdict.h"

#include <QDate>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace UpdateVerdict
{

namespace
{

// Names of the evidence groups, as recorded in the meta's
// `updateEvidenceSampled` key. "files" covers the anchor file id, both upload
// timestamps and the chain successor; "fileVersions" covers the two per-file
// version labels.
const QString FILES_FIELD        = QStringLiteral("files");
const QString FILE_VERSION_FIELD = QStringLiteral("fileVersions");

}  // namespace

QStringList requiredEvidenceFields()
{
  return {FILES_FIELD, FILE_VERSION_FIELD};
}

bool evidenceFieldsSampled(const QStringList& sampled)
{
  const QStringList required = requiredEvidenceFields();
  return std::all_of(required.begin(), required.end(), [&sampled](const QString& name) {
    return sampled.contains(name);
  });
}

bool fileVersionLabelsSampled(const QStringList& sampled)
{
  return sampled.contains(FILE_VERSION_FIELD);
}

namespace
{

// --- small string helpers -------------------------------------------------

QString trimFromBothEnds(QString text, const QString& chars)
{
  qsizetype begin = 0;
  qsizetype end   = text.size();
  while (begin < end && chars.contains(text[begin])) {
    ++begin;
  }
  while (end > begin && chars.contains(text[end - 1])) {
    --end;
  }
  return text.mid(begin, end - begin);
}

// VersionFixer.py: value.strip().strip('"').strip("'")
QString stripQuotes(const QString& value)
{
  return trimFromBothEnds(trimFromBothEnds(value.trimmed(), QStringLiteral("\"")),
                          QStringLiteral("'"));
}

// VersionFixer.py: _strip_version_prefix() — a leading "v"/"f" directly
// followed by a digit is decoration, not part of the version.
QString stripVersionPrefix(const QString& value)
{
  if (value.size() > 1) {
    const QChar head = value[0].toLower();
    if ((head == QLatin1Char('v') || head == QLatin1Char('f')) && value[1].isDigit()) {
      return value.mid(1);
    }
  }
  return value;
}

// --- version parsing ------------------------------------------------------

struct ParsedVersion
{
  QStringList parts;
  QString     suffix;
  bool        valid = false;
};

ParsedVersion parseVersion(const QString& raw)
{
  ParsedVersion result;

  const QString text = stripVersionPrefix(stripQuotes(raw));
  if (text.isEmpty()) {
    return result;
  }

  static const QRegularExpression re(QRegularExpression::anchoredPattern(
      QStringLiteral("(\\d+(?:[._-]\\d+)*)([._+-]?[A-Za-z][A-Za-z0-9._+-]*)?")));

  const QRegularExpressionMatch match = re.match(text);
  if (!match.hasMatch()) {
    return result;
  }

  result.parts = match.captured(1).split(QRegularExpression(QStringLiteral("[._-]")));
  result.suffix = match.captured(2);
  result.valid  = true;
  return result;
}

QVector<int> toSegments(const ParsedVersion& parsed)
{
  QVector<int> segments;
  segments.reserve(parsed.parts.size());
  for (const QString& part : parsed.parts) {
    bool ok = false;
    const int value = part.toInt(&ok);
    if (!ok) {
      return {};
    }
    segments.append(value);
  }
  if (segments.isEmpty()) {
    return {};
  }
  // "1.1.0.0" and "1.1" denote the same version; drop trailing zeros but keep
  // at least one segment so "0" stays distinguishable from "nothing".
  while (segments.size() > 1 && segments.last() == 0) {
    segments.removeLast();
  }
  return segments;
}

int compareSegments(const QVector<int>& left, const QVector<int>& right)
{
  const qsizetype count = std::max(left.size(), right.size());
  for (qsizetype i = 0; i < count; ++i) {
    const int l = i < left.size() ? left[i] : 0;
    const int r = i < right.size() ? right[i] : 0;
    if (l != r) {
      return l < r ? -1 : 1;
    }
  }
  return 0;
}

bool isYearSegment(int segment)
{
  return segment >= 1900 && segment <= 2100;
}

// The digit string of the version with all separators removed: "1.102" and
// "1.10.2" both produce "1102".
QString digitString(const ParsedVersion& parsed)
{
  QString digits;
  for (const QString& part : parsed.parts) {
    digits += part;
  }
  return digits;
}

}  // namespace

bool isActiveFileStatus(int status)
{
  // MAIN, UPDATE, OPTIONAL, MISC.
  //
  // Mirrors NexusInterface::isActiveFileStatus() (nexusinterface.h:193) by
  // literal category id, because this translation unit must stay free of
  // nexusinterface.h's QtNetwork/UIBAse dependencies. Nothing links the two:
  // if a category is added or reclassified upstream, change both switches
  // together or they will silently disagree.
  switch (status) {
  case 1:
  case 2:
  case 3:
  case 5:
    return true;
  default:
    return false;
  }
}

bool isDateVersion(const QString& value)
{
  const QString text = value.trimmed();
  if (text.isEmpty()) {
    return false;
  }

  // 2024.05.01 / d2024.05.01(.1)
  static const QRegularExpression dottedYmd(QRegularExpression::anchoredPattern(
      QStringLiteral("d?(\\d{4})\\.(\\d{1,2})\\.(\\d{1,2})(?:\\.\\d+)?")),
      QRegularExpression::CaseInsensitiveOption);
  {
    const QRegularExpressionMatch match = dottedYmd.match(text);
    if (match.hasMatch()) {
      const QDate date(match.captured(1).toInt(), match.captured(2).toInt(),
                       match.captured(3).toInt());
      if (date.isValid()) {
        return true;
      }
    }
  }

  // 01.02.2024 / 1.2.2024 (day/month order ambiguous, still a date)
  static const QRegularExpression dottedAmbiguous(QRegularExpression::anchoredPattern(
      QStringLiteral("(\\d{1,2})\\.(\\d{1,2})\\.(\\d{4})(?:\\.0)?")));
  {
    const QRegularExpressionMatch match = dottedAmbiguous.match(text);
    if (match.hasMatch()) {
      const int first  = match.captured(1).toInt();
      const int second = match.captured(2).toInt();
      const int year   = match.captured(3).toInt();
      if (QDate(year, first, second).isValid() || QDate(year, second, first).isValid()) {
        return true;
      }
    }
  }

  // anything with a /12/2024 style segment
  static const QRegularExpression slashedYear(
      QStringLiteral("/\\d{1,2}/(?:19|20)\\d{2}"));
  if (slashedYear.match(text).hasMatch()) {
    return true;
  }

  // strptime()-style dates from VersionFixer.py.
  static const QStringList formats = {
      QStringLiteral("MM/dd/yyyy"), QStringLiteral("MM/dd/yy"),
      QStringLiteral("dd/MM/yyyy"), QStringLiteral("dd/MM/yy"),
      QStringLiteral("yyyy/MM/dd"), QStringLiteral("MM-dd-yyyy"),
      QStringLiteral("MM-dd-yy"),   QStringLiteral("dd-MM-yyyy"),
      QStringLiteral("dd-MM-yy"),   QStringLiteral("yyyy-MM-dd")};
  for (const QString& format : formats) {
    if (QDate::fromString(text, format).isValid()) {
      return true;
    }
  }

  return false;
}

Comparison compareNormalized(const QString& installedRaw, const QString& newestRaw)
{
  // Every early return below states *why* the comparison could not be made.
  // `indeterminate` is what keeps a refused comparison from being rendered as
  // "up to date" (see Comparison in updateverdict.h).
  const auto undecidable = [] { return Comparison{0, true}; };

  const QString installed = installedRaw.trimmed();
  const QString newest    = newestRaw.trimmed();

  // An unknown side cannot be ordered; biased towards "no flag".
  if (installed.isEmpty() || newest.isEmpty()) {
    return undecidable();
  }

  // A date-shaped version is not orderable, so any pair involving one is
  // undecidable rather than equal.
  if (isDateVersion(installed) || isDateVersion(newest)) {
    return undecidable();
  }

  const ParsedVersion left  = parseVersion(installed);
  const ParsedVersion right = parseVersion(newest);
  if (!left.valid || !right.valid) {
    return undecidable();
  }

  QVector<int> leftSegments  = toSegments(left);
  QVector<int> rightSegments = toSegments(right);
  if (leftSegments.isEmpty() || rightSegments.isEmpty()) {
    return undecidable();
  }

  const int cmp = compareSegments(leftSegments, rightSegments);
  if (cmp == 0) {
    // The numeric comparison actually ran and found them equal. Suffixes
    // ("2.0b" vs "2.0") are display-only and can never by themselves raise or
    // clear a flag, so this is a real "no update", not an unknown.
    return Comparison{0, false};
  }

  // Scheme-change guard: a year-like leading segment against a small leading
  // segment means the two sides are versioned differently (2024.5 vs 1.5), not
  // that one is newer. Symmetric, so both directions stay unflagged.
  if ((isYearSegment(leftSegments.first()) && rightSegments.first() < 100) ||
      (isYearSegment(rightSegments.first()) && leftSegments.first() < 100)) {
    return undecidable();
  }

  // Compact-segment ambiguity: "1.102" and "1.10.2" are the same digit string
  // split differently and therefore cannot be ordered safely.
  if (left.parts.size() != right.parts.size() && digitString(left) == digitString(right)) {
    return undecidable();
  }

  return Comparison{cmp, false};
}

Verdict compute(const Evidence& evidence, const VersionEvidence& versions)
{
  // Rule 1 — "Ignore this update".
  if (versions.ignored) {
    return Verdict::None;
  }

  // Rule 2 — the author's own succession statement for the installed file.
  if (evidence.chainSuccessorFileId > 0) {
    return Verdict::Update;
  }

  // Rule 3 — the installed file has stopped being offered, so the page is no
  // longer about it: that is "not current" whatever the dates or the version
  // labels say. OLD_VERSION (4), REMOVED (6), ARCHIVED (7) and ARCHIVED_HIDDEN
  // (1000) are all inactive; MAIN (1), UPDATE (2), OPTIONAL_FILE (3) and MISC (5)
  // are current (see NexusInterface::isActiveFileStatus).
  //
  // Two gates keep this honest. An unknown upload date (0) means the file was
  // never seen — that is how a file only hidden by Nexus is recorded — so it
  // cannot be placed against the page at all and stays on rule 5 below, which
  // answers "unknown" rather than inventing a date. And a file that sits *ahead*
  // of everything the page still offers was not succeeded by anything, so it is
  // the rollback rule 4 already knows, not an update: folding it into rule 3
  // would turn a rollback row into a false "outdated".
  if (evidence.installedFileStatus > 0 &&
      !isActiveFileStatus(evidence.installedFileStatus) &&
      evidence.installedFileUpdate > 0) {
    if (evidence.latestFileUpdate > 0 &&
        evidence.installedFileUpdate > evidence.latestFileUpdate) {
      return Verdict::Downgrade;
    }
    return Verdict::Update;
  }

  // Rule 4 — upload timestamps decide whenever both are known, which keeps the
  // verdict independent of version-string formatting. Equal timestamps are
  // equal: there is deliberately no fall-through to the version strings.
  if (evidence.latestFileUpdate > 0 && evidence.installedFileUpdate > 0) {
    if (evidence.latestFileUpdate > evidence.installedFileUpdate) {
      // The anchor file was uploaded later than the installed one. That is
      // only an update if the anchor is actually a newer release *of what is
      // installed*: the author routinely uploads sibling files back to back
      // (SE/AE vs VR builds minutes apart, a second optional patch), and a
      // later upload of a different file is not an update to this one.
      //
      // The labels decide that, and on a meta written before the labels were
      // collected there is nothing to decide it with: the two empty strings
      // mean "never sampled", not "equal". Ruling Update here would be
      // asserting the question was settled when it was never asked, so the
      // honest answer is Unknown until the file list has actually been
      // fetched. (A label that *was* sampled and came back empty falls through
      // to the comparison below, which refuses and yields Update on the dates
      // alone — that is the pre-existing behaviour, unchanged.)
      if (!evidence.fileVersionSampled) {
        return Verdict::Unknown;
      }

      // The labels veto a red, they never raise one — an unknown label
      // (empty, unparsable, date-shaped) leaves the date alone, so the rule
      // still does not depend on how a version happens to be formatted.
      const Comparison comparison = compareNormalized(evidence.installedFileVersion,
                                                      evidence.latestFileVersion);
      if (comparison.indeterminate || comparison.cmp < 0) {
        return Verdict::Update;
      }
      return Verdict::None;
    }
    if (evidence.installedFileUpdate > evidence.latestFileUpdate &&
        !isActiveFileStatus(evidence.installedFileStatus)) {
      // A file newer than anything the mod still offers, and the installed one
      // is no longer listed as current: the author rolled it back.
      return Verdict::Downgrade;
    }
    return Verdict::None;
  }

  // Rule 5 — guarded version fallback, only when the evidence is incomplete
  // (e.g. a mod that has never been checked with evidence collection).
  // A refused comparison is Unknown, not None: we did not establish that the
  // installed version is current, we established that we cannot tell.
  const Comparison comparison = compareNormalized(versions.installed, versions.newest);
  if (comparison.cmp < 0) {
    return Verdict::Update;
  }
  if (comparison.cmp > 0) {
    // V2 asymmetry, applied here exactly as in rule 4: being ahead of the
    // recorded newest is only a rollback when the installed file has stopped
    // being offered. A still-listed newer file is simply the newest file.
    return isActiveFileStatus(evidence.installedFileStatus) ? Verdict::None
                                                            : Verdict::Downgrade;
  }
  return comparison.indeterminate ? Verdict::Unknown : Verdict::None;
}

}  // namespace UpdateVerdict
