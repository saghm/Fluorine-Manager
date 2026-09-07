#include "../src/steamcloudsync.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(SteamCloudScope, OnlyCyberpunkGameExecutable)
{
  EXPECT_TRUE(SteamCloud::supported("1091500", "/game/bin/x64/Cyberpunk2077.exe"));
  EXPECT_FALSE(SteamCloud::supported("1091500", "REDmod.exe"));
  EXPECT_FALSE(SteamCloud::supported("1091500", "REDprelauncher.exe"));
  EXPECT_FALSE(SteamCloud::supported("489830", "Cyberpunk2077.exe"));
}

TEST(SteamCloudEnvironment, SteamDoesNotInheritBundledLibrariesOrWineState)
{
  QProcessEnvironment env;
  env.insert("LD_LIBRARY_PATH", "/fluorine/lib");
  env.insert("FLUORINE_ORIG_LD_LIBRARY_PATH", "/host/lib");
  env.insert("QT_PLUGIN_PATH", "/fluorine/qt6plugins");
  env.insert("QT_QPA_PLATFORM", "offscreen");
  env.insert("WINEPREFIX", "/prefix");
  env.insert("SteamAppId", "1091500");
  env.insert("DISPLAY", ":0");
  const auto clean = SteamCloud::clientEnvironment(env);
  EXPECT_EQ(clean.value("LD_LIBRARY_PATH"), "/host/lib");
  EXPECT_FALSE(clean.contains("FLUORINE_ORIG_LD_LIBRARY_PATH"));
  EXPECT_FALSE(clean.contains("QT_PLUGIN_PATH"));
  EXPECT_FALSE(clean.contains("QT_QPA_PLATFORM"));
  EXPECT_FALSE(clean.contains("WINEPREFIX"));
  EXPECT_FALSE(clean.contains("SteamAppId"));
  EXPECT_EQ(clean.value("DISPLAY"), ":0");
}

TEST(SteamCloudLog, RequiresMatchingNewJobAndHandlesSplitLines)
{
  SteamCloud::JobLog log(true);
  log.consume("[AppID 1091500] Upload complete, result OK\n");
  EXPECT_FALSE(log.complete());
  log.consume("[AppID 489830] Starting sync (up,AC Exit,)\n");
  log.consume("[AppID 1091500] Upload complete, result OK\n");
  EXPECT_FALSE(log.complete());
  log.consume("[AppID 1091500] Starting sync (up,AC Exit,)\n[AppID 1091500] Upload comp");
  EXPECT_TRUE(log.started());
  log.consume("lete, result OK\n");
  EXPECT_TRUE(log.complete());
}

TEST(SteamCloudLog, DownloadMustFinishWatchingAndErrorsWin)
{
  SteamCloud::JobLog log(false);
  log.consume("[AppID 1091500] Starting sync (AC Launch,down,)\n");
  log.consume("[AppID 1091500] Download complete, result OK\n");
  EXPECT_FALSE(log.complete());
  log.consume("[AppID 1091500] AutoCloud done. Watching 10 files\n");
  EXPECT_TRUE(log.complete());
  log.consume("[AppID 1091500] sync conflict\n");
  EXPECT_FALSE(log.complete());
  EXPECT_FALSE(log.error().isEmpty());
}

TEST(SteamCloudLog, NoChangesUploadIsACompletionButWrongDirectionIsNot)
{
  SteamCloud::JobLog log(true);
  log.consume("[AppID 1091500] Starting sync (AC Launch,down,)\n");
  log.consume("[AppID 1091500] Upload complete in build list\n");
  EXPECT_FALSE(log.complete());
  log.consume("[AppID 1091500] Starting sync (up,AC Exit,)\n");
  log.consume("[AppID 1091500] Upload complete in build list\n");
  EXPECT_TRUE(log.complete());
}

TEST(SteamCloudJournal, RejectsDifferentAccountOrSaveCollection)
{
  QJsonObject record{{"account", "76561198871023486"}, {"source", "/prefix/saves"}, {"pending", true}};
  EXPECT_TRUE(SteamCloud::matchingJournal({}, "a", "b"));
  EXPECT_TRUE(SteamCloud::matchingJournal(record, "76561198871023486", "/prefix/saves"));
  EXPECT_FALSE(SteamCloud::matchingJournal(record, "76561198871023487", "/prefix/saves"));
  EXPECT_FALSE(SteamCloud::matchingJournal(record, "76561198871023486", "/other/saves"));
}

TEST(SteamCloudAccount, RejectsStartupPlaceholderAndMalformedIds)
{
  EXPECT_TRUE(SteamCloud::validAccount("76561198871023486"));
  EXPECT_TRUE(SteamCloud::validAccount("76561202255233023"));
  EXPECT_FALSE(SteamCloud::validAccount("76561197960265728"));
  EXPECT_FALSE(SteamCloud::validAccount("0"));
  EXPECT_FALSE(SteamCloud::validAccount(""));
  EXPECT_FALSE(SteamCloud::validAccount("76561202255233024"));
}

namespace {
void writeFile(const QString& path, const QByteArray& contents)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}
}

TEST(SteamCloudMapping, BacksUpDirectoryAndReusesCorrectLink)
{
  QTemporaryDir temp;
  const auto source = temp.filePath("fluorine/Cyberpunk 2077");
  const auto target = temp.filePath("steam/Cyberpunk 2077");
  writeFile(source + "/ManualSave-0/sav.dat", "new");
  writeFile(target + "/ManualSave-0/sav.dat", "old");
  const auto backup = SteamCloud::mapSaveFolder(source, target);
  EXPECT_TRUE(QFileInfo::exists(backup + "/ManualSave-0/sav.dat"));
  EXPECT_EQ(QFileInfo(target).canonicalFilePath(), source);
  EXPECT_TRUE(SteamCloud::mapSaveFolder(source, target).isEmpty());
  const auto other = temp.filePath("other/Cyberpunk 2077");
  ASSERT_TRUE(QDir().mkpath(other));
  EXPECT_THROW(SteamCloud::mapSaveFolder(other, target), std::runtime_error);
  EXPECT_EQ(QFileInfo(target).canonicalFilePath(), source);
}

TEST(SteamCloudMapping, RejectsWrongLeavesAndNonDirectoryTargets)
{
  QTemporaryDir temp;
  const auto source = temp.filePath("fluorine/Cyberpunk 2077");
  ASSERT_TRUE(QDir().mkpath(source));
  EXPECT_THROW(SteamCloud::mapSaveFolder(source, temp.filePath("steam")), std::runtime_error);
  const auto target = temp.filePath("steam/Cyberpunk 2077");
  writeFile(target, "file");
  EXPECT_THROW(SteamCloud::mapSaveFolder(source, target), std::runtime_error);
}

TEST(SteamCloudBackup, PreservesFilesAndRejectsSymlinks)
{
  QTemporaryDir temp;
  const auto source = temp.filePath("saves");
  writeFile(source + "/ManualSave-0/sav.dat", "save bytes");
  const auto snapshot = SteamCloud::snapshotSaves(source, temp.filePath("backups"));
  QFile file(snapshot + "/ManualSave-0/sav.dat");
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), "save bytes");
  ASSERT_TRUE(QFile::link(source + "/ManualSave-0/sav.dat", source + "/link"));
  EXPECT_THROW(SteamCloud::snapshotSaves(source, temp.filePath("backups")), std::runtime_error);
}

TEST(SteamCloudCollection, ComparesContentsAndReplacesWithoutRenamingSlots)
{
  QTemporaryDir temp;
  const auto steam = temp.filePath("steam/Cyberpunk 2077");
  const auto fluorine = temp.filePath("fluorine/Cyberpunk 2077");
  writeFile(steam + "/ManualSave-0/sav.dat", "cloud");
  writeFile(steam + "/steam_autocloud.vdf", "marker");
  writeFile(fluorine + "/ManualSave-0/sav.dat", "local");
  EXPECT_FALSE(SteamCloud::saveCollectionsMatch(steam, fluorine));
  const QString backup = SteamCloud::replaceSaveCollection(steam, fluorine);
  EXPECT_TRUE(QFileInfo::exists(backup + "/ManualSave-0/sav.dat"));
  EXPECT_TRUE(SteamCloud::saveCollectionsMatch(steam, fluorine));
  EXPECT_TRUE(QFileInfo::exists(fluorine + "/ManualSave-0/sav.dat"));
  EXPECT_FALSE(QFileInfo::exists(fluorine + "/steam_autocloud.vdf"));
  EXPECT_FALSE(QFileInfo::exists(fluorine + "/ManualSave-1/sav.dat"));
}

TEST(SteamCloudCache, RequiresSyncedHashAndIncludesNewSlots)
{
  QTemporaryDir temp;
  writeFile(temp.filePath("ManualSave-0/sav.dat"), "save bytes");
  const auto hash = QCryptographicHash::hash("save bytes", QCryptographicHash::Sha1).toHex();
  const QByteArray cache = "\"1091500\" { \"CD Projekt Red/Cyberpunk 2077/ManualSave-0/sav.dat\" { "
      "\"root\" \"9\" \"syncstate\" \"1\" \"persiststate\" \"0\" \"size\" \"10\" \"sha\" \"" + hash + "\" } }";
  EXPECT_TRUE(SteamCloud::cacheMatchesSaves(temp.path(), cache));
  EXPECT_FALSE(SteamCloud::cacheMatchesSaves(temp.path(), "invalid"));
  writeFile(temp.filePath("ManualSave-1/sav.dat"), "save bytes");
  EXPECT_FALSE(SteamCloud::cacheMatchesSaves(temp.path(), cache));
}


TEST(SteamCloudCache, DetectsCloudFilesMissingLocally)
{
  QTemporaryDir temp;
  const QByteArray cache = "\"1091500\" { \"CD Projekt Red/Cyberpunk 2077/ManualSave-0/sav.dat\" { "
      "\"root\" \"9\" \"syncstate\" \"1\" \"persiststate\" \"0\" \"size\" \"10\" "
      "\"sha\" \"0000000000000000000000000000000000000000\" } }";
  EXPECT_FALSE(SteamCloud::cacheMatchesSaves(temp.path(), cache));
}

TEST(SteamCloudLive, PreAndPostSyncWithoutLaunchingGame)
{
  const auto game = qEnvironmentVariable("FLUORINE_TEST_STEAM_CLOUD_GAME_DIR");
  const auto saves = qEnvironmentVariable("FLUORINE_TEST_STEAM_CLOUD_SAVE_DIR");
  if (game.isEmpty() || saves.isEmpty()) GTEST_SKIP() << "Opt-in live Steam test; may synchronize real saves";
  // The live smoke test must fail, not hang invisibly, if a user decision is needed.
  QTimer dismiss;
  QObject::connect(&dismiss, &QTimer::timeout, [] {
    for (auto* widget : QApplication::topLevelWidgets())
      if (auto* box = qobject_cast<QMessageBox*>(widget)) {
        if (qEnvironmentVariable("FLUORINE_TEST_ALLOW_STEAM_RESTART") == "1"
            && box->windowTitle() == "Restart Steam for cloud sync")
          box->button(QMessageBox::Yes)->click();
        else
          box->reject();
      }
  });
  dismiss.start(100);
  QTemporaryDir settings;
  SteamCloudSync session(nullptr, settings.filePath("test.ini"), game, saves);
  ASSERT_TRUE(session.prepare());
  ASSERT_TRUE(session.markLaunching());
  ASSERT_TRUE(session.finish(true));
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
