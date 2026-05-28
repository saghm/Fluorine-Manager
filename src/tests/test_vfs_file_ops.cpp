#include "vfs/inodetable.h"
#include "vfs/overwritemanager.h"

#include <fcntl.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

class TempRoot
{
public:
  TempRoot()
  {
    char tmpl[] = "/tmp/fluorine-vfs-ops-XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (d != nullptr) {
      m_path = d;
    }
  }

  ~TempRoot()
  {
    if (!m_path.empty()) {
      std::error_code ec;
      fs::remove_all(m_path, ec);
    }
  }

  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void writeText(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

std::string readText(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST(VfsFileOps, CopyOnWriteFromFdCanCopyBackingSourceToDifferentDestination)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path backing = tmp.path() / "Data";
  const fs::path staging = tmp.path() / "staging";
  const fs::path overwrite = tmp.path() / "overwrite";
  writeText(backing / "Plugin.esp.save_2026_05_27", "saved plugin");
  writeText(backing / "Plugin.esp", "old plugin");

  const int dirFd = open(backing.c_str(), O_RDONLY | O_DIRECTORY);
  ASSERT_GE(dirFd, 0);

  OverwriteManager overwriteManager(staging.string(), overwrite.string());
  const std::string staged = overwriteManager.copyOnWriteFromFd(
      dirFd, "Plugin.esp.save_2026_05_27", "Plugin.esp");
  close(dirFd);

  EXPECT_EQ(fs::path(staged), staging / "Plugin.esp");
  EXPECT_EQ(readText(staging / "Plugin.esp"), "saved plugin");
}

TEST(VfsFileOps, InodeRenameMovesDescendants)
{
  InodeTable table;
  const uint64_t parent = table.getOrCreate("meshes/actors");
  const uint64_t child = table.getOrCreate("meshes/actors/character/body.nif");

  table.rename("meshes/actors", "meshes/actors_backup");

  EXPECT_EQ(table.get("meshes/actors"), 0u);
  EXPECT_EQ(table.get("meshes/actors_backup"), parent);
  EXPECT_EQ(table.get("meshes/actors_backup/character/body.nif"), child);
  EXPECT_EQ(table.getPath(child), "meshes/actors_backup/character/body.nif");
}
