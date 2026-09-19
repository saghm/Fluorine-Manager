#include "gamelibrarywidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTextLayout>

namespace
{
class GameCardDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex&) const override
  {
    return {180, 268 + 3 * QFontMetrics(option.font).height()};
  }

  void paint(QPainter* p, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect card = option.rect.adjusted(3, 3, -3, -3);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const auto& palette = option.palette;
    const QColor foreground = palette.color(QPalette::Text);
    p->setBrush(palette.color(hovered ? QPalette::AlternateBase : QPalette::Base));
    p->setPen(QPen(palette.color(selected ? QPalette::Highlight : QPalette::Mid),
                   selected ? 2 : 1));
    p->drawRoundedRect(card, 7, 7);

    const QRect art(card.left() + 7, card.top() + 7, card.width() - 14, 240);
    QPainterPath clip;
    clip.addRoundedRect(art, 4, 4);
    p->save();
    p->setClipPath(clip);
    const auto cover = qvariant_cast<QPixmap>(index.data(GameLibraryWidget::CoverRole));
    if (!cover.isNull()) {
      // Fit the cover without stretching it; the clip trims only excess edges.
      const QSize fit = cover.size().scaled(art.size(), Qt::KeepAspectRatioByExpanding);
      const QRect target(art.center().x() - fit.width() / 2,
                         art.center().y() - fit.height() / 2, fit.width(), fit.height());
      p->drawPixmap(target, cover);
    } else {
      QLinearGradient gradient(art.topLeft(), art.bottomRight());
      gradient.setColorAt(0, palette.color(QPalette::AlternateBase));
      gradient.setColorAt(1, palette.color(QPalette::Button));
      p->fillRect(art, gradient);
      const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
      icon.paint(p, QRect(art.center() - QPoint(32, 32), QSize(64, 64)));
    }
    p->restore();

    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics fm(titleFont);
    const QRect title(art.left(), art.bottom() + 10, art.width(), 2 * fm.height());
    p->setPen(foreground);
    p->setFont(titleFont);
    QTextLayout layout(index.data(Qt::DisplayRole).toString(), titleFont);
    layout.beginLayout();
    for (int lineNumber = 0; lineNumber < 2; ++lineNumber) {
      auto line = layout.createLine();
      if (!line.isValid()) {
        break;
      }
      line.setLineWidth(title.width());
      line.setPosition(QPointF(0, lineNumber * fm.height()));
      if (lineNumber == 1) {
        const QString rest = layout.text().mid(line.textStart());
        p->drawText(title.left(), title.top() + fm.height() + fm.ascent(),
                    fm.elidedText(rest, Qt::ElideRight, title.width()));
      } else {
        line.draw(p, title.topLeft());
      }
    }
    layout.endLayout();

    p->setFont(option.font);
    QColor muted = foreground;
    muted.setAlpha(180);
    p->setPen(muted);
    const bool installed = index.data(GameLibraryWidget::InstalledRole).toBool();
    p->drawText(QRect(art.left(), title.bottom() + 5, art.width(), fm.height()),
                Qt::AlignLeft | Qt::AlignVCenter,
                installed ? tr("Installed") : tr("Locate installation"));
    if (option.state.testFlag(QStyle::State_HasFocus)) {
      p->setPen(QPen(palette.color(QPalette::Highlight), 1, Qt::DotLine));
      p->setBrush(Qt::NoBrush);
      p->drawRoundedRect(card.adjusted(3, 3, -3, -3), 5, 5);
    }
    p->restore();
  }
};
}  // namespace

GameLibraryWidget::GameLibraryWidget(QWidget* parent) : QListWidget(parent)
{
  connect(&m_artwork, &GameArtwork::coverReady, this, &GameLibraryWidget::applyCover);
  setViewMode(QListView::IconMode);
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setMouseTracking(true);
  setSpacing(5);
  setUniformItemSizes(true);
  setItemDelegate(new GameCardDelegate(this));
  setAccessibleName(tr("Supported game library"));
}

QListWidgetItem* GameLibraryWidget::addGame(const QString& title, const QString& steamId,
                                           const QIcon& fallback, bool installed,
                                           const QString& gameShortName,
                                           const QString& gameName)
{
  auto* item = new QListWidgetItem(fallback, title, this);
  const QString key = m_artwork.identify(gameShortName,
      gameName.isEmpty() ? title : gameName, steamId);
  item->setData(ArtworkKeyRole, key);
  if (key.startsWith("steam-")) {
    item->setData(SteamIdRole, key.mid(6));
  }
  item->setData(InstalledRole, installed);
  return item;
}

void GameLibraryWidget::applyCover(const QString& id, const QPixmap& cover)
{
  for (int row = 0; row < count(); ++row) {
    if (item(row)->data(ArtworkKeyRole).toString() == id) {
      item(row)->setData(CoverRole, cover);
    }
  }
}

void GameLibraryWidget::showEvent(QShowEvent* event)
{
  QListWidget::showEvent(event);
  m_artwork.setDownloadsEnabled(true);
  refreshArtwork();
}

void GameLibraryWidget::hideEvent(QHideEvent* event)
{
  m_artwork.setDownloadsEnabled(false);
  QListWidget::hideEvent(event);
}

void GameLibraryWidget::refreshArtwork()
{
  // Creating the wizard must not download the whole supported-games catalog.
  if (!isVisible()) {
    return;
  }
  for (int row = 0; row < count(); ++row) {
    auto* game = item(row);
    const QString id = game->data(ArtworkKeyRole).toString();
    if (!game->isHidden()) {
      m_artwork.request(id);
    }
  }
}
