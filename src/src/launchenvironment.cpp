#include "launchenvironment.h"

#include <QCoreApplication>
#include <QRegularExpression>

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
