#include "NifWidget.h"

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>

namespace {
std::shared_ptr<nifly::NifFile> triangle()
{
  auto nif = std::make_shared<nifly::NifFile>();
  nif->Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> vertices{{-1, 0, -1}, {1, 0, -1}, {0, 0, 1}};
  std::vector<nifly::Triangle> triangles{{0, 2, 1}};
  std::vector<nifly::Vector2> uvs{{0, 0}, {1, 0}, {0.5, 1}};
  auto* shape = nif->CreateShapeFromData("preview regression triangle", &vertices, &triangles, &uvs);
  EXPECT_NE(shape, nullptr);
  if (shape) shape->flags = 0; // Default nifly shapes may be hidden.
  EXPECT_EQ(nif->GetShapes().size(), 1);
  return nif;
}

bool hasWindowSystem()
{
  const auto platform = QApplication::platformName();
  return platform != "offscreen" && platform != "minimal";
}

void sendEarlyInput(NifWidget& widget)
{
  QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&widget, &wheel);
  QMouseEvent move(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20),
                   Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(&widget, &move);
}

TEST(NifPreview, InputBeforeInitializationIsIgnored)
{
  NifWidget widget(triangle(), nullptr);
  sendEarlyInput(widget);
  EXPECT_EQ(widget.context(), nullptr);
}

TEST(NifPreview, CoreContextShowsAnErrorAndIgnoresInput)
{
  if (!hasWindowSystem()) GTEST_SKIP() << "Requires a desktop OpenGL display";
  NifWidget widget(triangle(), nullptr);
  auto format = widget.format();
  format.setVersion(4, 1);
  format.setProfile(QSurfaceFormat::CoreProfile);
  widget.setFormat(format);
  widget.resize(160, 160);
  widget.show();
  QTest::qWait(100);
  ASSERT_TRUE(widget.isValid());
  ASSERT_EQ(widget.context()->format().profile(), QSurfaceFormat::CoreProfile);
  auto* error = widget.findChild<QLabel*>("nifPreviewError");
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  EXPECT_TRUE(error->text().contains("compatibility"));
  sendEarlyInput(widget);
}

TEST(NifPreview, RendersAndRecreatesResourcesWhenReparented)
{
  if (!hasWindowSystem()) GTEST_SKIP() << "Requires a desktop OpenGL display";
  MOBase::details::setPluginDataPath(QStringLiteral(FLUORINE_TEST_SOURCE_DIR "/libs/preview_nif/data"));
  QWidget first, second;
  first.resize(180, 180);
  second.resize(180, 180);
  NifWidget widget(triangle(), nullptr, false, &first);
  widget.resize(160, 160);
  first.show();
  QTest::qWait(100);
  ASSERT_TRUE(widget.isValid());
  auto* error = widget.findChild<QLabel*>("nifPreviewError");
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isHidden()) << error->text().toStdString();
  auto before = widget.grabFramebuffer();
  ASSERT_FALSE(before.isNull());
  EXPECT_NE(before.pixelColor(before.width() / 2, before.height() / 2), before.pixelColor(0, 0));
  widget.setParent(&second);
  widget.show();
  second.show();
  QTest::qWait(100);
  ASSERT_TRUE(widget.isValid());
  EXPECT_TRUE(error->isHidden()) << error->text().toStdString();
  auto after = widget.grabFramebuffer();
  ASSERT_FALSE(after.isNull());
  EXPECT_EQ(after.pixelColor(after.width() / 2, after.height() / 2),
            before.pixelColor(before.width() / 2, before.height() / 2));
}

TEST(NifPreview, MissingShadersShowAnErrorAndCleanUpPartialResources)
{
  if (!hasWindowSystem()) GTEST_SKIP() << "Requires a desktop OpenGL display";
  QTemporaryDir empty;
  MOBase::details::setPluginDataPath(empty.path());
  NifWidget widget(triangle(), nullptr);
  widget.resize(160, 160);
  widget.show();
  QTest::qWait(100);
  ASSERT_TRUE(widget.isValid());
  auto* error = widget.findChild<QLabel*>("nifPreviewError");
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  EXPECT_TRUE(error->text().contains("shaders"));
  sendEarlyInput(widget);
}
}

int main(int argc, char** argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
      qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
