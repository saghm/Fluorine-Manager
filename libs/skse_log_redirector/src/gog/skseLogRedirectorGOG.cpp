#include "skseLogRedirectorGOG.h"

QString SkseLogRedirectorGOG::name() const
{
  return "SKSE Log Redirector GOG";
}

QString SkseLogRedirectorGOG::localizedName() const
{
  return tr("SKSE Log Redirector GOG");
}

QString SkseLogRedirectorGOG::description() const
{
  return tr("Redirects \"\\Documents\\My Games\\Skyrim Special Edition\" to "
            "\"\\Documents\\My Games\\Skyrim Special Edition GOG\" via VFS.");
}

QString SkseLogRedirectorGOG::destFolderName() const
{
  return QStringLiteral("Skyrim Special Edition");
}
