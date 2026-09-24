#include <gtest/gtest.h>

#include "updateverdict.h"

namespace
{

using UpdateVerdict::Evidence;
using UpdateVerdict::Verdict;
using UpdateVerdict::VersionEvidence;

Evidence evidence(qint64 installed, qint64 latest, int chain = 0, int status = 1,
                  const QString& installedLabel = QString(),
                  const QString& anchorLabel = QString(), bool labelsSampled = true)
{
  Evidence e;
  e.installedFileUpdate  = installed;
  e.latestFileUpdate     = latest;
  e.chainSuccessorFileId = chain;
  e.installedFileStatus  = status;
  e.installedFileVersion = installedLabel;
  e.latestFileVersion    = anchorLabel;
  // Default to sampled: these fixtures describe a mod whose file list has been
  // fetched, where an empty label is the author's answer. Metas written before
  // the labels were collected are modelled by passing labelsSampled = false.
  e.fileVersionSampled   = labelsSampled;
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

// A refused comparison: "no flag", but explicitly *not* a claim of equality.
void expectUndecidable(const QString& installed, const QString& newest)
{
  const auto result = UpdateVerdict::compareNormalized(installed, newest);
  EXPECT_EQ(0, result.cmp);
  EXPECT_TRUE(result.indeterminate);
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

// The installed file has stopped being offered — OLD_VERSION (4) or REMOVED
// (6) — which outranks both the dates and the version-label veto: the author
// is saying the file is not current, so a label claiming otherwise cannot
// clear it.
TEST(UpdateVerdict, StaleInstalledFileStatusBeatsDates)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200, 0, 4, QStringLiteral("4.1.1"),
                                            QStringLiteral("4.1.0")),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.0"))));
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200, 0, 6, QStringLiteral("4.1.1"),
                                            QStringLiteral("4.1.0")),
                                   versions(QString(), QString())));
}

// An inactive installed file that never got an upload date — a file Nexus only
// hides — cannot be placed against the page at all, so it must not be claimed
// as an update: rule 5 answers instead, and with nothing else to go on that is
// "unknown", not a red flag.
TEST(UpdateVerdict, StaleStatusWithoutADateStaysUndecided)
{
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 200, 0, 4),
                                   versions(QString(), QStringLiteral("1.0"))));
}

// The other half of that rule: an archived file *with* a date behind the page
// is an update, and one ahead of the page is the rollback the author performed.
TEST(UpdateVerdict, StaleStatusDecidesFromTheDates)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200, 0, 7),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.0"))));
  EXPECT_EQ(Verdict::Downgrade,
            UpdateVerdict::compute(evidence(300, 200, 0, 7),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.0"))));
}

// --- date recency ----------------------------------------------------------

// The installed version string looks newer; the upload dates say otherwise.
// These are the *mod folder's* version= / newestVersion strings, which rule 4
// never reads: the veto below only looks at the two files' own labels.
TEST(UpdateVerdict, NewerUploadIsAnUpdateEvenWhenVersionsDisagree)
{
  const auto v = UpdateVerdict::compute(
      evidence(100, 200), versions(QStringLiteral("2.0"), QStringLiteral("1.0")));
  EXPECT_EQ(Verdict::Update, v);
}

// The anchor file was uploaded a minute after the installed one, but both
// carry the same release label (an SE/AE build and its VR sibling, say). A
// later upload of a different file is not an update to this one.
TEST(UpdateVerdict, LaterUploadOfTheSameReleaseIsNotAnUpdate)
{
  const auto v = UpdateVerdict::compute(
      evidence(100, 200, 0, 1, QStringLiteral("4.1.0"), QStringLiteral("4.1.0")),
      versions(QStringLiteral("4.1.0"), QStringLiteral("4.1.0")));
  EXPECT_EQ(Verdict::None, v);
}

TEST(UpdateVerdict, LaterUploadOfAGenuinelyNewerFileIsAnUpdate)
{
  const auto v = UpdateVerdict::compute(
      evidence(100, 200, 0, 1, QStringLiteral("4.0.6"), QStringLiteral("4.1.0")),
      versions(QStringLiteral("4.0.6"), QStringLiteral("4.1.0")));
  EXPECT_EQ(Verdict::Update, v);
}

// Dates say the anchor is newer, the labels say the installed file is. The
// labels only veto a red flag: with nothing positively established, we do not
// tell the user their own file is out of date.
TEST(UpdateVerdict, AnchorLabelBehindTheInstallDoesNotFlagAnUpdate)
{
  const auto v = UpdateVerdict::compute(
      evidence(100, 200, 0, 1, QStringLiteral("4.1.1"), QStringLiteral("4.1.0")),
      versions(QStringLiteral("4.1.1"), QStringLiteral("4.1.0")));
  EXPECT_EQ(Verdict::None, v);
}

// An unknown label must never clear an update the dates already proved.
TEST(UpdateVerdict, UnknownAnchorLabelLeavesTheDateAlone)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(
                evidence(100, 200, 0, 1, QString(), QStringLiteral("4.1.0")),
                versions(QStringLiteral("4.1.0"), QStringLiteral("4.1.0"))));
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200), versions(QString(), QString())));
}

// --- never-sampled labels: the meta predates the field ----------------------

// The date says the anchor is newer, but the labels that would decide whether
// that is an update or a sibling upload were never collected — a meta written
// before the label fields existed, with two empty strings meaning "never
// sampled". Ruling Update here would be asserting an answer nobody asked for.
TEST(UpdateVerdict, NeverSampledLabelsAreUnknownRatherThanUpdate)
{
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(100, 200, 0, 1, QString(), QString(), false),
                                   versions(QStringLiteral("4.1.0"),
                                            QStringLiteral("4.1.0"))));
  // The two strings being equal is exactly the case the veto exists to catch,
  // so an unsampled meta must not be read as having caught it.
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(
                evidence(100, 200, 0, 1, QStringLiteral("4.1.0"),
                         QStringLiteral("4.1.0"), false),
                versions(QStringLiteral("4.1.0"), QStringLiteral("4.1.0"))));
}

// Only rule 4's red branch depends on the labels. Everything that is
// established without them still holds on a never-sampled meta.
TEST(UpdateVerdict, NeverSampledLabelsOnlyBlockTheDateDrivenRed)
{
  // Nothing to decide: an out-of-date verdict needs no labels.
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(100, 100, 0, 1, QString(), QString(), false),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("9.9"))));
  // Rules 2 and 3 answer from the author's own statements.
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(900, 200, 55, 1, QString(), QString(), false),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("1.0"))));
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200, 0, 4, QString(), QString(), false),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("1.0"))));
  // A downgrade is asserted from the dates and the installed file's status.
  EXPECT_EQ(Verdict::Downgrade,
            UpdateVerdict::compute(evidence(300, 200, 0, 7, QString(), QString(), false),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("1.0"))));
  // Incomplete dates still fall through to the guarded version comparison.
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 0, 0, 1, QString(), QString(), false),
                                   versions(QStringLiteral("1.9"),
                                            QStringLiteral("2.0"))));
}

// A label that was collected and came back empty is not the same as a label
// that was never collected, and must keep the pre-existing date-driven answer:
// the author was asked, and an empty answer is an answer.
TEST(UpdateVerdict, SampledButEmptyLabelsLeaveTheDateDrivenUpdateInPlace)
{
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200, 0, 1, QString(), QString(), true),
                                   versions(QStringLiteral("4.1.0"),
                                            QStringLiteral("4.1.0"))));
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
  // Being ahead of the recorded newest is a rollback only when the installed
  // file is no longer offered (status 7 = archived).
  EXPECT_EQ(Verdict::Downgrade,
            UpdateVerdict::compute(evidence(0, 100, 0, 7),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.9"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(100, 100),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("2.0"))));
}

// The same guard rule 4 already has: a still-listed newer file is simply the
// newest file, not evidence that the author rolled the page back.
TEST(UpdateVerdict, RuleFiveDowngradeRequiresAnInactiveInstalledFile)
{
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(0, 100, 0, 1),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.9"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(0, 100, 0, 5),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.9"))));
  // ... and the inactive case does downgrade, so the guard is a guard and not
  // just an absence of the branch.
  EXPECT_EQ(Verdict::Downgrade,
            UpdateVerdict::compute(evidence(0, 100, 0, 7),
                                   versions(QStringLiteral("2.0"),
                                            QStringLiteral("1.9"))));
}

// --- unknown: the check ran but nothing could decide -----------------------

TEST(UpdateVerdict, MissingInstalledVersionIsUnknownNotUpToDate)
{
  // Regression guard: an empty `version=` used to be fed to
  // VersionInfo::operator<, where an invalid left operand reports "less than
  // anything" (versioninfo.cpp:337) and produced a bogus Update. It must now be
  // reported as unknown rather than as either an update or a clean mod.
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QString(), QStringLiteral("2.0"))));
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("not a version"),
                                            QStringLiteral("2.0"))));
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0), versions(QString(), QString())));
}

TEST(UpdateVerdict, GuardedRefusalsAreUnknownNotUpToDate)
{
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("2024-05-01"),
                                            QStringLiteral("2024-06-01"))));
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("1.102"),
                                            QStringLiteral("1.10.2"))));
  EXPECT_EQ(Verdict::Unknown,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("2024.5"),
                                            QStringLiteral("1.5"))));
}

TEST(UpdateVerdict, GenuinelyEqualVersionsAreNoneNotUnknown)
{
  // The numeric comparison ran and found them equal: that is a real "no update",
  // including the display-only suffix case from checklist item 4.
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("1.1.0.0b"),
                                            QStringLiteral("1.1.0.0"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("2.0b"),
                                            QStringLiteral("2.0"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(0, 0),
                                   versions(QStringLiteral("1.0"),
                                            QStringLiteral("1.0"))));
}

TEST(UpdateVerdict, EvidenceStillDecidesWhenTheVersionsCannot)
{
  // Unknown is a rule-5 outcome only; complete evidence outranks it.
  EXPECT_EQ(Verdict::Update,
            UpdateVerdict::compute(evidence(100, 200),
                                   versions(QString(), QStringLiteral("2.0"))));
  EXPECT_EQ(Verdict::None,
            UpdateVerdict::compute(evidence(100, 100),
                                   versions(QString(), QStringLiteral("9.9"))));
}

// --- comparison-time normalization (B.1) -----------------------------------

TEST(UpdateVerdict, CompareNormalizedOrdersOrdinaryVersions)
{
  EXPECT_LT(UpdateVerdict::compareNormalized(QStringLiteral("1.0"), QStringLiteral("2.0")).cmp, 0);
  EXPECT_GT(UpdateVerdict::compareNormalized(QStringLiteral("2.0"), QStringLiteral("1.0")).cmp, 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.0"), QStringLiteral("1.0")).cmp, 0);
  EXPECT_LT(UpdateVerdict::compareNormalized(QStringLiteral("v1.9"), QStringLiteral("2.0")).cmp, 0);
}

TEST(UpdateVerdict, CompareNormalizedIgnoresPaddingAndSuffixes)
{
  // Checklist item 4, verbatim: strings differing only in format must resolve to
  // equal and must not be flagged.
  const auto result =
      UpdateVerdict::compareNormalized(QStringLiteral("1.1.0.0b"), QStringLiteral("1.1.0.0"));
  EXPECT_EQ(0, result.cmp);
  EXPECT_FALSE(result.indeterminate);

  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("1.1.0.0"), QStringLiteral("1.1")).cmp, 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("2.0b"), QStringLiteral("2.0")).cmp, 0);
  EXPECT_EQ(UpdateVerdict::compareNormalized(QStringLiteral("  2.0  "), QStringLiteral("2.0")).cmp, 0);
}

TEST(UpdateVerdict, CompareNormalizedTreatsDatesAsUnorderable)
{
  expectUndecidable(QStringLiteral("2024-05-01"), QStringLiteral("1.0"));
  expectUndecidable(QStringLiteral("2024-05-01"), QStringLiteral("2024-06-01"));
  expectUndecidable(QStringLiteral("01/02/2024"), QStringLiteral("01/03/2024"));
}

TEST(UpdateVerdict, CompareNormalizedRefusesSchemeChanges)
{
  expectUndecidable(QStringLiteral("2024.5"), QStringLiteral("1.5"));
  expectUndecidable(QStringLiteral("1.5"), QStringLiteral("2024.5"));
}

TEST(UpdateVerdict, CompareNormalizedRefusesCompactSegmentAmbiguity)
{
  expectUndecidable(QStringLiteral("1.102"), QStringLiteral("1.10.2"));
}

TEST(UpdateVerdict, CompareNormalizedIsUnknownForUnknownSides)
{
  expectUndecidable(QString(), QStringLiteral("2.0"));
  expectUndecidable(QStringLiteral("1.0"), QString());
  expectUndecidable(QStringLiteral("garbage"), QStringLiteral("2.0"));
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

// --- evidence groups and the backfill trigger -------------------------------

// Pinned deliberately. Adding a name here is how a new evidence field backfills
// every meta that predates it (ModInfo::filteredMods gives pending mods a real
// file list), so the list is the one piece of this mechanism that has to be
// edited by hand. Pinning it means an addition fails here rather than quietly
// changing which metas the next update check refetches.
TEST(UpdateVerdict, RequiredEvidenceFieldsArePinned)
{
  const QStringList required = UpdateVerdict::requiredEvidenceFields();
  ASSERT_EQ(2, required.size());
  EXPECT_EQ(QStringLiteral("files"), required.at(0));
  EXPECT_EQ(QStringLiteral("fileVersions"), required.at(1));
}

TEST(UpdateVerdict, EvidenceFieldsSampledNeedsEveryGroup)
{
  const QStringList required = UpdateVerdict::requiredEvidenceFields();

  EXPECT_FALSE(UpdateVerdict::evidenceFieldsSampled({}));
  // Partial coverage is still a hole, and a meta in that state is due for a
  // refresh — that is the whole point of listing the groups.
  EXPECT_FALSE(UpdateVerdict::evidenceFieldsSampled({required.at(0)}));
  EXPECT_FALSE(UpdateVerdict::evidenceFieldsSampled({required.at(1)}));
  EXPECT_TRUE(UpdateVerdict::evidenceFieldsSampled(required));
  // Order and extras do not matter: the key is a set, written in whatever order
  // the list happens to have.
  EXPECT_TRUE(UpdateVerdict::evidenceFieldsSampled({required.at(1), required.at(0)}));
  EXPECT_TRUE(UpdateVerdict::evidenceFieldsSampled(
      {required.at(0), required.at(1), QStringLiteral("somethingNewer")}));
}

TEST(UpdateVerdict, FileVersionLabelsAreTrackedSeparately)
{
  const QStringList required = UpdateVerdict::requiredEvidenceFields();

  // "sampled and Nexus had nothing" (the group is listed, the strings are
  // empty) must be tellable apart from "never sampled" (the group is missing).
  EXPECT_FALSE(UpdateVerdict::fileVersionLabelsSampled({}));
  EXPECT_FALSE(UpdateVerdict::fileVersionLabelsSampled({required.at(0)}));
  EXPECT_TRUE(UpdateVerdict::fileVersionLabelsSampled({required.at(1)}));
  EXPECT_TRUE(UpdateVerdict::fileVersionLabelsSampled(required));
}
