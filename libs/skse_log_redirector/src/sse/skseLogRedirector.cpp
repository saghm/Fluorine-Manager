#include "skseLogRedirector.h"

QString SkseLogRedirector::name() const
{
  return "SKSE Log Redirector";
}

QString SkseLogRedirector::localizedName() const
{
  return tr("SKSE Log Redirector");
}

QString SkseLogRedirector::description() const
{
  return tr("Redirects \"\\Documents\\My Games\\SKYRIM.INI\" to "
            "\"\\Documents\\My Games\\Skyrim Special Edition\" via VFS.");
}

QString SkseLogRedirector::destFolderName() const
{
  // Verbatim from upstream .py: "Skyrim.INI" can be a directory created by a
  // Skyrim bug; redirecting it makes SKSE find logs in the SSE docs.
  return QStringLiteral("Skyrim.INI");
}
