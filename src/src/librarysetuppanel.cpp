#include "librarysetuppanel.h"
#include "settingsnavigation.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QLabel* description(const QString& text, QWidget* parent)
{
  auto* label = new QLabel(text, parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setForegroundRole(QPalette::PlaceholderText);
  return label;
}

QString normalizedPath(const QString& path)
{
  if (path.isEmpty()) {
    return {};
  }
  const QFileInfo file(path);
  const auto canonical = file.canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(file.absoluteFilePath()) : canonical;
}

class ElidedPathLabel : public QLabel
{
public:
  using QLabel::QLabel;
  QSize sizeHint() const override { return {180, fontMetrics().height() + 8}; }
  QSize minimumSizeHint() const override { return {40, fontMetrics().height() + 8}; }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(text(), Qt::ElideMiddle,
                                               contentsRect().width()));
  }
};
} // namespace

class LibraryPathRow : public QWidget
{
public:
  QPushButton* open;

  LibraryPathRow(const QString& name, QWidget* parent) : QWidget(parent)
  {
    setObjectName(name + "Row");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_label = new QLabel(this);
    layout->addWidget(m_label);
    auto* row = new QHBoxLayout;
    m_path = new ElidedPathLabel(this);
    m_path->setObjectName(name);
    m_path->setTextFormat(Qt::PlainText);
    m_path->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row->addWidget(m_path, 1);
    m_copy = new QPushButton(tr("Copy"), this);
    m_copy->setObjectName(name + "Copy");
    m_copy->setAutoDefault(false);
    row->addWidget(m_copy);
    open = new QPushButton(tr("Open"), this);
    open->setObjectName(name + "Open");
    open->setAutoDefault(false);
    row->addWidget(open);
    layout->addLayout(row);
    connect(m_copy, &QPushButton::clicked, this, [this] {
      QApplication::clipboard()->setText(m_path->text());
    });
  }

  void setPath(const QString& label, const QString& path)
  {
    m_label->setText(label);
    m_path->setText(path.isEmpty() ? tr("Not configured") : QDir::toNativeSeparators(path));
    m_path->setToolTip(m_path->text());
    m_path->setAccessibleName(label);
    m_copy->setEnabled(!path.isEmpty());
    open->setEnabled(!path.isEmpty());
    m_copy->setAccessibleName(tr("Copy %1").arg(label.toLower()));
    open->setAccessibleName(tr("Open %1").arg(label.toLower()));
    m_copy->setToolTip(tr("Copy the full path"));
    open->setToolTip(tr("Open in the file manager"));
  }

private:
  QLabel* m_label;
  ElidedPathLabel* m_path;
  QPushButton* m_copy;
};

class LibraryCover : public QWidget
{
public:
  QPixmap cover;
  QIcon fallback;

  explicit LibraryCover(QWidget* parent) : QWidget(parent)
  {
    setObjectName("setupCover");
    setFixedSize(120, 180);
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect(), 6, 6);
    painter.setClipPath(clip);
    painter.fillRect(rect(), palette().brush(QPalette::AlternateBase));
    if (!cover.isNull()) {
      const auto size = cover.size().scaled(this->size(), Qt::KeepAspectRatioByExpanding);
      const QRect target(QPoint((width() - size.width()) / 2,
                                 (height() - size.height()) / 2), size);
      painter.drawPixmap(target, cover);
    } else {
      fallback.paint(&painter, QRect(rect().center() - QPoint(32, 32), QSize(64, 64)));
    }
  }
};

LibrarySetupPanel::LibrarySetupPanel(QWidget* parent) : QScrollArea(parent)
{
  setFrameShape(QFrame::NoFrame);
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* container = new QWidget(this);
  auto* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(12, 0, 4, 0);
  m_empty = description(tr("Select a setup to see its details."), container);
  m_empty->setObjectName("emptySetupDetails");
  containerLayout->addWidget(m_empty);
  m_body = new QWidget(container);
  auto* layout = new QVBoxLayout(m_body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);
  auto* hero = new QHBoxLayout;
  hero->setSpacing(18);
  m_cover = new LibraryCover(m_body);
  hero->addWidget(m_cover, 0, Qt::AlignTop);
  auto* heading = new QVBoxLayout;
  heading->setSpacing(8);
  m_current = new QLabel(tr("Current"), m_body);
  m_current->setObjectName("currentSetupBadge");
  m_current->setStyleSheet("QLabel#currentSetupBadge { background: palette(highlight); "
                          "color: palette(highlighted-text); border-radius: 4px; "
                          "padding: 3px 8px; }");
  heading->addWidget(m_current, 0, Qt::AlignLeft);
  m_title = new QLabel(m_body);
  m_title->setObjectName("setupTitle");
  m_title->setTextFormat(Qt::PlainText);
  m_title->setWordWrap(true);
  m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto titleFont = font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
  titleFont.setBold(true);
  m_title->setFont(titleFont);
  heading->addWidget(m_title);
  m_game = description({}, m_body);
  m_game->setObjectName("setupGameName");
  heading->addWidget(m_game);
  m_rename = new QPushButton(tr("Rename…"), m_body);
  m_rename->setObjectName("renameSetup");
  m_rename->setAutoDefault(false);
  heading->addWidget(m_rename, 0, Qt::AlignLeft);
  m_renameHint = description(tr("Open another setup before renaming this one."), m_body);
  m_renameHint->setObjectName("renameSetupHint");
  heading->addWidget(m_renameHint);
  heading->addStretch();
  hero->addLayout(heading, 1);
  layout->addLayout(hero);

  auto* paths = new QWidget(m_body);
  paths->setObjectName("libraryPaths");
  auto* pathsLayout = new QVBoxLayout(paths);
  pathsLayout->setContentsMargins(8, 8, 8, 8);
  pathsLayout->setSpacing(12);
  m_setupPath = new LibraryPathRow("setupPath", paths);
  m_dataPath = new LibraryPathRow("dataPath", paths);
  m_gamePath = new LibraryPathRow("gamePath", paths);
  pathsLayout->addWidget(m_setupPath);
  pathsLayout->addWidget(m_dataPath);
  pathsLayout->addWidget(m_gamePath);
  layout->addWidget(new SettingsFoldout(tr("Paths"), paths, m_body));

  auto* launch = new QWidget(m_body);
  launch->setObjectName("libraryLaunchOptions");
  auto* launchLayout = new QVBoxLayout(launch);
  launchLayout->setContentsMargins(8, 8, 8, 8);
  launchLayout->setSpacing(8);
  m_steamDrm = new QCheckBox(tr("Steam DRM integration"), launch);
  m_steamDrm->setObjectName("steamDrmCheckBox");
  launchLayout->addWidget(m_steamDrm);
  launchLayout->addWidget(description(
      tr("Use Steam authentication for games that require it. Turn off for GOG "
         "or DRM-free copies."), launch));
  launchLayout->addSpacing(6);
  m_rootBuilder = new QCheckBox(tr("Enable VFS Root Builder"), launch);
  m_rootBuilder->setObjectName("vfsRootBuilderCheckBox");
  launchLayout->addWidget(m_rootBuilder);
  launchLayout->addWidget(description(
      tr("Deploy files from mods’ Root folders to the game folder when launching. "
         "Turn off if you use a separate Root Builder plugin."), launch));
  launchLayout->addSpacing(6);
  launchLayout->addWidget(description(tr("Changes apply immediately to this setup."), launch));
  layout->addWidget(new SettingsFoldout(tr("Launch options"), launch, m_body));
  m_more = new QToolButton(m_body);
  m_more->setObjectName("moreActions");
  m_more->setText(tr("More actions"));
  m_more->setPopupMode(QToolButton::InstantPopup);
  m_more->setToolButtonStyle(Qt::ToolButtonTextOnly);
  layout->addWidget(m_more, 0, Qt::AlignLeft);
  containerLayout->addWidget(m_body);
  containerLayout->addStretch();
  setWidget(container);

  connect(m_rename, &QPushButton::clicked, this, &LibrarySetupPanel::renameRequested);
  connect(m_setupPath->open, &QPushButton::clicked, this, &LibrarySetupPanel::openSetupFolder);
  connect(m_dataPath->open, &QPushButton::clicked, this, &LibrarySetupPanel::openDataFolder);
  connect(m_gamePath->open, &QPushButton::clicked, this, &LibrarySetupPanel::openGameFolder);
  connect(&m_artwork, &GameArtwork::coverReady, this,
          [this](const QString& key, const QPixmap& cover) {
            if (key == m_artworkKey) {
              m_cover->cover = cover;
              m_cover->update();
            }
          });
  clear();
}

void LibrarySetupPanel::setSetup(const LibrarySetupInfo& setup)
{
  m_empty->hide();
  m_body->show();
  m_title->setText(setup.name);
  m_game->setText(setup.gameName);
  m_current->setVisible(setup.current);
  m_rename->setEnabled(!setup.current);
  m_renameHint->setVisible(setup.current);
  m_rename->setToolTip(setup.current ? m_renameHint->text() : tr("Rename this setup"));
  const bool sharedPath = !setup.setupPath.isEmpty() &&
                         normalizedPath(setup.setupPath) == normalizedPath(setup.dataPath);
  m_setupPath->setPath(sharedPath ? tr("Setup and data folder") : tr("Setup folder"),
                       setup.setupPath);
  m_dataPath->setPath(tr("Data folder"), setup.dataPath);
  m_dataPath->setVisible(!sharedPath);
  m_gamePath->setPath(tr("Game folder"), setup.gamePath);
  const QSignalBlocker steamBlocker(m_steamDrm);
  const QSignalBlocker rootBlocker(m_rootBuilder);
  m_steamDrm->setChecked(setup.steamDrm);
  m_rootBuilder->setChecked(setup.rootBuilder);
  m_artworkKey = m_artwork.identify(setup.gameShortName, setup.gameName, setup.steamId);
  m_cover->fallback = setup.icon;
  m_cover->setAccessibleName(tr("%1 cover").arg(setup.gameName));
  m_cover->cover = m_artworkKey.isEmpty() ? QPixmap{} : m_artwork.localCover(m_artworkKey);
  m_cover->update();
  m_artwork.request(m_artworkKey);
}

void LibrarySetupPanel::clear()
{
  m_artworkKey.clear();
  m_cover->cover = {};
  m_body->hide();
  m_empty->show();
}
