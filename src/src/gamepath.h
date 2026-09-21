#ifndef GAMEPATH_H
#define GAMEPATH_H

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <uibase/utility.h>

// Imported portable instances resolve gamePath relative to ModOrganizer.ini,
// not the directory containing the running Fluorine executable.
inline QString resolveStoredGamePath(const QString& storedPath,
                                     const QString& iniPath)
{
  QString path = MOBase::normalizePathForHost(storedPath);
  if (path.isEmpty() || QDir::isAbsolutePath(path) ||
      MOBase::isWindowsDrivePath(path)) {
    return path;
  }

  // QDir::fromNativeSeparators() does not convert Windows separators on Linux.
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return QDir::cleanPath(QFileInfo(iniPath).absoluteDir().absoluteFilePath(path));
}

#endif  // GAMEPATH_H
