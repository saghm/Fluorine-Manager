#include "launchenvironment.h"

#include <QCoreApplication>
#include <QRegularExpression>

std::wstring commandLineFromUtf8Arguments(int argc, char* const argv[])
{
  QStringList arguments;
  for (int i = 0; i < argc; ++i) {
    QString argument = QString::fromUtf8(argv[i]);
    argument.replace('\\', "\\\\");
    argument.replace('"', "\\\"");
    argument.replace('\'', "\\'");
    arguments.append('"' + argument + '"');
  }
  return arguments.join(' ').toStdWString();
}

namespace
{
QString utf8Locale(QString locale)
{
  // A locale's language and modifier are independent of its codeset.
  // Keep e.g. ja_JP or sr_RS@latin when upgrading an older encoding.
  locale = locale.trimmed();
  const auto modifierPos = locale.indexOf('@');
  const QString modifier = modifierPos < 0 ? QString{} : locale.mid(modifierPos);
  QString base = modifierPos < 0 ? locale : locale.left(modifierPos);
  const auto codesetPos = base.indexOf('.');
  if (codesetPos >= 0) {
    const QString codeset = base.mid(codesetPos + 1).toUpper();
    if (codeset == "UTF-8" || codeset == "UTF8") return locale;
    base.truncate(codesetPos);
  }
  if (base.isEmpty() || base == "C" || base == "POSIX") return "C.UTF-8";
  return base + ".UTF-8" + modifier;
}
}

void prepareProtonLocale(QProcessEnvironment& environment)
{
  // Proton restores LC_ALL from HOST_LC_ALL. Honour that documented override
  // before LC_ALL, but never let an ASCII/legacy codeset reach Wine's Unix API.
  QString all = environment.value("HOST_LC_ALL");
  if (all.isEmpty()) all = environment.value("LC_ALL");
  if (!all.isEmpty()) {
    all = utf8Locale(all);
    environment.insert("LC_ALL", all);
    environment.insert("HOST_LC_ALL", all);
  } else {
    environment.remove("LC_ALL");
    environment.remove("HOST_LC_ALL");
  }

  environment.insert("LANG", utf8Locale(environment.value("LANG")));
  if (!environment.value("LC_CTYPE").isEmpty()) {
    environment.insert("LC_CTYPE", utf8Locale(environment.value("LC_CTYPE")));
  } else {
    // Leave this unset so Proton's Steam bridge can select the game's own
    // language. LANG supplies UTF-8 before that, and for non-Steam launches.
    environment.remove("LC_CTYPE");
  }
  // LC_MESSAGES, LANGUAGE and the other category overrides stay independent.
}

std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error)
{
  if (error) error->clear();
  static const QRegularExpression namePattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
  QMap<QString, QString> variables;
  const auto lines = text.split('\n');
  for (qsizetype i = 0; i < lines.size(); ++i) {
    QString line = lines[i];
    if (line.endsWith('\r')) line.chop(1);
    if (line.trimmed().isEmpty()) continue;
    const auto equals = line.indexOf('=');
    const QString name = line.left(equals).trimmed();
    if (equals < 0 || !namePattern.match(name).hasMatch() || line.contains(QChar::Null)) {
      if (error) {
        *error = QCoreApplication::translate("ExecutableEnvironment",
            "Line %1 must contain NAME=value, with a valid environment variable name.")
                     .arg(i + 1);
      }
      return std::nullopt;
    }
    variables.insert(name, line.mid(equals + 1));
  }
  return variables;
}
