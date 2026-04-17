#include "steamappinfo.h"

#include <QFileInfo>
#include <steam_appinfo_ffi.h>
#include <uibase/log.h>

namespace {

QHash<quint32, SteamAppInfo> g_cache;
bool g_loaded = false;

extern "C" void appinfoCallback(void* user, uint32_t appid,
                                const char* type_, const char* name)
{
  auto* map = static_cast<QHash<quint32, SteamAppInfo>*>(user);
  SteamAppInfo info;
  info.appid = appid;
  info.type = QString::fromUtf8(type_ ? type_ : "");
  info.name = QString::fromUtf8(name ? name : "");
  map->insert(appid, info);
}

}  // namespace

const QHash<quint32, SteamAppInfo>& loadSteamAppInfo(const QString& steamPath)
{
  if (g_loaded)
    return g_cache;
  g_loaded = true;

  const QString appinfoPath = steamPath + "/appcache/appinfo.vdf";
  if (!QFileInfo::exists(appinfoPath)) {
    MOBase::log::debug("steamappinfo: {} not found", appinfoPath.toStdString());
    return g_cache;
  }

  const QByteArray pathUtf8 = appinfoPath.toUtf8();
  const int rc = steam_appinfo_parse(pathUtf8.constData(), &g_cache,
                                     appinfoCallback);
  if (rc != 0) {
    MOBase::log::warn("steamappinfo: parse failed (rc={})", rc);
    g_cache.clear();
    return g_cache;
  }

  MOBase::log::info("steamappinfo: loaded {} entries", g_cache.size());
  return g_cache;
}

int steamAppTypeRank(const QString& type)
{
  const QString t = type.toLower();
  if (t == "game")
    return 0;
  if (t == "application")
    return 1;
  if (t.isEmpty())
    return 2;  // unknown — better to try than skip
  if (t == "demo")
    return 3;
  if (t == "tool" || t == "config" || t == "driver")
    return 4;
  return 2;
}
