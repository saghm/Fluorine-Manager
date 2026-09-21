#include "gamepath.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

TEST(GamePath, ResolvesImportedWindowsByteArrayBesideInstanceIni)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QDir instance(temporary.path());
  ASSERT_TRUE(instance.mkpath("Game Root"));
  QFile executable(instance.filePath("Game Root/SkyrimSE.exe"));
  ASSERT_TRUE(executable.open(QIODevice::WriteOnly));
  executable.close();

  const QString iniPath = instance.filePath("ModOrganizer.ini");
  QFile ini(iniPath);
  ASSERT_TRUE(ini.open(QIODevice::WriteOnly));
  ini.write("[General]\ngamePath=@ByteArray(.\\\\Game Root)\n");
  ini.close();

  QSettings settings(iniPath, QSettings::IniFormat);
  const QString path = resolveStoredGamePath(
      QString::fromUtf8(settings.value("gamePath").toByteArray()), iniPath);
  EXPECT_EQ(path, instance.filePath("Game Root"));
  EXPECT_TRUE(QDir(path).exists("SkyrimSE.exe"));
}

TEST(GamePath, ResolvesRelativePathsWithoutRequiringExistingDirectories)
{
  const QString iniPath = QStringLiteral("/instances/STD/ModOrganizer.ini");
  for (const QString& stored : {QStringLiteral("./Game Root"),
                                QStringLiteral("Game Root"),
                                QStringLiteral(".\\Game Root")}) {
    EXPECT_EQ(resolveStoredGamePath(stored, iniPath),
              QStringLiteral("/instances/STD/Game Root"));
  }
  EXPECT_EQ(resolveStoredGamePath("..\\Shared Game", iniPath),
            QStringLiteral("/instances/Shared Game"));
}

TEST(GamePath, PreservesAbsoluteAndEmptyPaths)
{
  const QString iniPath = QStringLiteral("/instances/STD/ModOrganizer.ini");
  EXPECT_EQ(resolveStoredGamePath("/games/Skyrim Special Edition", iniPath),
            QStringLiteral("/games/Skyrim Special Edition"));
  EXPECT_EQ(resolveStoredGamePath("Z:\\games\\Skyrim Special Edition", iniPath),
            QStringLiteral("/games/Skyrim Special Edition"));
  EXPECT_EQ(resolveStoredGamePath("C:\\Games\\Skyrim", iniPath),
            QStringLiteral("C:\\Games\\Skyrim"));
  EXPECT_TRUE(resolveStoredGamePath({}, iniPath).isEmpty());
}
