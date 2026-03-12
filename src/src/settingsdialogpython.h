#ifndef SETTINGSDIALOGPYTHON_H
#define SETTINGSDIALOGPYTHON_H

#include <QObject>

#include "settings.h"
#include "settingsdialog.h"

class PythonSettingsTab : public QObject, public SettingsTab
{
  Q_OBJECT

public:
  PythonSettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  void detectPython();
  void refreshVenvState();
  void onCreateVenv();
  void onDeleteVenv();
  void onOpenVenvFolder();

  QString m_pythonPath;
  QString m_venvPath;
};

#endif  // SETTINGSDIALOGPYTHON_H
