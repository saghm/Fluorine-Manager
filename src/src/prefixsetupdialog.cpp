#include "prefixsetupdialog.h"

#include "fluorineconfig.h"
#include "fluorinepaths.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QShowEvent>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include <log.h>
#include <utility.h>

// ============================================================================
// Construction / destruction
// ============================================================================

PrefixSetupDialog::PrefixSetupDialog(const QString& prefixPath,
                                     const QString& protonPath,
                                     uint32_t appId,
                                     QWidget* parent)
    : QDialog(parent)
    , m_prefixPath(prefixPath)
    , m_protonPath(protonPath)
    , m_appId(appId)
{
  setWindowTitle(tr("Wine Prefix Setup"));
  setMinimumSize(700, 500);
  resize(800, 600);
  setModal(true);

  buildUI();

  // Create the runner (lives on a worker thread).
  m_workerThread = new QThread(this);
  m_runner       = new PrefixSetupRunner(prefixPath, protonPath, appId);
  m_runner->moveToThread(m_workerThread);

  // Wire signals.
  connect(m_runner, &PrefixSetupRunner::stepStarted,
          this, &PrefixSetupDialog::onStepStarted, Qt::QueuedConnection);
  connect(m_runner, &PrefixSetupRunner::stepFinished,
          this, &PrefixSetupDialog::onStepFinished, Qt::QueuedConnection);
  connect(m_runner, &PrefixSetupRunner::logMessage,
          this, &PrefixSetupDialog::onLogMessage, Qt::QueuedConnection);
  connect(m_runner, &PrefixSetupRunner::progressChanged,
          this, &PrefixSetupDialog::onProgressChanged, Qt::QueuedConnection);
  connect(m_runner, &PrefixSetupRunner::finished,
          this, &PrefixSetupDialog::onFinished, Qt::QueuedConnection);

  // Clean up worker thread when dialog closes.
  connect(m_workerThread, &QThread::finished, m_runner, &QObject::deleteLater);

  m_workerThread->start();

  // Populate step list from the runner's step definitions.
  populateStepList();
}

PrefixSetupDialog::~PrefixSetupDialog()
{
  m_runner->cancel();
  m_workerThread->quit();
  m_workerThread->wait(5000);
}

// ============================================================================
// UI construction
// ============================================================================

void PrefixSetupDialog::buildUI()
{
  auto* mainLayout = new QVBoxLayout(this);

  // Status label at top.
  m_statusLabel = new QLabel(tr("Preparing..."), this);
  m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
  mainLayout->addWidget(m_statusLabel);

  // Splitter: step list (left) + log (right).
  auto* splitter = new QSplitter(Qt::Horizontal, this);

  m_stepList = new QListWidget(this);
  m_stepList->setMinimumWidth(250);
  m_stepList->setMaximumWidth(350);
  m_stepList->setSelectionMode(QAbstractItemView::SingleSelection);
  splitter->addWidget(m_stepList);

  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setFont(QFont("monospace", 9));
  m_logView->setPlaceholderText(tr("Setup log output will appear here..."));
  splitter->addWidget(m_logView);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  mainLayout->addWidget(splitter, 1);

  // Progress bar.
  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  mainLayout->addWidget(m_progressBar);

  // Button row.
  auto* buttonLayout = new QHBoxLayout;
  buttonLayout->addStretch();

  m_cancelBtn = new QPushButton(tr("Cancel"), this);
  connect(m_cancelBtn, &QPushButton::clicked, this, &PrefixSetupDialog::onCancel);
  buttonLayout->addWidget(m_cancelBtn);

  m_retryBtn = new QPushButton(tr("Retry Failed"), this);
  m_retryBtn->setVisible(false);
  connect(m_retryBtn, &QPushButton::clicked, this, &PrefixSetupDialog::onRetryFailed);
  buttonLayout->addWidget(m_retryBtn);

  m_deleteBtn = new QPushButton(tr("Delete Prefix && Show Log"), this);
  m_deleteBtn->setVisible(false);
  connect(m_deleteBtn, &QPushButton::clicked, this, &PrefixSetupDialog::onDeleteAndClose);
  buttonLayout->addWidget(m_deleteBtn);

  m_closeBtn = new QPushButton(tr("Close"), this);
  m_closeBtn->setVisible(false);
  connect(m_closeBtn, &QPushButton::clicked, this, &PrefixSetupDialog::onClose);
  buttonLayout->addWidget(m_closeBtn);

  mainLayout->addLayout(buttonLayout);
}

void PrefixSetupDialog::populateStepList()
{
  m_stepList->clear();
  for (const auto& step : m_runner->steps()) {
    auto* item = new QListWidgetItem(
        QStringLiteral("   %1").arg(step.displayName), m_stepList);
    item->setForeground(Qt::gray);
  }
}

void PrefixSetupDialog::updateButtons()
{
  m_cancelBtn->setVisible(m_running);
  m_retryBtn->setVisible(!m_running && !m_allSucceeded);
  m_deleteBtn->setVisible(!m_running && !m_allSucceeded);
  m_closeBtn->setVisible(!m_running);
}

// ============================================================================
// Auto-start on show
// ============================================================================

void PrefixSetupDialog::showEvent(QShowEvent* event)
{
  QDialog::showEvent(event);

  if (!m_started) {
    m_started = true;
    m_running = true;
    updateButtons();
    QMetaObject::invokeMethod(m_runner, "start", Qt::QueuedConnection);
  }
}

// ============================================================================
// Slots — step progress
// ============================================================================

void PrefixSetupDialog::onStepStarted(int index)
{
  if (index < 0 || index >= m_stepList->count()) return;

  auto* item = m_stepList->item(index);
  const QString name = m_runner->steps().at(index).displayName;
  item->setText(QStringLiteral("▶ %1").arg(name));
  item->setForeground(QColor("#4fc3f7"));  // light blue
  m_stepList->scrollToItem(item);

  m_statusLabel->setText(QStringLiteral("Running: %1").arg(name));
}

void PrefixSetupDialog::onStepFinished(int index, bool success, const QString& error)
{
  if (index < 0 || index >= m_stepList->count()) return;

  auto* item = m_stepList->item(index);
  const QString name = m_runner->steps().at(index).displayName;

  if (success) {
    item->setText(QStringLiteral("✓ %1").arg(name));
    item->setForeground(QColor("#66bb6a"));  // green
  } else {
    item->setText(QStringLiteral("✗ %1").arg(name));
    item->setForeground(QColor("#ef5350"));  // red
    if (!error.isEmpty()) {
      onLogMessage(QStringLiteral("ERROR [%1]: %2").arg(name, error));
    }
  }
}

void PrefixSetupDialog::onLogMessage(const QString& text)
{
  m_logView->append(text);

  // Also forward to MO2 log system.
  MOBase::log::info("{}", text);

  // Auto-scroll to bottom.
  auto* sb = m_logView->verticalScrollBar();
  if (sb) sb->setValue(sb->maximum());
}

void PrefixSetupDialog::onProgressChanged(float progress)
{
  m_progressBar->setValue(static_cast<int>(progress * 100.0f));
}

void PrefixSetupDialog::onFinished(bool allSucceeded)
{
  m_allSucceeded = allSucceeded;
  m_running      = false;
  updateButtons();

  if (allSucceeded) {
    m_statusLabel->setText(tr("Setup completed successfully!"));
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #66bb6a;");
    m_progressBar->setValue(100);
  } else {
    // Count failures.
    int failCount = 0;
    for (const auto& step : m_runner->steps()) {
      if (step.status == SetupStep::Failed) ++failCount;
    }
    m_statusLabel->setText(
        tr("Setup finished with %n failure(s). You can retry or delete the prefix.",
           "", failCount));
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ef5350;");
  }
}

// ============================================================================
// Button handlers
// ============================================================================

void PrefixSetupDialog::onCancel()
{
  m_runner->cancel();
  m_statusLabel->setText(tr("Cancelling..."));
  m_cancelBtn->setEnabled(false);
}

void PrefixSetupDialog::onRetryFailed()
{
  m_running = true;
  m_statusLabel->setText(tr("Retrying failed steps..."));
  m_statusLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
  updateButtons();

  // Reset failed items to pending appearance.
  for (int i = 0; i < m_stepList->count(); ++i) {
    if (m_runner->steps().at(i).status == SetupStep::Failed) {
      auto* item = m_stepList->item(i);
      item->setText(QStringLiteral("   %1").arg(m_runner->steps().at(i).displayName));
      item->setForeground(Qt::gray);
    }
  }

  QMetaObject::invokeMethod(m_runner, "retryFailed", Qt::QueuedConnection);
}

void PrefixSetupDialog::onDeleteAndClose()
{
  const auto answer = QMessageBox::warning(
      this, tr("Delete Prefix"),
      tr("This will delete the Wine prefix at:\n%1\n\n"
         "The log folder will be opened so you can share the log.\n\n"
         "Continue?").arg(m_prefixPath),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (answer != QMessageBox::Yes)
    return;

  // Delete the prefix.
  FluorineConfig cfg;
  cfg.prefix_path = m_prefixPath;
  cfg.destroyPrefix();

  // Open the logs folder.
  const QString logsPath = fluorineDataDir() + "/logs";
  QDesktopServices::openUrl(QUrl::fromLocalFile(logsPath));

  reject();
}

void PrefixSetupDialog::onClose()
{
  if (m_allSucceeded)
    accept();
  else
    reject();
}
