#include "skseLogRedirectorBase.h"

#include <uibase/imoinfo.h>
#include <uibase/iplugingame.h>
#include <uibase/pluginsetting.h>
#include <uibase/versioninfo.h>

#include <QDir>

using namespace MOBase;

bool SkseLogRedirectorBase::init(IOrganizer* moInfo)
{
  m_organizer = moInfo;
  return true;
}

QString SkseLogRedirectorBase::author() const
{
  return "sysdmp";
}

VersionInfo SkseLogRedirectorBase::version() const
{
  return {0, 0, 1};
}

QList<PluginSetting> SkseLogRedirectorBase::settings() const
{
  return {};
}

bool SkseLogRedirectorBase::enabledByDefault() const
{
  return false;
}

MappingType SkseLogRedirectorBase::mappings() const
{
  MappingType result;
  if (m_organizer == nullptr) {
    return result;
  }
  const IPluginGame* game = m_organizer->managedGame();
  if (game == nullptr) {
    return result;
  }

  const QString docsPath = game->documentsDirectory().absolutePath();
  // Sibling folder under "My Games" — e.g. <docs>/../Skyrim. QDir::cleanPath
  // collapses the "..", matching pathlib.Path.resolve() in the upstream .py.
  const QString siblingPath =
      QDir::cleanPath(docsPath + QStringLiteral("/../") + destFolderName());

  Mapping m;
  m.source       = docsPath;
  m.destination  = siblingPath;
  m.isDirectory  = true;
  m.createTarget = true;
  result.push_back(std::move(m));
  return result;
}
