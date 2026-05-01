// Tests the createTarget directory-mapping contract used by
// FuseConnector::deployExternalMappings (src/src/fuseconnector.cpp:758).
//
// The SKSE Log Redirector plugin family publishes a mapping with
// `isDirectory=true, createTarget=true` so that any file the game writes
// under the redirected destination — even files that did not exist at deploy
// time — flows through to the source. Per-file symlinks miss new writes;
// a single directory symlink does not. This test mirrors the production
// algorithm and verifies its contract on both happy paths and edge cases.
//
// The mirrored algorithm (kept intentionally close to the production code):
//
//   if isDirectory && createTarget:
//     ensure src exists
//     if dst missing OR dst is a symlink OR dst is empty dir:
//       remove dst (if symlink/empty dir), create dir symlink dst -> src
//     else (dst has real content):
//       fall back to per-file symlinks (current behavior, preserves data)

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

struct DeployResult
{
  bool dirSymlinkCreated   = false;
  bool fellBackToPerFile   = false;
  std::vector<fs::path> createdLinks;
};

DeployResult deployDirectoryMapping(const fs::path& src, const fs::path& dst,
                                    bool createTarget)
{
  DeployResult result;
  std::error_code ec;

  if (createTarget) {
    if (!fs::exists(src, ec)) {
      fs::create_directories(src, ec);
      if (ec) {
        ec.clear();
      }
    }
    const bool dstExists  = fs::exists(dst, ec);
    ec.clear();
    const bool dstIsLink  = dstExists && fs::is_symlink(dst, ec);
    ec.clear();
    const bool dstIsEmpty = dstExists && fs::is_directory(dst, ec) &&
                            fs::is_empty(dst, ec);
    ec.clear();

    if (!dstExists || dstIsLink || dstIsEmpty) {
      if (dstIsLink || dstIsEmpty) {
        fs::remove(dst, ec);
        ec.clear();
      }
      fs::create_directory_symlink(src, dst, ec);
      if (!ec) {
        result.dirSymlinkCreated = true;
        result.createdLinks.push_back(dst);
        return result;
      }
    }
  }

  // Fall back: per-file symlinks under src.
  result.fellBackToPerFile = true;
  if (!fs::exists(src, ec)) {
    return result;
  }
  for (auto it = fs::recursive_directory_iterator(
           src, fs::directory_options::skip_permission_denied);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto& entry = *it;
    const auto rel    = fs::relative(entry.path(), src, ec);
    if (ec || rel.empty()) {
      continue;
    }
    const fs::path destPath = dst / rel;
    if (entry.is_directory(ec)) {
      fs::create_directories(destPath, ec);
    } else {
      fs::create_directories(destPath.parent_path(), ec);
      if (fs::exists(destPath, ec) && !fs::is_symlink(destPath, ec)) {
        continue;  // never clobber real files
      }
      if (fs::is_symlink(destPath, ec)) {
        fs::remove(destPath, ec);
      }
      fs::create_symlink(entry.path(), destPath, ec);
      if (!ec) {
        result.createdLinks.push_back(destPath);
      }
    }
  }
  return result;
}

class TempRoot
{
public:
  TempRoot()
  {
    char tmpl[]    = "/tmp/fluorine-extmap-XXXXXX";
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

void writeFile(const fs::path& p, const std::string& contents)
{
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << contents;
}

}  // namespace

// Happy path: a fresh enable of the SKSE Log Redirector. Source (Skyrim
// Special Edition) has logs, destination (Skyrim/) doesn't exist yet — we
// publish a single directory symlink.
TEST(ExternalDirMapping, DeploysDirSymlinkForFreshDestination)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());

  const fs::path src = tmp.path() / "Skyrim Special Edition";
  const fs::path dst = tmp.path() / "Skyrim";
  writeFile(src / "skse64.log", "log A\n");
  writeFile(src / "papyrus.0.log", "log B\n");

  auto result = deployDirectoryMapping(src, dst, /*createTarget=*/true);
  EXPECT_TRUE(result.dirSymlinkCreated);
  EXPECT_FALSE(result.fellBackToPerFile);
  EXPECT_TRUE(fs::is_symlink(dst));
  EXPECT_EQ(src, fs::read_symlink(dst));
}

// Game writes a NEW log file at the destination AFTER deploy. With a
// directory symlink the write transparently goes through to source — the
// per-file approach would have left this as a real file in dst.
TEST(ExternalDirMapping, NewWritesFlowThroughDirSymlink)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());
  const fs::path src = tmp.path() / "src";
  const fs::path dst = tmp.path() / "dst";
  fs::create_directories(src);

  auto result = deployDirectoryMapping(src, dst, /*createTarget=*/true);
  ASSERT_TRUE(result.dirSymlinkCreated);

  // Game emits a new log AFTER deploy, into the *destination*.
  writeFile(dst / "new.log", "fresh data");

  // The file must exist physically under src, not as a real file at dst.
  EXPECT_TRUE(fs::exists(src / "new.log"));
  EXPECT_FALSE(fs::is_symlink(src / "new.log"));
  // Reading through dst returns the same bytes.
  std::ifstream f(dst / "new.log");
  std::string buf((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  EXPECT_EQ("fresh data", buf);
}

// Idempotency: re-running deploy on an existing symlink is a no-op rebuild,
// not a corruption — the destination still resolves to source afterwards.
TEST(ExternalDirMapping, IsIdempotentOnExistingSymlink)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());
  const fs::path src = tmp.path() / "src";
  const fs::path dst = tmp.path() / "dst";
  fs::create_directories(src);

  ASSERT_TRUE(deployDirectoryMapping(src, dst, true).dirSymlinkCreated);
  ASSERT_TRUE(deployDirectoryMapping(src, dst, true).dirSymlinkCreated);
  EXPECT_TRUE(fs::is_symlink(dst));
  EXPECT_EQ(src, fs::read_symlink(dst));
}

// Empty pre-existing destination dir is safe to replace with a symlink —
// happens when an earlier MO2 run created tracked dirs but no files.
TEST(ExternalDirMapping, ReplacesEmptyDestinationDir)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());
  const fs::path src = tmp.path() / "src";
  const fs::path dst = tmp.path() / "dst";
  fs::create_directories(src);
  fs::create_directories(dst);
  ASSERT_TRUE(fs::is_directory(dst));
  ASSERT_FALSE(fs::is_symlink(dst));

  auto result = deployDirectoryMapping(src, dst, true);
  EXPECT_TRUE(result.dirSymlinkCreated);
  EXPECT_TRUE(fs::is_symlink(dst));
}

// Safety contract: when the destination dir already has *real* files
// (from the user's existing Skyrim install or an unrelated tool), we must
// NEVER replace it with a symlink — that would orphan the user's data.
// Fall back to per-file symlinks of source contents.
TEST(ExternalDirMapping, FallsBackWhenDestinationHasRealFiles)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());
  const fs::path src = tmp.path() / "src";
  const fs::path dst = tmp.path() / "dst";
  writeFile(src / "skse64.log", "from src");
  writeFile(dst / "user-data.txt", "USER MUST KEEP");

  auto result = deployDirectoryMapping(src, dst, /*createTarget=*/true);
  EXPECT_FALSE(result.dirSymlinkCreated);
  EXPECT_TRUE(result.fellBackToPerFile);
  EXPECT_FALSE(fs::is_symlink(dst));
  EXPECT_TRUE(fs::exists(dst / "user-data.txt"));

  // Source files surfaced as symlinks under dst.
  EXPECT_TRUE(fs::is_symlink(dst / "skse64.log"));
  // User's own file stays put as a real file, not a symlink.
  EXPECT_FALSE(fs::is_symlink(dst / "user-data.txt"));
}

// Cleanup contract: removing the recorded symlink (and its parent dir if
// empty) leaves no leftover real files. This is what
// FuseConnector::cleanupExternalMappings does — `fs::is_symlink` /
// `fs::remove` work for directory symlinks too.
TEST(ExternalDirMapping, CleanupRemovesDirSymlinkAndLeavesNoFiles)
{
  TempRoot tmp;
  ASSERT_FALSE(tmp.path().empty());
  const fs::path src = tmp.path() / "src";
  const fs::path dst = tmp.path() / "dst";
  writeFile(src / "skse64.log", "x");

  auto result = deployDirectoryMapping(src, dst, true);
  ASSERT_TRUE(result.dirSymlinkCreated);

  // Game writes a brand new file post-deploy.
  writeFile(dst / "new.log", "y");
  EXPECT_TRUE(fs::exists(src / "new.log"));

  // Simulate cleanup: cleanup loop calls is_symlink + remove.
  std::error_code ec;
  for (const auto& p : result.createdLinks) {
    if (fs::is_symlink(p, ec)) {
      fs::remove(p, ec);
    }
  }
  EXPECT_FALSE(fs::exists(dst));
  // Source survives — user's actual data is preserved on plugin disable.
  EXPECT_TRUE(fs::exists(src / "skse64.log"));
  EXPECT_TRUE(fs::exists(src / "new.log"));
}
