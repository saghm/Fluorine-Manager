#include "settingsnavigation.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <gtest/gtest.h>

namespace
{
class SettingsNavigationTest : public ::testing::Test
{
protected:
  QDialog dialog;
  QTabWidget* pages = nullptr;
  SettingsNavigation* navigation = nullptr;
  QLineEdit* search = nullptr;
  QListWidget* sections = nullptr;
  QWidget* general = nullptr;
  QWidget* compatibility = nullptr;
  QWidget* updates = nullptr;
  QWidget* advanced = nullptr;
  QCheckBox* cache = nullptr;
  SettingsFoldout* foldout = nullptr;

  void SetUp() override
  {
    auto* layout = new QVBoxLayout(&dialog);
    pages = new QTabWidget(&dialog);
    general = new QWidget;
    updates = new QWidget;
    compatibility = new QWidget;
    pages->addTab(general, "General");
    pages->addTab(updates, "Updates");
    pages->addTab(compatibility, "Wine/Proton");
    auto* body = new QVBoxLayout(compatibility);
    advanced = new QWidget;
    advanced->setObjectName("advanced");
    auto* advancedLayout = new QVBoxLayout(advanced);
    cache = new QCheckBox("Disable filesystem cache", advanced);
    advancedLayout->addWidget(cache);
    // Private values must not become search terms.
    auto* secret = new QLineEdit("private-api-token", advanced);
    secret->setEchoMode(QLineEdit::Password);
    advancedLayout->addWidget(secret);
    foldout = new SettingsFoldout("Advanced options", advanced, compatibility);
    body->addWidget(foldout);
    navigation = new SettingsNavigation(pages, &dialog);
    layout->addWidget(navigation);
    navigation->addSection(general, "General", "Interface and profile defaults");
    navigation->addSection(compatibility, "Compatibility", "Windows programs", "wine proton");
    navigation->addSection(updates, "Updates", "Application updates", "stable nightly");
    search = dialog.findChild<QLineEdit*>("settingsSearch");
    sections = dialog.findChild<QListWidget*>("settingsSections");
    dialog.resize(900, 600);
    dialog.show();
    QApplication::processEvents();
  }
};

TEST_F(SettingsNavigationTest, SidebarPreservesStoredPageIndicesAndDirectLinks)
{
  sections->setCurrentRow(1);
  EXPECT_EQ(pages->currentWidget(), compatibility);
  EXPECT_EQ(pages->currentIndex(), 2);
  pages->setCurrentIndex(1); // Existing saved index or Updates shortcut.
  EXPECT_EQ(sections->currentRow(), 2);
  EXPECT_EQ(dialog.findChild<QLabel*>("settingsPageTitle")->text(), "Updates");
}

TEST_F(SettingsNavigationTest, SearchesOptionTextAndRestoresPreviousSection)
{
  pages->setCurrentWidget(updates);
  search->setText("filesystem cache");
  EXPECT_EQ(pages->currentWidget(), compatibility);
  EXPECT_TRUE(sections->item(0)->isHidden());
  EXPECT_FALSE(sections->item(1)->isHidden());
  EXPECT_FALSE(advanced->isHidden());
  cache->setChecked(true);
  search->clear();
  EXPECT_EQ(pages->currentWidget(), updates);
  EXPECT_TRUE(advanced->isHidden());
  EXPECT_TRUE(cache->isChecked()); // Collapsing must never reset preferences.
}

TEST_F(SettingsNavigationTest, SearchDoesNotIndexPrivateEnteredValues)
{
  search->setText("private-api-token");
  EXPECT_TRUE(dialog.findChild<QLabel*>("settingsNoResults")->isVisible());
  search->clear();
  EXPECT_FALSE(dialog.findChild<QLabel*>("settingsNoResults")->isVisible());
}

TEST_F(SettingsNavigationTest, SearchIsCaseInsensitiveAndUsesSectionKeywords)
{
  search->setText("  PROTON   Windows  ");
  EXPECT_EQ(pages->currentWidget(), compatibility);
  EXPECT_FALSE(sections->item(1)->isHidden());
  EXPECT_TRUE(sections->item(0)->isHidden());
  search->setText("proton cache");
  EXPECT_EQ(pages->currentWidget(), compatibility);
  EXPECT_FALSE(advanced->isHidden());
}

TEST_F(SettingsNavigationTest, SearchRestoresManuallyExpandedAdvancedOptions)
{
  pages->setCurrentWidget(compatibility);
  foldout->findChild<QToolButton*>()->click();
  EXPECT_FALSE(advanced->isHidden());
  search->setText("cache");
  search->clear();
  EXPECT_FALSE(advanced->isHidden());
}

TEST_F(SettingsNavigationTest, EnterInSearchNavigatesWithoutAcceptingDialog)
{
  bool accepted = false;
  QObject::connect(&dialog, &QDialog::accepted, &dialog, [&] { accepted = true; });
  search->setFocus();
  search->setText("nightly");
  QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QApplication::sendEvent(search, &enter);
  EXPECT_EQ(pages->currentWidget(), updates);
  EXPECT_TRUE(sections->hasFocus());
  EXPECT_FALSE(accepted);
}
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
