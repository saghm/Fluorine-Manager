#include "envsecurity.h"
#include "env.h"
#include "envmodule.h"
#include <log.h>
#include <utility.h>

#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

namespace env
{

using namespace MOBase;

SecurityProduct::SecurityProduct(QUuid guid, QString name, int provider, bool active,
                                 bool upToDate)
    : m_guid(std::move(guid)), m_name(std::move(name)), m_provider(provider),
      m_active(active), m_upToDate(upToDate)
{}

const QUuid& SecurityProduct::guid() const
{
  return m_guid;
}

const QString& SecurityProduct::name() const
{
  return m_name;
}

int SecurityProduct::provider() const
{
  return m_provider;
}

bool SecurityProduct::active() const
{
  return m_active;
}

bool SecurityProduct::upToDate() const
{
  return m_upToDate;
}

QString SecurityProduct::toString() const
{
  QString s;

  if (m_name.isEmpty()) {
    s += "(no name)";
  } else {
    s += m_name;
  }

  s += " (" + providerToString() + ")";

  if (!m_active) {
    s += ", inactive";
  }

  if (!m_upToDate) {
    s += ", definitions outdated";
  }

  return s;
}

QString SecurityProduct::providerToString() 
{
  return "n/a";
}

std::vector<SecurityProduct> getSecurityProducts()
{
  // Linux has no equivalent of Windows Security Center / WSC API.
  return {};
}

FileSecurity getFileSecurity(const QString& path)
{
  FileSecurity fs;

  struct stat st;
  if (stat(path.toStdString().c_str(), &st) != 0) {
    fs.error = QString("stat() failed for '%1': %2").arg(path).arg(strerror(errno));
    return fs;
  }

  struct passwd* pw = getpwuid(st.st_uid);
  if (pw) {
    uid_t currentUid = getuid();
    if (st.st_uid == currentUid) {
      fs.owner = "(this user)";
    } else {
      fs.owner = QString::fromUtf8(pw->pw_name);
    }
  } else {
    fs.owner = QString::number(st.st_uid);
  }

  if (st.st_mode & S_IRUSR)
    fs.rights.list.push_back("read");
  if (st.st_mode & S_IWUSR)
    fs.rights.list.push_back("write");
  if (st.st_mode & S_IXUSR) {
    fs.rights.list.push_back("execute");
    fs.rights.hasExecute = true;
  }

  if (access(path.toStdString().c_str(), R_OK | W_OK | X_OK) == 0) {
    fs.rights.normalRights = true;
  }

  return fs;
}

}  // namespace env
