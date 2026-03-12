#include "form43checker.h"

#include <uibase/ipluginlist.h>
#include <uibase/pluginrequirements.h>

#include <QCoreApplication>
#include <QFileInfo>

using namespace MOBase;

Form43Checker::Form43Checker() : m_organizer(nullptr) {}

bool Form43Checker::init(IOrganizer* moInfo)
{
  m_organizer = moInfo;
  return true;
}

QString Form43Checker::name() const
{
  return "Form 43 Plugin Checker (Native)";
}

QString Form43Checker::localizedName() const
{
  return tr("Form 43 Plugin Checker (Native)");
}

QString Form43Checker::author() const
{
  return "AnyOldName3";
}

QString Form43Checker::description() const
{
  return tr("Checks plugins (.ESM/.ESP files) to see if any are lower than "
            "Form 44 (Skyrim SE).");
}

VersionInfo Form43Checker::version() const
{
  return VersionInfo(1, 2, 0, VersionInfo::RELEASE_FINAL);
}

std::vector<std::shared_ptr<const IPluginRequirement>>
Form43Checker::requirements() const
{
  return {PluginRequirementFactory::gameDependency("Skyrim Special Edition")};
}

QList<PluginSetting> Form43Checker::settings() const
{
  return {};
}

std::vector<unsigned int> Form43Checker::activeProblems() const
{
  updateInvalidPlugins();
  if (!m_invalidPlugins.isEmpty()) {
    return {PROBLEM_FORM43};
  }
  return {};
}

QString Form43Checker::shortDescription(unsigned int key) const
{
  return tr("Form 43 (or lower) plugin detected");
}

QString Form43Checker::fullDescription(unsigned int key) const
{
  QStringList plugins = listPlugins();
  QString pluginListString =
      "<br><br>\u2022  " + plugins.join("<br>\u2022  ");

  QString output = tr("You have one or more plugins that are not form 44. "
                       "They are:%1")
                       .arg(pluginListString);
  output += "<br><br>";
  output += tr(
      "Form 43 (or lower) plugins are modules that were made for Skyrim LE "
      "(Oldrim) and have not been properly ported to Skyrim Special Edition, "
      "which uses form 44 plugins. This usually results in parts of the mod "
      "not working correctly."
      "<br><br>"
      "To be converted, these plugins simply need to be opened and saved with "
      "the SSE Creation Kit but their presence can be an indication that a mod "
      "was not properly ported to SSE and so can potentially have additional "
      "issues."
      "<br><br>"
      "Online guides can have more information on how to correctly convert mods "
      "for Skyrim SE.<br>");
  return output;
}

bool Form43Checker::hasGuidedFix(unsigned int key) const
{
  return false;
}

void Form43Checker::startGuidedFix(unsigned int key) const {}

void Form43Checker::updateInvalidPlugins() const
{
  m_invalidPlugins.clear();
  QStringList files = m_organizer->findFiles(
      "", QStringList() << "*.esp" << "*.esm");
  for (const QString& file : files) {
    int ver = getFormVersion(file);
    if (ver != -1 && ver < 44) {
      m_invalidPlugins.append(file);
    }
  }
}

int Form43Checker::getFormVersion(const QString& file) const
{
  QString pluginName = QFileInfo(file).fileName();
  return m_organizer->pluginList()->formVersion(pluginName);
}

QStringList Form43Checker::listPlugins() const
{
  QStringList result;
  for (const QString& file : m_invalidPlugins) {
    QString pluginName = QFileInfo(file).fileName();
    int ver            = getFormVersion(file);
    result.append(QString("%1 (form %2)").arg(pluginName).arg(ver));
  }
  return result;
}
