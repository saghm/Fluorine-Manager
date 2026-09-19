#pragma once

#include <QWidget>
#include <QVector>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTabWidget;
class QToolButton;

// Collapse advanced controls without disabling or resetting their values.
class SettingsFoldout : public QWidget
{
  Q_OBJECT
public:
  SettingsFoldout(const QString& title, QWidget* content, QWidget* parent = nullptr);
  void setSearchExpanded(bool expanded);
  QWidget* content() const { return m_content; }

private:
  QToolButton* m_toggle;
  QWidget* m_content;
  bool m_userExpanded = false;
  bool m_searchExpanded = false;
};

// Presents existing settings pages in a searchable sidebar. The underlying tab
// indices stay intact for saved selection, direct links and tutorial tracking.
class SettingsNavigation : public QWidget
{
public:
  explicit SettingsNavigation(QTabWidget* pages, QWidget* parent = nullptr);
  void addSection(QWidget* page, const QString& title, const QString& description,
                  const QString& keywords = {});

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct Section
  {
    QWidget* page;
    QString title;
    QString description;
    QString searchText;
  };

  QTabWidget* m_pages;
  QLineEdit* m_search;
  QListWidget* m_sections;
  QLabel* m_title;
  QLabel* m_description;
  QLabel* m_results;
  QStackedWidget* m_content;
  QVector<Section> m_entries;
  QWidget* m_beforeSearch = nullptr;

  void filterSections();
  void synchronizePage();
};
