#ifndef METAINIUTILS_H
#define METAINIUTILS_H

#include <QString>

namespace MetaIniUtils
{

// Pre-normalize a Qt INI file so QSettings (case-sensitive in IniFormat on
// Linux/Qt6) does not produce duplicate-cased keys when `setValue("foo", x)`
// is invoked on a file already containing `Foo=...`.
//
// Lowercases all keys, deduplicates per-section (keeping the latest
// non-empty value), and preserves comments, blank lines, and ordering of
// surviving keys. Multi-line values (trailing `\` continuations) are
// preserved as a single logical entry.
//
// No-op if the file does not exist or has no case mismatches / duplicates.
// Returns true if the file was modified.
bool normalizeMetaIniCase(const QString& path);

}  // namespace MetaIniUtils

#endif  // METAINIUTILS_H
