#include "vfsbridge.h"

#include "../fluorinepaths.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <system_error>

namespace
{
namespace fs = std::filesystem;

uint64_t hash64(const std::string& s)
{
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

std::string jsonEscape(const std::string& s)
{
  std::ostringstream out;
  out << '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (c < 0x20) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(c) << std::dec;
      } else {
        out << static_cast<char>(c);
      }
      break;
    }
  }
  out << '"';
  return out.str();
}

int64_t timeToNs(std::chrono::system_clock::time_point t)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             t.time_since_epoch())
      .count();
}

int64_t nowToNs()
{
  return timeToNs(std::chrono::system_clock::now());
}

void writeMeta(std::ostream& out, const VfsTree& tree,
               const std::string& data_dir, const std::string& overwrite_dir,
               const std::string& mount_point)
{
  out << "{\"record\":\"meta\","
      << "\"schema\":\"fluorine-vfs-index-v1\","
      << "\"data_dir\":" << jsonEscape(data_dir) << ','
      << "\"overwrite_dir\":" << jsonEscape(overwrite_dir) << ','
      << "\"mount_point\":" << jsonEscape(mount_point) << ','
      << "\"file_count\":" << tree.file_count << ','
      << "\"dir_count\":" << tree.dir_count << ','
      << "\"export_time_ns\":" << nowToNs() << "}\n";
}

void writeEntry(std::ostream& out, const std::string& virtual_path,
                const VfsNode& node)
{
  out << "{\"record\":\"entry\","
      << "\"type\":" << jsonEscape(node.is_directory ? "directory" : "file")
      << ",\"virtual_path\":" << jsonEscape(virtual_path);

  if (!node.is_directory) {
    const auto& fi = node.file_info;
    out << ",\"real_path\":" << jsonEscape(fi.real_path)
        << ",\"real_path_kind\":"
        << jsonEscape(fi.is_backing ? "backing_relative" : "absolute")
        << ",\"origin\":" << jsonEscape(fi.origin)
        << ",\"size\":" << fi.size
        << ",\"mtime_ns\":" << timeToNs(fi.mtime)
        << ",\"is_backing\":" << (fi.is_backing ? "true" : "false");
  }

  out << "}\n";
}

std::size_t writeNode(std::ostream& out, const VfsNode& node,
                      const std::string& virtual_path)
{
  std::size_t written = 0;
  if (!virtual_path.empty()) {
    writeEntry(out, virtual_path, node);
    ++written;
  }

  if (!node.is_directory) {
    return written;
  }

  auto children = node.listChildren();
  std::sort(children.begin(), children.end(),
            [](const auto& a, const auto& b) {
              return normalizeForLookup(a.first) < normalizeForLookup(b.first);
            });

  for (const auto& [displayName, child] : children) {
    if (child == nullptr) {
      continue;
    }
    std::string childPath = virtual_path;
    if (!childPath.empty()) {
      childPath += '/';
    }
    childPath += displayName;
    written += writeNode(out, *child, childPath);
  }

  return written;
}

}  // namespace

fs::path vfsBridgeIndexPath(
    const std::string& data_dir, const std::string& overwrite_dir,
    const std::vector<std::pair<std::string, std::string>>& mods)
{
  std::string fp;
  fp.reserve(4096);
  fp += data_dir;
  fp += '\0';
  fp += overwrite_dir;
  fp += '\0';
  for (const auto& [name, path] : mods) {
    fp += name;
    fp += '\0';
    fp += path;
    fp += '\0';
  }

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx.jsonl",
                static_cast<unsigned long long>(hash64(fp)));

  return fs::path(fluorineVfsBridgeDir().toStdString()) / buf;
}

VfsBridgeExportResult exportVfsBridgeIndex(
    const VfsTree& tree, const fs::path& path, const std::string& data_dir,
    const std::string& overwrite_dir, const std::string& mount_point)
{
  VfsBridgeExportResult result;
  result.path = path;

  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    result.error = ec.message();
    return result;
  }

  fs::path tmp = path;
  tmp += ".tmp";

  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      result.error = "failed to open temporary index file";
      return result;
    }

    writeMeta(out, tree, data_dir, overwrite_dir, mount_point);
    result.records_written = writeNode(out, tree.root, {});

    out.flush();
    if (!out.good()) {
      result.error = "failed while writing index file";
      fs::remove(tmp, ec);
      return result;
    }
  }

  fs::rename(tmp, path, ec);
  if (ec) {
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) {
      result.error = ec.message();
      fs::remove(tmp, ec);
      return result;
    }
  }

  result.ok = true;
  return result;
}
