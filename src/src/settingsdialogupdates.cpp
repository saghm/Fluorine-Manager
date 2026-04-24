#include "settingsdialogupdates.h"

#include "fluorineupdater.h"
#include "ui_settingsdialog.h"

#include <fluorine_build_info.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

UpdatesSettingsTab::UpdatesSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  // Build the tab contents programmatically so we don't have to hand-edit
  // settingsdialog.ui — the Updates section used to live on the General
  // tab; moving it to its own tab keeps General uncluttered and gives us
  // room for a "Check for updates now" button + live status line.
  QWidget* page = new QWidget(ui->tabWidget);
  auto* layout  = new QVBoxLayout(page);

  // --- Current build info ------------------------------------------------
  auto* infoGroup  = new QGroupBox(tr("Current build"), page);
  auto* infoLayout = new QFormLayout(infoGroup);

  const QString currentVersion =
      QStringLiteral(FLUORINE_DISPLAY_VERSION);
  const QString channel =
      QStringLiteral(FLUORINE_BUILD_CHANNEL);
  const QString commit = QStringLiteral(FLUORINE_BUILD_COMMIT);
  const QString timestamp = QStringLiteral(FLUORINE_BUILD_TIMESTAMP);

  m_currentVersionLabel = new QLabel(currentVersion, infoGroup);
  infoLayout->addRow(tr("Version:"), m_currentVersionLabel);

  QString buildLine = QStringLiteral("channel=%1").arg(channel);
  if (!commit.isEmpty()) {
    buildLine += QStringLiteral("  commit=%1").arg(commit);
  }
  if (!timestamp.isEmpty()) {
    buildLine += QStringLiteral("  timestamp=%1").arg(timestamp);
  }
  m_buildInfoLabel = new QLabel(buildLine, infoGroup);
  m_buildInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoLayout->addRow(tr("Build:"), m_buildInfoLabel);

  layout->addWidget(infoGroup);

  // --- Preferences -------------------------------------------------------
  auto* prefsGroup  = new QGroupBox(tr("Update preferences"), page);
  auto* prefsLayout = new QVBoxLayout(prefsGroup);

  m_checkForUpdates = new QCheckBox(tr("Check for updates on startup"),
                                    prefsGroup);
  m_checkForUpdates->setToolTip(
      tr("Query GitHub for a newer Fluorine Manager build when the app "
         "starts. Nothing is installed automatically — you'll see a log "
         "entry and can download the new build from the release page."));
  prefsLayout->addWidget(m_checkForUpdates);

  auto* channelRow = new QHBoxLayout();
  channelRow->addWidget(new QLabel(tr("Channel:"), prefsGroup));
  m_channelBox = new QComboBox(prefsGroup);
  m_channelBox->addItem(tr("Stable (tagged releases)"),
                        QStringLiteral("stable"));
  m_channelBox->addItem(tr("Beta (rolling build from main)"),
                        QStringLiteral("beta"));
  m_channelBox->setToolTip(
      tr("Stable: only tagged v* releases. Beta: the rolling `beta` "
         "release that's replaced on every successful CI build. Beta "
         "builds embed a commit hash and timestamp so the updater can "
         "tell whether you're already on the latest one."));
  channelRow->addWidget(m_channelBox, 1);
  prefsLayout->addLayout(channelRow);

  layout->addWidget(prefsGroup);

  // --- Actions -----------------------------------------------------------
  auto* actionGroup  = new QGroupBox(tr("Actions"), page);
  auto* actionLayout = new QVBoxLayout(actionGroup);

  auto* buttonRow = new QHBoxLayout();
  m_checkNowButton =
      new QPushButton(tr("Check for updates now"), actionGroup);
  buttonRow->addWidget(m_checkNowButton);
  buttonRow->addStretch(1);
  actionLayout->addLayout(buttonRow);

  m_statusLabel = new QLabel(actionGroup);
  m_statusLabel->setTextFormat(Qt::RichText);
  m_statusLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_statusLabel->setOpenExternalLinks(true);
  m_statusLabel->setWordWrap(true);
  actionLayout->addWidget(m_statusLabel);

  layout->addWidget(actionGroup);
  layout->addStretch(1);

  // Insert as the second tab (right after General) so it's easy to find.
  const int insertIndex =
      std::max(1, std::min(ui->tabWidget->count(), 1));
  ui->tabWidget->insertTab(insertIndex, page, tr("Updates"));

  // --- State -------------------------------------------------------------
  m_checkForUpdates->setChecked(settings().checkForUpdates());
  const QString currentChannel = settings().fluorineUpdateChannel();
  const int idx = m_channelBox->findData(currentChannel);
  m_channelBox->setCurrentIndex(idx >= 0 ? idx : 0);

  m_updater = new FluorineUpdater(&d);

  QObject::connect(m_checkNowButton, &QPushButton::clicked, &d,
                   [this]() { onCheckNow(); });

  QObject::connect(m_updater, &FluorineUpdater::updateAvailable, &d,
          [this](const FluorineUpdater::ReleaseInfo& info) {
            const QString url = info.htmlUrl.isEmpty()
                                    ? QString()
                                    : info.htmlUrl;
            QString line = tr("<b>Update available:</b> %1")
                               .arg(info.name.isEmpty() ? info.tagName
                                                        : info.name);
            if (!url.isEmpty()) {
              line += QStringLiteral(
                          " &mdash; <a href=\"%1\">view release</a>")
                          .arg(url);
            }
            m_statusLabel->setText(line);
            m_checkNowButton->setEnabled(true);
          });
  QObject::connect(m_updater, &FluorineUpdater::upToDate, &d,
          [this](const FluorineUpdater::ReleaseInfo&) {
            m_statusLabel->setText(
                tr("You're on the latest build for this channel."));
            m_checkNowButton->setEnabled(true);
          });
  QObject::connect(m_updater, &FluorineUpdater::checkFailed, &d,
          [this](const QString& reason) {
            m_statusLabel->setText(
                tr("<i>Update check failed:</i> %1").arg(reason));
            m_checkNowButton->setEnabled(true);
          });
}

void UpdatesSettingsTab::update()
{
  settings().setCheckForUpdates(m_checkForUpdates->isChecked());
  const QString channel = m_channelBox->currentData().toString();
  settings().setFluorineUpdateChannel(channel);
  // Keep the legacy usePrereleases flag aligned so the Nexus-side pre-
  // release toggle mirrors the Fluorine channel selection.
  settings().setUsePrereleases(channel == QStringLiteral("beta"));
}

void UpdatesSettingsTab::onCheckNow()
{
  m_statusLabel->setText(tr("Checking…"));
  m_checkNowButton->setEnabled(false);

  const QString channel = m_channelBox->currentData().toString();
  const FluorineUpdater::Channel c = FluorineUpdater::channelFromString(
      channel, FluorineUpdater::buildChannel());
  m_updater->checkForUpdates(c);
}
