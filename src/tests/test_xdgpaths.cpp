#include "fluorinepaths.h"
#include "fluorineconfig.h"
#include "nxmhandler_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <uibase/log.h>

#include <map>

namespace
{
QByteArray readFile(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void writeFile(const QString& path, const QByteArray& content)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(content), content.size());
}

class XdgPaths : public ::testing::Test
{
protected:
  QTemporaryDir temp;
  std::map<QByteArray, QByteArray> saved;

  void setEnv(const QByteArray& name, const QByteArray& value)
  {
    saved.try_emplace(name, qgetenv(name.constData()));
    if (value.isNull()) {
      qunsetenv(name.constData());
    } else {
      qputenv(name.constData(), value);
    }
  }

  void SetUp() override
  {
    ASSERT_TRUE(temp.isValid());
    setEnv("HOME", temp.filePath("home").toUtf8());
    setEnv("XDG_CONFIG_HOME", temp.filePath("custom config").toUtf8());
    setEnv("XDG_DATA_HOME", temp.filePath("custom data").toUtf8());
    // Desktop-cache commands and portal calls must never affect the real user.
    setEnv("PATH", temp.filePath("no-tools").toUtf8());
    setEnv("FLUORINE_ORIG_PATH", temp.filePath("no-tools").toUtf8());
    setEnv("MO2_BASE_DIR", {});
    setEnv("MO2_LIBS_DIR", {});
  }

  void TearDown() override
  {
    for (const auto& [name, value] : saved) {
      if (value.isNull()) {
        qunsetenv(name.constData());
      } else {
        qputenv(name.constData(), value);
      }
    }
  }
};

TEST_F(XdgPaths, CustomDirectoriesApplyToDataCacheSocketAndCredentials)
{
  EXPECT_EQ(fluorineDataDir(), temp.filePath("custom data/fluorine"));
  EXPECT_EQ(fluorineVfsCacheDir(), temp.filePath("custom data/fluorine/vfs_cache"));
  EXPECT_EQ(NxmHandlerLinux::socketPath(),
            temp.filePath("custom data/fluorine/tmp/mo2-nxm.sock"));
  const QString credentials = temp.filePath("custom config/ModOrganizer/credentials.ini");
  writeFile(credentials, "[General]\nModOrganizer2_test=relocated\n");
  QSettings settings(fluorineCredentialsPath(), QSettings::IniFormat);
  EXPECT_EQ(settings.value("ModOrganizer2_test").toString(), "relocated");
  EXPECT_FALSE(QFileInfo::exists(temp.filePath("home/.config")));
}

TEST_F(XdgPaths, MissingEmptyAndRelativeVariablesUseDefaults)
{
  for (const auto& value : {QByteArray{}, QByteArray(""), QByteArray("relative/path")}) {
    setEnv("XDG_DATA_HOME", value);
    setEnv("XDG_CONFIG_HOME", value);
    EXPECT_EQ(fluorineDataDir(), temp.filePath("home/.local/share/fluorine"));
    EXPECT_EQ(fluorineCredentialsPath(),
              temp.filePath("home/.config/ModOrganizer/credentials.ini"));
  }
}

TEST_F(XdgPaths, CopiesLegacyCredentialsWithoutOverwritingCustomCredentials)
{
  const QString legacy = temp.filePath("home/.config/ModOrganizer/credentials.ini");
  const QString current = temp.filePath("custom config/ModOrganizer/credentials.ini");
  writeFile(legacy, "[General]\nModOrganizer2_test=legacy\n");
  ASSERT_TRUE(QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
  EXPECT_EQ(fluorineCredentialsPath(), current);
  EXPECT_EQ(readFile(current), readFile(legacy));
  EXPECT_EQ(QFile::permissions(current), QFile::permissions(legacy));
  writeFile(current, "[General]\nModOrganizer2_test=current\n");
  EXPECT_EQ(fluorineCredentialsPath(), current);
  EXPECT_TRUE(readFile(current).contains("=current"));
  EXPECT_TRUE(readFile(legacy).contains("=legacy"));
}

TEST_F(XdgPaths, RegistersAndUnregistersInCustomDirectories)
{
  const QString mimeapps = temp.filePath("custom config/mimeapps.list");
  const QString desktop = temp.filePath(
      "custom data/applications/com.fluorine.manager.nxm-handler.desktop");
  writeFile(mimeapps, "[Default Applications]\ntext/plain=editor.desktop;\n");
  NxmHandlerLinux::registerHandler();
  EXPECT_TRUE(QFileInfo::exists(desktop));
  EXPECT_TRUE(readFile(mimeapps).contains(
      "x-scheme-handler/nxm=com.fluorine.manager.nxm-handler.desktop;"));
  EXPECT_TRUE(readFile(mimeapps).contains(
      "x-scheme-handler/modl=com.fluorine.manager.nxm-handler.desktop;"));
  EXPECT_TRUE(readFile(mimeapps).contains("text/plain=editor.desktop;"));
  EXPECT_FALSE(QFileInfo::exists(temp.filePath("home/.config")));
  EXPECT_FALSE(QFileInfo::exists(temp.filePath("home/.local/share")));

  NxmHandlerLinux::unregisterHandler();
  EXPECT_FALSE(QFileInfo::exists(desktop));
  const QByteArray defaults = readFile(mimeapps).split('[').value(1);
  EXPECT_FALSE(defaults.contains("x-scheme-handler/"));
  EXPECT_TRUE(defaults.contains("text/plain=editor.desktop;"));
}

TEST_F(XdgPaths, CustomDataHomeDoesNotExpandLegacyPrefixDeletionPermission)
{
  FluorineConfig config;
  config.prefix_path = temp.filePath("custom data/fluorine/Prefix/pfx");
  ASSERT_TRUE(QDir().mkpath(config.prefix_path + "/drive_c"));
  EXPECT_FALSE(config.canDestroyPrefix());
  ASSERT_TRUE(config.markPrefixOwned());
  EXPECT_TRUE(config.canDestroyPrefix());

  config.prefix_path = temp.filePath("home/.local/share/fluorine/Prefix/pfx");
  ASSERT_TRUE(QDir().mkpath(config.prefix_path + "/drive_c"));
  EXPECT_TRUE(config.canDestroyPrefix());
}
}  // namespace

int main(int argc, char** argv)
{
  QTemporaryDir bus;
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + bus.filePath("absent")).toUtf8());
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_xdgpaths";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
