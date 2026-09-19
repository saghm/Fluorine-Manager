#pragma once

class QWidget;
namespace Ui { class SettingsDialog; }

// Shared by the real dialog and the isolated layout review harness.
void setupSettingsLayout(Ui::SettingsDialog& ui, QWidget* dialog);
