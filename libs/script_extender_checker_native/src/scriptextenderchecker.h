#ifndef SCRIPTEXTENDERCHECKER_H
#define SCRIPTEXTENDERCHECKER_H

#include <memory>

#include <QString>
#include <QStringList>

#include <uibase/iplugin.h>
#include <uibase/iplugindiagnose.h>
#include <uibase/pluginrequirements.h>

class ScriptExtenderChecker : public QObject,
                              public MOBase::IPlugin,
                              public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginDiagnose)
  Q_PLUGIN_METADATA(IID "org.tannin.ScriptExtenderCheckerNative")

public:
  ScriptExtenderChecker();

public:  // IPlugin
  bool init(MOBase::IOrganizer* moInfo) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  std::vector<std::shared_ptr<const MOBase::IPluginRequirement>>
  requirements() const override;
  QList<MOBase::PluginSetting> settings() const override;

public:  // IPluginDiagnose
  std::vector<unsigned int> activeProblems() const override;
  QString shortDescription(unsigned int key) const override;
  QString fullDescription(unsigned int key) const override;
  bool hasGuidedFix(unsigned int key) const override;
  void startGuidedFix(unsigned int key) const override;

private:
  static const unsigned int PROBLEM_PLUGIN_LOAD = 0;

  enum class LogLocation { Docs, Install };

  struct GameType {
    LogLocation base;
    QString gameSuffix;
    QString editorSuffix;  // empty if no editor log
  };

  struct PluginMessage {
    QString pluginPath;
    QString origin;
    QString message;
    bool success;
  };

  QStringList listBadPluginMessages() const;
  QList<PluginMessage> parseLog(const QString& logPath) const;
  PluginMessage parseNormalLine(const QRegularExpressionMatch& match) const;
  PluginMessage parseCouldntLoadLine(const QRegularExpressionMatch& match) const;
  PluginMessage parseNotAPluginLine(const QRegularExpressionMatch& match) const;
  QString resolveOrigin(const QString& pluginPath) const;

  static const QMap<QString, GameType>& supportedGames();

  MOBase::IOrganizer* m_organizer;
};

#endif  // SCRIPTEXTENDERCHECKER_H
