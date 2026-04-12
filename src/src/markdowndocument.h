#ifndef MODORGANIZER_MARKDOWNDOCUMENT_H
#define MODORGANIZER_MARKDOWNDOCUMENT_H

#include <QObject>
#include <QString>

#ifdef MO2_WEBENGINE
#include <QWebEnginePage>
#endif

class MarkdownDocument : public QObject
{
  Q_OBJECT;
  Q_PROPERTY(QString text MEMBER m_text NOTIFY textChanged FINAL);

public:
  explicit MarkdownDocument(QObject* parent = nullptr);
  void setText(const QString& text);

signals:
  void textChanged(const QString& text);

private:
  QString m_text;
};

#ifdef MO2_WEBENGINE
class MarkdownPage : public QWebEnginePage
{
  Q_OBJECT;

public:
  explicit MarkdownPage(QObject* parent = nullptr);

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType, bool) override;
};
#endif

#endif  // MODORGANIZER_MARKDOWNDOCUMENT_H
