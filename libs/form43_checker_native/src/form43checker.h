#ifndef FORM43CHECKER_H
#define FORM43CHECKER_H

#include <memory>

#include <QString>
#include <QStringList>

#include <uibase/iplugin.h>
#include <uibase/iplugindiagnose.h>
#include <uibase/pluginrequirements.h>

class Form43Checker : public QObject,
                      public MOBase::IPlugin,
                      public MOBase::IPluginDiagnose
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginDiagnose)
  Q_PLUGIN_METADATA(IID "org.tannin.Form43CheckerNative")

public:
  Form43Checker();

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
  static const unsigned int PROBLEM_FORM43 = 0;

  void updateInvalidPlugins() const;
  int getFormVersion(const QString& file) const;
  QStringList listPlugins() const;

  MOBase::IOrganizer* m_organizer;
  mutable QStringList m_invalidPlugins;
};

#endif  // FORM43CHECKER_H
