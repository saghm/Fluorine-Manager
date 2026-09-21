#include "executableslist.h"
#include "launchenvironment.h"

#include <gtest/gtest.h>

TEST(ExecutableOptions, SteamDefaultsOnForNewAndPluginExecutables)
{
  EXPECT_TRUE(Executable().useSteam());
  const MOBase::ExecutableInfo info(QStringLiteral("Game"), QFileInfo("game.exe"));
  EXPECT_TRUE(Executable(info, Executable::UseProton).useSteam());
  EXPECT_TRUE(Executable(info, {}).useSteam());
}

TEST(ExecutableOptions, SteamChoiceSurvivesCloningMergingAndFlagChanges)
{
  Executable tool(QStringLiteral("xEdit"));
  tool.useSteam(false).steamAppID(QStringLiteral("489830"));
  const Executable clone = tool;
  EXPECT_FALSE(clone.useSteam());

  Executable edited;
  edited.mergeFrom(clone);
  edited.flags(Executable::UseProton | Executable::UseTerminal);
  EXPECT_FALSE(edited.useSteam());
  EXPECT_TRUE(edited.useProton());
  EXPECT_TRUE(edited.useTerminal());
  EXPECT_EQ(QStringLiteral("489830"), edited.steamAppID());

  edited.useSteam(true);
  EXPECT_TRUE(edited.useSteam());
  EXPECT_FALSE(tool.useSteam());
}

TEST(ExecutableOptions, WrapperOptionsSurviveCloneMergeAndCanBeCleared)
{
  Executable original("Skyrim");
  original.wrapperOptions("PROTON_ENABLE_WAYLAND=1 LD_PRELOAD= mangohud --dlsym %command%");
  Executable clone = original;
  Executable edited;
  edited.mergeFrom(clone);
  EXPECT_EQ(original.wrapperOptions(), edited.wrapperOptions());
  clone.wrapperOptions("");
  edited.mergeFrom(clone);
  EXPECT_TRUE(edited.wrapperOptions().isEmpty());
  EXPECT_FALSE(original.wrapperOptions().isEmpty());
}

TEST(ExecutableOptions, EnvironmentValuesAreLiteralAndEmptyOverridesArePreserved)
{
  const auto parsed = parseExecutableEnvironment(
      "\nPROTON_ENABLE_WAYLAND=0\r\nLD_PRELOAD=\nPATH_WITH_SPACES=/a b/c\n"
      "LITERAL=$(do not execute)=yes\nPROTON_ENABLE_WAYLAND=1\n");
  ASSERT_TRUE(parsed);
  EXPECT_EQ("1", parsed->value("PROTON_ENABLE_WAYLAND"));
  EXPECT_TRUE(parsed->contains("LD_PRELOAD"));
  EXPECT_EQ("", parsed->value("LD_PRELOAD"));
  EXPECT_EQ("/a b/c", parsed->value("PATH_WITH_SPACES"));
  EXPECT_EQ("$(do not execute)=yes", parsed->value("LITERAL"));
}

TEST(ExecutableOptions, InvalidEnvironmentRejectsTheWholeInput)
{
  for (const QString& input : {QString("VALID=1\nnot an assignment"),
                               QString("BAD-NAME=1"), QString("=value"),
                               QString("2BAD=1"), QString("A=x") + QChar::Null}) {
    QString error;
    EXPECT_FALSE(parseExecutableEnvironment(input, &error));
    EXPECT_FALSE(error.isEmpty());
  }
  EXPECT_TRUE(parseExecutableEnvironment(" \n\r\n")->isEmpty());
}

TEST(ExecutableOptions, WrapperCommandsAndAssignmentsUseTheGlobalSyntax)
{
  const auto options = parseLaunchWrapperOptions(
      "PROTON_ENABLE_WAYLAND=0\nTEST_VALUE=\"a b=c\" EMPTY=\n"
      "mangohud --dlsym \"/path with spaces/wrapper\" --option=value %command%");
  ASSERT_TRUE(options);
  EXPECT_EQ(options->commands, (QStringList{"mangohud", "--dlsym",
      "/path with spaces/wrapper", "--option=value"}));
  EXPECT_EQ(options->environment, (QMap<QString, QString>{{"PROTON_ENABLE_WAYLAND", "0"},
      {"TEST_VALUE", "a b=c"}, {"EMPTY", ""}}));
  QString error;
  EXPECT_FALSE(parseLaunchWrapperOptions(QString("mangohud") + QChar::Null, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_TRUE(parseLaunchWrapperOptions("", &error)->commands.isEmpty());
  EXPECT_TRUE(error.isEmpty());
}

TEST(ExecutableOptions, LegacyLiteralEnvironmentBecomesEquivalentWrapperOptions)
{
  const QString legacy =
      "PATH_WITH_SPACES=/a b/c\nEMPTY=\nDUPLICATE=old\nDUPLICATE=new\n"
      "LITERAL=$(do not execute)=yes\nQUOTES=\"hello\" \\\"world\\\"\n"
      "APOSTROPHE=don't change this\nWHITESPACE=  a b\t \n"
      "UNICODE=日本語 Русский\nCOMMAND=%command%\n";
  const auto expected = parseExecutableEnvironment(legacy);
  ASSERT_TRUE(expected);
  const auto migrated = parseLaunchWrapperOptions(wrapperOptionsFromLegacyEnvironment(legacy));
  ASSERT_TRUE(migrated);
  EXPECT_TRUE(migrated->commands.isEmpty());
  EXPECT_EQ(migrated->environment, *expected);
  EXPECT_TRUE(wrapperOptionsFromLegacyEnvironment("\n\r\n").isEmpty());
}

TEST(ExecutableOptions, TrayBehaviorDoesNotChangePinOrShortcutIcon)
{
  Executable program("Game");
  program.flags(Executable::UseProton | Executable::UseApplicationIcon |
                Executable::ShowInToolbar);
  program.flags(program.flags() | Executable::MinimizeToSystemTray);
  EXPECT_TRUE(program.minimizeToSystemTray());
  program.flags(program.flags() & ~Executable::MinimizeToSystemTray);
  EXPECT_FALSE(program.minimizeToSystemTray());
  EXPECT_TRUE(program.usesOwnIcon());
  EXPECT_TRUE(program.isShownOnToolbar());
  EXPECT_TRUE(program.useProton());

  program.flags(Executable::MinimizeToSystemTray);
  EXPECT_TRUE(program.minimizeToSystemTray());
  EXPECT_FALSE(program.usesOwnIcon());
  EXPECT_FALSE(program.isShownOnToolbar());
}
