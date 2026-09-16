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
QString getSlrRunScript() { return {}; }
QString fluorineDataDir() { return {}; }

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
        "PROTON_ENABLE_WAYLAND=0\nTEST_EMPTY=\nTEST_LITERAL=a b=$(literal)\nLC_ALL=C");
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

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_launch_environment";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
