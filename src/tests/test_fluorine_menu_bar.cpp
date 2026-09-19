#include "fluorinemenubar.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QTest>
#include <gtest/gtest.h>

namespace
{
// Exercise the Wayland path on the headless CI platform, too.
class TestMenuBar : public FluorineMenuBar
{
public:
  using FluorineMenuBar::FluorineMenuBar;

protected:
  void mousePressEvent(QMouseEvent* event) override
  {
    if (!closeActivePopup(event)) {
      QMenuBar::mousePressEvent(event);
    }
  }
};

class MenuBarTest : public ::testing::Test
{
protected:
  QMainWindow window;
  TestMenuBar* bar = new TestMenuBar(&window);
  QMenu* file = bar->addMenu("&File");
  QMenu* tools = bar->addMenu("&Tools");
  QAction* command = file->addAction("Run test action");
  int triggered = 0;

  void SetUp() override
  {
    tools->addAction("Another action");
    QObject::connect(command, &QAction::triggered, &window, [this] { ++triggered; });
    window.setMenuBar(bar);
    window.resize(480, 240);
    window.show();
    QApplication::processEvents();
  }

  QPoint position(QMenu* menu) const
  {
    return bar->actionGeometry(menu->menuAction()).center();
  }

  void click(QMenu* menu)
  {
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, position(menu));
    QApplication::processEvents();
  }

  void move(QMenu* menu, Qt::MouseButtons buttons = Qt::NoButton)
  {
    const auto point = position(menu);
    QMouseEvent event(QEvent::MouseMove, point, bar->mapToGlobal(point),
                      Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(bar, &event);
    QApplication::processEvents();
  }
};

TEST_F(MenuBarTest, RepeatedClickClosesWithoutGrabbingAndCanReopen)
{
  for (int i = 0; i < 3; ++i) {
    click(file);
    ASSERT_TRUE(file->isVisible());
    click(file);
    EXPECT_FALSE(file->isVisible());
    EXPECT_NE(QWidget::mouseGrabber(), bar);
    EXPECT_EQ(triggered, 0);
  }
}

TEST_F(MenuBarTest, DismissalResetsPressedStateEvenWithoutRealRelease)
{
  click(file);
  ASSERT_TRUE(file->isVisible());
  QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, position(file));
  EXPECT_FALSE(file->isVisible());
  EXPECT_NE(QWidget::mouseGrabber(), bar);
  move(tools, Qt::LeftButton);
  EXPECT_FALSE(tools->isVisible());
  QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, position(tools));
  click(tools);
  EXPECT_TRUE(tools->isVisible());
}

TEST_F(MenuBarTest, HoverSwitchesOpenMenusAndClickClosesCurrentMenu)
{
  click(file);
  move(tools);
  EXPECT_FALSE(file->isVisible());
  ASSERT_TRUE(tools->isVisible());
  click(tools);
  EXPECT_FALSE(tools->isVisible());
  move(file);
  EXPECT_FALSE(file->isVisible());
  EXPECT_NE(QWidget::mouseGrabber(), bar);
}

TEST_F(MenuBarTest, KeyboardAndActionActivationStillWorkAfterDismissal)
{
  click(file);
  click(file);
  click(file);
  QTest::keyClick(file, Qt::Key_Escape);
  EXPECT_FALSE(file->isVisible());
  bar->setFocus();
  QTest::keyClick(bar, Qt::Key_F, Qt::AltModifier);
  ASSERT_TRUE(file->isVisible());
  file->setActiveAction(command);
  QTest::keyClick(file, Qt::Key_Return);
  EXPECT_EQ(triggered, 1);
  EXPECT_FALSE(file->isVisible());
}

TEST_F(MenuBarTest, ClickingPopupActionStillTriggersAfterDismissal)
{
  click(file);
  click(file);
  click(file);
  QTest::mouseClick(file, Qt::LeftButton, Qt::NoModifier,
                    file->actionGeometry(command).center());
  EXPECT_EQ(triggered, 1);
  EXPECT_FALSE(file->isVisible());
}
} // namespace

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
