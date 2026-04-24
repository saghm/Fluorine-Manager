#ifndef SETTINGSDIALOGUPDATES_H
#define SETTINGSDIALOGUPDATES_H

#include "settingsdialog.h"

#include <QCoreApplication>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

class FluorineUpdater;

// Dedicated Settings > Updates tab. Hosts the Fluorine self-update
// preferences (channel selector, startup check toggle) plus an explicit
// "Check for updates now" button with live status feedback.
class UpdatesSettingsTab : public SettingsTab
{
  Q_DECLARE_TR_FUNCTIONS(UpdatesSettingsTab)
public:
  UpdatesSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  void onCheckNow();

  QCheckBox* m_checkForUpdates   = nullptr;
  QComboBox* m_channelBox        = nullptr;
  QLabel* m_currentVersionLabel  = nullptr;
  QLabel* m_buildInfoLabel       = nullptr;
  QPushButton* m_checkNowButton  = nullptr;
  QLabel* m_statusLabel          = nullptr;
  FluorineUpdater* m_updater     = nullptr;
};

#endif  // SETTINGSDIALOGUPDATES_H
