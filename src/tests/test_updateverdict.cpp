#include <gtest/gtest.h>

#include "updateverdict.h"

namespace
{

using UpdateVerdict::Evidence;
using UpdateVerdict::Verdict;
using UpdateVerdict::VersionEvidence;

Evidence evidence(qint64 installed, qint64 latest, int chain = 0, int status = 1)
{
  Evidence e;
  e.installedFileUpdate  = installed;
  e.latestFileUpdate     = latest;
  e.chainSuccessorFileId = chain;
  e.installedFileStatus  = status;
  return e;
}

VersionEvidence versions(const QString& installed, const QString& newest,
                         bool ignored = false)
{
  VersionEvidence v;
  v.installed = installed;
  v.newest    = newest;
  v.ignored   = ignored;
  return v;
}

}  // namespace

// --- rule ordering ---------------------------------------------------------

// "Ignore this update" outranks every other rule, including a successor.
TEST(UpdateVerdict, IgnoredBeatsChainSuccessor)
{
  const auto v = UpdateVerdict::compute(evidence(100, 200, 55),
                                        versions(QStringLiteral("1.0"),
                                                 QStringLiteral("2.0"), true));
  EXPECT_EQ(Verdict::None, v);
}

// The author's own succession statement outranks every version string.
TEST(UpdateVerdict, ChainSuccessorBeatsVersionsAndDates)
{
  const auto v = UpdateVerdict::compute(
      evidence(900, 200, 55),
      versions(QStringLiteral("9.0"), QStringLiteral("1.0")));
  EXPECT_EQ(Verdict::Update, v);
}

// OLD_VERSION (4) and REMOVED (6) mean "not current", which outranks dates.
TEST(UpdateVerdict, StaleInstalledFileStatusBeatsDates)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(900, 200, 0, 4),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.0"))));
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(900, 200, 0, 6),
                                   versions(QString(), QString())));
}

// --- date recency ----------------------------------------------------------

TEST(UpdateVerdict, NewerUploadIsAnUpdateEvenWhenVersionsDisagree)
{
  // The installed version string looks newer; the upload dates say otherwise.
  const auto v = UpdateVerdict::compute(
      evidence(100, 200), versions(QStringLiteral("2.0"), QStringLiteral("1.0")));
  EXPECT_EQ(Verdict::Update, v);
}

TEST(UpdateVerdict, EqualUploadsNeverFallThroughToVersions)
{
  const auto v = UpdateVerdict::compute(
      evidence(100, 100), versions(QStringLiteral("1.0"), QStringLiteral("9.9")));
  EXPECT_EQ(Verdict::None, v);
}

TEST(UpdateVerdict, OlderUploadWithInactiveFileIsADowngrade)
{
  const auto v =
      UpdateVerdict::compute(evidence(300, 200, 0, 7),
                             versions(QStringLiteral("2.0"), QStringLiteral("1.0")));
  EXPECT_EQ(Verdict::Downgrade, v);
}

TEST(UpdateVerdict, OlderUploadWithActiveFileIsNotADowngrade)
{
  const auto v =
      UpdateVerdict::compute(evidence(300, 200, 0, 1),
                             versions(QStringLiteral("2.0"), QStringLiteral("1.0")));
  EXPECT_EQ(Verdict::None, v);
}

// --- guarded fallback (incomplete evidence only) ---------------------------

TEST(UpdateVerdict, FallsBackToVersionsWhenDatesAreIncomplete)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 0),
                                   versions(QStringLiteral("1.9"),
                                            QStringLiteral("2.0"))));
  EXPECT_EQ(Verdict::Downgrade,
            UpdateVerdict::compute(evidence(0, 100),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.9"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(100, 100),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("2.0"))));
}

// --- comparison-time normalization (B.1) -----------------------------------

TEST(UpdateVerdict, CompareNormalizedOrdersOrdinaryVersions)
{
  EXPECT_LT(UpdateVerdict::compareNormalized(QStringLiteral("1.0"),
                                             QStringLiteral("2.0")), 0);
  EXPECT_GT(UpdateVerdict::compareNormalized(QStringLiteral("2.0"),
                                             QStringLiteral("1.0")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.0"),
                                             QStringLiteral("1.0")), 0);
  EXPECT_LT(UpdateVerdict::compareNormalized(QStringLiteral("v1.9"),
                                             QStringLiteral("2.0")), 0);
}

TEST(UpdateVerdict, CompareNormalizedIgnoresPaddingAndSuffixes)
{
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.1.0.0"),
                                             QStringLiteral("1.1")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("2.0b"),
                                             QStringLiteral("2.0")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("  2.0  "),
                                             QStringLiteral("2.0")), 0);
}

TEST(UpdateVerdict, CompareNormalizedTreatsDatesAsUnorderable)
{
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("2024-05-01"),
                                             QStringLiteral("1.0")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("2024-05-01"),
                                             QStringLiteral("2024-06-01")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("01/02/2024"),
                                             QStringLiteral("01/03/2024")), 0);
}

TEST(UpdateVerdict, CompareNormalizedRefusesSchemeChanges)
{
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("2024.5"),
                                             QStringLiteral("1.5")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.5"),
                                             QStringLiteral("2024.5")), 0);
}

TEST(UpdateVerdict, CompareNormalizedRefusesCompactSegmentAmbiguity)
{
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.102"),
                                             QStringLiteral("1.10.2")), 0);
}

TEST(UpdateVerdict, CompareNormalizedIsZeroForUnknownSides)
{
  EXPECT_EQ(UpdateVerdict::compareNormalized(QString(), QStringLiteral("2.0")), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.0"), QString()), 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("garbage"),
                                             QStringLiteral("2.0")), 0);
}

TEST(UpdateVerdict, IsDateVersionRecognisesDateShapes)
{
  EXPECT_TRUE(UpdateVerdict::isDateVersion(QStringLiteral("2024-05-01")));
  EXPECT_TRUE(UpdateVerdict::isDateVersion(QStringLiteral("2024.05.01")));
  EXPECT_TRUE(UpdateVerdict::isDateVersion(QStringLiteral("d2024.05.01")));
  EXPECT_TRUE(UpdateVerdict::isDateVersion(QStringLiteral("01.02.2024")));
  EXPECT_TRUE(UpdateVerdict::isDateVersion(QStringLiteral("01/02/2024")));
  EXPECT_FALSE(UpdateVerdict::isDateVersion(QStringLiteral("1.5")));
  EXPECT_FALSE(UpdateVerdict::isDateVersion(QString()));
}

TEST(UpdateVerdict, IsActiveFileStatusMatchesNexusCategories)
{
  EXPECT_TRUE(UpdateVerdict::isActiveFileStatus(1));  // MAIN
  EXPECT_TRUE(UpdateVerdict::isActiveFileStatus(2));  // UPDATE
  EXPECT_TRUE(UpdateVerdict::isActiveFileStatus(3));  // OPTIONAL
  EXPECT_TRUE(UpdateVerdict::isActiveFileStatus(5));  // MISC
  EXPECT_FALSE(UpdateVerdict::isActiveFileStatus(4));  // OLD_VERSION
  EXPECT_FALSE(UpdateVerdict::isActiveFileStatus(6));  // REMOVED
  EXPECT_FALSE(UpdateVerdict::isActiveFileStatus(7));  // ARCHIVED
  EXPECT_FALSE(UpdateVerdict::isActiveFileStatus(1000));  // ARCHIVED_HIDDEN
}
