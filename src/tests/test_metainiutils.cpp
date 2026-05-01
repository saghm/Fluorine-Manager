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

// Sanity: file with only lowercase keys is left untouched.
TEST(MetaIniUtils, NoOpOnAlreadyLowercase)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "author=Aether\n"
                         "category=\"11,\"\n";
  const QString path = writeIni(dir, contents);
  EXPECT_FALSE(MetaIniUtils::normalizeMetaIniCase(path));
  EXPECT_EQ(QByteArray(contents), readAll(path));
}

// The reported bug: pre-existing CamelCase keys + lowercase setValue() leaves
// duplicates on Linux/Qt6. Normalize must fold case-only duplicates.
TEST(MetaIniUtils, DedupesCaseOnlyDuplicates)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // Mirrors what ModInfoRegular::saveMeta() leaves behind when meta.ini
  // already had CamelCase keys: both casings coexist, and the lowercase
  // copy may even be empty because readMeta() couldn't see the original.
  const auto* contents =
      "[General]\n"
      "Author=Aether\n"
      "Category=11\n"
      "Color=@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff\\x43\\x43\\xff\\xff\\xff\\xff\\0\\0)\n"
      "Comments=keep me\n"
      "Converted=false\n"
      "author=\n"
      "category=\"11,\"\n"
      "comments=\n"
      "converted=true\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  // Open via QSettings and verify exactly one canonical lowercase key per
  // logical setting.
  QSettings s(path, QSettings::IniFormat);
  EXPECT_EQ(QStringList({"author", "category", "color", "comments", "converted"}),
            s.allKeys());

  // Empty lowercase duplicate should NOT clobber the non-empty CamelCase
  // value — `Comments=keep me` survives over `comments=`.
  EXPECT_EQ("Aether", s.value("author").toString());
  EXPECT_EQ("keep me", s.value("comments").toString());

  // For non-empty duplicates the latest write wins, mirroring last-writer
  // semantics so QSettings doesn't reintroduce stale data.
  EXPECT_EQ("11,", s.value("category").toString());
  EXPECT_TRUE(s.value("converted").toBool());

  // Color value (containing escaped binary) round-trips intact.
  const QByteArray after = readAll(path);
  EXPECT_TRUE(after.contains(
      "color=@Variant(\\0\\0\\0\\x43\\x1\\xff\\xff\\x43\\x43\\xff\\xff\\xff\\xff\\0\\0)"))
      << after.toStdString();
  EXPECT_FALSE(after.contains("Author="));
  EXPECT_FALSE(after.contains("Color="));
}

// QSettings IniFormat supports multi-line values via trailing-`\` line
// continuation. Continuation lines must travel with their key when the key is
// renamed to lowercase, otherwise the value gets corrupted.
TEST(MetaIniUtils, PreservesLineContinuations)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "NotesField=line one\\\n"
                         "  continued line two\n"
                         "Author=Aether\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  const QByteArray after = readAll(path);
  EXPECT_TRUE(after.contains("notesfield=line one\\\n"));
  EXPECT_TRUE(after.contains("  continued line two"));
  EXPECT_FALSE(after.contains("NotesField="));
}

// Plugin settings live in nested `[Plugins\<name>]` sections. The dedupe
// logic must scope per-section — `Foo=` under [General] must not collide
// with `foo=` under [Plugins\Whatever].
TEST(MetaIniUtils, DedupesPerSection)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto* contents = "[General]\n"
                         "Foo=top\n"
                         "[Plugins\\Sample]\n"
                         "Foo=plugin\n"
                         "foo=plugin-dup\n";
  const QString path = writeIni(dir, contents);
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));

  QSettings s(path, QSettings::IniFormat);
  EXPECT_EQ("top", s.value("foo").toString());
  s.beginGroup("Plugins/Sample");
  EXPECT_EQ("plugin-dup", s.value("foo").toString());
  s.endGroup();
}

// End-to-end: reproduce the exact symptom from the bug report — install
// path opens QSettings, writes lowercase keys; without normalization the
// resulting file ends up with CamelCase + lowercase duplicates.
TEST(MetaIniUtils, FixesQSettingsDuplicationOnLinux)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  // Pre-existing meta.ini in CamelCase (e.g. shipped inside the mod
  // archive, or migrated from MO2 Windows).
  const auto* preexisting = "[General]\n"
                            "Author=Aether\n"
                            "Category=11\n"
                            "Comments=keep me\n";
  const QString path = writeIni(dir, preexisting);

  // Without normalization, lowercase setValue adds NEW keys alongside the
  // CamelCase ones — confirm the bug exists in the bare QSettings flow.
  {
    QSettings s(path, QSettings::IniFormat);
    s.setValue("author", "Aether");
    s.setValue("category", "11,");
    s.setValue("comments", "");
  }
  const QByteArray dirty = readAll(path);
  EXPECT_TRUE(dirty.contains("Author=Aether"));
  EXPECT_TRUE(dirty.contains("author=Aether"));

  // Normalize and re-run the same install-style writes. After this the
  // file must have exactly one casing per key.
  EXPECT_TRUE(MetaIniUtils::normalizeMetaIniCase(path));
  {
    QSettings s(path, QSettings::IniFormat);
    s.setValue("author", "Aether");
    s.setValue("category", "11,");
  }

  const QByteArray clean = readAll(path);
  EXPECT_FALSE(clean.contains("Author="));
  EXPECT_FALSE(clean.contains("Category="));
  EXPECT_FALSE(clean.contains("Comments="));
  EXPECT_TRUE(clean.contains("author=Aether"));
  EXPECT_TRUE(clean.contains("category=\"11,\""));
  // The non-empty original Comments value survived the empty duplicate.
  EXPECT_TRUE(clean.contains("comments=keep me"));
}
