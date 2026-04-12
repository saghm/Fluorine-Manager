#include "markdowndocument.h"

#ifdef MO2_WEBENGINE
#include <shared/util.h>
#include <utility.h>
#endif

MarkdownDocument::MarkdownDocument(QObject* parent) : QObject(parent) {}

void MarkdownDocument::setText(const QString& text)
{
  if (m_text == text)
    return;

  m_text = text;
  emit textChanged(m_text);
}

#ifdef MO2_WEBENGINE
MarkdownPage::MarkdownPage(QObject* parent) : QWebEnginePage(parent) {}

bool MarkdownPage::acceptNavigationRequest(const QUrl& url, NavigationType, bool)
{
  static const QStringList allowed = {"qrc", "data"};

  if (!allowed.contains(url.scheme())) {
    MOBase::shell::Open(url);
    return false;
  }

  return true;
}
#endif
