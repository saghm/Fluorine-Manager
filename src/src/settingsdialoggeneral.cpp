#include "settingsdialoggeneral.h"
#include "categoriesdialog.h"
#include "ui_settingsdialog.h"
#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

GeneralSettingsTab::GeneralSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  // download list
  ui->compactBox->setChecked(settings().interface().compactDownloads());
  ui->showMetaBox->setChecked(settings().interface().metaDownloads());
  ui->hideDownloadInstallBox->setChecked(
      settings().interface().hideDownloadsAfterInstallation());
  ui->showDownloadNotificationsBox->setChecked(
      settings().interface().showDownloadNotifications());
  ui->enableArchiveParsingBox->setChecked(settings().archiveParsing());

  // profile defaults
  ui->localINIs->setChecked(settings().profileLocalInis());
  ui->localSaves->setChecked(settings().profileLocalSaves());
  ui->automaticArchiveInvalidation->setChecked(settings().profileArchiveInvalidation());

  // miscellaneous
  ui->centerDialogs->setChecked(settings().geometry().centerDialogs());
  ui->changeGameConfirmation->setChecked(
      settings().interface().showChangeGameConfirmation());
  ui->showMenubarOnAlt->setChecked(settings().interface().showMenubarOnAlt());
  ui->doubleClickPreviews->setChecked(
      settings().interface().doubleClicksOpenPreviews());

  QObject::connect(ui->categoriesBtn, &QPushButton::clicked, [&] {
    onEditCategories();
  });

  QObject::connect(ui->resetDialogsButton, &QPushButton::clicked, [&] {
    onResetDialogs();
  });
}

void GeneralSettingsTab::update()
{
  // download list
  settings().interface().setCompactDownloads(ui->compactBox->isChecked());
  settings().interface().setMetaDownloads(ui->showMetaBox->isChecked());
  settings().interface().setHideDownloadsAfterInstallation(
      ui->hideDownloadInstallBox->isChecked());
  settings().interface().setShowDownloadNotifications(
      ui->showDownloadNotificationsBox->isChecked());
  settings().setArchiveParsing(ui->enableArchiveParsingBox->isChecked());

  // Update settings are persisted by UpdatesSettingsTab (own tab).

  // profile defaults
  settings().setProfileLocalInis(ui->localINIs->isChecked());
  settings().setProfileLocalSaves(ui->localSaves->isChecked());
  settings().setProfileArchiveInvalidation(
      ui->automaticArchiveInvalidation->isChecked());

  // miscellaneous
  settings().geometry().setCenterDialogs(ui->centerDialogs->isChecked());
  settings().interface().setShowChangeGameConfirmation(
      ui->changeGameConfirmation->isChecked());
  settings().interface().setShowMenubarOnAlt(ui->showMenubarOnAlt->isChecked());
  settings().interface().setDoubleClicksOpenPreviews(
      ui->doubleClickPreviews->isChecked());
}

void GeneralSettingsTab::resetDialogs()
{
  settings().widgets().resetQuestionButtons();
  GlobalSettings::resetDialogs();
}

void GeneralSettingsTab::onEditCategories()
{
  CategoriesDialog catDialog(&dialog());

  if (catDialog.exec() == QDialog::Accepted) {
    catDialog.commitChanges();
  }
}

void GeneralSettingsTab::onResetDialogs()
{
  const auto r = QMessageBox::question(
      parentWidget(), QObject::tr("Confirm?"),
      QObject::tr(
          "This will reset all the choices you made to dialogs and make them all "
          "visible again. Continue?"),
      QMessageBox::Yes | QMessageBox::No);

  if (r == QMessageBox::Yes) {
    resetDialogs();
  }
}
