#ifndef ENV_MODULE_H
#define ENV_MODULE_H

#include <QDateTime>
#include <QString>

#include <sys/types.h>
#include <signal.h>

namespace env
{

// HandlePtr exists for compatibility with the rest of the codebase that
// expects a Win32 HANDLE-shaped type. On Linux there's nothing to close,
// so the deleter is a no-op.
struct HandleCloser
{
  using pointer = HANDLE;

  void operator()(HANDLE) {}
};

using HandlePtr = std::unique_ptr<HANDLE, HandleCloser>;

// represents one module
//
class Module
{
public:
  Module(QString path, std::size_t fileSize);

  // returns the module's path
  //
  const QString& path() const;

  // returns the module's path in lowercase and using forward slashes
  //
  QString displayPath() const;

  // returns the size in bytes, may be 0
  //
  std::size_t fileSize() const;

  // returns the x.x.x.x version embedded from the version info, may be empty
  //
  const QString& version() const;

  // returns the FileVersion entry from the resource file, returns
  // "(no version)" if not available
  //
  const QString& versionString() const;

  // returns the build date from the version info, or the creation time of the
  // file on the filesystem, may be empty
  //
  const QDateTime& timestamp() const;

  // returns the md5 of the file, may be empty for system files
  //
  const QString& md5() const;

  // converts timestamp() to a string for display, returns "(no timestamp)" if
  // not available
  //
  QString timestampString() const;

  // returns false for modules in system directories
  //
  bool interesting() const;

  // returns a string with all the above information on one line
  //
  QString toString() const;

private:
  struct FileInfo
  {
    QString fileDescription;
  };

  QString m_path;
  std::size_t m_fileSize;
  QString m_version;
  QDateTime m_timestamp;
  QString m_versionString;
  QString m_md5;

  FileInfo getFileInfo() const;
  QDateTime getTimestamp() const;
  QString getMD5() const;
};

// represents one process
//
class Process
{
public:
  Process();
  Process(pid_t pid, pid_t ppid, QString name);

  bool isValid() const;

  pid_t pid() const;
  pid_t ppid() const;

  const QString& name() const;

  HandlePtr openHandleForWait() const;

  // whether this process can be accessed; fails if the current process doesn't
  // have the proper permissions
  //
  bool canAccess() const;

  void addChild(Process p);
  std::vector<Process>& children();
  const std::vector<Process>& children() const;

private:
  pid_t m_pid;
  mutable std::optional<pid_t> m_ppid;
  mutable std::optional<QString> m_name;
  std::vector<Process> m_children;
};

std::vector<Process> getRunningProcesses();
std::vector<Module> getLoadedModules();

// builds a process tree from a given pid
//
Process getProcessTree(pid_t pid);

QString getProcessName(pid_t pid);
pid_t getProcessParentID(pid_t pid);

}  // namespace env

#endif  // ENV_MODULE_H
