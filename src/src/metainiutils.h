#ifndef METAINIUTILS_H
#define METAINIUTILS_H

#include <QString>

namespace MetaIniUtils
{

// Canonicalize the keys in a per-mod meta.ini to the case used by upstream
// MO2's setValue/value calls (e.g. "gameName", "installationFile").
//
// Qt6's QSettings IniFormat is case-sensitive on Linux, so a meta.ini
// containing both "gameName=" and "gamename=" surfaces as two distinct
// keys. This helper folds case-insensitively-equal keys to a single entry
// using the canonical CamelCase, keeping the non-empty value when one is
// blank, otherwise the later occurrence wins.
//
// Unknown keys keep their original case and are still deduped.
//
// No-op if the file does not exist, is empty, or contains no case
// mismatches / duplicates. Returns true if the file was modified.
bool normalizeMetaIniCase(const QString& path);

}  // namespace MetaIniUtils

#endif  // METAINIUTILS_H
