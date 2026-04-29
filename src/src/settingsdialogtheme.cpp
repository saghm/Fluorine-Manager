#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "colortable.h"
#include "instancemanager.h"
#include "modlist.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include "fluorinepaths.h"

#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

ThemeSettingsTab::ThemeSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  // style
  addStyles();
  selectStyle();

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

  if (oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
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
