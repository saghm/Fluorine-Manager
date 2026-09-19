#pragma once

#include "gameartwork.h"
#include <QListWidget>

// A keyboard-accessible game shelf. Artwork is cosmetic: detection and setup
// never depend on the network or on an image being available.
class GameLibraryWidget : public QListWidget
{
public:
  enum Role { InstalledRole = Qt::UserRole + 1, SteamIdRole, CoverRole, ArtworkKeyRole };

  explicit GameLibraryWidget(QWidget* parent = nullptr);
  QListWidgetItem* addGame(const QString& title, const QString& steamId,
                          const QIcon& fallback, bool installed,
                          const QString& gameShortName = {},
                          const QString& gameName = {});
  void refreshArtwork();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  GameArtwork m_artwork;
  void applyCover(const QString& artworkKey, const QPixmap& cover);
};
