#include "modidrecovery.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace ModIdRecovery
{

namespace
{

// VersionFixer.py: _canonical_nexus_mod_id(). Keeps only a plain positive
// integer without leading zeros (and at least `minDigits` digits), so a token
// like "007" or "1.5" can never be mistaken for a mod id.
QString canonicalModId(const QString& value, qsizetype minDigits = 1)
{
  const QString text = value.trimmed();

  static const QRegularExpression digits(QRegularExpression::anchoredPattern(
      QStringLiteral("\\d+")));
  if (!digits.match(text).hasMatch()) {
    return {};
  }
  if (text.size() < minDigits) {
    return {};
  }
  if (text.size() > 1 && text.startsWith(QLatin1Char('0'))) {
    return {};
  }
  if (text.toInt() <= 0) {
    return {};
  }
  return text;
}

QString baseName(const QString& path)
{
  const qsizetype slash = std::max(path.lastIndexOf(QLatin1Char('/')),
                                   path.lastIndexOf(QLatin1Char('\\')));
  return slash >= 0 ? path.mid(slash + 1) : path;
}

QString withoutArchiveSuffix(QString stem)
{
  static const QStringList extensions = {QStringLiteral(".tar.gz"), QStringLiteral(".7z"),
                                         QStringLiteral(".zip"), QStringLiteral(".rar")};
  const QString lowered = stem.toLower();
  for (const QString& extension : extensions) {
    if (lowered.endsWith(extension)) {
      stem.chop(extension.size());
      break;
    }
  }
  return stem;
}

}  // namespace

bool canApply(int current, int recovered)
{
  return current < 1 && recovered > 0;
}

int extractFromUrl(const QString& url)
{
  static const QRegularExpression link(
      QStringLiteral("nexusmods\\.com/[^/]+/mods/(\\d+)"),
      QRegularExpression::CaseInsensitiveOption);

  const QRegularExpressionMatch match = link.match(url);
  if (!match.hasMatch()) {
    return 0;
  }
  return canonicalModId(match.captured(1)).toInt();
}

int extractFromFilename(const QString& fileName)
{
  if (fileName.trimmed().isEmpty()) {
    return 0;
  }

  const QString stem = withoutArchiveSuffix(baseName(fileName));
  if (stem.isEmpty()) {
    return 0;
  }

  // Nexus download names: "<modid> v<version> YYYY-MM-DDTHH-MMZ".
  static const QRegularExpression isoPattern(
      QStringLiteral("\\b(\\d{1,9})\\s+v?\\d[A-Za-z0-9._+-]*\\s+"
                     "\\d{4}-\\d{2}-\\d{2}T\\d{2}-\\d{2}Z\\b"),
      QRegularExpression::CaseInsensitiveOption);
  {
    const QRegularExpressionMatch match = isoPattern.match(stem);
    if (match.hasMatch()) {
      if (const int isoId = canonicalModId(match.captured(1)).toInt(); isoId > 0) {
        return isoId;
      }
    }
  }

  QStringList tokens = stem.split(QLatin1Char('-'));

  // Trailing Nexus download timestamps (9-11 digits) are not version tokens.
  bool hasTrailingTimestamp = false;
  static const QRegularExpression trailingTimestamp(
      QRegularExpression::anchoredPattern(QStringLiteral("\\d{9,11}")));
  if (!tokens.isEmpty() && trailingTimestamp.match(tokens.constLast()).hasMatch()) {
    tokens.removeLast();
    hasTrailingTimestamp = true;
  }

  static const QRegularExpression versionHead(
      QRegularExpression::anchoredPattern(QStringLiteral("v?\\d.*")),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression versionPart(
      QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9._+]+")));

  std::vector<std::pair<int, QString>> candidates;
  for (qsizetype index = 0; index + 1 < tokens.size(); ++index) {
    // One- and two-digit filename tokens are overwhelmingly versions, dates or
    // part numbers. Small real Nexus ids must come from an explicit URL/.meta
    // value instead of this filename-only heuristic.
    const QString candidate = canonicalModId(tokens[index], 3);
    if (candidate.isEmpty()) {
      continue;
    }

    const QStringList versionTail = tokens.mid(index + 1);
    if (versionTail.isEmpty()) {
      continue;
    }
    if (!versionHead.match(versionTail.first()).hasMatch()) {
      continue;
    }

    bool tailIsVersionShaped = true;
    for (const QString& part : versionTail) {
      if (!versionPart.match(part).hasMatch()) {
        tailIsVersionShaped = false;
        break;
      }
    }
    if (!tailIsVersionShaped) {
      continue;
    }

    // "<id>-<one bare token>" is too weak without a timestamp to anchor it.
    if (!hasTrailingTimestamp && versionTail.size() == 1 &&
        !versionTail.first().contains(QLatin1Char('.')) &&
        !versionTail.first().contains(QLatin1Char('_'))) {
      continue;
    }

    candidates.emplace_back(candidate.size(), candidate);
  }

  if (!candidates.empty()) {
    const int longest = std::max_element(candidates.begin(), candidates.end(),
                                         [](const auto& lhs, const auto& rhs) {
                                           return lhs.first < rhs.first;
                                         })
                            ->first;

    QString winner;
    int matches = 0;
    for (const auto& [length, token] : candidates) {
      if (length == longest) {
        winner = token;
        ++matches;
      }
    }
    // Ambiguity between equally long winners means the filename does not carry
    // a safe answer; bail out rather than guess.
    if (matches == 1) {
      return winner.toInt();
    }
  }

  return 0;
}

int recover(const QString& metaUrl, const QString& installationFile, const QString& dlModId,
            const QString& dlUrl, const QString& dlDirectUrl)
{
  if (const int id = canonicalModId(dlModId).toInt(); id > 0) {
    return id;
  }
  if (const int id = extractFromUrl(metaUrl); id > 0) {
    return id;
  }
  if (const int id = extractFromUrl(dlUrl); id > 0) {
    return id;
  }

  // A download whose recorded direct URL is not on Nexus was not fetched from
  // Nexus, so its archive name carries no Nexus identity.
  if (!dlDirectUrl.trimmed().isEmpty() &&
      !dlDirectUrl.contains(QStringLiteral("nexusmods.com"), Qt::CaseInsensitive)) {
    return 0;
  }

  return extractFromFilename(installationFile);
}

}  // namespace ModIdRecovery
