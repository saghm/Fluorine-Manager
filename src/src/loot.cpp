#include "loot.h"
#include "json.h"
#include "lootdialog.h"
#include "organizercore.h"
#include "spawn.h"
#include <log.h>
#include <report.h>

#ifndef _WIN32
#include "fluorinepaths.h"
#include <poll.h>
#include <fcntl.h>
extern char** environ;
#endif

using namespace MOBase;
using namespace json;

static QString LootReportPath  = QDir::temp().absoluteFilePath("lootreport.json");
#ifdef _WIN32
static const DWORD PipeTimeout = 500;
#endif

#ifdef _WIN32
class AsyncPipe
{
public:
  AsyncPipe() : m_ioPending(false)
  {
    std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
    std::memset(&m_ov, 0, sizeof(m_ov));
  }

  env::HandlePtr create()
  {
    // creating pipe
    env::HandlePtr out(createPipe());
    if (out.get() == INVALID_HANDLE_VALUE) {
      return {};
    }

    HANDLE readEventHandle = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (readEventHandle == NULL) {
      const auto e = GetLastError();
      log::error("CreateEvent failed for loot, {}", formatSystemMessage(e));
      return {};
    }

    m_ov.hEvent = readEventHandle;
    m_readEvent.reset(readEventHandle);

    return out;
  }

  std::string read()
  {
    if (m_ioPending) {
      return checkPending();
    } else {
      return tryRead();
    }
  }

private:
  static const std::size_t bufferSize = 50000;

  env::HandlePtr m_stdout;
  env::HandlePtr m_readEvent;
  char m_buffer[bufferSize];
  OVERLAPPED m_ov;
  bool m_ioPending;

  HANDLE createPipe()
  {
    static const wchar_t* PipeName = L"\\\\.\\pipe\\lootcli_pipe";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength             = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle      = TRUE;

    env::HandlePtr pipe;

    // creating pipe
    {
      HANDLE pipeHandle =
          ::CreateNamedPipe(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 50'000,
                            50'000, PipeTimeout, &sa);

      if (pipeHandle == INVALID_HANDLE_VALUE) {
        const auto e = GetLastError();
        log::error("CreateNamedPipe failed, {}", formatSystemMessage(e));
        return INVALID_HANDLE_VALUE;
      }

      pipe.reset(pipeHandle);
    }

    {
      // duplicating the handle to read from it
      HANDLE outputRead = INVALID_HANDLE_VALUE;

      const auto r =
          DuplicateHandle(GetCurrentProcess(), pipe.get(), GetCurrentProcess(),
                          &outputRead, 0, TRUE, DUPLICATE_SAME_ACCESS);

      if (!r) {
        const auto e = GetLastError();
        log::error("DuplicateHandle for pipe failed, {}", formatSystemMessage(e));
        return INVALID_HANDLE_VALUE;
      }

      m_stdout.reset(outputRead);
    }

    // creating handle to pipe which is passed to CreateProcess()
    HANDLE outputWrite = ::CreateFileW(PipeName, FILE_WRITE_DATA | SYNCHRONIZE, 0, &sa,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (outputWrite == INVALID_HANDLE_VALUE) {
      const auto e = GetLastError();
      log::error("CreateFileW for pipe failed, {}", formatSystemMessage(e));
      return INVALID_HANDLE_VALUE;
    }

    return outputWrite;
  }

  std::string tryRead()
  {
    DWORD bytesRead = 0;

    if (!::ReadFile(m_stdout.get(), m_buffer, bufferSize, &bytesRead, &m_ov)) {
      const auto e = GetLastError();

      switch (e) {
      case ERROR_IO_PENDING: {
        m_ioPending = true;
        break;
      }

      case ERROR_BROKEN_PIPE: {
        // broken pipe probably means lootcli is finished
        break;
      }

      default: {
        log::error("{}", formatSystemMessage(e));
        break;
      }
      }

      return {};
    }

    return {m_buffer, m_buffer + bytesRead};
  }

  std::string checkPending()
  {
    DWORD bytesRead = 0;

    const auto r = WaitForSingleObject(m_readEvent.get(), PipeTimeout);

    if (r == WAIT_FAILED) {
      const auto e = GetLastError();
      log::error("WaitForSingleObject in AsyncPipe failed, {}", formatSystemMessage(e));
      return {};
    }

    if (!::GetOverlappedResult(m_stdout.get(), &m_ov, &bytesRead, FALSE)) {
      const auto e = GetLastError();

      switch (e) {
      case ERROR_IO_INCOMPLETE: {
        break;
      }

      case WAIT_TIMEOUT: {
        break;
      }

      case ERROR_BROKEN_PIPE: {
        // broken pipe probably means lootcli is finished
        break;
      }

      default: {
        log::error("GetOverlappedResult failed, {}", formatSystemMessage(e));
        break;
      }
      }

      return {};
    }

    ::ResetEvent(m_readEvent.get());
    m_ioPending = false;

    return {m_buffer, m_buffer + bytesRead};
  }
};
#else
// Linux implementation using POSIX pipes
class AsyncPipe
{
public:
  AsyncPipe() : m_readFd(-1), m_writeFd(-1) {}

  ~AsyncPipe()
  {
    if (m_readFd >= 0)
      ::close(m_readFd);
    if (m_writeFd >= 0)
      ::close(m_writeFd);
  }

  env::HandlePtr create()
  {
    int fds[2];
    if (::pipe(fds) != 0) {
      log::error("pipe() failed: {}", strerror(errno));
      return {};
    }
    m_readFd  = fds[0];
    m_writeFd = fds[1];

    // set read end to non-blocking
    int flags = ::fcntl(m_readFd, F_GETFL, 0);
    ::fcntl(m_readFd, F_SETFL, flags | O_NONBLOCK);

    // return a non-null HandlePtr to indicate success
    return env::HandlePtr(reinterpret_cast<HANDLE>(static_cast<intptr_t>(1)));
  }

  int readFd() const { return m_readFd; }
  int writeFd() const { return m_writeFd; }

  void closeWriteEnd()
  {
    if (m_writeFd >= 0) {
      ::close(m_writeFd);
      m_writeFd = -1;
    }
  }

  std::string read()
  {
    if (m_readFd < 0)
      return {};

    char buffer[50000];
    ssize_t n = ::read(m_readFd, buffer, sizeof(buffer));
    if (n > 0) {
      return std::string(buffer, n);
    }
    return {};
  }

private:
  int m_readFd;
  int m_writeFd;
};
#endif

log::Levels levelFromLoot(lootcli::LogLevels level)
{
  using LC = lootcli::LogLevels;

  switch (level) {
  case LC::Trace:  // fall-through
  case LC::Debug:
    return log::Debug;

  case LC::Info:
    return log::Info;

  case LC::Warning:
    return log::Warning;

  case LC::Error:
    return log::Error;

  default:
    return log::Info;
  }
}

QString Loot::Report::toMarkdown() const
{
  QString s;

  if (!okay) {
    s += "## " + tr("Loot failed to run") + "\n";

    if (errors.empty() && warnings.empty()) {
      s += tr("No errors were reported. The log below might have more information.\n");
    }
  }

  s += errorsMarkdown();

  if (okay) {
    s += "\n" + successMarkdown();
  }

  return s;
}

QString Loot::Report::successMarkdown() const
{
  QString s;

  if (!messages.empty()) {
    s += "### " + QObject::tr("General messages") + "\n";

    for (auto&& m : messages) {
      s += " - " + m.toMarkdown() + "\n";
    }
  }

  if (!plugins.empty()) {
    if (!s.isEmpty()) {
      s += "\n";
    }

    s += "### " + QObject::tr("Plugins") + "\n";

    for (auto&& p : plugins) {
      const auto ps = p.toMarkdown();
      if (!ps.isEmpty()) {
        s += ps + "\n";
      }
    }
  }

  if (s.isEmpty()) {
    s += "**" + QObject::tr("No messages.") + "**\n";
  }

  s += stats.toMarkdown();

  return s;
}

QString Loot::Report::errorsMarkdown() const
{
  QString s;

  if (!errors.empty()) {
    s += "### " + tr("Errors") + ":\n";

    for (auto&& e : errors) {
      s += " - " + e + "\n";
    }
  }

  if (!warnings.empty()) {
    if (!s.isEmpty()) {
      s += "\n";
    }

    s += "### " + tr("Warnings") + ":\n";

    for (auto&& w : warnings) {
      s += " - " + w + "\n";
    }
  }

  return s;
}

QString Loot::Stats::toMarkdown() const
{
  return QString("`stats: %1s, lootcli %2, loot %3`")
      .arg(QString::number(time / 1000.0, 'f', 2))
      .arg(lootcliVersion)
      .arg(lootVersion);
}

QString Loot::Plugin::toMarkdown() const
{
  QString s;

  if (!incompatibilities.empty()) {
    s += " - **" + QObject::tr("Incompatibilities") + ": ";

    QString fs;
    for (auto&& f : incompatibilities) {
      if (!fs.isEmpty()) {
        fs += ", ";
      }

      fs += f.displayName.isEmpty() ? f.name : f.displayName;
    }

    s += fs + "**\n";
  }

  if (!missingMasters.empty()) {
    s += " - **" + QObject::tr("Missing masters") + ": ";

    QString ms;
    for (auto&& m : missingMasters) {
      if (!ms.isEmpty()) {
        ms += ", ";
      }

      ms += m;
    }

    s += ms + "**\n";
  }

  for (auto&& m : messages) {
    s += " - " + m.toMarkdown() + "\n";
  }

  for (auto&& d : dirty) {
    s += " - " + d.toMarkdown(false) + "\n";
  }

  if (!s.isEmpty()) {
    s = "#### " + name + "\n" + s;
  }

  return s;
}

QString Loot::Dirty::toString(bool isClean) const
{
  if (isClean) {
    return QObject::tr("Verified clean by %1")
        .arg(cleaningUtility.isEmpty() ? "?" : cleaningUtility);
  }

  QString s = cleaningString();

  if (!info.isEmpty()) {
    s += " " + info;
  }

  return s;
}

QString Loot::Dirty::toMarkdown(bool isClean) const
{
  return toString(isClean);
}

QString Loot::Dirty::cleaningString() const
{
  return QObject::tr("%1 found %2 ITM record(s), %3 deleted reference(s) and %4 "
                     "deleted navmesh(es).")
      .arg(cleaningUtility.isEmpty() ? "?" : cleaningUtility)
      .arg(itm)
      .arg(deletedReferences)
      .arg(deletedNavmesh);
}

QString Loot::Message::toMarkdown() const
{
  QString s;

  switch (type) {
  case log::Error: {
    s += "**" + QObject::tr("Error") + "**: ";
    break;
  }

  case log::Warning: {
    s += "**" + QObject::tr("Warning") + "**: ";
    break;
  }

  default: {
    break;
  }
  }

  s += text;

  return s;
}

Loot::Loot(OrganizerCore& core)
    : m_core(core), m_thread(nullptr), m_cancel(false), m_result(false)
{}

Loot::~Loot()
{
  if (m_thread) {
    m_thread->wait();
  }

  deleteReportFile();
}

bool Loot::start(QWidget* parent, bool didUpdateMasterList)
{
  deleteReportFile();

  log::debug("starting loot");

  m_pipe.reset(new AsyncPipe);

  env::HandlePtr stdoutHandle = m_pipe->create();
  if (!stdoutHandle) {
    return false;
  }

  // vfs
  m_core.prepareVFS();

  // spawning
  if (!spawnLootcli(parent, didUpdateMasterList, std::move(stdoutHandle))) {
    return false;
  }

  // starting thread
  log::debug("starting loot thread");
  m_thread.reset(QThread::create([&] {
    lootThread();
  }));
  m_thread->start();

  return true;
}

bool Loot::spawnLootcli(QWidget* parent, bool didUpdateMasterList,
                        env::HandlePtr stdoutHandle)
{
#ifdef _WIN32
  const auto logLevel = m_core.settings().diagnostics().lootLogLevel();

  QStringList parameters;
  parameters << "--game" << m_core.managedGame()->lootGameName()

             << "--gamePath"
             << QString("\"%1\"").arg(
                    m_core.managedGame()->gameDirectory().absolutePath())

             << "--pluginListPath"
             << QString("\"%1/loadorder.txt\"").arg(m_core.profilePath())

             << "--logLevel"
             << QString::fromStdString(lootcli::logLevelToString(logLevel))

             << "--out" << QString("\"%1\"").arg(LootReportPath)

             << "--language" << m_core.settings().interface().language();

  if (didUpdateMasterList) {
    parameters << "--skipUpdateMasterlist";
  }

  spawn::SpawnParameters sp;
  sp.binary    = QFileInfo(qApp->applicationDirPath() + "/loot/lootcli.exe");
  sp.arguments = parameters.join(" ");
  sp.currentDirectory.setPath(qApp->applicationDirPath() + "/loot");
  sp.hooked = true;
  sp.stdOut = stdoutHandle.get();

  HANDLE lootHandle = spawn::startBinary(parent, sp);

  if (lootHandle == INVALID_HANDLE_VALUE) {
    emit log(log::Levels::Error, tr("failed to start loot"));
    return false;
  }

  m_lootProcess.reset(lootHandle);

  return true;
#else
  Q_UNUSED(parent);
  Q_UNUSED(stdoutHandle);

  const auto logLevel = m_core.settings().diagnostics().lootLogLevel();

  QStringList parameters;
  parameters << "--game" << m_core.managedGame()->lootGameName()

             << "--gamePath" << m_core.managedGame()->gameDirectory().absolutePath()

             << "--pluginListPath"
             << QString("%1/loadorder.txt").arg(m_core.profilePath())

             << "--logLevel"
             << QString::fromStdString(lootcli::logLevelToString(logLevel))

             << "--out" << LootReportPath

             << "--language" << m_core.settings().interface().language();

  if (didUpdateMasterList) {
    parameters << "--skipUpdateMasterlist";
  }

  QString binary = qApp->applicationDirPath() + "/lootcli";

  if (!QFile::exists(binary)) {
    emit log(log::Levels::Error, tr("lootcli not found at %1").arg(binary));
    return false;
  }

  // build argv for posix_spawn
  std::string binaryStr = binary.toStdString();
  std::vector<std::string> argStrs;
  argStrs.push_back(binaryStr);
  for (const auto& p : parameters) {
    argStrs.push_back(p.toStdString());
  }

  std::vector<char*> argv;
  for (auto& s : argStrs) {
    argv.push_back(s.data());
  }
  argv.push_back(nullptr);

  int writeFd = m_pipe->writeFd();
  int readFd  = m_pipe->readFd();

  pid_t pid = fork();
  if (pid == -1) {
    emit log(log::Levels::Error,
             tr("failed to start lootcli: %1").arg(strerror(errno)));
    return false;
  }

  if (pid == 0) {
    // child process — only async-signal-safe calls allowed here
    dup2(writeFd, STDOUT_FILENO);
    close(writeFd);
    close(readFd);

    std::string workDir = qApp->applicationDirPath().toStdString();
    chdir(workDir.c_str());

    execv(binaryStr.c_str(), argv.data());
    _exit(127);
  }

  // parent process
  m_childPid = pid;
  m_pipe->closeWriteEnd();

  log::debug("lootcli spawned with pid {}", pid);
  return true;
#endif
}

void Loot::cancel()
{
  if (!m_cancel) {
    log::debug("loot received cancel request");
    m_cancel = true;
  }
}

bool Loot::result() const
{
  return m_result;
}

const QString& Loot::outPath() const
{
  return LootReportPath;
}

const Loot::Report& Loot::report() const
{
  return m_report;
}

const std::vector<QString>& Loot::errors() const
{
  return m_errors;
}

const std::vector<QString>& Loot::warnings() const
{
  return m_warnings;
}

void Loot::lootThread()
{
  try {
    m_result = false;

    if (waitForCompletion()) {
      m_result = true;
    }

    m_report = createReport();
  } catch (...) {
    log::error("unhandled exception in loot thread");
  }

  log::debug("finishing loot thread");
  emit finished();
}

bool Loot::waitForCompletion()
{
#ifdef _WIN32
  bool terminating = false;

  log::debug("loot thread waiting for completion on lootcli");

  for (;;) {
    DWORD res = WaitForSingleObject(m_lootProcess.get(), 100);

    if (res == WAIT_OBJECT_0) {
      log::debug("lootcli has completed");
      // done
      break;
    }

    if (res == WAIT_FAILED) {
      const auto e = GetLastError();
      log::error("failed to wait on loot process, {}", formatSystemMessage(e));
      return false;
    }

    if (m_cancel) {
      log::debug("terminating lootcli process");
      ::TerminateProcess(m_lootProcess.get(), 1);

      log::debug("waiting for loocli process to terminate");
      WaitForSingleObject(m_lootProcess.get(), INFINITE);

      log::debug("lootcli terminated");
      return false;
    }

    processStdout(m_pipe->read());
  }

  if (m_cancel) {
    return false;
  }

  processStdout(m_pipe->read());

  // checking exit code
  DWORD exitCode = 0;

  if (!::GetExitCodeProcess(m_lootProcess.get(), &exitCode)) {
    const auto e = GetLastError();
    log::error("failed to get exit code for loot, {}", formatSystemMessage(e));
    return false;
  }

  if (exitCode != 0UL) {
    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
    return false;
  }

  return true;
#else
  if (m_childPid <= 0) {
    return false;
  }

  log::debug("loot thread waiting for completion on lootcli");

  for (;;) {
    // poll the pipe for data with 100ms timeout
    struct pollfd pfd;
    pfd.fd     = m_pipe->readFd();
    pfd.events = POLLIN;
    ::poll(&pfd, 1, 100);

    processStdout(m_pipe->read());

    int status;
    pid_t result = waitpid(m_childPid, &status, WNOHANG);

    if (result == m_childPid) {
      log::debug("lootcli has completed");

      // read any remaining output
      processStdout(m_pipe->read());

      if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
          emit log(log::Levels::Error,
                   tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
          return false;
        }
        return true;
      } else {
        emit log(log::Levels::Error, tr("lootcli terminated abnormally"));
        return false;
      }
    }

    if (result == -1) {
      log::error("waitpid failed: {}", strerror(errno));
      return false;
    }

    if (m_cancel) {
      log::debug("terminating lootcli process");
      ::kill(m_childPid, SIGTERM);

      log::debug("waiting for lootcli process to terminate");
      waitpid(m_childPid, &status, 0);

      log::debug("lootcli terminated");
      return false;
    }
  }
#endif
}

void Loot::processStdout(const std::string& lootOut)
{
  emit output(QString::fromStdString(lootOut));

  m_outputBuffer += lootOut;
  if (m_outputBuffer.empty()) {
    return;
  }

  std::size_t start = 0;

  for (;;) {
    const auto newline = m_outputBuffer.find("\n", start);
    if (newline == std::string::npos) {
      break;
    }

    const std::string_view line(m_outputBuffer.c_str() + start, newline - start);
    const auto m = lootcli::parseMessage(line);

    if (m.type == lootcli::MessageType::None) {
      log::error("unrecognised loot output: '{}'", line);
      continue;
    }

    processMessage(m);

    start = newline + 1;
  }

  m_outputBuffer.erase(0, start);
}

void Loot::processMessage(const lootcli::Message& m)
{
  switch (m.type) {
  case lootcli::MessageType::Log: {
    const auto level = levelFromLoot(m.logLevel);

    if (level == log::Error) {
      m_errors.push_back(QString::fromStdString(m.log));
    } else if (level == log::Warning) {
      m_warnings.push_back(QString::fromStdString(m.log));
    }

    emit log(level, QString::fromStdString(m.log));
    break;
  }

  case lootcli::MessageType::Progress: {
    emit progress(m.progress);
    break;
  }
  }
}

Loot::Report Loot::createReport() const
{
  Report r;

  r.okay     = m_result;
  r.errors   = m_errors;
  r.warnings = m_warnings;

  if (m_result) {
    processOutputFile(r);
  }

  return r;
}

void Loot::deleteReportFile()
{
  if (QFile::exists(LootReportPath)) {
    log::debug("deleting temporary loot report '{}'", LootReportPath);
    const auto r = shell::Delete(QFileInfo(LootReportPath));

    if (!r) {
      log::error("failed to remove temporary loot json report '{}': {}", LootReportPath,
                 r.toString());
    }
  }
}

void Loot::processOutputFile(Report& r) const
{
  log::debug("parsing json output file at '{}'", LootReportPath);

  QFile outFile(LootReportPath);
  if (!outFile.open(QIODevice::ReadOnly)) {
    emit log(MOBase::log::Error, QString("failed to open file, %1 (error %2)")
                                     .arg(outFile.errorString())
                                     .arg(outFile.error()));

    return;
  }

  QJsonParseError e;
  const QJsonDocument doc = QJsonDocument::fromJson(outFile.readAll(), &e);
  if (doc.isNull()) {
    emit log(MOBase::log::Error,
             QString("invalid json, %1 (error %2)").arg(e.errorString()).arg(e.error));

    return;
  }

  requireObject(doc, "root");

  const QJsonObject object = doc.object();

  r.messages = reportMessages(getOpt<QJsonArray>(object, "messages"));
  r.plugins  = reportPlugins(getOpt<QJsonArray>(object, "plugins"));
  r.stats    = reportStats(getWarn<QJsonObject>(object, "stats"));
}

std::vector<Loot::Plugin> Loot::reportPlugins(const QJsonArray& plugins) const
{
  std::vector<Loot::Plugin> v;

  for (auto pluginValue : plugins) {
    const auto o = convertWarn<QJsonObject>(pluginValue, "plugin");
    if (o.isEmpty()) {
      continue;
    }

    auto p = reportPlugin(o);
    if (!p.name.isEmpty()) {
      v.emplace_back(std::move(p));
    }
  }

  return v;
}

Loot::Plugin Loot::reportPlugin(const QJsonObject& plugin) const
{
  Plugin p;

  p.name = getWarn<QString>(plugin, "name");
  if (p.name.isEmpty()) {
    return {};
  }

  // ignore disabled plugins; lootcli doesn't know if a plugin is enabled or not
  // and will report information on any plugin that's in the filesystem
  if (!m_core.pluginList()->isEnabled(p.name)) {
    return {};
  }

  if (plugin.contains("incompatibilities")) {
    p.incompatibilities = reportFiles(getOpt<QJsonArray>(plugin, "incompatibilities"));
  }

  if (plugin.contains("messages")) {
    p.messages = reportMessages(getOpt<QJsonArray>(plugin, "messages"));
  }

  if (plugin.contains("dirty")) {
    p.dirty = reportDirty(getOpt<QJsonArray>(plugin, "dirty"));
  }

  if (plugin.contains("clean")) {
    p.clean = reportDirty(getOpt<QJsonArray>(plugin, "clean"));
  }

  if (plugin.contains("missingMasters")) {
    p.missingMasters = reportStringArray(getOpt<QJsonArray>(plugin, "missingMasters"));
  }

  p.loadsArchive  = getOpt(plugin, "loadsArchive", false);
  p.isMaster      = getOpt(plugin, "isMaster", false);
  p.isLightMaster = getOpt(plugin, "isLightMaster", false);

  return p;
}

Loot::Stats Loot::reportStats(const QJsonObject& stats) const
{
  Stats s;

  s.time           = getWarn<qint64>(stats, "time");
  s.lootcliVersion = getWarn<QString>(stats, "lootcliVersion");
  s.lootVersion    = getWarn<QString>(stats, "lootVersion");

  return s;
}

std::vector<Loot::Message> Loot::reportMessages(const QJsonArray& array) const
{
  std::vector<Loot::Message> v;

  for (auto messageValue : array) {
    const auto o = convertWarn<QJsonObject>(messageValue, "message");
    if (o.isEmpty()) {
      continue;
    }

    Message m;

    const auto type = getWarn<QString>(o, "type");

    if (type == "info") {
      m.type = log::Info;
    } else if (type == "warn") {
      m.type = log::Warning;
    } else if (type == "error") {
      m.type = log::Error;
    } else {
      log::error("unknown message type '{}'", type);
      m.type = log::Info;
    }

    m.text = getWarn<QString>(o, "text");

    if (!m.text.isEmpty()) {
      v.emplace_back(std::move(m));
    }
  }

  return v;
}

std::vector<Loot::File> Loot::reportFiles(const QJsonArray& array) const
{
  std::vector<Loot::File> v;

  for (auto&& fileValue : array) {
    const auto o = convertWarn<QJsonObject>(fileValue, "file");
    if (o.isEmpty()) {
      continue;
    }

    File f;

    f.name        = getWarn<QString>(o, "name");
    f.displayName = getOpt<QString>(o, "displayName");

    if (!f.name.isEmpty()) {
      v.emplace_back(std::move(f));
    }
  }

  return v;
}

std::vector<Loot::Dirty> Loot::reportDirty(const QJsonArray& array) const
{
  std::vector<Loot::Dirty> v;

  for (auto&& dirtyValue : array) {
    const auto o = convertWarn<QJsonObject>(dirtyValue, "dirty");

    Dirty d;

    d.crc               = getWarn<qint64>(o, "crc");
    d.itm               = getOpt<qint64>(o, "itm");
    d.deletedReferences = getOpt<qint64>(o, "deletedReferences");
    d.deletedNavmesh    = getOpt<qint64>(o, "deletedNavmesh");
    d.cleaningUtility   = getOpt<QString>(o, "cleaningUtility");
    d.info              = getOpt<QString>(o, "info");

    v.emplace_back(std::move(d));
  }

  return v;
}

std::vector<QString> Loot::reportStringArray(const QJsonArray& array) const
{
  std::vector<QString> v;

  for (auto&& sv : array) {
    auto s = convertWarn<QString>(sv, "string");
    if (s.isEmpty()) {
      continue;
    }

    v.emplace_back(std::move(s));
  }

  return v;
}

#ifndef _WIN32

static const QString LootGitHubRepo =
    "SulfurNitride/loot-linux-build-for-fluorine";

static QString lootAppImagePath()
{
  return fluorineDataDir() + "/bin/LOOT.AppImage";
}

// Fetch the download URL for the latest Linux AppImage from GitHub releases.
static QString fetchLootDownloadUrl()
{
  QNetworkAccessManager nam;
  QEventLoop loop;

  QUrl apiUrl(QString("https://api.github.com/repos/%1/releases/latest")
                  .arg(LootGitHubRepo));
  QNetworkRequest req(apiUrl);
  req.setRawHeader("Accept", "application/vnd.github.v3+json");
  req.setRawHeader("User-Agent", "Fluorine-Manager");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);

  auto* reply = nam.get(req);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    log::error("Failed to query GitHub releases: {} (HTTP {})",
               reply->errorString(),
               reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                   .toInt());
    reply->deleteLater();
    return {};
  }

  auto doc = QJsonDocument::fromJson(reply->readAll());
  reply->deleteLater();

  auto assets = doc.object()["assets"].toArray();
  for (const auto& asset : assets) {
    auto obj  = asset.toObject();
    auto name = obj["name"].toString();
    if (name.contains("linux", Qt::CaseInsensitive) &&
        name.endsWith(".AppImage", Qt::CaseInsensitive)) {
      return obj["browser_download_url"].toString();
    }
  }

  // fallback: try any .AppImage
  for (const auto& asset : assets) {
    auto obj  = asset.toObject();
    auto name = obj["name"].toString();
    if (name.endsWith(".AppImage", Qt::CaseInsensitive)) {
      return obj["browser_download_url"].toString();
    }
  }

  log::error("No Linux AppImage found in latest release of {}", LootGitHubRepo);
  return {};
}

// Download the LOOT AppImage with a progress dialog.
static bool downloadLootAppImage(QWidget* parent, const QString& url,
                                 const QString& destPath)
{
  QNetworkAccessManager nam;
  QEventLoop loop;
  QProgressDialog progress(QObject::tr("Downloading LOOT..."), QObject::tr("Cancel"),
                           0, 100, parent);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setValue(0);

  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("User-Agent", "Fluorine-Manager");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);

  auto* reply = nam.get(req);

  QObject::connect(reply, &QNetworkReply::downloadProgress,
                   [&](qint64 received, qint64 total) {
                     if (total > 0) {
                       progress.setMaximum(static_cast<int>(total / 1024));
                       progress.setValue(static_cast<int>(received / 1024));
                     }
                   });

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    if (reply->error() != QNetworkReply::OperationCanceledError) {
      log::error("Failed to download LOOT: {}", reply->errorString());
    }
    reply->deleteLater();
    return false;
  }

  QDir().mkpath(QFileInfo(destPath).absolutePath());

  QFile f(destPath);
  if (!f.open(QIODevice::WriteOnly)) {
    log::error("Failed to write LOOT AppImage: {}", f.errorString());
    reply->deleteLater();
    return false;
  }

  f.write(reply->readAll());
  f.close();
  reply->deleteLater();

  // Make executable
  QFile::setPermissions(destPath,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup);

  log::info("LOOT AppImage downloaded to {}", destPath);
  return true;
}

// Ensure the LOOT AppImage is available, downloading if necessary.
static bool ensureLootAvailable(QWidget* parent)
{
  QString path = lootAppImagePath();
  if (QFile::exists(path)) {
    return true;
  }

  log::info("LOOT AppImage not found, downloading from GitHub...");

  QString url = fetchLootDownloadUrl();
  if (url.isEmpty()) {
    QMessageBox::critical(
        parent, QObject::tr("LOOT"),
        QObject::tr("Could not find a LOOT release to download.\n\n"
                     "Please check https://github.com/%1/releases")
            .arg(LootGitHubRepo));
    return false;
  }

  return downloadLootAppImage(parent, url, path);
}

// Map MO2's lootGameName() to LOOT's internal folder name (used by --game).
// LOOT's getDefaultLootFolderName() uses full names for some games.
static QString lootFolderName(const QString& mo2GameName)
{
  static const QMap<QString, QString> map = {
      {"SkyrimSE", "Skyrim Special Edition"},
      {"SkyrimVR", "Skyrim VR"},
      {"EnderalSE", "Enderal Special Edition"},
      {"Fallout4VR", "Fallout4VR"},
  };
  return map.value(mo2GameName, mo2GameName);
}

// Write (or update) LOOT's settings.toml so that --game / --game-path work.
// We always overwrite the game path and local_path to match the current
// Fluorine instance, since the user may switch between instances.
static void ensureLootSettings(const QString& lootDataPath,
                               const QString& mo2GameName,
                               const QString& folderName,
                               const QString& gamePath,
                               const QString& localPath)
{
  QDir().mkpath(lootDataPath);
  QDir().mkpath(localPath);

  // Map MO2 game names to LOOT masterlist repository names.
  static const QMap<QString, QString> masterlistRepos = {
      {"FalloutNV", "falloutnv"},   {"Fallout3", "fallout3"},
      {"Fallout4", "fallout4"},     {"Fallout4VR", "fallout4vr"},
      {"Skyrim", "skyrim"},         {"SkyrimSE", "skyrimse"},
      {"SkyrimVR", "skyrimvr"},     {"Enderal", "enderal"},
      {"EnderalSE", "enderalse"},   {"Oblivion", "oblivion"},
      {"Morrowind", "morrowind"},   {"Starfield", "starfield"},
      {"Nehrim", "oblivion"},
  };
  QString repoName  = masterlistRepos.value(mo2GameName, mo2GameName.toLower());
  QString masterlistUrl =
      QString("https://raw.githubusercontent.com/loot/%1/v0.26/masterlist.yaml")
          .arg(repoName);

  // Always rewrite the settings file so the path matches the current
  // Fluorine instance.  Use the "type" key (not "gameId") because LOOT's
  // type parser accepts MO2's short names like "SkyrimSE".
  QString toml = QString(
      "updateMasterlist = true\n"
      "\n"
      "[[games]]\n"
      "type = \"%1\"\n"
      "name = \"%2\"\n"
      "folder = \"%2\"\n"
      "path = \"%3\"\n"
      "local_path = \"%4\"\n"
      "masterlistSource = \"%5\"\n"
      "\n")
      .arg(mo2GameName, folderName, gamePath, localPath, masterlistUrl);

  QString settingsPath = lootDataPath + "/settings.toml";
  QFile f(settingsPath);
  if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    f.write(toml.toUtf8());
    f.close();
    log::info("Created LOOT settings at {}", settingsPath);
  }
}

// Launch the full LOOT GUI and wait for it to exit.
static bool launchLootGui(QWidget* parent, OrganizerCore& core)
{
  QString appImage = lootAppImagePath();
  if (!QFile::exists(appImage)) {
    return false;
  }

  QString mo2GameName = core.managedGame()->lootGameName();
  QString folderName  = lootFolderName(mo2GameName);
  QString gamePath    = core.managedGame()->gameDirectory().absolutePath();
  QString lootDataPath = fluorineDataDir() + "/loot";

  // Resolve the Fluorine prefix AppData/Local/<game> path — this is where
  // LOOT auto-detects the plugins.txt / loadorder.txt from, so we must
  // use it as local_path and deploy our load order files there.
  QString localPath;
  {
    QString prefixBase = fluorineDataDir() + "/Prefix/pfx/drive_c/users/steamuser/AppData/Local";
    // Use documentsDirectory leaf name as the AppData/Local subfolder —
    // this matches how Bethesda games map their local data folder.
    QString dataDirName = core.managedGame()->documentsDirectory().dirName();
    if (dataDirName.isEmpty() || dataDirName == ".") {
      dataDirName = core.managedGame()->gameShortName();
    }
    QString candidate = prefixBase + "/" + dataDirName;
    if (!dataDirName.isEmpty() && QDir(prefixBase).exists()) {
      localPath = candidate;
    } else {
      localPath = lootDataPath + "/local/" + folderName;
    }
  }

  // Pre-seed LOOT settings so --game resolution works on first launch.
  ensureLootSettings(lootDataPath, mo2GameName, folderName, gamePath, localPath);

  // Copy the profile's load order files to the local path so LOOT sees
  // the current load order.
  QDir().mkpath(localPath);
  QString profilePlugins   = core.profilePath() + "/plugins.txt";
  QString profileLoadOrder = core.profilePath() + "/loadorder.txt";
  QString lootPlugins      = localPath + "/plugins.txt";
  QString lootLoadOrder    = localPath + "/loadorder.txt";

  QFile::remove(lootPlugins);
  QFile::remove(lootLoadOrder);
  if (QFile::exists(profilePlugins)) {
    QFile::copy(profilePlugins, lootPlugins);
  }
  if (QFile::exists(profileLoadOrder)) {
    QFile::copy(profileLoadOrder, lootLoadOrder);
  }

  // Mount the FUSE VFS so LOOT sees the merged mod files in the Data directory.
  log::info("Mounting VFS for LOOT...");
  core.prepareVFS();

  QStringList args;
  args << "--game" << folderName
       << "--game-path" << gamePath
       << "--loot-data-path" << lootDataPath;

  log::info("Launching LOOT GUI: {} {}", appImage, args.join(" "));

  QProcess lootProcess;
  lootProcess.setProgram(appImage);
  lootProcess.setArguments(args);

  // Restore the original environment so LOOT's AppImage doesn't inherit
  // Fluorine's bundled library/plugin paths (which are incompatible with
  // LOOT's own bundled Qt).
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString origLdPath = env.value("FLUORINE_ORIG_LD_LIBRARY_PATH");
  if (!origLdPath.isEmpty()) {
    env.insert("LD_LIBRARY_PATH", origLdPath);
  } else {
    env.remove("LD_LIBRARY_PATH");
  }
  QString origQtPluginPath = env.value("FLUORINE_ORIG_QT_PLUGIN_PATH");
  if (!origQtPluginPath.isEmpty()) {
    env.insert("QT_PLUGIN_PATH", origQtPluginPath);
  } else {
    env.remove("QT_PLUGIN_PATH");
  }
  QString origPath = env.value("FLUORINE_ORIG_PATH");
  if (!origPath.isEmpty()) {
    env.insert("PATH", origPath);
  }
  lootProcess.setProcessEnvironment(env);

  lootProcess.start();
  if (!lootProcess.waitForStarted(5000)) {
    log::error("Failed to start LOOT: {}", lootProcess.errorString());
    QMessageBox::critical(
        parent, QObject::tr("LOOT"),
        QObject::tr("Failed to start LOOT:\n%1").arg(lootProcess.errorString()));
    core.unmountVFS();
    return false;
  }

  // Show a non-modal message while LOOT is running.
  QMessageBox infoBox(QMessageBox::Information, QObject::tr("LOOT"),
                      QObject::tr("LOOT is running.\n\n"
                                  "Sort your load order in LOOT, then close it "
                                  "to apply the changes in Fluorine."),
                      QMessageBox::Cancel, parent);
  infoBox.setWindowModality(Qt::WindowModal);

  // Poll for LOOT to exit or user to cancel.
  QTimer pollTimer;
  QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
    if (lootProcess.state() == QProcess::NotRunning) {
      infoBox.accept();
    }
  });
  pollTimer.start(500);

  int dialogResult = infoBox.exec();
  pollTimer.stop();

  if (dialogResult != QMessageBox::Accepted &&
      lootProcess.state() == QProcess::Running) {
    // User cancelled — kill LOOT
    lootProcess.terminate();
    if (!lootProcess.waitForFinished(3000)) {
      lootProcess.kill();
      lootProcess.waitForFinished(2000);
    }
    core.unmountVFS();
    return false;
  }

  lootProcess.waitForFinished(-1);

  int exitCode = lootProcess.exitCode();
  log::info("LOOT exited with code {}", exitCode);

  // For FileTime-based games (FalloutNV, Fallout3, Oblivion), LOOT sets
  // load order via file timestamps rather than modifying loadorder.txt.
  // Read the plugin timestamps from the still-mounted VFS and write a
  // sorted loadorder.txt before unmounting (timestamps are lost on unmount).
  if (core.managedGame()->loadOrderMechanism() ==
      MOBase::IPluginGame::LoadOrderMechanism::FileTime) {
    QString dataPath = core.managedGame()->dataDirectory().absolutePath();
    QDir dataDir(dataPath);
    QStringList pluginFilters = {"*.esp", "*.esm", "*.esl"};

    struct PluginMtime {
      QString name;
      qint64 mtime;
    };
    std::vector<PluginMtime> plugins;

    for (const auto& entry : dataDir.entryInfoList(pluginFilters, QDir::Files)) {
      plugins.push_back({entry.fileName(),
                         entry.lastModified().toSecsSinceEpoch()});
    }

    std::sort(plugins.begin(), plugins.end(),
              [](const PluginMtime& a, const PluginMtime& b) {
                return a.mtime < b.mtime;
              });

    if (!plugins.empty()) {
      QFile lo(profileLoadOrder);
      if (lo.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&lo);
        for (const auto& p : plugins) {
          out << p.name << "\n";
        }
        lo.close();
        log::info("Wrote sorted load order ({} plugins) from VFS timestamps",
                  plugins.size());
      }
    }
  }

  // Discard any COW'd files in staging (LOOT opens plugins with write access
  // to set timestamps, which triggers copy-on-write — we don't want those
  // copies ending up in overwrite).
  core.discardVFSStagingOnUnmount();

  // Unmount the VFS now that LOOT has finished.
  log::info("Unmounting VFS after LOOT...");
  core.unmountVFS();

  // Copy LOOT's updated load order files back to the profile.
  // For FileTime games, we already wrote loadorder.txt from VFS timestamps
  // above — skip the copy-back so we don't overwrite it with the old order.
  bool isFileTime = core.managedGame()->loadOrderMechanism() ==
                    MOBase::IPluginGame::LoadOrderMechanism::FileTime;

  // LOOT may write "Plugins.txt" (capital P) while we deployed "plugins.txt"
  // (lowercase).  On case-sensitive Linux these are two different files — the
  // lowercase one is our pre-LOOT copy, the capital-P one is LOOT's output.
  // Prefer the capital-P variant when it exists; otherwise fall back to the
  // lowercase file (in case a future LOOT version writes lowercase).
  QString lootPluginsActual;
  {
    QString capitalP = localPath + "/Plugins.txt";
    if (QFile::exists(capitalP)) {
      lootPluginsActual = capitalP;
    } else if (QFile::exists(lootPlugins)) {
      lootPluginsActual = lootPlugins;
    }
  }
  if (!lootPluginsActual.isEmpty()) {
    QFile::remove(profilePlugins);
    QFile::copy(lootPluginsActual, profilePlugins);
    log::info("Copied LOOT {} back to profile", lootPluginsActual);
  }

  bool isPluginsTxt = core.managedGame()->loadOrderMechanism() ==
                      MOBase::IPluginGame::LoadOrderMechanism::PluginsTxt;

  if (!isFileTime && !isPluginsTxt) {
    // For games that use a separate loadorder.txt (not FileTime, not
    // PluginsTxt), copy LOOT's loadorder.txt back.
    QString lootLoadOrderActual;
    {
      QString capitalL = localPath + "/Loadorder.txt";
      if (QFile::exists(capitalL)) {
        lootLoadOrderActual = capitalL;
      } else if (QFile::exists(lootLoadOrder)) {
        lootLoadOrderActual = lootLoadOrder;
      }
    }
    if (!lootLoadOrderActual.isEmpty()) {
      QFile::remove(profileLoadOrder);
      QFile::copy(lootLoadOrderActual, profileLoadOrder);
      log::info("Copied LOOT {} back to profile", lootLoadOrderActual);
    }
  }

  if (isPluginsTxt && !lootPluginsActual.isEmpty()) {
    // For PluginsTxt games (FO4, SkyrimSE, etc.), LOOT writes the sorted
    // order into plugins.txt but does NOT update loadorder.txt.  If we
    // blindly copy the stale loadorder.txt back, readPluginLists() will
    // prefer the old order from loadorder.txt over the freshly-sorted
    // plugins.txt.  Fix this by regenerating loadorder.txt from the
    // sorted plugins.txt so both files are consistent.
    QFile pluginsFile(profilePlugins);
    if (pluginsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QStringList sortedOrder;
      while (!pluginsFile.atEnd()) {
        QString line = QString::fromUtf8(pluginsFile.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#'))
          continue;
        if (line.startsWith('*'))
          line.remove(0, 1);
        if (!line.isEmpty())
          sortedOrder.append(line);
      }
      pluginsFile.close();

      if (!sortedOrder.isEmpty()) {
        QFile lo(profileLoadOrder);
        if (lo.open(QIODevice::WriteOnly | QIODevice::Text)) {
          QTextStream out(&lo);
          for (const auto& p : sortedOrder) {
            out << p << "\n";
          }
          lo.close();
          log::info("Regenerated loadorder.txt from LOOT-sorted plugins.txt "
                    "({} plugins)", sortedOrder.size());
        }
      }
    }
  }

  return true;
}
#endif

bool runLoot(QWidget* parent, OrganizerCore& core, bool didUpdateMasterList)
{
  core.savePluginList();

#ifndef _WIN32
  // Linux: download and launch the full LOOT GUI
  Q_UNUSED(didUpdateMasterList);

  try {
    if (!ensureLootAvailable(parent)) {
      return false;
    }

    return launchLootGui(parent, core);
  } catch (const std::exception& e) {
    reportError(QObject::tr("failed to run loot: %1").arg(e.what()));
    return false;
  }
#else
  try {
    Loot loot(core);
    LootDialog dialog(parent, core, loot);

    if (!loot.start(parent, didUpdateMasterList)) {
      return false;
    }

    dialog.exec();

    return dialog.result();
  } catch (const UsvfsConnectorException& e) {
    log::debug("{}", e.what());
    return false;
  } catch (const std::exception& e) {
    reportError(QObject::tr("failed to run loot: %1").arg(e.what()));
    return false;
  }
#endif
}
