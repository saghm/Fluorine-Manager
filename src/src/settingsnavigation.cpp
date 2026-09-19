#include "settingsnavigation.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
QString searchableText(QWidget* page)
{
  QStringList text;
  // Index labels, help and available choices, never entered values or credentials.
  for (auto* widget : page->findChildren<QWidget*>()) {
    text << widget->toolTip() << widget->accessibleName();
    if (auto* label = qobject_cast<QLabel*>(widget)) {
      if (!label->property("settingsSearchExclude").toBool()) {
        text << label->text();
      }
    } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
      text << button->text();
    } else if (auto* group = qobject_cast<QGroupBox*>(widget)) {
      text << group->title();
    } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
      for (int i = 0; i < combo->count(); ++i) {
        text << combo->itemText(i);
      }
    }
  }
  return QTextDocumentFragment::fromHtml(text.join(' ')).toPlainText().remove('&');
}

bool matches(const QString& text, const QStringList& words)
{
  return std::all_of(words.begin(), words.end(), [&](const QString& word) {
    return text.contains(word, Qt::CaseInsensitive);
  });
}
}

SettingsFoldout::SettingsFoldout(const QString& title, QWidget* content, QWidget* parent)
    : QWidget(parent), m_toggle(new QToolButton(this)), m_content(content)
{
  setObjectName(content->objectName() + "Foldout");
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_toggle->setObjectName(content->objectName() + "Toggle");
  m_toggle->setText(title);
  m_toggle->setCheckable(true);
  m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_toggle->setArrowType(Qt::RightArrow);
  m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_toggle->setAccessibleName(title);
  layout->addWidget(m_toggle);
  layout->addWidget(content);
  content->hide();
  connect(m_toggle, &QToolButton::toggled, this, [this](bool expanded) {
    m_userExpanded = expanded;
    m_content->setVisible(expanded);
    m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
  });
}

void SettingsFoldout::setSearchExpanded(bool expanded)
{
  if (m_searchExpanded == expanded) {
    return;
  }
  m_searchExpanded = expanded;
  const bool visible = expanded || m_userExpanded;
  const QSignalBlocker blocker(m_toggle);
  m_toggle->setChecked(visible);
  m_toggle->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
  m_content->setVisible(visible);
}

SettingsNavigation::SettingsNavigation(QTabWidget* pages, QWidget* parent)
    : QWidget(parent), m_pages(pages)
{
  setObjectName("settingsNavigation");
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(18);

  auto* sidebar = new QWidget(this);
  sidebar->setMinimumWidth(190);
  sidebar->setMaximumWidth(245);
  auto* navigation = new QVBoxLayout(sidebar);
  navigation->setContentsMargins(0, 0, 0, 0);
  m_search = new QLineEdit(sidebar);
  m_search->setObjectName("settingsSearch");
  m_search->setPlaceholderText(tr("Search settings…"));
  m_search->setAccessibleName(tr("Search settings"));
  m_search->setToolTip(tr("Find a settings section by name or option. Ctrl+F to search."));
  m_search->setClearButtonEnabled(true);
  m_search->installEventFilter(this);
  navigation->addWidget(m_search);

  m_sections = new QListWidget(sidebar);
  m_sections->setObjectName("settingsSections");
  m_sections->setAccessibleName(tr("Settings sections"));
  m_sections->setFrameShape(QFrame::NoFrame);
  m_sections->setSpacing(3);
  m_sections->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  navigation->addWidget(m_sections, 1);
  m_results = new QLabel(sidebar);
  m_results->setWordWrap(true);
  navigation->addWidget(m_results);
  layout->addWidget(sidebar);

  m_content = new QStackedWidget(this);
  auto* detail = new QWidget(m_content);
  auto* detailLayout = new QVBoxLayout(detail);
  detailLayout->setContentsMargins(0, 0, 0, 0);
  m_title = new QLabel(detail);
  m_title->setObjectName("settingsPageTitle");
  QFont heading = font();
  heading.setBold(true);
  if (heading.pointSizeF() > 0) {
    heading.setPointSizeF(heading.pointSizeF() * 1.35);
  }
  m_title->setFont(heading);
  m_description = new QLabel(detail);
  m_description->setWordWrap(true);
  detailLayout->addWidget(m_title);
  detailLayout->addWidget(m_description);
  m_pages->tabBar()->hide();
  m_pages->setDocumentMode(true);
  detailLayout->addWidget(m_pages, 1);
  m_content->addWidget(detail);
  auto* empty = new QLabel(tr("No matching settings.\nTry another word or clear the search."),
                           m_content);
  empty->setObjectName("settingsNoResults");
  empty->setAlignment(Qt::AlignCenter);
  empty->setWordWrap(true);
  m_content->addWidget(empty);
  layout->addWidget(m_content, 1);

  connect(m_search, &QLineEdit::textChanged, this, [this] { filterSections(); });
  connect(m_sections, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0 && row < m_entries.size()) {
      m_pages->setCurrentWidget(m_entries[row].page);
      synchronizePage();
    }
  });
  connect(m_pages, &QTabWidget::currentChanged, this, [this] { synchronizePage(); });
  auto* find = new QShortcut(QKeySequence::Find, this);
  connect(find, &QShortcut::activated, this, [this] {
    m_search->setFocus();
    m_search->selectAll();
  });
  auto* clear = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  clear->setEnabled(false);
  connect(clear, &QShortcut::activated, m_search, &QLineEdit::clear);
  connect(m_search, &QLineEdit::textChanged, clear, [clear](const QString& text) {
    clear->setEnabled(!text.isEmpty());
  });
  setFocusProxy(m_sections);
  QWidget::setTabOrder(m_search, m_sections);
}

void SettingsNavigation::addSection(QWidget* page, const QString& title,
                                     const QString& description, const QString& keywords)
{
  m_entries.push_back({page, title, description,
                       title + ' ' + description + ' ' + keywords + ' ' + searchableText(page)});
  auto* item = new QListWidgetItem(title, m_sections);
  item->setSizeHint(QSize(0, fontMetrics().height() + 20));
  item->setToolTip(description);
  // Labels change, page indices and object names do not.
  m_pages->setTabText(m_pages->indexOf(page), title);
  synchronizePage();
}

void SettingsNavigation::synchronizePage()
{
  for (int row = 0; row < m_entries.size(); ++row) {
    const auto& section = m_entries[row];
    if (section.page != m_pages->currentWidget()) {
      continue;
    }
    const QSignalBlocker blocker(m_sections);
    m_sections->setCurrentRow(row);
    m_title->setText(section.title);
    m_description->setText(section.description);
    return;
  }
}

void SettingsNavigation::filterSections()
{
  const auto words = m_search->text().simplified().split(' ', Qt::SkipEmptyParts);
  if (!words.isEmpty() && !m_beforeSearch) {
    m_beforeSearch = m_pages->currentWidget();
  }
  int first = -1;
  int count = 0;
  for (int row = 0; row < m_entries.size(); ++row) {
    const auto& section = m_entries[row];
    const bool match = matches(section.searchText, words);
    m_sections->item(row)->setHidden(!match);
    if (match) {
      ++count;
      if (first < 0) {
        first = row;
      }
    }
    for (auto* foldout : section.page->findChildren<SettingsFoldout*>()) {
      const auto text = searchableText(foldout);
      // A query can combine the section name with a specific advanced option,
      // e.g. "proton cache". The section matches the whole query; the foldout
      // only needs to contain one of its terms to reveal the relevant control.
      foldout->setSearchExpanded(match && std::any_of(words.begin(), words.end(),
          [&](const QString& word) { return text.contains(word, Qt::CaseInsensitive); }));
    }
  }
  m_results->setText(words.isEmpty() ? QString() : tr("%1 matching sections").arg(count));
  m_content->setCurrentIndex(count == 0 ? 1 : 0);
  if (words.isEmpty() && m_beforeSearch) {
    m_pages->setCurrentWidget(m_beforeSearch);
    m_beforeSearch = nullptr;
  } else if (first >= 0 &&
             (!m_sections->currentItem() || m_sections->currentItem()->isHidden())) {
    m_sections->setCurrentRow(first);
  }
  synchronizePage();
}

bool SettingsNavigation::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == m_search && event->type() == QEvent::KeyPress) {
    const auto key = static_cast<QKeyEvent*>(event)->key();
    if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter) {
      m_sections->setFocus();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}
