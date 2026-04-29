#pragma once

#include <uibase/filemapping.h>
#include <uibase/iplugin.h>
#include <uibase/ipluginfilemapper.h>

#include <QList>
#include <QObject>
#include <QString>

namespace MOBase
{
struct PluginSetting;
class IOrganizer;
}  // namespace MOBase

// Shared base for the SKSELogRedirector plugin family (sysdmp). Each variant
// (Steam SSE, GOG, VR) only differs in the destination folder name under
// "My Games" — everything else is identical, so the variants subclass this
// base and override destFolderName().
//
// All variants are off by default: only one is applicable per game and the
// user opts into the matching one.
class SkseLogRedirectorBase : public QObject,
                              public MOBase::IPlugin,
                              public MOBase::IPluginFileMapper
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginFileMapper)

public:
  // IPlugin
  bool init(MOBase::IOrganizer* moInfo) override;
  QString author() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;
  bool enabledByDefault() const override;

  // IPluginFileMapper
  MappingType mappings() const override;

protected:
  // Sibling folder name under "My Games" — e.g. "Skyrim", "Skyrim VR",
  // "Skyrim Special Edition", "Skyrim.INI".
  virtual QString destFolderName() const = 0;

  MOBase::IOrganizer* m_organizer = nullptr;
};
