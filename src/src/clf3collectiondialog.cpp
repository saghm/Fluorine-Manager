#include "clf3collectiondialog.h"
#include "clf3installutils.h"
#include "curatedguidenxmbroker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

namespace
{
bool
writeJson(const QString& path, const QJsonObject& object)
{
  QSaveFile file(path);
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject
readJson(const QString& path, qint64 limit = 32 * 1024 * 1024)
{
  QFile file(path);
  if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > limit)
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
}

Clf3CollectionDialog::Clf3CollectionDialog(Clf3CollectionHost::AuthRequest auth,
                                           QWidget* parent,
                                           bool browseOnOpen,
                                           QNetworkAccessManager* publicNetwork)
  : QDialog(parent)
  , m_host(std::move(auth), nullptr, publicNetwork)
  , m_images({}, nullptr, publicNetwork)
  , m_brokerConsumer("collection-" + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
  m_rendering = true;
  setWindowTitle(InstallationEnabled ? tr("Nexus Collections") : tr("Nexus Collections (WIP)"));
  resize(1280, 820);
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_pages = new QStackedWidget;
  m_pages->setObjectName("collectionPages");
  outer->addWidget(m_pages, 1);
  auto* browsePage = new QWidget;
  auto* layout = new QVBoxLayout(browsePage);
  m_pages->addWidget(browsePage);
  auto* heading = new QLabel(InstallationEnabled ? tr("Choose a Nexus Collection")
                                                 : tr("Nexus Collections (WIP)"));
  auto font = heading->font();
  font.setPointSize(font.pointSize() + 4);
  font.setBold(true);
  heading->setFont(font);
  layout->addWidget(heading);
  auto* notice =
    new QLabel(tr("Work in progress — browsing is available. Collection downloads, "
                  "installation and resume are temporarily disabled while bugs are fixed."));
  notice->setObjectName("collectionWipNotice");
  notice->setWordWrap(true);
  notice->setVisible(!InstallationEnabled);
  layout->addWidget(notice);
  m_engine = new QLabel(tr("Checking CLF3 Collections support…"));
  m_engine->setObjectName("collectionEngine");
  auto* account = new QHBoxLayout;
  auto* connectNexus = new QPushButton(tr("Connect to Nexus…"));
  connectNexus->setObjectName("collectionConnectNexus");
  connectNexus->setVisible(InstallationEnabled);
  account->addWidget(connectNexus);
  account->addWidget(m_engine, 1);
  layout->addLayout(account);
  connect(
    connectNexus, &QPushButton::clicked, this, &Clf3CollectionDialog::nexusConnectionRequested);
  auto* filters = new QHBoxLayout;
  m_games = new QComboBox;
  m_games->setObjectName("collectionGameFilter");
  m_games->setPlaceholderText(tr("Checking supported games…"));
  m_games->setEnabled(false);
  m_search = new QLineEdit;
  m_search->setPlaceholderText(tr("Search Collections"));
  m_search->setObjectName("collectionSearch");
  m_sort = new QComboBox;
  m_sort->addItem(tr("Most downloaded"), "downloads");
  m_sort->addItem(tr("Recently updated"), "updatedAt");
  m_sort->addItem(tr("Newest"), "createdAt");
  m_adult = new QCheckBox(tr("Show adult content"));
  auto* search = new QPushButton(tr("Search"));
  search->setObjectName("collectionSearchButton");
  for (QWidget* widget :
       std::initializer_list<QWidget*>{ m_games, m_search, m_sort, m_adult, search })
    filters->addWidget(widget);
  layout->addLayout(filters);
  auto* gallery = new QHBoxLayout;
  m_catalog = new QListWidget;
  m_catalog->setObjectName("collectionCatalog");
  m_catalog->setViewMode(QListView::IconMode);
  m_catalog->setMovement(QListView::Static);
  m_catalog->setResizeMode(QListView::Adjust);
  m_catalog->setIconSize(QSize(240, 135));
  m_catalog->setGridSize(QSize(270, 225));
  m_catalog->setSpacing(7);
  m_catalog->setWordWrap(true);
  m_catalog->setUniformItemSizes(true);
  m_catalog->setMinimumHeight(270);
  m_details = new QLabel(tr("Select a collection to see its author, game and revision."));
  m_details->setWordWrap(true);
  m_details->setTextFormat(Qt::RichText);
  m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_details->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_details->setOpenExternalLinks(true);
  m_details->setMinimumWidth(290);
  m_details->setMaximumWidth(360);
  gallery->addWidget(m_catalog, 1);
  gallery->addWidget(m_details);
  layout->addLayout(gallery, 1);
  auto* paging = new QHBoxLayout;
  m_previous = new QPushButton(tr("Previous"));
  m_next = new QPushButton(tr("Next"));
  m_resultCount = new QLabel;
  paging->addWidget(m_previous);
  paging->addWidget(m_resultCount, 1, Qt::AlignCenter);
  paging->addWidget(m_next);
  layout->addLayout(paging);
  auto* source = new QHBoxLayout;
  m_source = new QLineEdit;
  m_source->setObjectName("collectionSource");
  m_source->setPlaceholderText("https://www.nexusmods.com/games/…/collections/…/revisions/…");
  m_revision = new QSpinBox;
  m_revision->setObjectName("collectionRevision");
  m_revision->setRange(0, 2147483647);
  m_revision->setSpecialValueText(tr("Resolve latest once"));
  source->addWidget(new QLabel(tr("Collection URL:")));
  source->addWidget(m_source, 1);
  source->addWidget(new QLabel(tr("Revision:")));
  source->addWidget(m_revision);
  layout->addLayout(source);
  auto* browseActions = new QHBoxLayout;
  m_resume = new QPushButton(tr("Resume saved job"));
  m_resume->setObjectName("collectionResume");
  m_resume->setEnabled(false);
  m_configure = new QPushButton(InstallationEnabled ? tr("Configure Installation →")
                                                    : tr("Downloads disabled (WIP)"));
  m_configure->setObjectName("collectionConfigure");
  m_configure->setEnabled(InstallationEnabled);
  auto* browseClose = new QPushButton(tr("Close"));
  browseActions->addWidget(m_resume);
  browseActions->addStretch();
  browseActions->addWidget(m_configure);
  browseActions->addWidget(browseClose);
  layout->addLayout(browseActions);
  connect(browseClose, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_configure,
          &QPushButton::clicked,
          this,
          [this]
          {
            if (InstallationEnabled)
              m_pages->setCurrentIndex(1);
          });
  connect(m_catalog, &QListWidget::itemActivated, m_configure, &QPushButton::click);

  auto* reviewPage = new QWidget;
  layout = new QVBoxLayout(reviewPage);
  m_pages->addWidget(reviewPage);
  auto* reviewHeading = new QHBoxLayout;
  m_back = new QPushButton(tr("← Back to Collections"));
  m_back->setObjectName("collectionBack");
  reviewHeading->addWidget(m_back);
  reviewHeading->addStretch();
  layout->addLayout(reviewHeading);
  connect(m_back, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(0); });
  m_selection = new QLabel;
  m_selection->setObjectName("collectionSelection");
  m_selection->setFont(font);
  m_selection->setTextFormat(Qt::PlainText);
  m_selection->setWordWrap(true);
  m_selection->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_selection);
  connect(m_source, &QLineEdit::textChanged, this, &Clf3CollectionDialog::updateSelection);
  auto* form = new QFormLayout;
  auto path = [&](QFormLayout* target, const QString& label, const QString& name, bool file)
  {
    auto* edit = new QLineEdit;
    edit->setObjectName(name);
    auto* row = new QHBoxLayout;
    row->addWidget(edit);
    auto* button = new QPushButton(tr("Browse…"));
    m_pathButtons.append(button);
    row->addWidget(button);
    target->addRow(label, row);
    connect(button,
            &QPushButton::clicked,
            this,
            [=, this]
            {
              const QString value =
                file ? QFileDialog::getOpenFileName(this, label, edit->text())
                     : QFileDialog::getExistingDirectory(this, label, edit->text());
              if (!value.isEmpty())
                edit->setText(value);
            });
    connect(edit, &QLineEdit::textChanged, this, &Clf3CollectionDialog::markDirty);
    return edit;
  };
  m_output = path(form, tr("Instance folder"), "collectionOutput", false);
  m_cache = path(form, tr("Downloads folder"), "collectionCache", false);
  m_game = path(form, tr("Game folder"), "collectionGame", false);
  m_game->setPlaceholderText(tr("Choose the installed game folder if it was not detected"));
  m_output->setPlaceholderText(tr("Choose a new instance folder"));
  m_cache->setPlaceholderText(tr("Choose where to keep downloaded archives"));
  layout->addLayout(form);
  auto* advanced = new QToolButton;
  advanced->setObjectName("collectionAdvancedToggle");
  advanced->setText(tr("Advanced"));
  advanced->setCheckable(true);
  advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advanced->setArrowType(Qt::RightArrow);
  layout->addWidget(advanced, 0, Qt::AlignLeft);
  auto* overrides = new QWidget;
  overrides->setObjectName("collectionAdvanced");
  auto* advancedForm = new QFormLayout(overrides);
  advancedForm->setContentsMargins(0, 0, 0, 0);
  m_package = path(advancedForm, tr("Local collection package"), "collectionPackage", true);
  m_package->setPlaceholderText(
    tr("Automatic download; optionally use a local archive or directory"));
  m_ini = path(advancedForm, tr("Profile INI source"), "collectionIni", false);
  m_ini->setPlaceholderText(tr("Automatic"));
  m_masterlist = path(advancedForm, tr("LOOT masterlist"), "collectionMasterlist", true);
  m_masterlist->setPlaceholderText(tr("Automatic download for the selected game"));
  layout->addWidget(overrides);
  overrides->hide();
  connect(advanced,
          &QToolButton::toggled,
          this,
          [advanced, overrides](bool expanded)
          {
            overrides->setVisible(expanded);
            advanced->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
          });
  m_reviewPanel = new QWidget;
  m_reviewPanel->setObjectName("collectionReviewPanel");
  auto* reviewLayout = new QVBoxLayout(m_reviewPanel);
  reviewLayout->setContentsMargins(0, 0, 0, 0);
  reviewLayout->addWidget(
    new QLabel(tr("Review mods and compatibility. Check any optional mods you want.")));
  auto* split = new QSplitter;
  m_members = new QListWidget;
  m_members->setObjectName("collectionMembers");
  m_review = new QPlainTextEdit;
  m_review->setObjectName("collectionReview");
  m_review->setReadOnly(true);
  split->addWidget(m_members);
  split->addWidget(m_review);
  reviewLayout->addWidget(split, 1);
  layout->addWidget(m_reviewPanel, 1);
  m_reviewPanel->hide();
  layout->addStretch();
  m_status = new QLabel(tr("Select a collection or paste its URL."));
  m_status->setObjectName("collectionStatus");
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  outer->addWidget(m_status);
  m_progress = new QProgressBar;
  m_progress->setObjectName("collectionProgress");
  m_progress->setRange(0, 1);
  layout->addWidget(m_progress);
  m_progress->hide();
  auto* actions = new QHBoxLayout;
  auto button = [&](const QString& label, const QString& name)
  {
    auto* b = new QPushButton(label);
    b->setObjectName(name);
    actions->addWidget(b);
    return b;
  };
  actions->addStretch();
  m_planButton = button(tr("Review collection"), "collectionPlan");
  m_install = button(tr("Install"), "collectionInstall");
  m_cancel = button(tr("Cancel"), "collectionCancel");
  m_browser = button(tr("Open exact file page"), "collectionBrowser");
  m_manual = button(tr("Select archive…"), "collectionManual");
  m_add = button(tr("Add to Fluorine"), "collectionAdd");
  m_openButton = button(tr("Add and Open"), "collectionOpen");
  auto* close = button(tr("Close"), "collectionClose");
  layout->addLayout(actions);
  m_add->setEnabled(false);
  m_add->hide();
  m_openButton->setEnabled(false);
  m_openButton->hide();
  m_cancel->hide();
  m_manual->setVisible(false);
  m_browser->setVisible(false);
  m_install->setEnabled(false);
  m_install->hide();
  m_planButton->setEnabled(false);
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  connect(search,
          &QPushButton::clicked,
          this,
          [this]
          {
            m_page = 0;
            browse();
          });
  connect(m_search, &QLineEdit::returnPressed, search, &QPushButton::click);
  connect(m_games,
          &QComboBox::currentIndexChanged,
          this,
          [this]
          {
            m_page = 0;
            browse();
          });
  connect(m_sort,
          &QComboBox::currentIndexChanged,
          this,
          [this]
          {
            m_page = 0;
            browse();
          });
  connect(m_adult,
          &QCheckBox::toggled,
          this,
          [this]
          {
            m_page = 0;
            browse();
          });
  connect(m_previous,
          &QPushButton::clicked,
          this,
          [this]
          {
            if (m_page > 0)
              --m_page;
            browse();
          });
  connect(m_next,
          &QPushButton::clicked,
          this,
          [this]
          {
            ++m_page;
            browse();
          });
  connect(m_catalog,
          &QListWidget::currentItemChanged,
          this,
          [this](QListWidgetItem* item)
          {
            if (!item || m_busy)
              return;
            const auto entry = item->data(Qt::UserRole).toJsonObject();
            const auto game = entry.value("game").toObject();
            const auto revision = entry.value("latestPublishedRevision").toObject();
            const auto domain = game.value("domainName").toString();
            m_source->setText(
              Clf3CollectionHost::sourceUrl({ { "domain", domain },
                                              { "slug", entry.value("slug") },
                                              { "revision", revision.value("revisionNumber") } }));
            const auto pinned =
              Clf3CollectionHost::sourceUrl({ { "domain", domain },
                                              { "slug", entry.value("slug") },
                                              { "revision", revision.value("revisionNumber") } });
            m_details->setText(
              tr("<h2>%1</h2><p>By %2<br>%3</p><p>Revision %4 · %5 mods<br>%6 "
                 "downloads</p><p>%7</p><p><a href=\"%8\">View collection on Nexus Mods</a></p>")
                .arg(entry.value("name").toString().toHtmlEscaped(),
                     entry.value("user").toObject().value("name").toString().toHtmlEscaped(),
                     gameLabel(domain).toHtmlEscaped())
                .arg(revision.value("revisionNumber").toInt())
                .arg(revision.value("modCount").toInt())
                .arg(entry.value("totalDownloads").toInteger())
                .arg(entry.value("summary").toString().toHtmlEscaped(), pinned.toHtmlEscaped()));
            m_game->setText(m_detectedGames.value(
              domain,
              m_detectedGames.value(
                m_support.value(domain).value("name").toString(game.value("name").toString()))));
            m_status->setText(
              !InstallationEnabled ? tr("Collection downloads and installation are disabled (WIP).")
              : m_support.value(domain).value("experimental").toBool()
                ? tr("Experimental game support. Check compatibility before installing; "
                     "game launch has not been validated.")
                : tr("Choose Configure Installation to continue."));
          });
  connect(m_source,
          &QLineEdit::textChanged,
          this,
          [this]
          {
            QSignalBlocker blocker(m_revision);
            m_revision->setValue(
              Clf3CollectionHost::locator(m_source->text()).value("revision").toInt());
            markDirty();
          });
  connect(m_revision,
          &QSpinBox::valueChanged,
          this,
          [this](int revision)
          {
            auto locator = Clf3CollectionHost::locator(m_source->text());
            if (locator.isEmpty())
              return;
            locator.insert("revision", revision);
            m_source->setText(Clf3CollectionHost::sourceUrl(locator));
          });
  connect(m_members,
          &QListWidget::itemChanged,
          this,
          [this]
          {
            if (!m_rendering)
              markDirty();
          });
  connect(m_planButton, &QPushButton::clicked, this, &Clf3CollectionDialog::reviewPlan);
  connect(m_install, &QPushButton::clicked, this, &Clf3CollectionDialog::install);
  connect(m_resume, &QPushButton::clicked, this, &Clf3CollectionDialog::resume);
  connect(m_cancel, &QPushButton::clicked, this, &Clf3CollectionDialog::cancel);
  connect(m_add,
          &QPushButton::clicked,
          this,
          [this]
          {
            if (m_published)
            {
              m_open = false;
              accept();
            }
          });
  connect(m_openButton,
          &QPushButton::clicked,
          this,
          [this]
          {
            if (m_published)
            {
              m_open = true;
              accept();
            }
          });
  connect(&m_host, &Clf3CollectionHost::failed, this, &Clf3CollectionDialog::fail);
  connect(&m_host,
          &Clf3CollectionHost::progress,
          this,
          [this](qint64 bytes, qint64 total)
          {
            m_progress->setRange(0, total > 0 ? 1000 : 0);
            if (total > 0)
              m_progress->setValue(int(1000.0 * bytes / total));
          });
  connect(&m_controller,
          &Clf3ProcessController::collectionCapabilities,
          this,
          [this, browseOnOpen](QJsonObject caps)
          {
            m_capabilities = caps;
            const bool available = installationAvailable();
            {
              const QSignalBlocker blocker(m_games);
              m_games->clear();
              m_support.clear();
              for (const auto& value : caps.value("game_support").toArray())
              {
                const auto game = Clf3CollectionCompat::gameSupport(caps, value.toObject());
                const auto domain = game.value("domain").toString();
                if (domain.isEmpty() || m_support.contains(domain) ||
                    Clf3CollectionHost::sourceUrl({ { "domain", domain }, { "slug", "check" } })
                      .isEmpty())
                  continue;
                m_support.insert(domain, game);
                m_games->addItem(gameLabel(domain), domain);
              }
              if (m_games->count())
                m_games->setCurrentIndex(0);
            }
            updateSelection();
            m_engine->setText((available || !InstallationEnabled)
                                ? tr("CLF3 %1 · %2 supported games")
                                    .arg(caps.value("engine_version").toString())
                                    .arg(m_support.size())
                                : tr("CLF3 %1 does not expose a compatible Collections "
                                     "installation interface.")
                                    .arg(caps.value("engine_version").toString()));
            m_planButton->setEnabled(available);
            m_resume->setEnabled(available);
            setBusy(false);
            if (browseOnOpen)
              browse();
          });
  connect(&m_controller,
          &Clf3ProcessController::collectionRevisionRequired,
          this,
          [this](QString job, QString request, QJsonObject locator)
          {
            if (!InstallationEnabled)
              return;
            auto provide = [this, job, request](QJsonObject package)
            {
              m_locator = package.value("locator").toObject();
              m_packagePath = package.value("package_path").toString();
              const QString pinned = Clf3CollectionHost::sourceUrl(m_locator);
              m_source->setText(pinned);
              m_plannedSource = pinned;
              if (!save())
              {
                m_controller.rejectCollectionRequest(job, request);
                return;
              }
              m_controller.sendCollectionPackage(job, request, m_locator, 1, m_packagePath);
            };
            if (!m_packagePath.isEmpty() && m_plannedSource == m_source->text() &&
                QFileInfo::exists(m_packagePath))
            {
              provide({ { "locator", m_locator }, { "package_path", m_packagePath } });
            }
            else if (!m_package->text().trimmed().isEmpty())
            {
              if (locator.value("revision").toInt() <= 0)
              {
                fail(tr("A local package requires an explicit revision URL."));
                m_controller.rejectCollectionRequest(job, request);
                return;
              }
              provide(
                { { "locator", locator },
                  { "package_path", QFileInfo(m_package->text().trimmed()).canonicalFilePath() } });
            }
            else
              m_host.package(locator, m_cache->text() + "/collections/" + m_identity, provide);
          });
  connect(&m_controller,
          &Clf3ProcessController::collectionSources,
          this,
          [this](QJsonObject sources) { m_sources = sources; });
  connect(&m_controller,
          &Clf3ProcessController::collectionPlanReady,
          this,
          &Clf3CollectionDialog::showPlan);
  connect(&m_controller,
          &Clf3ProcessController::collectionPublished,
          this,
          [this](const QString& report)
          {
            if (usesLocalWorker())
            {
              m_status->setText(tr("Verifying the published installation…"));
              m_progress->setRange(0, 0);
              m_compat.verifyPublication(m_workerRequest,
                                         m_support.value(m_plan.value("domain").toString()));
            }
            else
              published(report);
          });
  connect(&m_compat, &Clf3CollectionCompat::failed, this, &Clf3CollectionDialog::fail);
  connect(&m_compat, &Clf3CollectionCompat::verified, this, &Clf3CollectionDialog::published);
  connect(&m_compat,
          &Clf3CollectionCompat::prepared,
          this,
          [this](QString runtime, QJsonObject sources)
          {
            m_localSources = sources;
            m_controller.startCollectionLocalPlan(
              m_packagePath, m_locator, selectedOptional(), runtime);
          });
  connect(&m_controller, &Clf3ProcessController::failed, this, &Clf3CollectionDialog::fail);
  connect(&m_controller,
          &Clf3ProcessController::cancelled,
          this,
          [this]
          {
            setBusy(false);
            m_status->setText(m_pendingError.isEmpty()
                                ? tr("Cancelled. Verified archives and staged files are retained. "
                                     "Resume this exact job when ready.")
                                : m_pendingError);
            m_pendingError.clear();
          });
  connect(&m_controller, &Clf3ProcessController::phaseChanged, m_status, &QLabel::setText);
  connect(&m_controller,
          &Clf3ProcessController::statusChanged,
          this,
          [this](const QString& text) { m_status->setText(Clf3InstallUtils::redactLog(text)); });
  connect(&m_controller,
          &Clf3ProcessController::overallProgress,
          this,
          [this](int completed, int total)
          {
            m_progress->setRange(0, total);
            m_progress->setValue(completed);
          });
  connect(m_browser,
          &QPushButton::clicked,
          this,
          [this]
          {
            if (!InstallationEnabled || m_currentArtifact.value("source_type") != "nexus")
              return;
            const auto a = m_currentArtifact;
            CuratedGuideNxmBroker::instance().expectForConsumer(m_brokerConsumer,
                                                                a.value("id").toString(),
                                                                a.value("domain").toString(),
                                                                a.value("mod_id").toInt(),
                                                                a.value("file_id").toInt());
            QDesktopServices::openUrl(
              QUrl(QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                     .arg(a.value("domain").toString())
                     .arg(a.value("mod_id").toInteger())
                     .arg(a.value("file_id").toInteger())));
          });
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::acceptedForConsumer,
          this,
          [this](QString consumer, QString id, QString url)
          {
            if (!InstallationEnabled || consumer != m_brokerConsumer ||
                id != m_currentArtifact.value("id").toString())
              return;
            setBusy(true);
            acquireCurrent(url);
          });
  connect(
    m_manual,
    &QPushButton::clicked,
    this,
    [this]
    {
      if (!InstallationEnabled || m_currentArtifact.isEmpty())
        return;
      const QString path =
        QFileDialog::getOpenFileName(this, tr("Select the exact collection archive"));
      if (path.isEmpty())
        return;
      const auto artifact = m_currentArtifact;
      setBusy(true);
      m_host.verify(
        path,
        artifact.value("expected_md5").toString().toLatin1(),
        artifact.value("expected_size").isDouble()
          ? artifact.value("expected_size").toVariant().toLongLong()
          : -1,
        false,
        [this, path, artifact](bool valid)
        {
          if (!valid)
          {
            fail(tr(
              "This archive differs from the exact recorded file. Choose the requested version."));
            return;
          }
          m_artifacts.insert(artifact.value("id").toString(), QFileInfo(path).canonicalFilePath());
          if (!save())
            return;
          ++m_artifactIndex;
          acquireNext();
        });
    });
  m_rendering = false;
  QTimer::singleShot(0, this, [this] { m_controller.queryCollectionCapabilities(); });
}
Clf3CollectionDialog::~Clf3CollectionDialog()
{
  m_compat.cancel();
  m_host.cancel();
  m_images.cancel();
  CuratedGuideNxmBroker::instance().clearConsumer(m_brokerConsumer);
  if (m_controller.isRunning())
    m_controller.cancel();
}
QString
Clf3CollectionDialog::createdInstanceDir() const
{
  return m_published ? m_output->text() : QString{};
}
bool
Clf3CollectionDialog::usesLocalWorker() const
{
  return !(m_capabilities.value("hosted_installation_available").toBool() &&
           m_capabilities.value("capabilities")
             .toArray()
             .contains("collection_hosted_install_v1")) &&
         Clf3CollectionCompat::supports(m_capabilities);
}
bool
Clf3CollectionDialog::installationAvailable() const
{
  if (!InstallationEnabled)
    return false;
  return (m_capabilities.value("hosted_installation_available").toBool() &&
          m_capabilities.value("capabilities")
            .toArray()
            .contains("collection_hosted_install_v1")) ||
         Clf3CollectionCompat::supports(m_capabilities);
}
QString
Clf3CollectionDialog::pendingPath() const
{
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         "/collections/pending.json";
}
QString
Clf3CollectionDialog::gameLabel(const QString& domain) const
{
  const auto support = m_support.value(domain);
  const auto name = support.value("name").toString(domain);
  return support.value("experimental").toBool() ? tr("%1 (Experimental)").arg(name) : name;
}
void
Clf3CollectionDialog::updateSelection()
{
  const auto locator = Clf3CollectionHost::locator(m_source->text());
  const auto domain = locator.value("domain").toString();
  QString name = locator.value("slug").toString(tr("Choose a collection"));
  if (m_plannedSource == m_source->text() && !m_plan.isEmpty())
    name = m_plan.value("name").toString(name);
  else if (const auto* selected = m_catalog->currentItem())
  {
    const auto entry = selected->data(Qt::UserRole).toJsonObject();
    if (entry.value("slug") == locator.value("slug") &&
        entry.value("game").toObject().value("domainName").toString() == domain)
      name = entry.value("name").toString(name);
  }
  const auto revision = locator.value("revision").toInt();
  m_selection->setText(
    tr("%1 · %2\n%3")
      .arg(name,
           revision > 0 ? tr("Revision %1").arg(revision) : tr("Latest revision"),
           gameLabel(domain)));
}
QStringList
Clf3CollectionDialog::selectedOptional() const
{
  QStringList selected;
  for (int i = 0; i < m_members->count(); ++i)
  {
    auto* item = m_members->item(i);
    if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
      selected << item->data(Qt::UserRole).toString();
  }
  return selected;
}
void
Clf3CollectionDialog::browse()
{
  const auto domain = m_games->currentData().toString();
  if (m_busy || !m_support.contains(domain))
    return;
  const auto generation = ++m_catalogGeneration;
  m_host.search(
    domain,
    m_search->text(),
    m_page,
    m_sort->currentData().toString(),
    !m_adult->isChecked(),
    [this, generation, domain](QJsonObject value)
    {
      if (generation != m_catalogGeneration)
        return;
      m_images.cancel();
      ++m_thumbnailGeneration;
      m_activeThumbnails = 0;
      m_thumbnailQueue.clear();
      m_catalog->clear();
      QPixmap placeholder(480, 270);
      placeholder.fill(palette().color(QPalette::AlternateBase));
      {
        QPainter painter(&placeholder);
        painter.setPen(palette().color(QPalette::Text));
        auto font = painter.font();
        font.setPixelSize(28);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(placeholder.rect(), Qt::AlignCenter, tr("Nexus Collections"));
      }
      const auto page = value.value("data").toObject().value("collectionsV2").toObject();
      QSet<QString> queued;
      for (const auto& v : page.value("nodes").toArray())
      {
        const auto item = v.toObject();
        const auto game = item.value("game").toObject();
        if (game.value("domainName").toString() != domain || !m_support.contains(domain))
          continue;
        const auto revision = item.value("latestPublishedRevision").toObject();
        auto* row =
          new QListWidgetItem(tr("%1\n%2\n%3\nRevision %4 · %5 mods")
                                .arg(item.value("name").toString(),
                                     item.value("user").toObject().value("name").toString(),
                                     gameLabel(domain))
                                .arg(revision.value("revisionNumber").toInt())
                                .arg(revision.value("modCount").toInt()),
                              m_catalog);
        const auto artwork = item.value("tileImage").toObject().value("thumbnailUrl").toString();
        row->setData(Qt::UserRole, item);
        row->setData(Qt::UserRole + 1, artwork);
        row->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        row->setToolTip(item.value("summary").toString());
        if (const auto* icon = m_thumbnails.object(artwork))
        {
          row->setIcon(*icon);
          row->setData(Qt::UserRole + 2, true);
        }
        else
        {
          row->setIcon(QIcon(placeholder));
          if (!queued.contains(artwork) && Clf3CollectionHost::thumbnailUrlAllowed(QUrl(artwork)))
          {
            queued.insert(artwork);
            m_thumbnailQueue.enqueue(artwork);
          }
        }
      }
      m_previous->setEnabled(m_page > 0);
      m_next->setEnabled((m_page + 1) * 24 < page.value("totalCount").toInt());
      m_resultCount->setText(
        tr("Page %1 · %2 collections").arg(m_page + 1).arg(page.value("totalCount").toInt()));
      if (m_catalog->count())
        m_catalog->setCurrentRow(0);
      else
        m_details->setText(tr("No collections match these filters."));
      pumpThumbnails();
    });
}
void
Clf3CollectionDialog::pumpThumbnails()
{
  while (m_activeThumbnails < 4 && !m_thumbnailQueue.isEmpty())
  {
    const auto url = m_thumbnailQueue.dequeue();
    const auto generation = m_thumbnailGeneration;
    ++m_activeThumbnails;
    m_images.thumbnail(QUrl(url),
                       [this, url, generation](QImage image)
                       {
                         if (generation != m_thumbnailGeneration)
                           return;
                         --m_activeThumbnails;
                         if (!image.isNull())
                         {
                           // Keep the full author-provided artwork, including titles on square
                           // images.
                           const auto picture = QPixmap::fromImage(image).scaled(
                             480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                           QPixmap frame(480, 270);
                           frame.fill(Qt::transparent);
                           {
                             QPainter painter(&frame);
                             painter.drawPixmap(
                               (480 - picture.width()) / 2, (270 - picture.height()) / 2, picture);
                           }
                           const QIcon icon(frame);
                           m_thumbnails.insert(url, new QIcon(icon));
                           for (int i = 0; i < m_catalog->count(); ++i)
                           {
                             auto* row = m_catalog->item(i);
                             if (row->data(Qt::UserRole + 1).toString() == url)
                             {
                               row->setIcon(icon);
                               row->setData(Qt::UserRole + 2, true);
                             }
                           }
                         }
                         pumpThumbnails();
                       });
  }
}

void
Clf3CollectionDialog::markDirty()
{
  if (m_rendering)
    return;
  m_dirty = true;
  m_published = false;
  m_install->setEnabled(false);
  m_add->setEnabled(false);
  m_add->hide();
  m_openButton->setEnabled(false);
  m_openButton->hide();
  if (m_source->text() != m_plannedSource)
  {
    m_reviewPanel->hide();
    m_install->hide();
  }
}
void
Clf3CollectionDialog::setBusy(bool busy)
{
  m_busy = busy;
  m_back->setEnabled(!busy);
  m_configure->setEnabled(!busy && InstallationEnabled);
  emit busyChanged(busy);
  for (auto* edit : { m_source, m_package, m_game, m_output, m_cache, m_ini, m_masterlist })
    edit->setEnabled(!busy && !m_installStarted);
  for (auto* button : m_pathButtons)
    button->setEnabled(!busy && !m_installStarted);
  m_revision->setEnabled(!busy && !m_installStarted);
  m_members->setEnabled(!busy && !m_installStarted);
  m_catalog->setEnabled(!busy && !m_installStarted);
  m_games->setEnabled(!busy && !m_support.isEmpty());
  m_search->setEnabled(!busy);
  const bool hosted = installationAvailable();
  m_planButton->setEnabled(!busy && hosted && !m_installStarted);
  m_planButton->setVisible(!m_installStarted && !m_published);
  m_install->setEnabled(!busy && !m_dirty && !m_plan.isEmpty() &&
                        m_plan.value("blockers").toArray().isEmpty() && hosted && !m_published);
  m_resume->setEnabled(!busy && hosted && QFileInfo::exists(pendingPath()));
  m_cancel->setEnabled(busy);
  m_cancel->setVisible(busy);
  m_progress->setVisible(busy || m_published);
  m_install->setVisible(!m_published && !m_plan.isEmpty() && !m_reviewPanel->isHidden());
  m_install->setText(m_installStarted ? tr("Resume installation") : tr("Install"));
  m_add->setVisible(m_published);
  m_openButton->setVisible(m_published);
  m_manual->setEnabled(!busy && InstallationEnabled);
  m_browser->setEnabled(!busy && InstallationEnabled);
  if (!busy && m_deferredClose >= 0)
  {
    const int result = m_deferredClose;
    m_deferredClose = -1;
    QTimer::singleShot(0, this, [this, result] { done(result); });
  }
}
void
Clf3CollectionDialog::fail(const QString& message)
{
  m_compat.cancel();
  m_host.cancel();
  if (m_controller.isRunning())
  {
    m_pendingError = Clf3InstallUtils::redactLog(message);
    m_controller.cancel();
    m_status->setText(m_pendingError);
    return;
  }
  setBusy(false);
  m_status->setText(Clf3InstallUtils::redactLog(message));
  m_manual->setVisible(!m_currentArtifact.isEmpty());
  m_browser->setVisible(m_currentArtifact.value("source_type") == "nexus");
}
bool
Clf3CollectionDialog::save()
{
  if (m_job.isEmpty() || !QDir().mkpath(m_job))
  {
    fail(tr("Cannot create the collection job directory."));
    return false;
  }
  QJsonArray selected;
  for (const auto& id : selectedOptional())
    selected.append(id);
  const QJsonObject saved{ { "version", 1 },
                           { "job", m_job },
                           { "identity", m_identity },
                           { "source", m_plannedSource },
                           { "locator", m_locator },
                           { "package", m_packagePath },
                           { "plan", m_plan },
                           { "game", m_game->text() },
                           { "cache", m_cache->text() },
                           { "output", m_output->text() },
                           { "ini", m_ini->text() },
                           { "masterlist", m_masterlist->text() },
                           { "artifacts", m_artifacts },
                           { "started", m_installStarted } };
  if (!writeJson(m_job + "/saved.json", saved) || !writeJson(pendingPath(), { { "job", m_job } }))
  {
    fail(tr("Cannot save the job for resume."));
    return false;
  }
  return true;
}
void
Clf3CollectionDialog::reviewPlan()
{
  if (!InstallationEnabled)
    return;
  m_pages->setCurrentIndex(1);
  if (m_busy || m_controller.isRunning())
    return;
  markDirty();
  // Freeze normalized paths into the reviewed job and its eventual publication report.
  m_rendering = true;
  for (auto* edit : { m_package, m_game, m_output, m_cache, m_ini, m_masterlist })
  {
    const auto value = edit->text().trimmed();
    if (!value.isEmpty())
      edit->setText(QDir::cleanPath(value));
  }
  m_rendering = false;
  auto locator = Clf3CollectionHost::locator(m_source->text().trimmed());
  if (locator.isEmpty())
  {
    fail(tr("Enter a valid Nexus Collection URL without authorization parameters."));
    return;
  }
  if (!m_support.contains(locator.value("domain").toString()))
  {
    fail(tr("This game has no reviewed installation adapter in the selected engine."));
    return;
  }
  if (!QFileInfo(m_game->text()).isDir())
  {
    fail(tr("Choose the source game folder so CLF3 can check the runtime."));
    return;
  }
  if (!QDir::isAbsolutePath(m_output->text()) || !QDir::isAbsolutePath(m_cache->text()))
  {
    fail(tr("Choose an instance folder and a downloads folder before reviewing the collection."));
    return;
  }
  m_host.cancel();
  ++m_catalogGeneration;
  if (m_job.isEmpty() || m_plannedSource != m_source->text() ||
      (!m_package->text().trimmed().isEmpty() && !m_packagePath.isEmpty() &&
       QFileInfo(m_package->text().trimmed()).canonicalFilePath() != m_packagePath))
  {
    m_identity = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_job = QFileInfo(pendingPath()).absolutePath() + "/jobs/" + m_identity;
    m_packagePath.clear();
    m_artifacts = {};
    m_plan = {};
    m_members->clear();
    m_installStarted = false;
  }
  setBusy(true);
  m_status->setText(tr("Acquiring the pinned package and building the review…"));
  if (usesLocalWorker())
  {
    reviewLocalPackage();
    return;
  }
  m_controller.startCollectionPlan(m_source->text().trimmed(),
                                   {},
                                   false,
                                   selectedOptional(),
                                   QFileInfo(m_game->text()).canonicalFilePath());
}
void
Clf3CollectionDialog::reviewLocalPackage()
{
  if (!InstallationEnabled)
    return;
  auto provide = [this](QJsonObject package)
  {
    m_locator = package.value("locator").toObject();
    m_packagePath = package.value("package_path").toString();
    m_plannedSource = Clf3CollectionHost::sourceUrl(m_locator);
    m_source->setText(m_plannedSource);
    if (!save())
      return;
    m_compat.prepare(m_packagePath,
                     QFileInfo(m_game->text()).canonicalFilePath(),
                     m_support.value(m_locator.value("domain").toString()),
                     m_controller.enginePath());
  };
  if (!m_packagePath.isEmpty() && m_plannedSource == m_source->text() &&
      QFileInfo::exists(m_packagePath))
    provide({ { "locator", m_locator }, { "package_path", m_packagePath } });
  else if (!m_package->text().isEmpty())
  {
    const auto locator = Clf3CollectionHost::locator(m_source->text());
    if (locator.value("revision").toInt() <= 0)
    {
      fail(tr("A local package requires an explicit revision URL."));
      return;
    }
    provide({ { "locator", locator },
              { "package_path", QFileInfo(m_package->text()).canonicalFilePath() } });
  }
  else
    m_host.package(Clf3CollectionHost::locator(m_source->text()),
                   m_cache->text() + "/collections/" + m_identity,
                   provide);
}
void
Clf3CollectionDialog::showPlan(const QJsonObject& plan)
{
  if (!m_resumeExpected.isEmpty() && m_resumeExpected != plan)
  {
    m_resumeExpected = {};
    fail(
      tr("The saved package or plan changed. Start a new job and review it before installation."));
    return;
  }
  m_resumeExpected = {};
  m_rendering = true;
  m_plan = plan;
  if (usesLocalWorker())
  {
    m_sources = {};
    for (const auto& value : plan.value("members").toArray())
    {
      const auto member = value.toObject();
      const auto source =
        m_localSources.value(QString::number(member.value("source_index").toInt()));
      if (member.value("selected").toBool() && source.isString())
        m_sources.insert(member.value("artifact_id").toString(), source);
    }
  }
  m_members->clear();
  for (const auto& v : plan.value("members").toArray())
  {
    const auto member = v.toObject();
    auto* item = new QListWidgetItem(tr("%1 · %2 · %3")
                                       .arg(member.value("name").toString(),
                                            member.value("version").toString(),
                                            member.value("mode").toString()),
                                     m_members);
    item->setData(Qt::UserRole, member.value("id").toString());
    item->setToolTip(tr("Member %1\nArtifact %2\nInstaller signature %3\nPhase %4")
                       .arg(member.value("id").toString(),
                            member.value("artifact_id").toString(),
                            member.value("install_signature").toString())
                       .arg(member.value("phase").toInt()));
    if (member.value("optional").toBool())
    {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(member.value("selected").toBool() ? Qt::Checked : Qt::Unchecked);
    }
  }
  QStringList lines{ tr("Revision %1 · %2 mods selected\n%3\nGame version: %4")
                       .arg(m_locator.value("revision").toInt())
                       .arg(plan.value("installation_order").toArray().size())
                       .arg(gameLabel(plan.value("domain").toString()),
                            plan.value("game_version").toString(tr("not reported"))) };
  for (const auto& v : plan.value("blockers").toArray())
  {
    const auto d = v.toObject();
    lines << tr("BLOCKER [%1] %2").arg(d.value("code").toString(), d.value("message").toString());
  }
  for (const auto& v : plan.value("warnings").toArray())
  {
    const auto d = v.toObject();
    lines << tr("Warning [%1] %2").arg(d.value("code").toString(), d.value("message").toString());
  }
  lines << tr("After changing optional mods, choose Review collection again.");
  lines << tr("Installation creates a separate game copy. Game launch has not been validated.");
  m_review->setPlainText(Clf3InstallUtils::redactLog(lines.join("\n\n")));
  updateSelection();
  m_reviewPanel->show();
  m_rendering = false;
  m_dirty = false;
  if (!save())
    return;
  setBusy(false);
  m_status->setText(plan.value("blockers").toArray().isEmpty()
                      ? tr("Review complete. Choose optional mods, or install this exact revision.")
                      : tr("Installation is blocked. Review the compatibility problems above."));
}
void
Clf3CollectionDialog::resume()
{
  if (!InstallationEnabled)
    return;
  m_pages->setCurrentIndex(1);
  if (m_busy || m_controller.isRunning() || !installationAvailable())
    return;
  const auto pending = readJson(pendingPath(), 4096);
  const auto job = pending.value("job").toString();
  const auto saved = readJson(job + "/saved.json");
  if (saved.value("version").toInt() != 1 || saved.value("job").toString() != job ||
      !QDir::isAbsolutePath(job) ||
      Clf3CollectionHost::locator(saved.value("source").toString()).value("revision").toInt() <= 0)
  {
    fail(tr("The saved collection job is invalid."));
    return;
  }
  m_published = false;
  m_add->setEnabled(false);
  m_openButton->setEnabled(false);
  m_rendering = true;
  m_job = job;
  m_identity = saved.value("identity").toString();
  m_packagePath = saved.value("package").toString();
  m_plannedSource = saved.value("source").toString();
  m_locator = saved.value("locator").toObject();
  m_artifacts = saved.value("artifacts").toObject();
  m_plan = saved.value("plan").toObject();
  m_installStarted = saved.value("started").toBool();
  m_source->setText(m_plannedSource);
  m_package->setText(m_packagePath);
  m_game->setText(saved.value("game").toString());
  m_output->setText(saved.value("output").toString());
  m_cache->setText(saved.value("cache").toString());
  m_ini->setText(saved.value("ini").toString());
  m_masterlist->setText(saved.value("masterlist").toString());
  m_rendering = false;
  if (m_plan.isEmpty())
  {
    reviewPlan();
    return;
  }
  showPlan(m_plan);
  m_resumeExpected = m_plan;
  reviewPlan();
}
void
Clf3CollectionDialog::install()
{
  if (!InstallationEnabled)
    return;
  if (m_busy || m_dirty || !m_plan.value("blockers").toArray().isEmpty())
    return;
  for (auto* edit : { m_game, m_cache, m_output })
  {
    if (!QDir::isAbsolutePath(edit->text()) || edit->text().contains("/../"))
    {
      fail(tr("Choose absolute paths without parent traversal."));
      return;
    }
  }
  for (const auto& protectedPath : { m_game->text(), m_cache->text(), m_job })
  {
    if (Clf3InstallUtils::pathsOverlap(m_output->text(), protectedPath))
    {
      fail(tr("The new instance must be separate from the game, cache and job folders."));
      return;
    }
  }
  if (Clf3InstallUtils::pathsOverlap(m_game->text(), m_cache->text()))
  {
    fail(tr("The download cache must be separate from the source game."));
    return;
  }
  if (QFileInfo::exists(m_output->text()) && !m_installStarted)
  {
    fail(tr("The destination already exists. Choose a new folder; existing installations are "
            "preserved."));
    return;
  }
  m_installStarted = true;
  if (!save())
    return;
  m_host.cancel();
  ++m_catalogGeneration;
  setBusy(true);
  if (QFileInfo::exists(m_output->text()))
  {
    runWorker();
    return;
  }
  m_artifactIndex = 0;
  acquireNext();
}
void
Clf3CollectionDialog::acquireNext()
{
  if (!InstallationEnabled)
    return;
  m_currentArtifact = {};
  m_manual->hide();
  m_browser->hide();
  const auto artifacts = m_plan.value("artifacts").toArray();
  while (m_artifactIndex < artifacts.size())
  {
    auto artifact = artifacts[m_artifactIndex].toObject();
    if (artifact.value("source_type") == "bundle")
    {
      ++m_artifactIndex;
      continue;
    }
    m_currentArtifact = artifact;
    QString name = artifact.value("id").toString();
    for (const auto& member : m_plan.value("members").toArray())
    {
      const auto object = member.toObject();
      if (object.value("selected").toBool() && object.value("artifact_id") == artifact.value("id"))
      {
        name = object.value("name").toString();
        break;
      }
    }
    m_status->setText(tr("Acquiring exact archive %1 of %2 · %3")
                        .arg(m_artifactIndex + 1)
                        .arg(artifacts.size())
                        .arg(name));
    const auto retained = m_artifacts.value(artifact.value("id").toString()).toString();
    if (!retained.isEmpty())
    {
      m_host.verify(retained,
                    artifact.value("expected_md5").toString().toLatin1(),
                    artifact.value("expected_size").isDouble()
                      ? artifact.value("expected_size").toVariant().toLongLong()
                      : -1,
                    false,
                    [this](bool valid)
                    {
                      if (valid)
                      {
                        ++m_artifactIndex;
                        acquireNext();
                      }
                      else
                        acquireCurrent();
                    });
    }
    else
      acquireCurrent();
    return;
  }
  m_status->setText(tr("Verifying the pinned LOOT masterlist…"));
  const auto support = m_support.value(m_plan.value("domain").toString());
  const auto target = m_job + "/masterlist.yaml";
  if (!m_masterlist->text().isEmpty())
  {
    // Custom masterlists are pinned to this job on first use, then reverified by CLF3.
    if (!QFileInfo::exists(target) && !QFile::copy(m_masterlist->text(), target))
    {
      fail(tr("Cannot pin the selected masterlist."));
      return;
    }
    runWorker();
  }
  else
    m_host.masterlist(support, target, [this](QString) { runWorker(); });
}
void
Clf3CollectionDialog::acquireCurrent(const QString& nxm)
{
  if (!InstallationEnabled)
    return;
  const auto artifact = m_currentArtifact;
  m_host.artifact(artifact,
                  QUrl(m_sources.value(artifact.value("id").toString()).toString()),
                  m_cache->text(),
                  nxm,
                  [this, artifact](QString path)
                  {
                    CuratedGuideNxmBroker::instance().clearConsumer(m_brokerConsumer);
                    m_artifacts.insert(artifact.value("id").toString(), path);
                    if (!save())
                      return;
                    ++m_artifactIndex;
                    acquireNext();
                  });
}
void
Clf3CollectionDialog::runWorker()
{
  if (!InstallationEnabled)
    return;
  const auto master = m_job + "/masterlist.yaml";
  QFile masterFile(master);
  if (!masterFile.open(QIODevice::ReadOnly))
  {
    fail(tr("The pinned masterlist is missing."));
    return;
  }
  const auto hash = QString::fromLatin1(
    QCryptographicHash::hash(masterFile.readAll(), QCryptographicHash::Sha256).toHex());
  const auto pinPath = m_job + "/masterlist-pin.json";
  const auto pin = readJson(pinPath, 4096);
  if ((!pin.isEmpty() && pin.value("sha256").toString() != hash) ||
      (pin.isEmpty() && !writeJson(pinPath, { { "sha256", hash } })))
  {
    fail(tr("The job's pinned masterlist changed."));
    return;
  }
  if (!writeJson(m_job + "/plan.json", m_plan) ||
      !writeJson(m_job + "/artifacts.json", m_artifacts))
  {
    fail(tr("Cannot save verified worker inputs."));
    return;
  }
  const QString signature = QString::fromLatin1(
    QCryptographicHash::hash(QJsonDocument(m_plan).toJson(QJsonDocument::Compact),
                             QCryptographicHash::Sha256)
      .toHex());
  const QJsonObject request{ { "protocol_version", 1 },
                             { "job_identity", m_identity },
                             { "package", m_packagePath },
                             { "plan", m_job + "/plan.json" },
                             { "artifacts", m_job + "/artifacts.json" },
                             { "stage", m_job + "/stage-" + signature },
                             { "game", m_game->text() },
                             { "output", m_output->text() },
                             { "profile_ini",
                               m_ini->text().isEmpty() ? QJsonValue(QJsonValue::Null)
                                                       : QJsonValue(m_ini->text()) },
                             { "masterlist", master },
                             { "masterlist_sha256", hash } };
  m_workerRequest = request;
  if (usesLocalWorker())
  {
    if (QFileInfo::exists(m_output->text()))
    {
      m_status->setText(tr("Verifying the published installation…"));
      m_progress->setRange(0, 0);
      m_compat.verifyPublication(request, m_support.value(m_plan.value("domain").toString()));
    }
    else
      m_controller.startCollectionLocalInstall(request);
  }
  else
    m_controller.startCollectionInstall(request);
}
void
Clf3CollectionDialog::published(const QString& reportPath)
{
  if (!InstallationEnabled)
    return;
  const auto report = readJson(reportPath, 1024 * 1024);
  if (reportPath != m_output->text() + "/.collection/report.json" ||
      report.value("output").toString() != m_output->text() ||
      report.value("package_sha256") != m_plan.value("package_sha256") ||
      report.value("revision") != m_locator.value("revision") ||
      report.value("installed_members").toInt(-1) !=
        m_plan.value("installation_order").toArray().size() ||
      !QFileInfo::exists(m_output->text() + "/ModOrganizer.ini"))
  {
    fail(tr("The publication report does not match this reviewed job."));
    return;
  }
  m_published = true;
  setBusy(false);
  m_add->setEnabled(true);
  m_openButton->setEnabled(true);
  m_progress->setRange(0, 1);
  m_progress->setValue(1);
  m_status->setText(tr("Published and verified: %1 members, %2 files. Game launch has not been "
                       "tested. Choose Add to Fluorine or Add and Open.")
                      .arg(report.value("installed_members").toInt())
                      .arg(report.value("verified_mod_files").toInt()));
}
void
Clf3CollectionDialog::cancel()
{
  m_compat.cancel();
  m_host.cancel();
  CuratedGuideNxmBroker::instance().clearConsumer(m_brokerConsumer);
  if (m_controller.isRunning())
    m_controller.cancel();
  else
  {
    setBusy(false);
    m_status->setText(tr("Cancelled. Verified archives are retained for resume."));
  }
}
void
Clf3CollectionDialog::done(int result)
{
  if (m_busy || m_controller.isRunning())
  {
    m_deferredClose = result;
    cancel();
    return;
  }
  if (result == QDialog::Accepted && !m_published)
    return;
  QDialog::done(result);
}
