#include "protonlauncher.h"
#include "launchenvironment.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>
#include <uibase/log.h>

// Avoid probing the user's Steam installation in a launch test. The launcher
// and the subprocess/environment handling themselves are the production code.
QString findSteamPath() { return {}; }
static QString slrRunScript;
QString getSlrRunScript() { return slrRunScript; }
QString fluorineDataDir() { return {}; }

TEST(ProtonLocale, NeutralFallbacksUseUnicode)
{
  for (const QString& locale : {QString{}, QString{"C"}, QString{"POSIX"}}) {
    SCOPED_TRACE(locale.toStdString());
    QProcessEnvironment env;
    env.insert("LANG", locale);
    prepareProtonLocale(env);
    EXPECT_EQ(env.value("LANG"), "C.UTF-8");
    EXPECT_FALSE(env.contains("LC_ALL"));
    EXPECT_FALSE(env.contains("HOST_LC_ALL"));
    EXPECT_FALSE(env.contains("LC_CTYPE"));
  }
}

TEST(ProtonLocale, PreservesLanguageAndIndependentCategories)
{
  QProcessEnvironment env;
  env.insert("LANG", "ja_JP.UTF-8");
  env.insert("LC_MESSAGES", "fr_FR.UTF-8");
  env.insert("LANGUAGE", "fr:ja:en");
  env.insert("LC_NUMERIC", "C");
  env.insert("LC_TIME", "de_DE.UTF-8");
  const auto original = env;
  prepareProtonLocale(env);
  EXPECT_EQ(env, original);
}

TEST(ProtonLocale, UpgradesEncodingWithoutDiscardingLanguageOrModifier)
{
  const QMap<QString, QString> locales{
      {"ja_JP.SJIS", "ja_JP.UTF-8"},
      {"zh_CN.GB18030", "zh_CN.UTF-8"},
      {"ru_RU.KOI8-R", "ru_RU.UTF-8"},
      {"de_DE.ISO-8859-1", "de_DE.UTF-8"},
      {"sr_RS.ISO-8859-2@latin", "sr_RS.UTF-8@latin"},
      {"ar_SA", "ar_SA.UTF-8"},
      {"hi_IN.utf8", "hi_IN.utf8"},
      {"ko_KR.UTF-8", "ko_KR.UTF-8"},
      {"C", "C.UTF-8"}, {"POSIX", "C.UTF-8"}};
  for (auto it = locales.cbegin(); it != locales.cend(); ++it) {
    SCOPED_TRACE(it.key().toStdString());
    QProcessEnvironment env;
    env.insert("LANG", it.key());
    env.insert("LC_CTYPE", it.key());
    prepareProtonLocale(env);
    EXPECT_EQ(env.value("LANG"), it.value());
    EXPECT_EQ(env.value("LC_CTYPE"), it.value());
    EXPECT_FALSE(env.contains("LC_ALL"));
  }
}

TEST(ProtonLocale, ProtonHostOverrideWinsAndIsPassedThrough)
{
  QProcessEnvironment env;
  env.insert("HOST_LC_ALL", "ja_JP.SJIS");
  env.insert("LC_ALL", "C");
  env.insert("LANG", "de_DE.UTF-8");
  prepareProtonLocale(env);
  EXPECT_EQ(env.value("HOST_LC_ALL"), "ja_JP.UTF-8");
  EXPECT_EQ(env.value("LC_ALL"), "ja_JP.UTF-8");
  EXPECT_EQ(env.value("LANG"), "de_DE.UTF-8");
}

TEST(ProtonLocale, EmptyOverridesAllowSteamGameLanguageSelection)
{
  QProcessEnvironment env;
  env.insert("HOST_LC_ALL", "");
  env.insert("LC_ALL", "");
  env.insert("LC_CTYPE", "");
  env.insert("LANG", "ru_RU.UTF-8");
  prepareProtonLocale(env);
  EXPECT_FALSE(env.contains("HOST_LC_ALL"));
  EXPECT_FALSE(env.contains("LC_ALL"));
  EXPECT_FALSE(env.contains("LC_CTYPE"));
  EXPECT_EQ(env.value("LANG"), "ru_RU.UTF-8");
}

TEST(LaunchEnvironment, ExecutableValuesWinForNativeAndProtonLaunches)
{
  for (bool proton : {false, true}) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString script = temp.path() + "/capture environment";
    const QString output = temp.path() + "/result";
    QFile file(script);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n"
               "printf '%s\\n' \"$PROTON_ENABLE_WAYLAND\" \"$TEST_KEEP\" "
               "\"$TEST_EMPTY\" \"$TEST_LITERAL\" \"$LC_ALL\" > \"$FLUORINE_TEST_OUTPUT.tmp\"\n"
               "/bin/mv -- \"$FLUORINE_TEST_OUTPUT.tmp\" \"$FLUORINE_TEST_OUTPUT\"\n");
    file.close();
    ASSERT_TRUE(file.setPermissions(file.permissions() | QFile::ExeOwner));
    ProtonLauncher launcher;
    launcher.setBinary(script).setWorkingDir(temp.path())
        .setWrapper("PROTON_ENABLE_WAYLAND=1 TEST_KEEP=global TEST_EMPTY=global")
        .setSteamDrm(false).setUseSLR(false);
    if (proton) launcher.setProtonPath(script).setPrefix(temp.path());
    const auto environment = parseExecutableEnvironment(
        "PROTON_ENABLE_WAYLAND=0\nTEST_EMPTY=\nTEST_LITERAL=a b=$(literal)\nLC_ALL=C\nHOST_LC_ALL=");
    ASSERT_TRUE(environment);
    for (auto it = environment->cbegin(); it != environment->cend(); ++it) {
      launcher.addEnvVar(it.key(), it.value());
    }
    launcher.addEnvVar("FLUORINE_TEST_OUTPUT", output);
    ASSERT_TRUE(launcher.launch().first);
    QElapsedTimer timer;
    timer.start();
    while (!QFile::exists(output) && timer.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    QFile result(output);
    ASSERT_TRUE(result.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray("0\nglobal\n\na b=$(literal)\n") +
                                (proton ? "C.UTF-8\n" : "C\n");
    EXPECT_EQ(expected, result.readAll());
    QCoreApplication::processEvents();
  }
}

TEST(LaunchEnvironment, SteamBridgeKeepsPreparedEnvironmentWithoutPreloads)
{
  for (const auto& mode : {std::pair{false, false}, std::pair{true, false},
                          std::pair{false, true}, std::pair{true, true}}) {
    const auto [steamDrm, slr] = mode;
    SCOPED_TRACE(testing::Message() << "steam=" << steamDrm << " slr=" << slr);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString script = temp.path() + "/capture";
    const QString output = temp.path() + "/result";
    QFile file(script);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n"
               "printf '%s\\n' \"$SteamEnv\" \"$LC_ALL\" \"$HOST_LC_ALL\" "
               "\"$LC_MESSAGES\" \"$LANGUAGE\" \"$LD_PRELOAD\" \"$UMU_ID\" "
               "\"$@\" > \"$FLUORINE_TEST_OUTPUT.tmp\"\n"
               "/bin/mv -- \"$FLUORINE_TEST_OUTPUT.tmp\" \"$FLUORINE_TEST_OUTPUT\"\n");
    file.close();
    ASSERT_TRUE(file.setPermissions(file.permissions() | QFile::ExeOwner));
    QFile runtime(temp.path() + "/runtime");
    ASSERT_TRUE(runtime.open(QIODevice::WriteOnly));
    runtime.write("#!/bin/sh\n"
                  "while [ \"$#\" -gt 0 ]; do\n"
                  "  case \"$1\" in\n"
                  "    --ld-preload=*) exit 90 ;;\n"
                  "    --) shift; exec \"$@\" ;;\n"
                  "  esac\n"
                  "  shift\n"
                  "done\nexit 91\n");
    runtime.close();
    ASSERT_TRUE(runtime.setPermissions(runtime.permissions() | QFile::ExeOwner));
    slrRunScript = runtime.fileName();
    ProtonLauncher launcher;
    launcher.setBinary("game.exe").setProtonPath(script).setPrefix(temp.path())
        .setWorkingDir(temp.path()).setSteamDrm(steamDrm).setUseSLR(slr)
        .setWrapper("LC_ALL=C HOST_LC_ALL=C");
    launcher.addEnvVar("HOST_LC_ALL", "C.UTF-8");
    launcher.addEnvVar("LC_MESSAGES", "ja_JP.UTF-8");
    launcher.addEnvVar("LANGUAGE", "ja:en");
    launcher.addEnvVar("SteamEnv", "0");
    launcher.addEnvVar("LD_PRELOAD", "");
    launcher.addEnvVar("FLUORINE_TEST_OUTPUT", output);
    ASSERT_TRUE(launcher.launch().first);
    QElapsedTimer timer;
    timer.start();
    while (!QFile::exists(output) && timer.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(10);
    }
    QFile result(output);
    ASSERT_TRUE(result.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray(steamDrm ? "1\n" : "0\n") +
        "C.UTF-8\nC.UTF-8\nja_JP.UTF-8\nja:en\n\n\nwaitforexitandrun\ngame.exe\n";
    EXPECT_EQ(result.readAll(), expected);
    QCoreApplication::processEvents();
  }
  slrRunScript.clear();
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_launch_environment";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
