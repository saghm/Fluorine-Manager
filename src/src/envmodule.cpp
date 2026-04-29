#include "envmodule.h"
#include "env.h"
#include <log.h>
#include <utility.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <signal.h>

namespace env
{

using namespace MOBase;

// the rationale for logging md5 was to make sure the various files were the
// same as in the released version; this turned out to be of dubious interest,
// while adding to the startup time
constexpr bool UseMD5 = false;

Module::Module(QString path, std::size_t fileSize)
    : m_path(std::move(path)), m_fileSize(fileSize)
{
  const auto fi = getFileInfo();

  m_timestamp     = getTimestamp();
  m_versionString = fi.fileDescription;

  if (UseMD5) {
    m_md5 = getMD5();
  }
}

const QString& Module::path() const
{
  return m_path;
}

QString Module::displayPath() const
{
  return QDir::fromNativeSeparators(m_path.toLower());
}

std::size_t Module::fileSize() const
{
  return m_fileSize;
}

const QString& Module::version() const
{
  return m_version;
}

const QString& Module::versionString() const
{
  return m_versionString;
}

const QDateTime& Module::timestamp() const
{
  return m_timestamp;
}

const QString& Module::md5() const
{
  return m_md5;
}

QString Module::timestampString() const
{
  if (!m_timestamp.isValid()) {
    return "(no timestamp)";
  }

  return m_timestamp.toString(Qt::DateFormat::ISODate);
}

QString Module::toString() const
{
  QStringList sl;

  sl.push_back(displayPath());
  sl.push_back(QString("%1 B").arg(m_fileSize));

  if (m_version.isEmpty() && m_versionString.isEmpty()) {
    sl.push_back("(no version)");
  } else {
    if (!m_version.isEmpty()) {
      sl.push_back(m_version);
    }

    if (!m_versionString.isEmpty() && m_versionString != m_version) {
      sl.push_back(versionString());
    }
  }

  if (m_timestamp.isValid()) {
    sl.push_back(m_timestamp.toString(Qt::DateFormat::ISODate));
  } else {
    sl.push_back("(no timestamp)");
  }

  if (!m_md5.isEmpty()) {
    sl.push_back(m_md5);
  }

  return sl.join(", ");
}

Module::FileInfo Module::getFileInfo() const
{
  // ELF .so files don't carry the equivalent of a Win32 PE version resource.
  return {};
}

QDateTime Module::getTimestamp() const
{
  QFileInfo fi(m_path);
  if (!fi.exists()) {
    return {};
  }
  return fi.lastModified();
}

bool Module::interesting() const
{
  if (m_path.startsWith("/usr/lib/", Qt::CaseInsensitive) ||
      m_path.startsWith("/usr/lib64/", Qt::CaseInsensitive) ||
      m_path.startsWith("/lib/", Qt::CaseInsensitive) ||
      m_path.startsWith("/lib64/", Qt::CaseInsensitive)) {
    return false;
  }

  return true;
}

QString Module::getMD5() const
{
  static const std::set<QString> ignore = {
      "/usr/lib/", "/usr/lib64/", "/usr/share/", "/lib/", "/lib64/"};

  for (auto&& i : ignore) {
    if (m_path.startsWith(i, Qt::CaseInsensitive)) {
      return {};
    }
  }

  QFile f(m_path);
  if (!f.open(QFile::ReadOnly)) {
    log::error("failed to open file '{}' for md5", m_path);
    return {};
  }

  QCryptographicHash hash(QCryptographicHash::Md5);
  if (!hash.addData(&f)) {
    log::error("failed to calculate md5 for '{}'", m_path);
    return {};
  }

  return hash.result().toHex();
}

// -- Process implementation --

Process::Process() : m_pid(0) {}

Process::Process(pid_t pid, pid_t ppid, QString name)
    : m_pid(pid), m_ppid(ppid), m_name(std::move(name))
{}

bool Process::isValid() const
{
  return (m_pid != 0);
}

pid_t Process::pid() const
{
  return m_pid;
}

pid_t Process::ppid() const
{
  if (!m_ppid) {
    m_ppid = getProcessParentID(m_pid);
  }

  return *m_ppid;
}

const QString& Process::name() const
{
  if (!m_name) {
    m_name = getProcessName(m_pid);
  }

  return *m_name;
}

HandlePtr Process::openHandleForWait() const
{
  // On Linux, the caller can use waitpid() or kill(pid, 0) to check on the
  // process. We just wrap the pid in the HANDLE-shaped type so callers
  // designed for the Win32 API still compile.
  if (kill(m_pid, 0) == 0) {
    return HandlePtr(HandleCloser::pointer(static_cast<uintptr_t>(m_pid)));
  }
  return {};
}

bool Process::canAccess() const
{
  return (kill(m_pid, 0) == 0);
}

void Process::addChild(Process p)
{
  m_children.push_back(p);
}

std::vector<Process>& Process::children()
{
  return m_children;
}

const std::vector<Process>& Process::children() const
{
  return m_children;
}

// -- Free functions --

std::vector<Module> getLoadedModules()
{
  std::vector<Module> v;

  std::ifstream maps("/proc/self/maps");
  if (!maps.is_open()) {
    log::error("failed to open /proc/self/maps");
    return {};
  }

  std::set<QString> seen;
  std::string line;

  while (std::getline(maps, line)) {
    // Each /proc/self/maps line: address perms offset dev inode pathname.
    // The pathname (last field) is what we want when it's an absolute path.
    auto spacePos = line.rfind(' ');
    if (spacePos == std::string::npos) {
      continue;
    }

    std::string path = line.substr(spacePos + 1);
    if (path.empty() || path[0] != '/') {
      continue;
    }

    QString qpath = QString::fromStdString(path);

    if (seen.contains(qpath)) {
      continue;
    }
    seen.insert(qpath);

    QFileInfo fi(qpath);
    if (fi.exists()) {
      v.push_back(Module(qpath, fi.size()));
    }
  }

  std::sort(v.begin(), v.end(), [](auto&& a, auto&& b) {
    return (a.displayPath().compare(b.displayPath(), Qt::CaseInsensitive) < 0);
  });

  return v;
}

std::vector<Process> getRunningProcesses()
{
  std::vector<Process> v;

  DIR* procDir = opendir("/proc");
  if (!procDir) {
    log::error("failed to open /proc");
    return {};
  }

  struct dirent* entry;
  while ((entry = readdir(procDir)) != nullptr) {
    bool isNumeric = true;
    for (const char* p = entry->d_name; *p; ++p) {
      if (*p < '0' || *p > '9') {
        isNumeric = false;
        break;
      }
    }

    if (!isNumeric) {
      continue;
    }

    pid_t pid = static_cast<pid_t>(std::strtol(entry->d_name, nullptr, 10));

    QString name;
    {
      std::string commPath = "/proc/" + std::string(entry->d_name) + "/comm";
      std::ifstream commFile(commPath);
      if (commFile.is_open()) {
        std::string commName;
        std::getline(commFile, commName);
        name = QString::fromStdString(commName);
      }
    }

    pid_t ppid = 0;
    {
      std::string statusPath = "/proc/" + std::string(entry->d_name) + "/status";
      std::ifstream statusFile(statusPath);
      if (statusFile.is_open()) {
        std::string statusLine;
        while (std::getline(statusFile, statusLine)) {
          if (statusLine.compare(0, 5, "PPid:") == 0) {
            ppid = static_cast<pid_t>(
                std::strtol(statusLine.c_str() + 5, nullptr, 10));
            break;
          }
        }
      }
    }

    v.push_back(Process(pid, ppid, name));
  }

  closedir(procDir);
  return v;
}

void findChildren(Process& parent, const std::vector<Process>& processes)
{
  for (auto&& p : processes) {
    if (p.ppid() == parent.pid()) {
      Process child = p;
      findChildren(child, processes);

      parent.addChild(child);
    }
  }
}

Process getProcessTree(pid_t pid)
{
  Process root;

  const auto v = getRunningProcesses();

  for (auto&& p : v) {
    if (p.pid() == pid) {
      Process child = p;
      findChildren(child, v);
      root.addChild(child);
      break;
    }
  }

  return root;
}

QString getProcessName(pid_t pid)
{
  std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
  std::ifstream commFile(commPath);

  if (!commFile.is_open()) {
    log::error("can't get name of process {}", pid);
    return "unknown";
  }

  std::string name;
  std::getline(commFile, name);
  return QString::fromStdString(name);
}

pid_t getProcessParentID(pid_t pid)
{
  std::string statusPath = "/proc/" + std::to_string(pid) + "/status";
  std::ifstream statusFile(statusPath);

  if (!statusFile.is_open()) {
    return 0;
  }

  std::string line;
  while (std::getline(statusFile, line)) {
    if (line.compare(0, 5, "PPid:") == 0) {
      return static_cast<pid_t>(std::strtol(line.c_str() + 5, nullptr, 10));
    }
  }

  return 0;
}

}  // namespace env
