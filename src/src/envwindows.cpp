#include "envwindows.h"
#include "env.h"
#include "envmodule.h"
#include <log.h>
#include <utility.h>

#include <sys/utsname.h>
#include <unistd.h>
#include <fstream>
#include <string>

namespace env
{

using namespace MOBase;

WindowsInfo::WindowsInfo()
{
  m_reported = getKernelVersion();
  m_real     = m_reported;
  m_release  = getRelease();
  m_elevated = getElevated();
}

WindowsInfo::Version WindowsInfo::getKernelVersion() const
{
  struct utsname uts;
  if (uname(&uts) != 0) {
    log::error("uname() failed");
    return {};
  }

  Version v;

  // Parse kernel version like "6.18.9-2-cachyos".
  QString kver      = QString::fromUtf8(uts.release);
  QStringList parts = kver.split('.');

  if (!parts.empty())
    v.major = parts[0].toUInt();
  if (parts.size() >= 2)
    v.minor = parts[1].toUInt();
  if (parts.size() >= 3) {
    QString buildStr = parts[2];
    int dashPos      = buildStr.indexOf('-');
    if (dashPos >= 0) {
      buildStr = buildStr.left(dashPos);
    }
    v.build = buildStr.toUInt();
  }

  return v;
}

WindowsInfo::Release WindowsInfo::getRelease() const
{
  Release r;

  std::ifstream osRelease("/etc/os-release");
  if (osRelease.is_open()) {
    std::string line;
    while (std::getline(osRelease, line)) {
      auto eqPos = line.find('=');
      if (eqPos == std::string::npos)
        continue;

      std::string key = line.substr(0, eqPos);
      std::string val = line.substr(eqPos + 1);

      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
        val = val.substr(1, val.size() - 2);
      }

      if (key == "PRETTY_NAME") {
        r.buildLab = QString::fromStdString(val);
      } else if (key == "VERSION_ID") {
        r.ID = QString::fromStdString(val);
      }
    }
  }

  return r;
}

std::optional<bool> WindowsInfo::getElevated() const
{
  return (geteuid() == 0);
}

bool WindowsInfo::compatibilityMode() const
{
  return false;
}

const WindowsInfo::Version& WindowsInfo::reportedVersion() const
{
  return m_reported;
}

const WindowsInfo::Version& WindowsInfo::realVersion() const
{
  return m_real;
}

const WindowsInfo::Release& WindowsInfo::release() const
{
  return m_release;
}

std::optional<bool> WindowsInfo::isElevated() const
{
  return m_elevated;
}

QString WindowsInfo::toString() const
{
  QStringList sl;

  sl.push_back("version " + m_reported.toString());

  if (!m_release.ID.isEmpty()) {
    sl.push_back("release " + m_release.ID);
  }
  if (!m_release.buildLab.isEmpty()) {
    sl.push_back(m_release.buildLab);
  }

  QString elevated = "?";
  if (m_elevated.has_value()) {
    elevated = (*m_elevated ? "yes" : "no");
  }
  sl.push_back("elevated: " + elevated);

  return sl.join(", ");
}

}  // namespace env
