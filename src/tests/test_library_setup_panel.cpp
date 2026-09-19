#include "librarysetuppanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <gtest/gtest.h>

namespace
{
class LibraryPanelTest : public ::testing::Test
{
protected:
  LibrarySetupPanel panel;
  LibrarySetupInfo setup;

  void SetUp() override
  {
    setup.name = "My setup";
    setup.gameName = "Example game";
    setup.setupPath = "/games/example/setup";
    setup.dataPath = setup.setupPath;
    setup.gamePath = "/games/example/game";
    panel.resize(580, 500);
    panel.show();
  }

  template <class T> T* child(const char* name)
  {
    return panel.findChild<T*>(name);
  }
};

TEST_F(LibraryPanelTest, SelectingSetupsDoesNotWriteLaunchOptions)
{
  int changes = 0;
  QObject::connect(panel.steamDrmCheckBox(), &QCheckBox::toggled, [&] { ++changes; });
  QObject::connect(panel.rootBuilderCheckBox(), &QCheckBox::toggled, [&] { ++changes; });
  setup.steamDrm = true;
  setup.rootBuilder = true;
  panel.setSetup(setup);
  EXPECT_TRUE(panel.steamDrmCheckBox()->isChecked());
  EXPECT_TRUE(panel.rootBuilderCheckBox()->isChecked());
  setup.steamDrm = false;
  setup.rootBuilder = false;
  panel.setSetup(setup);
  EXPECT_FALSE(panel.steamDrmCheckBox()->isChecked());
  EXPECT_FALSE(panel.rootBuilderCheckBox()->isChecked());
  EXPECT_EQ(changes, 0);
  panel.steamDrmCheckBox()->click();
  EXPECT_EQ(changes, 1);
}

TEST_F(LibraryPanelTest, SharedPathsCollapseAndCopyRetainsFullPath)
{
  setup.setupPath = "/home/example/a-very-long-library-folder/a-very-long-setup-name";
  setup.dataPath = setup.setupPath + "/.";
  panel.setSetup(setup);
  child<QToolButton>("libraryPathsToggle")->click();
  QApplication::processEvents();
  EXPECT_TRUE(child<QWidget>("dataPathRow")->isHidden());
  EXPECT_EQ(child<QLabel>("setupPath")->text(), setup.setupPath);
  child<QPushButton>("setupPathCopy")->click();
  EXPECT_EQ(QApplication::clipboard()->text(), setup.setupPath);
  setup.dataPath = "/separate/data";
  panel.setSetup(setup);
  EXPECT_FALSE(child<QWidget>("dataPathRow")->isHidden());
  EXPECT_EQ(child<QLabel>("dataPath")->text(), setup.dataPath);
}

TEST_F(LibraryPanelTest, CurrentSetupShowsRenameExplanation)
{
  setup.current = true;
  panel.setSetup(setup);
  EXPECT_FALSE(child<QLabel>("currentSetupBadge")->isHidden());
  EXPECT_FALSE(child<QPushButton>("renameSetup")->isEnabled());
  EXPECT_FALSE(child<QLabel>("renameSetupHint")->isHidden());
  setup.current = false;
  panel.setSetup(setup);
  EXPECT_TRUE(child<QLabel>("currentSetupBadge")->isHidden());
  EXPECT_TRUE(child<QPushButton>("renameSetup")->isEnabled());
  EXPECT_TRUE(child<QLabel>("renameSetupHint")->isHidden());
}

TEST_F(LibraryPanelTest, EmptySelectionHidesPreviousDetails)
{
  panel.setSetup(setup);
  panel.clear();
  EXPECT_FALSE(child<QLabel>("emptySetupDetails")->isHidden());
  EXPECT_FALSE(child<QLabel>("setupTitle")->isVisible());
  EXPECT_FALSE(panel.steamDrmCheckBox()->isVisible());
}

TEST_F(LibraryPanelTest, MissingPathsCannotBeCopiedOrOpened)
{
  setup.gamePath.clear();
  panel.setSetup(setup);
  EXPECT_FALSE(child<QPushButton>("gamePathCopy")->isEnabled());
  EXPECT_FALSE(child<QPushButton>("gamePathOpen")->isEnabled());
}
} // namespace

int main(int argc, char** argv)
{
  QTemporaryDir cache;
  if (!cache.isValid()) return 1;
  qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
  QApplication app(argc, argv);
  app.setApplicationName("FluorineLibraryTests");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
