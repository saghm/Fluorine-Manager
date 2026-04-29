#ifndef CSVBUILDER_H
#define CSVBUILDER_H

#include <QString>
#include <QTextStream>
#include <QVariant>
#include <vector>

class CSVException : public std::exception
{

public:
  CSVException(const QString& text) :  m_Message(text.toLocal8Bit()) {}

  const char* what() const noexcept override { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

class CSVBuilder
{

public:
  enum EFieldType
  {
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_FLOAT
  };

  enum EQuoteMode
  {
    QUOTE_NEVER,
    QUOTE_ONDEMAND,
    QUOTE_ALWAYS
  };

  enum ELineBreak
  {
    BREAK_LF,
    BREAK_CRLF,
    BREAK_CR
  };

public:
  CSVBuilder(QIODevice* target);
  ~CSVBuilder();

  void setFieldSeparator(char sep);
  void setLineBreak(ELineBreak lineBreak);
  void setEscapeMode(EFieldType type, EQuoteMode mode);
  void setFields(const std::vector<std::pair<QString, EFieldType>>& fields);
  void setDefault(const QString& field, const QVariant& value);

  void writeHeader();

  void setRowField(const QString& field, const QVariant& value);
  void writeRow();

  void addRow(const std::map<QString, QVariant>& data);

private:
  const char* lineBreak();
  const char separator() const;

  void fieldValid();
  void checkFields(const std::vector<QString>& fields) const;
  void checkValue(const QString& field, const QVariant& value);
  void writeData(const std::map<QString, QVariant>& data, bool check);

  void quoteInsert(QTextStream& stream, int value);
  void quoteInsert(QTextStream& stream, float value);
  void quoteInsert(QTextStream& stream, const QString& value);

private:
  QTextStream m_Out;
  char m_Separator{','};
  ELineBreak m_LineBreak{BREAK_CRLF};
  std::map<EFieldType, EQuoteMode> m_QuoteMode;
  std::vector<QString> m_Fields;
  std::map<QString, EFieldType> m_FieldTypes;
  std::map<QString, QVariant> m_Defaults;
  std::map<QString, QVariant> m_RowBuffer;
};

#endif  // CSVBUILDER_H
