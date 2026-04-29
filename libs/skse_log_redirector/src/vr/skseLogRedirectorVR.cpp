#include "skseLogRedirectorVR.h"

QString SkseLogRedirectorVR::name() const
{
  return "SKSE Log Redirector (test build with VR workaround)";
}

QString SkseLogRedirectorVR::localizedName() const
{
  return tr("SKSE Log Redirector (test build with VR workaround)");
}

QString SkseLogRedirectorVR::description() const
{
  return tr("Redirects \"\\Documents\\My Games\\Skyrim VR\" to "
            "\"\\Documents\\My Games\\Skyrim Special Edition\" via VFS.");
}

QString SkseLogRedirectorVR::destFolderName() const
{
  return QStringLiteral("Skyrim VR");
}
