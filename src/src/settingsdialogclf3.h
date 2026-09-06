#ifndef SETTINGSDIALOGCLF3_H
#define SETTINGSDIALOGCLF3_H

#include "settingsdialog.h"

#include <QCoreApplication>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

// Dedicated Settings > CLF3 tab. Hosts the CLF3 install performance
// overrides (download/extraction parallelism, extraction strategy).
// Each knob is a checkbox plus a value widget; an unchecked knob leaves
// the corresponding key absent from Settings so the CLF3 flag is omitted.
class Clf3SettingsTab : public SettingsTab
{
  Q_DECLARE_TR_FUNCTIONS(Clf3SettingsTab)
public:
  Clf3SettingsTab(Settings& settings, SettingsDialog& dialog);

  void update() override;

private:
  QCheckBox* m_concurrentCheck    = nullptr;
  QSpinBox* m_concurrentSpin      = nullptr;
  QCheckBox* m_installCheck       = nullptr;
  QSpinBox* m_installSpin         = nullptr;
  QCheckBox* m_bsaCheck           = nullptr;
  QSpinBox* m_bsaSpin             = nullptr;
  QCheckBox* m_sevenzipCheck      = nullptr;
  QSpinBox* m_sevenzipSpin        = nullptr;
  QCheckBox* m_extractCheck       = nullptr;
  QComboBox* m_extractBox         = nullptr;
  QLineEdit* m_downloadDirEdit    = nullptr;
};

#endif  // SETTINGSDIALOGCLF3_H
