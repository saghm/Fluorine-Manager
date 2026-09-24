#include <gtest/gtest.h>

#include <QString>

#include "modidrecovery.h"

using ModIdRecovery::canApply;
using ModIdRecovery::extractFromFilename;
using ModIdRecovery::extractFromUrl;
using ModIdRecovery::recover;

// --- canApply: never overwrites a working id -------------------------------

TEST(ModIdRecovery, CanApplyOnlyFillsInvalidIds)
{
  EXPECT_TRUE(canApply(0, 4947));
  EXPECT_TRUE(canApply(-1, 4947));
  EXPECT_TRUE(canApply(-42, 1));
  EXPECT_FALSE(canApply(4947, 555));  // a valid id is never re-pointed
  EXPECT_FALSE(canApply(0, 0));
  EXPECT_FALSE(canApply(0, -1));
}

// --- URL extraction --------------------------------------------------------

TEST(ModIdRecovery, ExtractsModIdFromNexusLink)
{
  EXPECT_EQ(191684,
            extractFromUrl(QStringLiteral(
                "https://www.nexusmods.com/skyrimspecialedition/mods/191684")));
  EXPECT_EQ(321, extractFromUrl(
                     QStringLiteral("https://nexusmods.com/fallout4/mods/321")));
}

TEST(ModIdRecovery, RejectsNonNexusAndInvalidIdsFromUrl)
{
  EXPECT_EQ(0, extractFromUrl(QStringLiteral("https://example.com/skyrim/mods/191684")));
  EXPECT_EQ(0, extractFromUrl(QString()));
  EXPECT_EQ(0, extractFromUrl(QStringLiteral("https://nexusmods.com/skyrim/mods/0")));
  EXPECT_EQ(0, extractFromUrl(QStringLiteral("https://nexusmods.com/skyrim/mods/007")));
}

// --- filename heuristics ---------------------------------------------------

TEST(ModIdRecovery, RecoversIdFromNexusIsoFilename)
{
  EXPECT_EQ(191684, extractFromFilename(QStringLiteral(
                        "191684 v2.0.1 2024-05-01T10-30Z.7z")));
  EXPECT_EQ(191684, extractFromFilename(QStringLiteral(
                        "Some Mod Name-191684 v2.0.1 2024-05-01T10-30Z.zip")));
}

TEST(ModIdRecovery, RecoversIdFromVersionShapedTokenScan)
{
  EXPECT_EQ(4947, extractFromFilename(QStringLiteral("Sweet Mother HD-4947-2-0.7z")));
  EXPECT_EQ(4947, extractFromFilename(QStringLiteral("sweet-mother-hd-4947-2-0.rar")));
}

TEST(ModIdRecovery, LongestCandidateWins)
{
  EXPECT_EQ(2000, extractFromFilename(QStringLiteral("pack-100-1.5-2000-2.5")));
}

TEST(ModIdRecovery, RefusesIdsShorterThanThreeDigits)
{
  EXPECT_EQ(0, extractFromFilename(QStringLiteral("X-12-name-1.5")));
}

TEST(ModIdRecovery, RefusesNonVersionTail)
{
  EXPECT_EQ(0, extractFromFilename(QStringLiteral("X-123-name-abc")));
}

TEST(ModIdRecovery, RefusesAmbiguousCandidates)
{
  EXPECT_EQ(0, extractFromFilename(QStringLiteral("100-1.5-200-2.5-100-3.5")));
}

TEST(ModIdRecovery, RefusesMissingOrJunkNames)
{
  EXPECT_EQ(0, extractFromFilename(QString()));
  EXPECT_EQ(0, extractFromFilename(QStringLiteral("random-file")));
}

// --- recovery priority and the non-Nexus gate ------------------------------

TEST(ModIdRecovery, PrefersRecordedModId)
{
  EXPECT_EQ(4947, recover(QString(), QString(), QStringLiteral("4947"),
                          QStringLiteral("https://nexusmods.com/skyrim/mods/555"),
                          QString()));
  EXPECT_EQ(4947, recover(
                      QStringLiteral("https://nexusmods.com/skyrim/mods/555"),
                      QStringLiteral("Sweet Mother HD-4947-2-0.7z"),
                      QStringLiteral("4947"), QString(), QString()));
}

TEST(ModIdRecovery, FallsBackToMetaUrlThenDownloadUrl)
{
  EXPECT_EQ(555, recover(QStringLiteral("https://www.nexusmods.com/skyrim/mods/555"),
                         QString(), QStringLiteral("007"), QString(), QString()));
  EXPECT_EQ(321, recover(QString(), QString(), QString(),
                          QStringLiteral("https://www.nexusmods.com/fallout4/mods/321"),
                          QString()));
}

TEST(ModIdRecovery, FallsBackToFilenameWhenMetaIsSilent)
{
  EXPECT_EQ(191684, recover(QString(),
                            QStringLiteral("191684 v2.0.1 2024-05-01T10-30Z.7z"),
                            QString(), QString(), QString()));
}

TEST(ModIdRecovery, SkipsFilenameForNonNexusDownloads)
{
  EXPECT_EQ(0, recover(QString(),
                       QStringLiteral("191684 v2.0.1 2024-05-01T10-30Z.7z"),
                       QString(), QString(),
                       QStringLiteral("https://evil.example.com/file.7z")));
  // An explicit Nexus URL in the download meta still counts.
  EXPECT_EQ(191684,
            recover(QString(),
                    QStringLiteral("191684 v2.0.1 2024-05-01T10-30Z.7z"), QString(),
                    QString(),
                    QStringLiteral("https://cdn.nexusmods.com/file.7z")));
}
