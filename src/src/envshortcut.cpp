#include "envshortcut.h"

#ifdef _WIN32

#include "env.h"
#include "executableslist.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

class ShellLinkException
{
public:
  ShellLinkException(QString s) : m_what(std::move(s)) {}

  const QString& what() const { return m_what; }

private:
  QString m_what;
};

// just a wrapper around IShellLink operations that throws ShellLinkException
// on errors
//
class ShellLinkWrapper
{
public:
  ShellLinkWrapper()
  {
    m_link = createShellLink();
    m_file = createPersistFile();
  }

  void setPath(const QString& s)
  {
    if (s.isEmpty()) {
      throw ShellLinkException("path cannot be empty");
    }

    const auto r = m_link->SetPath(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set target path '%1'").arg(s));
  }

  void setArguments(const QString& s)
  {
    const auto r = m_link->SetArguments(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set arguments '%1'").arg(s));
  }

  void setDescription(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetDescription(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set description '%1'").arg(s));
  }

  void setIcon(const QString& file, int i)
  {
    if (file.isEmpty()) {
      return;
    }

    const auto r = m_link->SetIconLocation(file.toStdWString().c_str(), i);
    throwOnFail(r, QString("failed to set icon '%1' @ %2").arg(file).arg(i));
  }

  void setWorkingDirectory(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetWorkingDirectory(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set working directory '%1'").arg(s));
  }

  void save(const QString& path)
  {
    const auto r = m_file->Save(path.toStdWString().c_str(), TRUE);
    throwOnFail(r, QString("failed to save link '%1'").arg(path));
  }

private:
  COMPtr<IShellLink> m_link;
  COMPtr<IPersistFile> m_file;

  void throwOnFail(HRESULT r, const QString& s)
  {
    if (FAILED(r)) {
      throw ShellLinkException(QString("%1, %2").arg(s).arg(formatSystemMessage(r)));
    }
  }

  COMPtr<IShellLink> createShellLink()
  {
    void* link = nullptr;

    const auto r = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IShellLink, &link);

    throwOnFail(r, "failed to create IShellLink instance");

    if (!link) {
      throw ShellLinkException("creating IShellLink worked, pointer is null");
    }

    return COMPtr<IShellLink>(static_cast<IShellLink*>(link));
  }

  COMPtr<IPersistFile> createPersistFile()
  {
    void* file = nullptr;

    const auto r = m_link->QueryInterface(IID_IPersistFile, &file);
    throwOnFail(r, "failed to get IPersistFile interface");

    if (!file) {
      throw ShellLinkException("querying IPersistFile worked, pointer is null");
    }

    return COMPtr<IPersistFile>(static_cast<IPersistFile*>(file));
  }
};

Shortcut::Shortcut() : m_iconIndex(0) {}

Shortcut::Shortcut(const Executable& exe) : Shortcut()
{
  const auto i = *InstanceManager::singleton().currentInstance();

  m_name   = MOBase::sanitizeFileName(exe.title());
  m_target = QFileInfo(qApp->applicationFilePath()).absoluteFilePath();

  m_arguments = QString("\"moshortcut://%1:%2\"")
                    .arg(i.isPortable() ? "" : i.displayName())
                    .arg(exe.title());

  m_description = QString("Run %1 with ModOrganizer").arg(exe.title());

  if (exe.usesOwnIcon()) {
    m_icon = exe.binaryInfo().absoluteFilePath();
  }

  m_workingDirectory = qApp->applicationDirPath();
}

Shortcut& Shortcut::name(const QString& s)
{
  m_name = s;
  return *this;
}

Shortcut& Shortcut::target(const QString& s)
{
  m_target = s;
  return *this;
}

Shortcut& Shortcut::arguments(const QString& s)
{
  m_arguments = s;
  return *this;
}

Shortcut& Shortcut::description(const QString& s)
{
  m_description = s;
  return *this;
}

Shortcut& Shortcut::icon(const QString& s, int index)
{
  m_icon      = s;
  m_iconIndex = index;
  return *this;
}

Shortcut& Shortcut::workingDirectory(const QString& s)
{
  m_workingDirectory = s;
  return *this;
}

bool Shortcut::exists(Locations loc) const
{
  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  return QFileInfo(path).exists();
}

bool Shortcut::toggle(Locations loc)
{
  if (exists(loc)) {
    return remove(loc);
  } else {
    return add(loc);
  }
}

bool Shortcut::add(Locations loc)
{
  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}' @ {}\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_iconIndex, m_workingDirectory);

  if (m_target.isEmpty()) {
    log::error("shortcut: target is empty");
    return false;
  }

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("shorcut file will be saved at '{}'", path);

  try {
    ShellLinkWrapper link;

    link.setPath(m_target);
    link.setArguments(m_arguments);
    link.setDescription(m_description);
    link.setIcon(m_icon, m_iconIndex);
    link.setWorkingDirectory(m_workingDirectory);

    link.save(path);

    return true;
  } catch (ShellLinkException& e) {
    log::error("{}\nshortcut file was not saved", e.what());
  }

  return false;
}

bool Shortcut::remove(Locations loc)
{
  log::debug("removing shortcut for '{}' from {}", m_name, toString(loc));

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("path to shortcut file is '{}'", path);

  if (!QFile::exists(path)) {
    log::error("can't remove shortcut '{}', file not found", path);
    return false;
  }

  if (!MOBase::shellDelete({path})) {
    const auto e = ::GetLastError();

    log::error("failed to remove shortcut '{}', {}", path, formatSystemMessage(e));

    return false;
  }

  return true;
}

QString Shortcut::shortcutPath(Locations loc) const
{
  const auto dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }

  const auto file = shortcutFilename();
  if (file.isEmpty()) {
    return {};
  }

  return dir + QDir::separator() + file;
}

QString Shortcut::shortcutDirectory(Locations loc) const
{
  QString dir;

  try {
    switch (loc) {
    case Desktop:
      dir = MOBase::getDesktopDirectory();
      break;

    case StartMenu:
      dir = MOBase::getStartMenuDirectory();
      break;

    case None:
    default:
      log::error("shortcut: bad location {}", loc);
      break;
    }
  } catch (std::exception&) {
  }

  return QDir::toNativeSeparators(dir);
}

QString Shortcut::shortcutFilename() const
{
  if (m_name.isEmpty()) {
    log::error("shortcut name is empty");
    return {};
  }

  return m_name + ".lnk";
}

QString toString(Shortcut::Locations loc)
{
  switch (loc) {
  case Shortcut::None:
    return "none";

  case Shortcut::Desktop:
    return "desktop";

  case Shortcut::StartMenu:
    return "start menu";

  default:
    return QString("? (%1)").arg(static_cast<int>(loc));
  }
}

}  // namespace env

#else  // Linux

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include "executableslist.h"
#include "instancemanager.h"

namespace env
{

// Returns the path to the AppImage file itself, or falls back to the
// running binary if not running from an AppImage.
static QString appImageOrBinary()
{
  QString appImage = QProcessEnvironment::systemEnvironment().value("APPIMAGE");
  if (!appImage.isEmpty() && QFile::exists(appImage)) {
    return appImage;
  }
  return QFileInfo(qApp->applicationFilePath()).absoluteFilePath();
}

Shortcut::Shortcut() : m_iconIndex(0) {}

Shortcut::Shortcut(const Executable& exe) : Shortcut()
{
  const auto i = *InstanceManager::singleton().currentInstance();

  m_name   = exe.title();
  m_target = appImageOrBinary();

  // For portable instances, use the absolute directory path so MO2 can
  // find it (line 595 in instancemanager.cpp handles abs path lookup).
  // For global instances, use the display name.
  QString instanceId = i.isPortable() ? QDir(i.directory()).absolutePath()
                                      : i.displayName();
  m_arguments = QString("\"moshortcut://%1:%2\"")
                    .arg(instanceId)
                    .arg(exe.title());

  m_description = QString("Run %1 with Fluorine").arg(exe.title());

  // .exe icons can't be used on Linux — only use native icon formats.
  // Fall back to the Fluorine app icon bundled in the AppImage.
  if (exe.usesOwnIcon()) {
    QString iconPath = exe.binaryInfo().absoluteFilePath();
    if (!iconPath.endsWith(".exe", Qt::CaseInsensitive)) {
      m_icon = iconPath;
    }
  }
  if (m_icon.isEmpty()) {
    // Install the Fluorine icon to the user's icon theme so it persists
    // after the AppImage unmounts, then reference it by icon name.
    QString appDir = QProcessEnvironment::systemEnvironment().value("APPDIR");
    if (!appDir.isEmpty()) {
      QString bundled = appDir + "/usr/share/icons/hicolor/256x256/apps/com.fluorine.manager.png";
      if (QFile::exists(bundled)) {
        QString destDir = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
        QString dest    = destDir + "/com.fluorine.manager.png";
        if (!QFile::exists(dest)) {
          QDir().mkpath(destDir);
          QFile::copy(bundled, dest);
        }
      }
    }
    m_icon = "com.fluorine.manager";
  }

  m_workingDirectory = QFileInfo(m_target).absolutePath();
}

Shortcut& Shortcut::name(const QString& s)
{
  m_name = s;
  return *this;
}

Shortcut& Shortcut::target(const QString& s)
{
  m_target = s;
  return *this;
}

Shortcut& Shortcut::arguments(const QString& s)
{
  m_arguments = s;
  return *this;
}

Shortcut& Shortcut::description(const QString& s)
{
  m_description = s;
  return *this;
}

Shortcut& Shortcut::icon(const QString& s, int index)
{
  m_icon      = s;
  m_iconIndex = index;
  return *this;
}

Shortcut& Shortcut::workingDirectory(const QString& s)
{
  m_workingDirectory = s;
  return *this;
}

bool Shortcut::exists(Locations loc) const
{
  return QFile::exists(shortcutPath(loc));
}

bool Shortcut::toggle(Locations loc)
{
  if (exists(loc)) {
    return remove(loc);
  } else {
    return add(loc);
  }
}

bool Shortcut::add(Locations loc)
{
  if (loc != Desktop) {
    return false;
  }

  const QString path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  QDir().mkpath(QFileInfo(path).absolutePath());

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }

  QTextStream out(&file);
  out << "[Desktop Entry]\n";
  out << "Type=Application\n";
  out << "Name=" << (m_name.isEmpty() ? "Fluorine" : m_name) << "\n";
  if (!m_description.isEmpty()) {
    out << "Comment=" << m_description << "\n";
  }
  // .desktop Exec values require quoting paths that contain spaces
  out << "Exec=\"" << m_target << "\"";
  if (!m_arguments.isEmpty()) {
    out << " " << m_arguments;
  }
  out << "\n";
  if (!m_workingDirectory.isEmpty()) {
    out << "Path=" << m_workingDirectory << "\n";
  }
  if (!m_icon.isEmpty()) {
    out << "Icon=" << m_icon << "\n";
  }
  out << "Terminal=false\n";
  out << "Categories=Game;\n";

  file.close();

  // Make it executable (required by some desktop environments)
  file.setPermissions(file.permissions() | QFileDevice::ExeUser);

  return true;
}

bool Shortcut::remove(Locations loc)
{
  const QString path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }
  return QFile::remove(path);
}

QString Shortcut::shortcutPath(Locations loc) const
{
  const QString dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }
  return dir + "/" + shortcutFilename();
}

QString Shortcut::shortcutDirectory(Locations loc) const
{
  if (loc == Desktop) {
    // XDG desktop directory
    QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
      return desktop;
    }
    return QDir::homePath() + "/Desktop";
  }
  return {};
}

QString Shortcut::shortcutFilename() const
{
  if (m_name.isEmpty()) {
    return "fluorine.desktop";
  }
  return m_name + ".desktop";
}

QString toString(Shortcut::Locations loc)
{
  switch (loc) {
  case Shortcut::None:
    return "none";

  case Shortcut::Desktop:
    return "desktop";

  case Shortcut::StartMenu:
    return "start menu";

  default:
    return QString("? (%1)").arg(static_cast<int>(loc));
  }
}

}  // namespace env

#endif  // _WIN32
