#include "gamelibrarywidget.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
QString artworkKey(const QListWidgetItem* item)
{
  return item->data(GameLibraryWidget::ArtworkKeyRole).toString();
}

void cacheCover(const QListWidgetItem* item, const QColor& color)
{
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + "/game-artwork";
  ASSERT_TRUE(QDir().mkpath(directory));
  QPixmap image(60, 90);
  image.fill(color);
  ASSERT_TRUE(image.save(directory + "/" + artworkKey(item) + ".png"));
}
}

TEST(GameArtwork, NonSteamGamesHaveDistinctCovers)
{
  GameLibraryWidget shelf;
  const auto* base = shelf.addGame("Black & White 2", "", {}, false, "BW2");
  const auto* expansion = shelf.addGame("Battle of the Gods", "", {}, false, "BOTG");
  const auto* daggerfall = shelf.addGame("Daggerfall Unity", "", {}, false, "daggerfallunity");
  EXPECT_FALSE(artworkKey(base).isEmpty());
  EXPECT_FALSE(artworkKey(expansion).isEmpty());
  EXPECT_FALSE(artworkKey(daggerfall).isEmpty());
  EXPECT_NE(artworkKey(base), artworkKey(expansion));
}

TEST(GameArtwork, ChosenCoversStaySeparateFromBaseGames)
{
  GameLibraryWidget shelf;
  const auto* fallout3 = shelf.addGame("Fallout 3", "22370", {}, true, "Fallout3");
  const auto* ttw = shelf.addGame("TTW", "22380", {}, true, "TTW");
  const auto* vegas = shelf.addGame("New Vegas", "22380", {}, true, "FalloutNV");
  EXPECT_EQ(artworkKey(fallout3), "sgdb-52054");
  EXPECT_EQ(artworkKey(ttw), "sgdb-552788");
  EXPECT_NE(artworkKey(ttw), artworkKey(vegas));
}

TEST(GameArtwork, SharedPluginShortNameUsesStableGameIdentity)
{
  GameLibraryWidget shelf;
  const auto* gta3 = shelf.addGame("Localized title", "", {}, false,
      "grandtheftautothetrilogy", "GTA III - Definitive Edition");
  const auto* vice = shelf.addGame("Localized title", "", {}, false,
      "grandtheftautothetrilogy", "GTA: Vice City - Definitive Edition");
  const auto* san = shelf.addGame("Localized title", "", {}, false,
      "grandtheftautothetrilogy", "GTA: San Andreas - Definitive Edition");
  EXPECT_EQ(artworkKey(gta3), "steam-1546970");
  EXPECT_EQ(artworkKey(vice), "steam-1546990");
  EXPECT_EQ(artworkKey(san), "steam-1547000");
}

TEST(GameArtwork, CachedArtWorksOfflineWithoutMarkingGamesInstalled)
{
  GameLibraryWidget shelf;
  auto* base = shelf.addGame("Black & White 2", "", {}, false, "BW2");
  auto* expansion = shelf.addGame("Battle of the Gods", "", {}, false, "BOTG");
  cacheCover(base, Qt::red);
  cacheCover(expansion, Qt::green);
  expansion->setHidden(true);
  shelf.show();
  QApplication::processEvents();
  EXPECT_EQ(qvariant_cast<QPixmap>(base->data(GameLibraryWidget::CoverRole))
                .toImage().pixelColor(0, 0), QColor(Qt::red));
  EXPECT_TRUE(expansion->data(GameLibraryWidget::CoverRole).isNull());
  expansion->setHidden(false);
  shelf.refreshArtwork();
  EXPECT_EQ(qvariant_cast<QPixmap>(expansion->data(GameLibraryWidget::CoverRole))
                .toImage().pixelColor(0, 0), QColor(Qt::green));
  EXPECT_FALSE(base->data(GameLibraryWidget::InstalledRole).toBool());
  EXPECT_FALSE(expansion->data(GameLibraryWidget::InstalledRole).toBool());
}

TEST(GameArtwork, UnknownGamesDoNotTurnIdentifiersIntoPaths)
{
  GameLibraryWidget shelf;
  const auto* game = shelf.addGame("Unknown", "../../outside", {}, false, "unknown");
  EXPECT_TRUE(artworkKey(game).isEmpty());
}

int main(int argc, char** argv)
{
  QTemporaryDir cache;
  if (!cache.isValid()) {
    return 1;
  }
  qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
  QApplication app(argc, argv);
  app.setApplicationName("FluorineArtworkTests");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
