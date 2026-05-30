#include "vdfparser.h"

namespace {

class VdfParser
{
public:
  explicit VdfParser(const QString& text) : m_text(text) {}

  VdfValue parse() { return parseObject(); }

private:
  const QString& m_text;
  int m_pos{0};

  QChar peek() const
  {
    return (m_pos < m_text.size()) ? m_text[m_pos] : QChar(0);
  }

  QChar advance()
  {
    return (m_pos < m_text.size()) ? m_text[m_pos++] : QChar(0);
  }

  bool atEnd() const { return m_pos >= m_text.size(); }

  void skipWhitespaceAndComments()
  {
    for (;;) {
      while (!atEnd() && peek().isSpace())
        advance();

      if (peek() == '/') {
        advance();
        if (peek() == '/') {
          advance();
          while (!atEnd() && peek() != '\n')
            advance();
          continue;
        }
      }
      break;
    }
  }

  QString parseQuotedString()
  {
    if (advance() != '"')
      return {};

    QString result;
    for (;;) {
      if (atEnd()) return {};
      QChar c = advance();
      if (c == '"') break;
      if (c == '\\') {
        if (atEnd()) return {};
        QChar esc = advance();
        if (esc == 'n') result += '\n';
        else if (esc == 't') result += '\t';
        else if (esc == '\\') result += '\\';
        else if (esc == '"') result += '"';
        else { result += '\\'; result += esc; }
      } else {
        result += c;
      }
    }
    return result;
  }

  VdfValue parseObject()
  {
    QMap<QString, VdfValue> map;

    for (;;) {
      skipWhitespaceAndComments();
      if (atEnd()) break;

      if (peek() == '}') {
        advance();
        break;
      }

      if (peek() == '"') {
        QString key = parseQuotedString();
        if (key.isNull()) break;

        skipWhitespaceAndComments();

        if (peek() == '"') {
          QString value = parseQuotedString();
          map.insert(key, VdfValue(value));
        } else if (peek() == '{') {
          advance();
          map.insert(key, parseObject());
        } else {
          break;
        }
      } else {
        advance();
      }
    }

    return VdfValue(std::move(map));
  }
};

}  // namespace

VdfValue parseVdf(const QString& content)
{
  VdfParser parser(content);
  return parser.parse();
}

AppManifest AppManifest::fromVdf(const QString& content)
{
  VdfValue root = parseVdf(content);
  const VdfValue* appState = root.get(QStringLiteral("AppState"));
  if (!appState)
    return {};

  AppManifest m;
  m.app_id     = appState->getString(QStringLiteral("appid"));
  m.name       = appState->getString(QStringLiteral("name"));
  m.install_dir = appState->getString(QStringLiteral("installdir"));
  m.state_flags = appState->getString(QStringLiteral("StateFlags")).toUInt();
  return m;
}

QStringList parseLibraryFolders(const QString& content)
{
  QStringList paths;
  VdfValue root = parseVdf(content);
  const VdfValue* folders = root.get(QStringLiteral("libraryfolders"));
  if (!folders || !folders->isObject())
    return paths;

  for (const VdfValue& entry : folders->asObject()) {
    QString path;
    if (entry.isString()) {
      path = entry.asString();
    } else {
      path = entry.getString(QStringLiteral("path"));
    }

    if (!path.isEmpty() && !paths.contains(path))
      paths.append(path);
  }
  return paths;
}
