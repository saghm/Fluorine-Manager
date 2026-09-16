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

TEST(ExecutableOptions, EnvironmentSurvivesCloneMergeAndCanBeCleared)
{
  Executable original("Skyrim");
  original.environment("PROTON_ENABLE_WAYLAND=1\nLD_PRELOAD=");
  Executable clone = original;
  Executable edited;
  edited.mergeFrom(clone);
  EXPECT_EQ(original.environment(), edited.environment());
  clone.environment("");
  edited.mergeFrom(clone);
  EXPECT_TRUE(edited.environment().isEmpty());
  EXPECT_FALSE(original.environment().isEmpty());
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
