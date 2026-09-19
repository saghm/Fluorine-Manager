#pragma once

#include "gameartwork.h"
#include <QIcon>
#include <QScrollArea>

class QLabel;
class QPushButton;
class QCheckBox;
class QToolButton;
class LibraryPathRow;
class LibraryCover;

struct LibrarySetupInfo
{
  QString name, gameName, setupPath, dataPath, gamePath;
  QString gameShortName, steamId;
  QIcon icon;
  bool current = false;
  bool steamDrm = false;
  bool rootBuilder = false;
};

class LibrarySetupPanel : public QScrollArea
{
  Q_OBJECT
public:
  explicit LibrarySetupPanel(QWidget* parent = nullptr);
  void setSetup(const LibrarySetupInfo& setup);
  void clear();
  QToolButton* moreActions() const { return m_more; }
  QCheckBox* steamDrmCheckBox() const { return m_steamDrm; }
  QCheckBox* rootBuilderCheckBox() const { return m_rootBuilder; }

signals:
  void renameRequested();
  void openSetupFolder();
  void openDataFolder();
  void openGameFolder();

private:
  QWidget* m_body;
  QLabel* m_empty;
  QLabel* m_title;
  QLabel* m_game;
  QLabel* m_current;
  QLabel* m_renameHint;
  QPushButton* m_rename;
  LibraryCover* m_cover;
  LibraryPathRow* m_setupPath;
  LibraryPathRow* m_dataPath;
  LibraryPathRow* m_gamePath;
  QCheckBox* m_steamDrm;
  QCheckBox* m_rootBuilder;
  QToolButton* m_more;
  GameArtwork m_artwork;
  QString m_artworkKey;
};
