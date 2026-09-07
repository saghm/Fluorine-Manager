#include "../src/clf3galleryloader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

namespace {
class Clf3Gallery : public ::testing::Test
{
protected:
  QTemporaryDir directory;
  QByteArray previous;
  bool hadOverride;
  void SetUp() override
  {
    hadOverride = qEnvironmentVariableIsSet("FLUORINE_CLF3_PATH");
    previous = qgetenv("FLUORINE_CLF3_PATH");
    qputenv("FLUORINE_CLF3_PATH", directory.filePath("clf3").toUtf8());
  }
  void TearDown() override
  {
    if (hadOverride) qputenv("FLUORINE_CLF3_PATH", previous);
    else qunsetenv("FLUORINE_CLF3_PATH");
  }
  void script(const QByteArray& body)
  {
    QFile file(directory.filePath("clf3"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("#!/bin/sh\n" + body);
    file.close();
    ASSERT_TRUE(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
  }
};

TEST_F(Clf3Gallery, FailedStartupIsReportedAndCanBeRetried)
{
  Clf3GalleryLoader loader;
  QSignalSpy failed(&loader, &Clf3GalleryLoader::failed);
  QSignalSpy loaded(&loader, &Clf3GalleryLoader::loaded);
  loader.load();
  ASSERT_TRUE(failed.wait(3000));
  EXPECT_FALSE(loader.isBusy());
  EXPECT_EQ(loaded.count(), 0);
  script("printf '%s' '{\"modlists\":[],\"installed_games\":[\"SkyrimSE\"]}'\n");
  loader.load();
  ASSERT_TRUE(loaded.wait(3000));
  EXPECT_EQ(failed.count(), 1);
}

TEST_F(Clf3Gallery, InvalidJsonIsNotAnEmptyGallery)
{
  script("echo 'not JSON'\n");
  Clf3GalleryLoader loader;
  QSignalSpy failed(&loader, &Clf3GalleryLoader::failed);
  QSignalSpy loaded(&loader, &Clf3GalleryLoader::loaded);
  loader.load();
  ASSERT_TRUE(failed.wait(3000));
  EXPECT_EQ(loaded.count(), 0);
}

TEST_F(Clf3Gallery, RefreshPassesArgumentsAndDuplicateLoadsDoNotStartAgain)
{
  script("[ \"$*\" = 'gallery --host-metadata --refresh' ] || exit 8\n"
         "printf '%s' '{\"modlists\":[]}'\n");
  Clf3GalleryLoader loader;
  QSignalSpy loaded(&loader, &Clf3GalleryLoader::loaded);
  QSignalSpy failed(&loader, &Clf3GalleryLoader::failed);
  loader.load(true);
  loader.load(false);
  ASSERT_TRUE(loaded.wait(3000));
  EXPECT_EQ(loaded.count(), 1);
  EXPECT_EQ(failed.count(), 0);
}

TEST_F(Clf3Gallery, NonzeroExitIsNotSuccess)
{
  script("echo '[]'\nexit 9\n");
  Clf3GalleryLoader loader;
  QSignalSpy failed(&loader, &Clf3GalleryLoader::failed);
  loader.load();
  ASSERT_TRUE(failed.wait(3000));
  EXPECT_TRUE(failed.at(0).at(0).toString().contains("9"));
}
}

// Opt-in release smoke test, not part of offline regression requirements.
TEST(Clf3LiveGallery, DownloadsAndLoadsPublicGallery)
{
  const auto cache = qEnvironmentVariable("FLUORINE_TEST_LIVE_CLF3_CACHE");
  if (cache.isEmpty()) GTEST_SKIP() << "Set FLUORINE_TEST_LIVE_CLF3_CACHE for a live network smoke test";
  ASSERT_TRUE(qEnvironmentVariableIsEmpty("FLUORINE_CLF3_PATH"));
  Clf3GalleryLoader loader(nullptr, nullptr, cache);
  QSignalSpy loaded(&loader, &Clf3GalleryLoader::loaded);
  QSignalSpy failed(&loader, &Clf3GalleryLoader::failed);
  QObject::connect(&loader, &Clf3GalleryLoader::statusChanged,
                   [](const QString& status) { qInfo().noquote() << status; });
  loader.load();
  QElapsedTimer timer;
  timer.start();
  while (loaded.isEmpty() && failed.isEmpty() && timer.elapsed() < 240000)
    QTest::qWait(50);
  ASSERT_TRUE(failed.isEmpty()) << failed.first().first().toString().toStdString();
  ASSERT_FALSE(loaded.isEmpty());
  const auto document = qvariant_cast<QJsonDocument>(loaded.first().first());
  const auto lists = document.isArray() ? document.array()
                                       : document.object().value("modlists").toArray();
  EXPECT_FALSE(lists.isEmpty());
  qInfo() << "Live gallery modlists:" << lists.size();
}
