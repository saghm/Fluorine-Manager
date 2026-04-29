#ifndef ENV_SHELL_H
#define ENV_SHELL_H

#include "env.h"
#include <QFileInfo>
#include <QMainWindow>
#include <QPoint>
#include <QString>

namespace env
{

// Shell context menus are Windows-only — these stubs let the rest of the
// codebase compile and call into them as no-ops on Linux.
class ShellMenu
{
public:
  ShellMenu(QMainWindow*) {}

  ShellMenu(const ShellMenu&)            = delete;
  ShellMenu& operator=(const ShellMenu&) = delete;
  ShellMenu(ShellMenu&&)                 = default;
  ShellMenu& operator=(ShellMenu&&)      = default;

  void addFile(QFileInfo) {}
  int fileCount() const { return 0; }
  void exec(const QPoint&) {}
};

class ShellMenuCollection
{
public:
  ShellMenuCollection(QMainWindow*) {}

  void addDetails(QString) {}
  void add(QString, ShellMenu) {}
  void exec(const QPoint&) {}
};

}  // namespace env

#endif  // ENV_SHELL_H
