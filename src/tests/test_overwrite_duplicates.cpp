#include "vfs/trackedwrites.h"

#include <strings.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{

void writeText(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

}  // namespace

TEST(OverwriteDuplicates, DetectsIdenticalAndDifferentMatches)
{
  const fs::path base = fs::temp_directory_path() / "fluorine_overwrite_dup_test";
  fs::remove_all(base);

  const fs::path overwrite = base / "overwrite";
  const fs::path modA      = base / "ModA";
  const fs::path modB      = base / "ModB";

  writeText(overwrite / "SKSE/Plugins/Identical.ini", "same\n");
  writeText(overwrite / "SKSE/Plugins/Different.ini", "overwrite\n");
  writeText(overwrite / "SKSE/Plugins/OnlyOverwrite.ini", "only overwrite\n");

  writeText(modA / "SKSE/Plugins/Identical.ini", "same\n");
  writeText(modA / "SKSE/Plugins/Different.ini", "mod a\n");
  writeText(modB / "SKSE/Plugins/Different.ini", "mod b wins\n");

  TrackedWrites tracker;
  const std::vector<std::pair<std::string, std::string>> mods = {
      {"ModA", modA.string()},
      {"ModB", modB.string()},
  };

  const auto duplicates = tracker.scanOverwriteDuplicates(overwrite.string(), mods);
  ASSERT_EQ(duplicates.size(), 2u);

  const auto identical = std::find_if(
      duplicates.begin(), duplicates.end(),
      [](const TrackedWrites::DuplicateEntry& entry) {
        return entry.relative_path == "SKSE/Plugins/Identical.ini";
      });
  ASSERT_NE(identical, duplicates.end());
  EXPECT_EQ(identical->mod_name, "ModA");
  EXPECT_EQ(identical->state, TrackedWrites::DuplicateState::Identical);

  const auto different = std::find_if(
      duplicates.begin(), duplicates.end(),
      [](const TrackedWrites::DuplicateEntry& entry) {
        return entry.relative_path == "SKSE/Plugins/Different.ini";
      });
  ASSERT_NE(different, duplicates.end());
  EXPECT_EQ(different->mod_name, "ModB");
  EXPECT_EQ(different->state, TrackedWrites::DuplicateState::Different);

  fs::remove_all(base);
}
