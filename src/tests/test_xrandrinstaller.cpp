#include "xrandrinstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
QByteArray arMember(const QByteArray& name, const QByteArray& contents)
{
  return name.leftJustified(16, ' ') + QByteArray("0").leftJustified(12, ' ') +
         QByteArray("0").leftJustified(6, ' ').repeated(2) +
         QByteArray("100644").leftJustified(8, ' ') +
         QByteArray::number(contents.size()).leftJustified(10, ' ') + "`\n" +
         contents + (contents.size() % 2 ? "\n" : "");
}

QByteArray tarMember(const QByteArray& name, const QByteArray& contents)
{
  QByteArray header(512, '\0');
  header.replace(0, name.size(), name);
  auto octal = [&](int offset, int width, qint64 value) {
    header.replace(offset, width, QByteArray::number(value, 8).rightJustified(width - 1, '0') + '\0');
  };
  octal(100, 8, 0755);
  octal(108, 8, 0);
  octal(116, 8, 0);
  octal(124, 12, contents.size());
  octal(136, 12, 0);
  header.replace(148, 8, QByteArray(8, ' '));
  header[156] = '0';
  header.replace(257, 6, QByteArray("ustar\0", 6));
  header.replace(263, 2, "00");
  unsigned checksum = 0;
  for (unsigned char byte : header) checksum += byte;
  header.replace(148, 8, QByteArray::number(checksum, 8).rightJustified(6, '0') + QByteArray("\0 ", 2));
  return header + contents + QByteArray((512 - contents.size() % 512) % 512, '\0');
}

class XrandrInstaller : public testing::Test {
protected:
  QTemporaryDir dir;
  QByteArray previousPath = qgetenv("PATH");
  QByteArray binary = QByteArray("\x7f" "ELF", 4) + "test helper";
  QString package = dir.filePath("package.deb");
  QString destination = dir.filePath("installed/xrandr");

  void SetUp() override { ASSERT_TRUE(dir.isValid()); }
  void TearDown() override { qputenv("PATH", previousPath); }
  void write(const QString& path, const QByteArray& data) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(data), data.size());
  }
  QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
  }
  void makePackage(const QByteArray& path = "./usr/bin/xrandr",
                   const QByteArray& data = {}) {
    auto tar = tarMember(path, data.isEmpty() ? binary : data) + QByteArray(1024, '\0');
    write(package, "!<arch>\n" + arMember("debian-binary", "2.0\n") +
                   arMember("control.tar", "odd") + arMember("data.tar/", tar));
  }
};

TEST_F(XrandrInstaller, InstallsExecutableWithoutArAndReplacesOldHelper)
{
  const auto tar = QStandardPaths::findExecutable("tar");
  ASSERT_FALSE(tar.isEmpty());
  ASSERT_TRUE(QDir().mkpath(dir.filePath("tools")));
  ASSERT_TRUE(QFile::link(tar, dir.filePath("tools/tar")));
  qputenv("PATH", dir.filePath("tools").toLocal8Bit());
  ASSERT_TRUE(QStandardPaths::findExecutable("ar").isEmpty());
  makePackage();
  ASSERT_TRUE(QDir().mkpath(QFileInfo(destination).absolutePath()));
  write(destination, "old helper");
  EXPECT_TRUE(installXrandrFromDeb(package, destination).isEmpty());
  EXPECT_EQ(read(destination), binary);
  EXPECT_TRUE(QFileInfo(destination).isExecutable());
}

TEST_F(XrandrInstaller, RejectsMalformedAndTruncatedPackages)
{
  for (const auto& bytes : {QByteArray("not a deb"),
       QByteArray("!<arch>\n") + arMember("data.tar", "abc").chopped(2),
       QByteArray("!<arch>\n") + arMember("control.tar", "abc"),
       QByteArray("!<arch>\n") + arMember("data.tar", "abc") + arMember("data.tar.xz", "def")}) {
    write(package, bytes);
    EXPECT_FALSE(installXrandrFromDeb(package, destination).isEmpty());
    EXPECT_FALSE(QFileInfo::exists(destination));
  }
}

TEST_F(XrandrInstaller, InstallsXzCompressedDebianPayloadWithoutAr)
{
  const auto xz = QStandardPaths::findExecutable("xz");
  if (xz.isEmpty()) GTEST_SKIP() << "xz is required to exercise Debian's compressed payload";
  const auto tar = QStandardPaths::findExecutable("tar");
  ASSERT_FALSE(tar.isEmpty());
  QProcess compress;
  compress.start(xz, {"--compress", "--stdout"});
  ASSERT_TRUE(compress.waitForStarted());
  compress.write(tarMember("./usr/bin/xrandr", binary) + QByteArray(1024, '\0'));
  compress.closeWriteChannel();
  ASSERT_TRUE(compress.waitForFinished());
  ASSERT_EQ(compress.exitCode(), 0);
  const auto payload = compress.readAllStandardOutput();
  ASSERT_FALSE(payload.isEmpty());
  write(package, "!<arch>\n" + arMember("debian-binary", "2.0\n") + arMember("data.tar.xz", payload));
  ASSERT_TRUE(QDir().mkpath(dir.filePath("tools")));
  ASSERT_TRUE(QFile::link(tar, dir.filePath("tools/tar")));
  ASSERT_TRUE(QFile::link(xz, dir.filePath("tools/xz")));
  qputenv("PATH", dir.filePath("tools").toLocal8Bit());
  EXPECT_TRUE(installXrandrFromDeb(package, destination).isEmpty());
  EXPECT_EQ(read(destination), binary);
}

TEST_F(XrandrInstaller, ReportsMissingTarWithoutLosingPreviousHelper)
{
  makePackage();
  ASSERT_TRUE(QDir().mkpath(QFileInfo(destination).absolutePath()));
  write(destination, "old helper");
  qputenv("PATH", dir.path().toLocal8Bit());
  EXPECT_TRUE(installXrandrFromDeb(package, destination).contains("Cannot start tar"));
  EXPECT_EQ(read(destination), "old helper");
}

TEST_F(XrandrInstaller, RejectsMissingMemberAndNonExecutablePayload)
{
  makePackage("./usr/bin/unrelated");
  EXPECT_TRUE(installXrandrFromDeb(package, destination).contains("extraction failed"));
  EXPECT_FALSE(QFileInfo::exists(destination));
  makePackage("./usr/bin/xrandr", "not an executable");
  EXPECT_TRUE(installXrandrFromDeb(package, destination).contains("valid executable"));
  EXPECT_FALSE(QFileInfo::exists(destination));
}

TEST_F(XrandrInstaller, CancellationDoesNotTouchDestination)
{
  makePackage();
  int cancel = 1;
  EXPECT_TRUE(installXrandrFromDeb(package, destination, &cancel).contains("cancelled"));
  EXPECT_FALSE(QFileInfo::exists(destination));
}

TEST_F(XrandrInstaller, ReportsDestinationErrorsAndDoesNotFollowSymlinks)
{
  makePackage();
  write(dir.filePath("blocked"), "keep");
  EXPECT_FALSE(installXrandrFromDeb(package, dir.filePath("blocked/xrandr")).isEmpty());
  ASSERT_TRUE(QFile::link(dir.filePath("blocked"), dir.filePath("link")));
  EXPECT_FALSE(installXrandrFromDeb(package, dir.filePath("link")).isEmpty());
  EXPECT_TRUE(QFileInfo(dir.filePath("link")).isSymLink());
  EXPECT_EQ(read(dir.filePath("blocked")), "keep");
}
}
