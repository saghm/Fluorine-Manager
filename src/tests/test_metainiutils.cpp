#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "metainiutils.h"

namespace
{

QString writeIni(const QTemporaryDir& dir, const char* contents)
{
  const QString path = QDir(dir.path()).filePath("meta.ini");
  QFile f(path);
  EXPECT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(contents);
  f.close();
  return path;
}

QByteArray readAll(const QString& path)
{
  QFile f(path);
  EXPECT_TRUE(f.open(QIODevice::ReadOnly));
  return f.readAll();
}

}  // namespace

// Sanity: empty / non-existent file is a no-op.
TEST(MetaIniUtils, NoOpOnMissingFile)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = QDir(dir.path()).filePath("nope.ini");
  EXPECT_FALSE(MetaIniUtils::normalizeMetaIniCase(path));
}

// Sanity: file with only canonical-case keys is left untouched.
TEST(MetaIniUtils, NoOpOnAlreadyCanonical)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "author=Aether\n"
                         "category=\"11,\"\n"
                         "gameName=Skyrim\n"
                         "installationFile=Foo.7z\n";
  const QString path = writeIni(dir, contents);
  EXPECT_FALSE(MetaIniUtils::normalizeMetaIniCase(path));
  EXPECT_EQ(QByteArray(contents), readAll(path));
}

// The reported bug: pre-existing CamelCase keys + a lowercased duplicate
// (left over from the 5d1fb29 era) coexist on Linux/Qt6. Normalize must fold
// them back to the upstream MO2 case.
TEST(MetaIniUtils, FoldsLowercaseDuplicatesToUpstreamCase)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // Mirrors the dirty meta.ini in the bug report: CamelCase keys with
  // empty/default values from the broken saveMeta(), and lowercase keys
  // carrying the real preserved values.
  const auto* contents =
      "[General]\n"
      "gameName=SkyrimSE\n"
      "gamename=Skyrim\n"
      "installationFile=\n"
      "installationfile=Sweet Mother HD-4947-2-0.7z\n"
      "newestVersion=\n"
      "newestversion=2.0.0.0\n"
      "lastNexusQuery=2026-05-01T17:28:22Z\n"
      "lastnexusquery=2026-05-01T17:27:42Z\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  // Open via QSettings using the upstream CamelCase keys.
  QSettings s(path, QSettings::IniFormat);
  // Each logical setting collapses to a single canonical-case key.
  QStringList keys = s.allKeys();
  std::sort(keys.begin(), keys.end());
  EXPECT_EQ(QStringList({"gameName", "installationFile", "lastNexusQuery",
                         "newestVersion"}),
            keys);

  // Empty CamelCase line dropped in favor of the non-empty lowercase value.
  EXPECT_EQ("Sweet Mother HD-4947-2-0.7z",
            s.value("installationFile").toString());
  EXPECT_EQ("2.0.0.0", s.value("newestVersion").toString());

  // Both halves were non-empty for gameName and lastNexusQuery — last writer
  // wins, mirroring saveMeta()'s order-of-writes.
  EXPECT_EQ("Skyrim", s.value("gameName").toString());
  EXPECT_EQ("2026-05-01T17:27:42Z", s.value("lastNexusQuery").toString());

  const QByteArray after = readAll(path);
  EXPECT_FALSE(after.contains("gamename="));
  EXPECT_FALSE(after.contains("installationfile="));
  EXPECT_FALSE(after.contains("newestversion="));
  EXPECT_FALSE(after.contains("lastnexusquery="));
}

// QSettings IniFormat supports multi-line values via trailing-`\` line
// continuation. Continuation lines must travel with their key when the key
// is canonicalized, otherwise the value gets corrupted.
TEST(MetaIniUtils, PreservesLineContinuations)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "nexusdescription=line one\\\n"
                         "  continued line two\n"
                         "author=Aether\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  const QByteArray after = readAll(path);
  EXPECT_TRUE(after.contains("nexusDescription=line one\\\n"));
  EXPECT_TRUE(after.contains("  continued line two"));
  EXPECT_FALSE(after.contains("nexusdescription="));
}

// Plugin settings live in nested `[Plugins\<name>]` sections. The dedupe
// logic must scope per-section — a key under [General] must not collide
// with the same lowercase key under [Plugins\Whatever].
TEST(MetaIniUtils, DedupesPerSection)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "gameName=Skyrim\n"
                         "[Plugins\\Sample]\n"
                         "Foo=plugin\n"
                         "foo=plugin-dup\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  QSettings s(path, QSettings::IniFormat);
  EXPECT_EQ("Skyrim", s.value("gameName").toString());
  s.beginGroup("Plugins/Sample");
  // Unknown plugin key keeps its first-seen case, last-writer wins on value.
  EXPECT_EQ("plugin-dup", s.value("Foo").toString());
  s.endGroup();
}

// End-to-end: an upstream-style saveMeta sequence over a normalized file
// updates in place — no second case-mismatched key appears.
TEST(MetaIniUtils, UpstreamSaveMetaProducesNoDupes)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // Dirty file from the 5d1fb29 era — CamelCase + lowercase coexist.
  const auto* preexisting = "[General]\n"
                            "gameName=SkyrimSE\n"
                            "gamename=Skyrim\n"
                            "installationFile=\n"
                            "installationfile=Foo.7z\n";
  const QString path = writeIni(dir, preexisting);

  // Normalize folds lowercase dupes back into the upstream CamelCase keys.
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  // Upstream-style saveMeta: CamelCase setValue on every key.
  {
    QSettings s(path, QSettings::IniFormat);
    s.setValue("gameName", s.value("gameName"));
    s.setValue("installationFile", s.value("installationFile"));
  }

  const QByteArray clean = readAll(path);
  EXPECT_TRUE(clean.contains("gameName=Skyrim"));
  EXPECT_TRUE(clean.contains("installationFile=Foo.7z"));
  // No lowercase dupes resurrected.
  EXPECT_FALSE(clean.contains("gamename="));
  EXPECT_FALSE(clean.contains("installationfile="));
}
