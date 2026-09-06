#include "settingsdialogclf3.h"

#include "clf3installutils.h"
#include "settings.h"
#include "ui_settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <utility>

namespace
{
int defaultThreadCount()
{
  return std::max(1, QThread::idealThreadCount());
}

QHBoxLayout* makeOverrideRow(const QString& label, QCheckBox*& check,
                             QWidget* editor, QWidget* parent)
{
  auto* row = new QHBoxLayout();
  check     = new QCheckBox(label, parent);
  row->addWidget(check);
  row->addWidget(editor, 1);
  return row;
}

QSpinBox* makeWorkerSpin(QWidget* parent, int value)
{
  auto* spin = new QSpinBox(parent);
  spin->setRange(1, 256);
  spin->setValue(value);
  return spin;
}
}  // namespace

Clf3SettingsTab::Clf3SettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  QWidget* page  = new QWidget(ui->tabWidget);
  auto* layout   = new QVBoxLayout(page);
  auto* group    = new QGroupBox(tr("Install performance"), page);
  auto* form     = new QVBoxLayout(group);

  auto* hint = new QLabel(
      tr("Checked knobs are passed to CLF3 as install flags. "
         "Unchecked knobs are left unset and the flag is omitted."),
      group);
  hint->setWordWrap(true);
  form->addWidget(hint);

  const int threads = defaultThreadCount();

  m_concurrentSpin = makeWorkerSpin(group, threads);
  m_concurrentSpin->setToolTip(tr("Maximum concurrent downloads (--concurrent)"));
  form->addLayout(makeOverrideRow(tr("Concurrent downloads"), m_concurrentCheck,
                                  m_concurrentSpin, group));

  m_installSpin = makeWorkerSpin(group, threads);
  m_installSpin->setToolTip(tr("Parallel workers for extraction/install (--install-workers)"));
  form->addLayout(makeOverrideRow(tr("Install workers"), m_installCheck,
                                  m_installSpin, group));

  m_bsaSpin = makeWorkerSpin(group, 1);
  m_bsaSpin->setToolTip(tr("BSA/BA2 archives processed concurrently (--bsa-workers)"));
  form->addLayout(
      makeOverrideRow(tr("BSA workers"), m_bsaCheck, m_bsaSpin, group));

  m_sevenzipSpin = makeWorkerSpin(group, threads);
  m_sevenzipSpin->setToolTip(tr("7z archives processed concurrently (--sevenzip-workers)"));
  form->addLayout(makeOverrideRow(tr("7z workers"), m_sevenzipCheck,
                                  m_sevenzipSpin, group));

  m_extractBox = new QComboBox(group);
  m_extractBox->addItem(tr("Streaming"), QStringLiteral("streaming"));
  m_extractBox->addItem(tr("Phased"), QStringLiteral("phased"));
  m_extractBox->setToolTip(tr("Extraction strategy (--extract)"));
  form->addLayout(makeOverrideRow(tr("Extract strategy"), m_extractCheck,
                                  m_extractBox, group));

  layout->addWidget(group);

  auto* pathsGroup  = new QGroupBox(tr("Default paths"), page);
  auto* pathsLayout = new QVBoxLayout(pathsGroup);

  auto* downloadRow = new QHBoxLayout();
  m_downloadDirEdit = new QLineEdit(pathsGroup);
  m_downloadDirEdit->setPlaceholderText(
      tr("Leave empty for the built-in default"));
  m_downloadDirEdit->setToolTip(
      tr("Prefills the Download cache field for new CLF3 installations"));
  auto* downloadBrowse = new QPushButton(tr("Browse…"), pathsGroup);
  downloadRow->addWidget(new QLabel(tr("Default download cache:"),
                                    pathsGroup));
  downloadRow->addWidget(m_downloadDirEdit, 1);
  downloadRow->addWidget(downloadBrowse);
  pathsLayout->addLayout(downloadRow);

  layout->addWidget(pathsGroup);
  layout->addStretch(1);

  ui->tabWidget->addTab(page, tr("CLF3"));

  QObject::connect(downloadBrowse, &QPushButton::clicked, m_downloadDirEdit,
                   [this] {
                     const QString dir = QFileDialog::getExistingDirectory(
                         m_downloadDirEdit,
                         tr("Select default download cache"),
                         m_downloadDirEdit->text());
                     if (!dir.isEmpty())
                       m_downloadDirEdit->setText(dir);
                   });

  m_downloadDirEdit->setText(Clf3InstallUtils::loadDefaultDownloadDir());

  const Clf3Tuning saved = Clf3InstallUtils::loadPerfTuning();
  if (saved.concurrentDownloads) {
    m_concurrentCheck->setChecked(true);
    m_concurrentSpin->setValue(*saved.concurrentDownloads);
  }
  if (saved.installWorkers) {
    m_installCheck->setChecked(true);
    m_installSpin->setValue(*saved.installWorkers);
  }
  if (saved.bsaWorkers) {
    m_bsaCheck->setChecked(true);
    m_bsaSpin->setValue(*saved.bsaWorkers);
  }
  if (saved.sevenzipWorkers) {
    m_sevenzipCheck->setChecked(true);
    m_sevenzipSpin->setValue(*saved.sevenzipWorkers);
  }
  if (saved.extractStrategy) {
    const int idx = m_extractBox->findData(*saved.extractStrategy);
    if (idx >= 0) {
      m_extractCheck->setChecked(true);
      m_extractBox->setCurrentIndex(idx);
    }
  }

  const std::pair<QCheckBox*, QWidget*> rows[] = {
      {m_concurrentCheck, m_concurrentSpin},
      {m_installCheck, m_installSpin},
      {m_bsaCheck, m_bsaSpin},
      {m_sevenzipCheck, m_sevenzipSpin},
      {m_extractCheck, m_extractBox},
  };
  for (const auto& [check, editor] : rows) {
    editor->setEnabled(check->isChecked());
    QObject::connect(check, &QCheckBox::toggled, editor,
                     &QWidget::setEnabled);
  }
}

void Clf3SettingsTab::update()
{
  Clf3Tuning tuning;
  if (m_concurrentCheck->isChecked())
    tuning.concurrentDownloads = m_concurrentSpin->value();
  if (m_installCheck->isChecked())
    tuning.installWorkers = m_installSpin->value();
  if (m_bsaCheck->isChecked())
    tuning.bsaWorkers = m_bsaSpin->value();
  if (m_sevenzipCheck->isChecked())
    tuning.sevenzipWorkers = m_sevenzipSpin->value();
  if (m_extractCheck->isChecked())
    tuning.extractStrategy = m_extractBox->currentData().toString();
  Clf3InstallUtils::savePerfTuning(tuning);
  Clf3InstallUtils::saveDefaultDownloadDir(m_downloadDirEdit->text().trimmed());
}
