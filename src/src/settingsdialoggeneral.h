#ifndef SETTINGSDIALOGGENERAL_H
#define SETTINGSDIALOGGENERAL_H

#include "plugincontainer.h"
#include "settings.h"
#include "settingsdialog.h"

class GeneralSettingsTab : public SettingsTab
{
public:
  GeneralSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  void resetDialogs();

  void onEditCategories();
  void onResetDialogs();
};

#endif  // SETTINGSDIALOGGENERAL_H
