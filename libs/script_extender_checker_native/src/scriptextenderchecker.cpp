#include "scriptextenderchecker.h"

#include <uibase/iplugingame.h>
#include <uibase/pluginrequirements.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

using namespace MOBase;

// Regex patterns matching SKSE/F4SE/etc log formats
static const QRegularExpression RE_NORMAL(
    R"(plugin (?P<pluginPath>.+) \((?P<infoVersion>[\dA-Fa-f]{8}) (?P<name>.*) (?P<version>[\dA-Fa-f]{8})\) (?P<loadStatus>.+?)(?P<errorCode> \d+)?( \(handle \d+\))?\s*$)");

static const QRegularExpression RE_COULDNT_LOAD(
    R"(couldn't load plugin (?P<pluginPath>.+) \(Error (?:code )?(?P<lastError>[-+]?\d+)(?::\s*(?P<seDetails>.*))?\)\s*)");

static const QRegularExpression RE_NOT_PLUGIN(
    R"(plugin (?P<pluginPath>.+) does not appear to be an (?:SK|F4|NV|FO|OB)SE plugin\s*)");

ScriptExtenderChecker::ScriptExtenderChecker() : m_organizer(nullptr) {}

const QMap<QString, ScriptExtenderChecker::GameType>&
ScriptExtenderChecker::supportedGames()
{
  static const QMap<QString, GameType> games = {
      {"Skyrim",
       {LogLocation::Docs, "SKSE/skse.log", "SKSE/skse_editor.log"}},
      {"Skyrim Special Edition",
       {LogLocation::Docs, "SKSE/skse64.log", ""}},
      {"Skyrim VR",
       {LogLocation::Docs, "SKSE/sksevr.log", ""}},
      {"Fallout 4",
       {LogLocation::Docs, "F4SE/f4se.log", ""}},
      {"Oblivion",
       {LogLocation::Install, "obse.log", "obse_editor.log"}},
      {"New Vegas",
       {LogLocation::Install, "nvse.log", "nvse_editor.log"}},
      {"TTW",
       {LogLocation::Install, "nvse.log", "nvse_editor.log"}},
      {"Fallout 3",
       {LogLocation::Install, "fose.log", "fose_editor.log"}},
  };
  return games;
}

bool ScriptExtenderChecker::init(IOrganizer* moInfo)
{
  m_organizer = moInfo;
  m_organizer->onFinishedRun(
      [this](const QString&, unsigned int) { invalidate(); });
  return true;
}

QString ScriptExtenderChecker::name() const
{
  return "Script Extender Plugin Load Checker (Native)";
}

QString ScriptExtenderChecker::localizedName() const
{
  return tr("Script Extender Plugin Load Checker (Native)");
}

QString ScriptExtenderChecker::author() const
{
  return "AnyOldName3";
}

QString ScriptExtenderChecker::description() const
{
  return tr("Checks script extender log to see if any plugins failed to load.");
}

VersionInfo ScriptExtenderChecker::version() const
{
  return VersionInfo(1, 2, 0, VersionInfo::RELEASE_FINAL);
}

std::vector<std::shared_ptr<const IPluginRequirement>>
ScriptExtenderChecker::requirements() const
{
  const auto& games = supportedGames();
  return {PluginRequirementFactory::gameDependency(QStringList(games.keys()))};
}

QList<PluginSetting> ScriptExtenderChecker::settings() const
{
  return {};
}

std::vector<unsigned int> ScriptExtenderChecker::activeProblems() const
{
  if (!listBadPluginMessages().isEmpty()) {
    return {PROBLEM_PLUGIN_LOAD};
  }
  return {};
}

QString ScriptExtenderChecker::shortDescription(unsigned int key) const
{
  return tr("Script extender log reports incompatible plugins.");
}

QString ScriptExtenderChecker::fullDescription(unsigned int key) const
{
  QStringList plugins     = listBadPluginMessages();
  QString pluginListString = "\n  \u2022 " + plugins.join("\n  \u2022 ");
  return tr("You have one or more script extender plugins which failed to "
            "load!\n\n"
            "If you want this notification to go away, here are some steps you "
            "can take:\n"
            "  \u2022 Look for updates to the mod or the specific plugin "
            "included in the mod.\n"
            "  \u2022 Disable the mod containing the plugin.\n"
            "  \u2022 Hide or delete the plugin from the mod.\n\n"
            "To refresh the script extender logs, you will need to run the game "
            "and/or editor again!\n\n"
            "The failed plugins are:%1")
      .arg(pluginListString);
}

bool ScriptExtenderChecker::hasGuidedFix(unsigned int key) const
{
  return false;
}

void ScriptExtenderChecker::startGuidedFix(unsigned int key) const {}

QString ScriptExtenderChecker::resolveOrigin(const QString& pluginPath) const
{
  try {
    QString dataDir =
        m_organizer->managedGame()->dataDirectory().absolutePath();
    QString relativePath = QDir(dataDir).relativeFilePath(pluginPath);
    QStringList origins  = m_organizer->getFileOrigins(relativePath);
    if (!origins.isEmpty()) {
      return origins.first();
    }
  } catch (...) {
  }
  return QString();
}

ScriptExtenderChecker::PluginMessage
ScriptExtenderChecker::parseNormalLine(
    const QRegularExpressionMatch& match) const
{
  PluginMessage msg;
  msg.pluginPath    = match.captured("pluginPath");
  QString name      = match.captured("name");
  QString version   = match.captured("version");
  QString status    = match.captured("loadStatus");
  msg.origin        = resolveOrigin(msg.pluginPath);
  msg.success =
      msg.origin.isEmpty() || status == "loaded correctly" || status == "no version data";

  if (!msg.success) {
    QString trStatus = status;
    // Translate known statuses
    if (status == "disabled, address library needs to be updated")
      trStatus = tr("disabled, address library needs to be updated");
    else if (status == "disabled, fatal error occurred while loading plugin")
      trStatus = tr("disabled, fatal error occurred while loading plugin");
    else if (status == "disabled, bad version data")
      trStatus = tr("disabled, bad version data");
    else if (status == "disabled, no name specified")
      trStatus = tr("disabled, no name specified");
    else if (status == "disabled, unsupported version independence method")
      trStatus = tr("disabled, unsupported version independence method");
    else if (status == "disabled, incompatible with current runtime version")
      trStatus = tr("disabled, incompatible with current runtime version");
    else if (status == "disabled, requires newer script extender")
      trStatus = tr("disabled, requires newer script extender");
    else if (status == "reported as incompatible during query")
      trStatus = tr("reported as incompatible during query");
    else if (status == "reported as incompatible during load")
      trStatus = tr("reported as incompatible during load");
    else if (status ==
             "disabled, fatal error occurred while checking plugin compatibility")
      trStatus = tr("disabled, fatal error occurred while checking plugin "
                     "compatibility");
    else if (status == "disabled, fatal error occurred while querying plugin")
      trStatus = tr("disabled, fatal error occurred while querying plugin");

    msg.message = tr("%1 version %2 (%3, %4) %5.")
                      .arg(name)
                      .arg(version)
                      .arg(QFileInfo(msg.pluginPath).fileName())
                      .arg(msg.origin)
                      .arg(trStatus);
  }
  return msg;
}

ScriptExtenderChecker::PluginMessage
ScriptExtenderChecker::parseCouldntLoadLine(
    const QRegularExpressionMatch& match) const
{
  PluginMessage msg;
  msg.pluginPath = match.captured("pluginPath");
  int lastError  = match.captured("lastError").toInt();
  QString details = match.captured("seDetails").trimmed();
  msg.origin      = resolveOrigin(msg.pluginPath);
  msg.success     = msg.origin.isEmpty();

  if (!msg.success) {
    QString fileName = QFileInfo(msg.pluginPath).fileName();
    if (lastError == 126) {
      msg.message =
          tr("Couldn't load %1 (%2). A dependency DLL could not be found "
             "(code %3). %4")
              .arg(fileName)
              .arg(msg.origin)
              .arg(lastError)
              .arg(details);
    } else if (lastError == 193) {
      msg.message = tr("Couldn't load %1 (%2). A DLL is invalid (code %3).")
                        .arg(fileName)
                        .arg(msg.origin)
                        .arg(lastError);
    } else {
      msg.message =
          tr("Couldn't load %1 (%2). The last error code was %3.")
              .arg(fileName)
              .arg(msg.origin)
              .arg(lastError);
    }
  }
  return msg;
}

ScriptExtenderChecker::PluginMessage
ScriptExtenderChecker::parseNotAPluginLine(
    const QRegularExpressionMatch& match) const
{
  PluginMessage msg;
  msg.pluginPath = match.captured("pluginPath");
  msg.origin     = resolveOrigin(msg.pluginPath);
  msg.success    = msg.origin.isEmpty();

  if (!msg.success) {
    msg.message =
        tr("%1 (%2) does not appear to be a script extender plugin.")
            .arg(QFileInfo(msg.pluginPath).fileName())
            .arg(msg.origin);
  }
  return msg;
}

QList<ScriptExtenderChecker::PluginMessage>
ScriptExtenderChecker::parseLog(const QString& logPath) const
{
  QList<PluginMessage> messages;

  QFile file(logPath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return messages;
  }

  // Script extender logs use cp1252 encoding
  QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  stream.setCodec("Windows-1252");
#else
  stream.setEncoding(QStringConverter::Latin1);
#endif

  while (!stream.atEnd()) {
    QString line = stream.readLine() + "\n";

    QRegularExpressionMatch match = RE_NORMAL.match(line);
    if (match.hasMatch()) {
      messages.append(parseNormalLine(match));
      continue;
    }

    match = RE_COULDNT_LOAD.match(line);
    if (match.hasMatch()) {
      messages.append(parseCouldntLoadLine(match));
      continue;
    }

    match = RE_NOT_PLUGIN.match(line);
    if (match.hasMatch()) {
      messages.append(parseNotAPluginLine(match));
      continue;
    }
  }

  return messages;
}

QStringList ScriptExtenderChecker::listBadPluginMessages() const
{
  const auto& games = supportedGames();
  QString gameName  = m_organizer->managedGame()->gameName();

  if (!games.contains(gameName)) {
    return {};
  }

  const GameType& gameType = games[gameName];

  QString baseDir;
  if (gameType.base == LogLocation::Docs) {
    baseDir = m_organizer->managedGame()->documentsDirectory().absolutePath();
  } else {
    baseDir = m_organizer->managedGame()->gameDirectory().absolutePath();
  }

  QList<PluginMessage> gameMessages;
  QList<PluginMessage> editorMessages;

  if (!gameType.gameSuffix.isEmpty()) {
    gameMessages = parseLog(QDir(baseDir).filePath(gameType.gameSuffix));
  }
  if (!gameType.editorSuffix.isEmpty()) {
    editorMessages = parseLog(QDir(baseDir).filePath(gameType.editorSuffix));
  }

  QStringList result;

  // Report game log failures that aren't successful in editor log
  for (const auto& gameMsg : gameMessages) {
    if (!gameMsg.success) {
      bool editorOk = false;
      for (const auto& editorMsg : editorMessages) {
        if (gameMsg.pluginPath == editorMsg.pluginPath && editorMsg.success) {
          editorOk = true;
          break;
        }
      }
      if (!editorOk && !result.contains(gameMsg.message)) {
        result.append(gameMsg.message);
      }
    }
  }

  // Report editor log failures that aren't successful in game log
  for (const auto& editorMsg : editorMessages) {
    if (!editorMsg.success) {
      bool gameOk = false;
      for (const auto& gameMsg : gameMessages) {
        if (editorMsg.pluginPath == gameMsg.pluginPath && gameMsg.success) {
          gameOk = true;
          break;
        }
      }
      if (!gameOk && !result.contains(editorMsg.message)) {
        result.append(editorMsg.message);
      }
    }
  }

  return result;
}
