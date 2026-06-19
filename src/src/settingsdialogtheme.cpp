#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "colortable.h"
#include "instancemanager.h"
#include "modlist.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include "fluorinepaths.h"

#include <QFontDatabase>
#include <QFontInfo>

#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

ThemeSettingsTab::ThemeSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  // style
  addStyles();
  selectStyle();
  selectQssFontSize();
  updateDefaultFontSizeHint();
  populateFontFamilies();
  selectFontFamily();

  QObject::connect(ui->styleBox, &QComboBox::currentIndexChanged, [&] {
    updateDefaultFontSizeHint();
  });

  // colors
  ui->colorTable->load(s);

  QObject::connect(ui->resetColorsBtn, &QPushButton::clicked, [&] {
    ui->colorTable->resetColors();
  });

  QObject::connect(ui->exploreStyles, &QPushButton::clicked, [&] {
    onExploreStyles();
  });
}

void ThemeSettingsTab::update()
{
  // style
  const QString oldStyle = settings().interface().styleName().value_or("");
  const QString newStyle =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();
  const int oldQssFontSize = settings().interface().qssFontSize();
  const int newQssFontSize = ui->qssFontSizeSpinBox->value();
  const QString oldFontFamily = settings().interface().fontFamily();
  const QString newFontFamily =
      ui->fontFamilyCombo->itemData(ui->fontFamilyCombo->currentIndex()).toString();

  if (oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
  }

  if (oldQssFontSize != newQssFontSize) {
    settings().interface().setQssFontSize(newQssFontSize);
  }

  if (oldFontFamily != newFontFamily) {
    settings().interface().setFontFamily(newFontFamily);
  }

  if (oldStyle != newStyle || oldQssFontSize != newQssFontSize ||
      oldFontFamily != newFontFamily) {
    emit settings().styleChanged(newStyle);
  }

  // colors
  ui->colorTable->commitColors();
}

void ThemeSettingsTab::addStyles()
{
  ui->styleBox->addItem("None", "");
  for (auto&& key : QStyleFactory::keys()) {
    ui->styleBox->addItem(key, key);
  }

  ui->styleBox->insertSeparator(ui->styleBox->count());

  // Collect .qss files from all stylesheet search directories, deduplicating
  // by filename so bundled themes aren't listed twice.
  const QString ssSubdir = QString::fromStdWString(AppConfig::stylesheetsPath());
  QStringList searchDirs;
  searchDirs << QCoreApplication::applicationDirPath() + "/" + ssSubdir;
  if (auto ci = InstanceManager::singleton().currentInstance()) {
    // currentInstance() returns a bare Instance (readFromIni() not called),
    // so baseDirectory() is empty. Use directory() which is always set.
    const QString instanceDir = ci->directory() + "/" + ssSubdir;
    if (!searchDirs.contains(instanceDir))
      searchDirs << instanceDir;
  }
  const QString userDir = fluorineDataDir() + "/stylesheets";
  if (!searchDirs.contains(userDir))
    searchDirs << userDir;

  QSet<QString> seen;
  for (const auto& dir : searchDirs) {
    QDirIterator iter(dir, QStringList("*.qss"), QDir::Files);
    while (iter.hasNext()) {
      iter.next();
      const QString fileName = iter.fileName();
      if (seen.contains(fileName))
        continue;
      seen.insert(fileName);
      ui->styleBox->addItem(iter.fileInfo().completeBaseName(), fileName);
    }
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
}

void ThemeSettingsTab::selectQssFontSize()
{
  ui->qssFontSizeSpinBox->setValue(settings().interface().qssFontSize());
}

void ThemeSettingsTab::updateDefaultFontSizeHint()
{
  int px = QFontInfo(QApplication::font()).pixelSize();
  if (px > 0) {
    ui->qssFontSizeSpinBox->setSpecialValueText(
        QStringLiteral("Default (%1 px)").arg(px));
  }
}

void ThemeSettingsTab::populateFontFamilies()
{
  ui->fontFamilyCombo->addItem("Default (DejaVu Sans)", QString());

  QStringList families = QFontDatabase::families();
  families.sort();
  for (const QString& family : families) {
    ui->fontFamilyCombo->addItem(family, family);
  }
}

void ThemeSettingsTab::selectFontFamily()
{
  const QString family = settings().interface().fontFamily();
  const int idx = ui->fontFamilyCombo->findData(family);
  if (idx != -1) {
    ui->fontFamilyCombo->setCurrentIndex(idx);
  }
}

void ThemeSettingsTab::onExploreStyles()
{
  // Open the instance's stylesheets directory (where custom themes from
  // modlists live), or the user data dir as fallback.
  QString ssPath;
  if (auto ci = InstanceManager::singleton().currentInstance()) {
    ssPath =
        ci->directory() + "/" + QString::fromStdWString(AppConfig::stylesheetsPath());
  } else {
    ssPath = fluorineDataDir() + "/stylesheets";
  }
  QDir().mkpath(ssPath);
  shell::Explore(ssPath);
}
