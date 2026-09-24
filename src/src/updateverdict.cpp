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

int compareNormalized(const QString& installedRaw, const QString& newestRaw)
{
  const QString installed = installedRaw.trimmed();
  const QString newest    = newestRaw.trimmed();

  // An unknown side cannot be ordered; biased towards "no flag".
  if (installed.isEmpty() || newest.isEmpty()) {
    return 0;
  }

  // A date-shaped version is not orderable, so any pair involving one is equal.
  if (isDateVersion(installed) || isDateVersion(newest)) {
    return 0;
  }

  const ParsedVersion left  = parseVersion(installed);
  const ParsedVersion right = parseVersion(newest);
  if (!left.valid || !right.valid) {
    return 0;
  }

  QVector<int> leftSegments  = toSegments(left);
  QVector<int> rightSegments = toSegments(right);
  if (leftSegments.isEmpty() || rightSegments.isEmpty()) {
    return 0;
  }

  const int cmp = compareSegments(leftSegments, rightSegments);
  if (cmp == 0) {
    // Equal numeric core: suffixes ("2.0b" vs "2.0") are display-only and can
    // never by themselves raise or clear a flag.
    return 0;
  }

  // Scheme-change guard: a year-like leading segment against a small leading
  // segment means the two sides are versioned differently (2024.5 vs 1.5), not
  // that one is newer. Symmetric, so both directions stay unflagged.
  if ((isYearSegment(leftSegments.first()) && rightSegments.first() < 100) ||
      (isYearSegment(rightSegments.first()) && leftSegments.first() < 100)) {
    return 0;
  }

  // Compact-segment ambiguity: "1.102" and "1.10.2" are the same digit string
  // split differently and therefore cannot be ordered safely.
  if (left.parts.size() != right.parts.size() && digitString(left) == digitString(right)) {
    return 0;
  }

  return cmp;
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

  // Rule 3 — installed file category (preserved verbatim from the previous
  // verdict): OLD_VERSION (4) and REMOVED (6) both mean "not current".
  // (ARCHIVED_HIDDEN is 1000, not 6 — see NexusInterface::FileStatus.)
  if (evidence.installedFileStatus == 4 || evidence.installedFileStatus == 6) {
    return Verdict::Update;
  }

  // Rule 4 — upload timestamps decide whenever both are known, which keeps the
  // verdict independent of version-string formatting. Equal timestamps are
  // equal: there is deliberately no fall-through to the version strings.
  if (evidence.latestFileUpdate > 0 && evidence.installedFileUpdate > 0) {
    if (evidence.latestFileUpdate > evidence.installedFileUpdate) {
      return Verdict::Update;
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
  const int cmp = compareNormalized(versions.installed, versions.newest);
  if (cmp < 0) {
    return Verdict::Update;
  }
  if (cmp > 0) {
    return Verdict::Downgrade;
  }
  return Verdict::None;
}

}  // namespace UpdateVerdict
