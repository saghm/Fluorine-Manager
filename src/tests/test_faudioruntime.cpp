#include "faudioruntime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
QByteArray read(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

void write(const QString& path, const QByteArray& data)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(data), data.size());
}

class FAudioRuntimeTest : public testing::Test {
protected:
  QTemporaryDir temporary;
  QString prefix = temporary.path() + "/pfx";
  QString bundles = temporary.path() + "/faudio";
  FAudioPayload payload;
  QString error;

  void SetUp() override
  {
    ASSERT_TRUE(temporary.isValid());
    ASSERT_TRUE(QDir().mkpath(prefix + "/drive_c/windows/system32"));
    ASSERT_TRUE(QDir().mkpath(prefix + "/drive_c/windows/syswow64"));
    makePack("latest", "first");
    makePack("safe", "baseline");
  }

  void makePack(const QString& variant, const QByteArray& revision)
  {
    QStringList dlls;
    for (const auto& family : {std::pair{"xaudio2_", 10}, {"x3daudio1_", 8},
                              {"xactengine3_", 8}, {"xapofx1_", 5}}) {
      const int first = QString(family.first) == "xapofx1_" ? 1 : 0;
      for (int i = first; i < first + family.second; ++i)
        dlls.append(QString(family.first) + QString::number(i) + ".dll");
    }
    for (int i : {0, 4, 7, 9}) dlls.append("xactengine2_" + QString::number(i) + ".dll");
    const QString bundle = bundles + "/" + variant;
    QByteArray checksums;
    for (const QString& arch : {QStringLiteral("i386-windows"), QStringLiteral("x86_64-windows")}) {
      for (const QString& dll : dlls) {
        const QString relative = arch + "/" + dll;
        const QByteArray data = QByteArray(128, '\0') + relative.toUtf8() + revision;
        write(bundle + "/" + relative, data);
        checksums += QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() +
                     "  " + relative.toUtf8() + "\n";
      }
    }
    write(bundle + "/sha256sums.txt", checksums);
    write(bundle + "/version.txt", "faudio=26.09\noverride=native\nvariant=" + variant.toUtf8() +
          "\nrecipe=" + revision + "\n");
  }

  bool install(const QString& variant = "latest")
  {
    return installFAudioPayload(prefix, bundles + "/" + variant, payload, error);
  }

  bool refresh(const QString& variant = {})
  {
    return refreshFAudioPayload(prefix, bundles, variant, payload, error);
  }
};

TEST_F(FAudioRuntimeTest, InstallsBothArchitecturesAndSkipsUnchangedFiles)
{
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_TRUE(payload.changed);
  EXPECT_EQ(payload.dlls.size(), 35);
  EXPECT_EQ(read(prefix + "/drive_c/windows/system32/xaudio2_7.dll"),
            read(bundles + "/latest/x86_64-windows/xaudio2_7.dll"));
  EXPECT_EQ(read(prefix + "/drive_c/windows/syswow64/xactengine3_7.dll"),
            read(bundles + "/latest/i386-windows/xactengine3_7.dll"));
  ASSERT_TRUE(refresh()) << error.toStdString();
  EXPECT_FALSE(payload.changed);
}

TEST_F(FAudioRuntimeTest, RefreshesRecipeEvenWhenFAudioVersionIsUnchanged)
{
  ASSERT_TRUE(install());
  makePack("latest", "compatibility-fix");
  ASSERT_TRUE(refresh()) << error.toStdString();
  EXPECT_TRUE(payload.changed);
  EXPECT_TRUE(read(prefix + "/drive_c/windows/system32/xaudio2_7.dll").endsWith("compatibility-fix"));
  EXPECT_TRUE(read(prefix + "/.fluorine-faudio/version.txt").contains("compatibility-fix"));
}

TEST_F(FAudioRuntimeTest, RepairsFilesReplacedDuringProtonChanges)
{
  ASSERT_TRUE(install());
  const QString target = prefix + "/drive_c/windows/system32/xaudio2_7.dll";
  write(target, "different Proton audio DLL");
  ASSERT_TRUE(QFile::remove(prefix + "/drive_c/windows/syswow64/xactengine3_7.dll"));
  ASSERT_TRUE(refresh()) << error.toStdString();
  EXPECT_TRUE(payload.changed);
  EXPECT_EQ(read(target), read(bundles + "/latest/x86_64-windows/xaudio2_7.dll"));
  EXPECT_TRUE(QFileInfo::exists(prefix + "/drive_c/windows/syswow64/xactengine3_7.dll"));
}

TEST_F(FAudioRuntimeTest, PreservesRecordedVariantAndHonorsExplicitOverride)
{
  ASSERT_TRUE(install("safe"));
  ASSERT_TRUE(refresh());
  EXPECT_FALSE(payload.changed);
  ASSERT_TRUE(refresh("latest"));
  EXPECT_TRUE(payload.changed);
  EXPECT_TRUE(read(prefix + "/.fluorine-faudio/version.txt").contains("variant=latest"));
  EXPECT_FALSE(refresh("../latest"));
}

TEST_F(FAudioRuntimeTest, LeavesUnmanagedPrefixesAlone)
{
  ASSERT_TRUE(refresh());
  EXPECT_FALSE(payload.changed);
  EXPECT_FALSE(QFileInfo::exists(prefix + "/.fluorine-faudio"));
}

TEST_F(FAudioRuntimeTest, RejectsDamagedPackBeforeChangingAnyInstalledFile)
{
  ASSERT_TRUE(install());
  const QByteArray original = read(prefix + "/drive_c/windows/system32/xaudio2_7.dll");
  const QByteArray marker = read(prefix + "/.fluorine-faudio/version.txt");
  makePack("latest", "replacement");
  write(bundles + "/latest/i386-windows/xaudio2_9.dll", "corrupt");
  ASSERT_FALSE(refresh());
  EXPECT_EQ(read(prefix + "/drive_c/windows/system32/xaudio2_7.dll"), original);
  EXPECT_EQ(read(prefix + "/.fluorine-faudio/version.txt"), marker);
}

TEST_F(FAudioRuntimeTest, RejectsDuplicateChecksumAndIncompleteModuleSets)
{
  const QString checksums = bundles + "/latest/sha256sums.txt";
  const QByteArray contents = read(checksums);
  auto lines = contents.split('\n');
  lines[1] = lines[0];
  write(checksums, lines.join('\n'));
  EXPECT_FALSE(install());
  write(checksums, contents);
  ASSERT_TRUE(QFile::rename(bundles + "/latest/i386-windows/xaudio2_9.dll",
                           bundles + "/latest/i386-windows/unexpected.dll"));
  EXPECT_FALSE(install());
}

TEST_F(FAudioRuntimeTest, ReplacesDllLinksWithoutWritingSharedProtonFiles)
{
  const QString shared = temporary.path() + "/proton/xaudio2_7.dll";
  const QString target = prefix + "/drive_c/windows/system32/xaudio2_7.dll";
  write(shared, "shared runner");
  ASSERT_TRUE(QFile::link(shared, target));
  ASSERT_TRUE(install()) << error.toStdString();
  EXPECT_FALSE(QFileInfo(target).isSymLink());
  EXPECT_EQ(read(shared), "shared runner");
}

TEST_F(FAudioRuntimeTest, RejectsDirectoryLinksAndMissingBundles)
{
  ASSERT_TRUE(QDir(prefix + "/drive_c/windows/syswow64").removeRecursively());
  const QString shared = temporary.path() + "/proton";
  ASSERT_TRUE(QDir().mkpath(shared));
  ASSERT_TRUE(QFile::link(shared, prefix + "/drive_c/windows/syswow64"));
  EXPECT_FALSE(install());
  EXPECT_TRUE(QDir(shared).isEmpty());
  ASSERT_TRUE(QFile::remove(prefix + "/drive_c/windows/syswow64"));
  ASSERT_TRUE(QDir().mkpath(prefix + "/drive_c/windows/syswow64"));
  ASSERT_TRUE(install());
  ASSERT_TRUE(QDir(bundles).removeRecursively());
  EXPECT_FALSE(refresh());
}
}
