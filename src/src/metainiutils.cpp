#include "metainiutils.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <QSaveFile>

namespace MetaIniUtils
{

namespace
{

// QSettings IniFormat continues a value when the line ends with an *odd*
// number of trailing backslashes; an even count is just an escaped backslash
// in the value.
bool endsInOddBackslashes(const QByteArray& line)
{
  int n = 0;
  for (qsizetype i = line.size() - 1; i >= 0 && line[i] == '\\'; --i) {
    ++n;
  }
  return (n % 2) == 1;
}

}  // namespace

bool normalizeMetaIniCase(const QString& path)
{
  QFile in(path);
  if (!in.exists() || !in.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray raw = in.readAll();
  in.close();

  if (raw.isEmpty()) {
    return false;
  }

  // Preserve trailing-newline status to write back faithfully.
  const bool hadTrailingNewline = raw.endsWith('\n');

  // QByteArray::split keeps an empty trailing element if the data ends in
  // the separator; drop it so we don't synthesize a phantom blank line.
  QList<QByteArray> rawLines = raw.split('\n');
  if (hadTrailingNewline && !rawLines.isEmpty() && rawLines.back().isEmpty()) {
    rawLines.removeLast();
  }

  struct Entry
  {
    bool isKey = false;
    QByteArray key;          // lowercased
    QByteArray rawLine;      // for non-key lines
    QByteArray fullKeyLine;  // for key lines, possibly multi-line via \\\n
  };

  struct Section
  {
    QByteArray header;  // [Name] line, empty for the implicit pre-header section
    QList<Entry> entries;
    QHash<QByteArray, qsizetype> indexByKey;
  };

  QList<Section> sections;
  Section cur;  // implicit pre-header / "General" section
  bool changed = false;

  for (qsizetype i = 0; i < rawLines.size(); ++i) {
    const QByteArray& line   = rawLines[i];
    const QByteArray trimmed = line.trimmed();

    if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
      sections.push_back(std::move(cur));
      cur        = Section{};
      cur.header = line;
      continue;
    }

    if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#')) {
      Entry e;
      e.isKey   = false;
      e.rawLine = line;
      cur.entries.push_back(std::move(e));
      continue;
    }

    const int eq = line.indexOf('=');
    if (eq < 0) {
      Entry e;
      e.isKey   = false;
      e.rawLine = line;
      cur.entries.push_back(std::move(e));
      continue;
    }

    // Collect continuation lines (odd trailing backslashes).
    QByteArray full = line;
    while (endsInOddBackslashes(full) && i + 1 < rawLines.size()) {
      ++i;
      full.append('\n');
      full.append(rawLines[i]);
    }

    const int fullEq           = full.indexOf('=');
    const QByteArray rawKey    = full.left(fullEq).trimmed();
    const QByteArray valuePart = full.mid(fullEq);  // includes '='
    const QByteArray lowerKey  = rawKey.toLower();
    if (rawKey != lowerKey) {
      changed = true;
    }

    Entry e;
    e.isKey       = true;
    e.key         = lowerKey;
    e.fullKeyLine = lowerKey + valuePart;

    auto it = cur.indexByKey.find(lowerKey);
    if (it != cur.indexByKey.end()) {
      Entry& prev = cur.entries[it.value()];
      const QByteArray newVal =
          valuePart.size() > 1 ? valuePart.mid(1).trimmed() : QByteArray();
      const int prevEq = prev.fullKeyLine.indexOf('=');
      const QByteArray prevVal =
          (prevEq >= 0 && prevEq + 1 < prev.fullKeyLine.size())
              ? prev.fullKeyLine.mid(prevEq + 1).trimmed()
              : QByteArray();
      if (newVal.isEmpty() && !prevVal.isEmpty()) {
        // Keep the existing non-empty value; drop the empty duplicate.
      } else {
        prev = std::move(e);
      }
      changed = true;
    } else {
      cur.entries.push_back(std::move(e));
      cur.indexByKey.insert(lowerKey, cur.entries.size() - 1);
    }
  }
  sections.push_back(std::move(cur));

  if (!changed) {
    return false;
  }

  QByteArray out;
  out.reserve(raw.size());
  for (qsizetype si = 0; si < sections.size(); ++si) {
    const Section& s = sections[si];
    if (!s.header.isEmpty()) {
      out.append(s.header);
      out.append('\n');
    }
    for (const Entry& e : s.entries) {
      if (e.isKey) {
        out.append(e.fullKeyLine);
      } else {
        out.append(e.rawLine);
      }
      out.append('\n');
    }
  }
  if (!hadTrailingNewline && out.endsWith('\n')) {
    out.chop(1);
  }

  QSaveFile saveFile(path);
  if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  saveFile.write(out);
  return saveFile.commit();
}

}  // namespace MetaIniUtils
