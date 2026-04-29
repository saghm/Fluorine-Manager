#ifndef MODINFODIALOGFWD_H
#define MODINFODIALOGFWD_H

#include "filerenamer.h"
#include <QStyledItemDelegate>

class ModInfo;
using ModInfoPtr = QSharedPointer<ModInfo>;

enum class ModInfoTabIDs
{
  None      = -1,
  TextFiles = 0,
  IniFiles = 1,
  Images = 2,
  Esps = 3,
  Conflicts = 4,
  Categories = 5,
  Nexus = 6,
  Notes = 7,
  Filetree = 8
};

class PluginContainer;

bool canPreviewFile(const PluginContainer& pluginContainer, bool isArchive,
                    const QString& filename);
bool canRunFile(bool isArchive, const QString& filename);
bool canOpenFile(bool isArchive, const QString& filename);
bool canExploreFile(bool isArchive, const QString& filename);
bool canHideFile(bool isArchive, const QString& filename);
bool canUnhideFile(bool isArchive, const QString& filename);

FileRenamer::RenameResults hideFile(FileRenamer& renamer, const QString& oldName);
FileRenamer::RenameResults unhideFile(FileRenamer& renamer, const QString& oldName);
FileRenamer::RenameResults restoreHiddenFilesRecursive(FileRenamer& renamer,
                                                       const QString& targetDir);

class ElideLeftDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

protected:
  void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& i) const override
  {
    QStyledItemDelegate::initStyleOption(o, i);
    o->textElideMode = Qt::ElideLeft;
  }
};

#endif  // MODINFODIALOGFWD_H
