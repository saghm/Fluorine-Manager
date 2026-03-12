#ifndef BASICGAMESPROXY_H
#define BASICGAMESPROXY_H

#include <uibase/ipluginproxy.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class BasicGamesProxy : public QObject, public MOBase::IPluginProxy
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginProxy)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  Q_PLUGIN_METADATA(IID "com.tannin.ModOrganizer.PluginProxy/1.0")
#endif

public:
  BasicGamesProxy();
  ~BasicGamesProxy() override;

  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  // IPluginProxy
  QStringList pluginList(const QDir& pluginPath) const override;
  QList<QObject*> load(const QString& identifier) override;
  void unload(const QString& identifier) override;

private:
  MOBase::IOrganizer* m_organizer = nullptr;
  QHash<QString, QList<QObject*>> m_loaded;
};

#endif  // BASICGAMESPROXY_H
