#include "basicgamesproxy.h"
#include "basicgameplugin.h"
#include "gamedefs.h"

#include <uibase/versioninfo.h>

BasicGamesProxy::BasicGamesProxy() {}

BasicGamesProxy::~BasicGamesProxy()
{
  // Clean up all loaded plugins
  for (auto& list : m_loaded) {
    qDeleteAll(list);
  }
  m_loaded.clear();
}

bool BasicGamesProxy::init(MOBase::IOrganizer* organizer)
{
  m_organizer = organizer;
  return true;
}

QString BasicGamesProxy::name() const
{
  return "Basic Games Native";
}

QString BasicGamesProxy::author() const
{
  return "Fluorine Manager";
}

QString BasicGamesProxy::description() const
{
  return "Native C++ implementation of basic game plugins";
}

MOBase::VersionInfo BasicGamesProxy::version() const
{
  return MOBase::VersionInfo(1, 0, 0);
}

QList<MOBase::PluginSetting> BasicGamesProxy::settings() const
{
  return {};
}

QStringList BasicGamesProxy::pluginList(const QDir&) const
{
  QStringList list;
  const auto& defs = allGameDefinitions();
  for (size_t i = 0; i < defs.size(); ++i) {
    list.append(QString("native_game_%1").arg(i));
  }
  return list;
}

QList<QObject*> BasicGamesProxy::load(const QString& identifier)
{
  // Already loaded?
  auto it = m_loaded.find(identifier);
  if (it != m_loaded.end()) {
    return it.value();
  }

  QList<QObject*> plugins;

  // Parse the index from the identifier
  if (!identifier.startsWith("native_game_"))
    return plugins;

  bool ok    = false;
  int index  = identifier.mid(12).toInt(&ok);
  if (!ok)
    return plugins;

  const auto& defs = allGameDefinitions();
  if (index < 0 || index >= static_cast<int>(defs.size()))
    return plugins;

  auto* plugin = new BasicGamePlugin(defs[index]);
  if (m_organizer) {
    plugin->init(m_organizer);
  }
  plugins.append(plugin);

  m_loaded.insert(identifier, plugins);
  return plugins;
}

void BasicGamesProxy::unload(const QString& identifier)
{
  auto it = m_loaded.find(identifier);
  if (it != m_loaded.end()) {
    qDeleteAll(it.value());
    m_loaded.erase(it);
  }
}
