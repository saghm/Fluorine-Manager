#include "settingsdialoglayout.h"
#include "settingsnavigation.h"
#include "ui_settingsdialog.h"

#include <QBoxLayout>
#include <QLabel>
#include <QTabWidget>

void setupSettingsLayout(Ui::SettingsDialog& ui, QWidget* dialog)
{
  auto fold = [](QWidget* content, const QString& title) {
    auto* parent = content->parentWidget();
    auto* layout = qobject_cast<QBoxLayout*>(parent->layout());
    const int position = layout->indexOf(content);
    layout->removeWidget(content);
    layout->insertWidget(position, new SettingsFoldout(title, content, parent));
  };
  fold(ui.protonMaintenanceContent, QObject::tr("Maintenance"));
  fold(ui.advancedCompatibilityContent, QObject::tr("Advanced"));

  ui.verticalLayout->removeWidget(ui.tabWidget);
  auto* navigation = new SettingsNavigation(ui.tabWidget, dialog);
  navigation->addSection(ui.generalTab, QObject::tr("General"),
      QObject::tr("Interface behavior and new-profile defaults for this library setup."),
      "confirmation dialogs categories saves ini profiles");
  navigation->addSection(ui.tab, QObject::tr("Appearance"),
      QObject::tr("Theme, fonts and colors for this library setup."), "style colour size");
  navigation->addSection(ui.uiTab, QObject::tr("Mod List"),
      QObject::tr("Display, filtering and separators for this library setup’s mods."),
      "archives conflicts priority sorting filters");
  navigation->addSection(ui.nexusTab, QObject::tr("Downloads & Nexus"),
      QObject::tr("Download preferences for this setup and your shared Nexus account. "
                  "Account, link-handler and cache actions take effect immediately."),
      "login authentication api key nxm servers network notifications");
  navigation->addSection(ui.pathsTab, QObject::tr("Paths"),
      QObject::tr("Game and storage locations for this library setup."),
      "storage directories folders downloads mods profiles overwrite cache");
  navigation->addSection(ui.protonTab, QObject::tr("Compatibility"),
      QObject::tr("Run Windows games and tools with Proton."),
      "wine proton prefix runtime slr fuse usvfs performance mangohud launch");
  navigation->addSection(ui.pluginsTab, QObject::tr("Plugins"),
      QObject::tr("Game support and tools. Enabling or disabling a plugin applies immediately."),
      "extensions installers proxy preview support blacklist blocked");
  if (auto* updates = ui.tabWidget->findChild<QWidget*>("updatesTab")) {
    navigation->addSection(updates, QObject::tr("Updates"),
        QObject::tr("Update preferences for this setup. Installing an update changes "
                    "the app and restarts it immediately."), "version channel stable nightly releases");
  }
  if (auto* clf3 = ui.tabWidget->findChild<QWidget*>("clf3Tab")) {
    navigation->addSection(clf3, QObject::tr("CLF3"),
        QObject::tr("Install performance and default paths for CLF3 modlist installs. "
                    "Stored in wabbajack.ini and shared across instances."),
        "downloads workers threads extraction performance concurrent cache");
  }
  navigation->addSection(ui.diagnosticsTab, QObject::tr("Diagnostics"),
      QObject::tr("Logs and crash reporting preferences for this library setup."),
      "troubleshooting debug errors warnings dumps");
  ui.verticalLayout->insertWidget(0, navigation, 1);
}
