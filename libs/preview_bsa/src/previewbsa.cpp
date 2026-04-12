/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Bsa Preview plugin for MO

Bsa Preview plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Bsa Preview plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Bsa Preview plugin.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "previewbsa.h"

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtPlugin>

#include <uibase/filterwidget.h>
#include <uibase/log.h>
#include <uibase/utility.h>

#include "simplefiletreeitem.h"
#include "simplefiletreemodel.h"

#include <uibase/imoinfo.h>

#include <memory>
#include <vector>

using namespace MOBase;

PreviewBsa::PreviewBsa() : m_MOInfo(nullptr) {}

bool PreviewBsa::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;

  auto bsaPreview = std::bind(&PreviewBsa::genBsaPreview, this, std::placeholders::_1,
                              std::placeholders::_2);

  m_PreviewGenerators["bsa"] = bsaPreview;
  m_PreviewGenerators["ba2"] = bsaPreview;

  return true;
}

QString PreviewBsa::name() const
{
  return "Preview Bsa";
}

QString PreviewBsa::localizedName() const
{
  return tr("Preview BSA");
}

QString PreviewBsa::author() const
{
  return "AL12 & MO2 Team";
}

QString PreviewBsa::description() const
{
  return tr("Supports previewing contents of Bethesda Archive files, BSAs and BA2s");
}

MOBase::VersionInfo PreviewBsa::version() const
{
  return VersionInfo(1, 1, 0, VersionInfo::RELEASE_FINAL);
}

QList<MOBase::PluginSetting> PreviewBsa::settings() const
{
  QList<PluginSetting> result;
  result.push_back(PluginSetting(
      "enabled", tr("Enable previewing of BSA file contents"), QVariant(true)));
  return result;
}

std::set<QString> PreviewBsa::supportedExtensions() const
{
  std::set<QString> extensions;
  for (const auto& generator : m_PreviewGenerators) {
    extensions.insert(generator.first);
  }

  return extensions;
}

QWidget* PreviewBsa::genFilePreview(const QString& fileName, const QSize& maxSize) const
{
  auto iter = m_PreviewGenerators.find(QFileInfo(fileName).suffix().toLower());
  if (iter != m_PreviewGenerators.end()) {
    return iter->second(fileName, maxSize);
  } else {
    return nullptr;
  }
}

void PreviewBsa::readFiles(const BSA::Folder::Ptr archiveFolder)
{
  const auto fileCount = archiveFolder->getNumFiles();
  for (unsigned int i = 0; i < fileCount; ++i) {
    const BSA::File::Ptr file = archiveFolder->getFile(i);

    m_Files << QString::fromStdString(file->getFilePath());
  }

  // recurse into subdirectories
  const auto dirCount = archiveFolder->getNumSubFolders();
  for (unsigned int i = 0; i < dirCount; ++i) {
    const BSA::Folder::Ptr folder = archiveFolder->getSubFolder(i);

    readFiles(folder);
  }
}

QWidget* PreviewBsa::genBsaPreview(const QString& fileName, const QSize&)
{
  m_Files.clear();
  QWidget* wrapper    = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout();

  QLabel* infoLabel = new QLabel();

  // Kept alive via shared_ptr captured by the double-click lambda below so
  // we can extract files from the still-open archive on demand without
  // re-reading it each time.
  auto archive = std::make_shared<BSA::Archive>();
  BSA::EErrorCode res =
      archive->read(fileName.toLocal8Bit().constData(), true);
  if ((res != BSA::ERROR_NONE) && (res != BSA::ERROR_INVALIDHASHES)) {
    log::error("invalid bsa '{}', error {}", fileName, res);
    infoLabel->setText("Unable to parse archive. Unrecognized format.");
    archive->close();
    return wrapper;
  }
  const BSA::Folder::Ptr archiveDir = archive->getRoot();
  readFiles(archiveDir);
  QString infoString =
      tr("Archive Format: %1 , Compression: %2 , File count: %3 , Version: %4 , "
         "Archive type: %5 , Archive flags: %6");
  infoString = infoString.arg(getFormat(archive->getType()))
                   .arg((archive->getFlags() & 0x00000004) ? tr("yes") : tr("no"))
                   .arg(m_Files.size())
                   .arg(getVersion(archive->getType()))
                   .arg(archive->getType())
                   .arg("0x" + QString::number(archive->getFlags(), 16));
  infoLabel->setText(infoString);
  layout->addWidget(infoLabel);

  QTreeView* view            = new QTreeView();
  SimpleFileTreeModel* model = new SimpleFileTreeModel(m_Files, view);

  view->setModel(model);
  layout->addWidget(view);

  QLineEdit* lineEdit = new QLineEdit();
  layout->addWidget(lineEdit);

  model->setFilterWidgetEdit(lineEdit);
  model->setFilterWidgetList(view);

  view->setSortingEnabled(true);
  view->sortByColumn(0, Qt::SortOrder::AscendingOrder);

  // Double-click: extract the selected file from the archive and hand the
  // raw bytes to another preview plugin via IOrganizer::previewFileData.
  // The view's model is wrapped in FilterWidgetProxyModel, so walk up via
  // index.parent() + DisplayRole (proxy-transparent) instead of using
  // index.internalPointer() directly.
  IOrganizer* mo = m_MOInfo;
  QObject::connect(
      view, &QTreeView::doubleClicked, view,
      [mo, archive, view](const QModelIndex& index) {
        if (!index.isValid()) return;

        // Ignore folders — only files have no children.
        if (index.model()->rowCount(index) > 0) {
          return;
        }

        QStringList parts;
        QModelIndex cur = index;
        while (cur.isValid()) {
          parts.prepend(cur.data(Qt::DisplayRole).toString());
          cur = cur.parent();
        }
        const QString path = parts.join('/');
        if (path.isEmpty()) return;

        BSA::File::Ptr file = archive->findFile(path.toStdString());
        if (!file) {
          log::warn("preview_bsa: file not found in archive: {}", path);
          return;
        }
        std::vector<unsigned char> data;
        if (archive->extractToMemory(file, data) != BSA::ERROR_NONE) {
          log::warn("preview_bsa: extract failed for: {}", path);
          return;
        }
        QByteArray bytes(reinterpret_cast<const char*>(data.data()),
                         static_cast<int>(data.size()));

        if (!mo->previewFileData(view->window(), path, bytes)) {
          log::warn("preview_bsa: no preview plugin for: {}", path);
        }
      });

  wrapper->setLayout(layout);
  return wrapper;
}

QString PreviewBsa::getFormat(ArchiveType type) const
{
  switch (type) {
  case ArchiveType::TYPE_MORROWIND:
    return "Morrowind";
  case ArchiveType::TYPE_OBLIVION:
    return "Oblivion";
  case ArchiveType::TYPE_FALLOUT3:
    return "Fallout 3, New Vegas, Skyrim LE";
  case ArchiveType::TYPE_SKYRIMSE:
    return "Skyrim Special Edition";
  case ArchiveType::TYPE_FALLOUT4:
    return "Fallout 4";
  case ArchiveType::TYPE_STARFIELD:
    return "Starfield";
  case ArchiveType::TYPE_STARFIELD_LZ4_TEXTURE:
    return "Starfield DDS LZ4";
  case ArchiveType::TYPE_FALLOUT4NG_7:
  case ArchiveType::TYPE_FALLOUT4NG_8:
    return "Fallout 4 NextGen";
  default:
    throw data_invalid_exception(makeString("invalid type %d", type));
  }
}

BSAULong PreviewBsa::getVersion(ArchiveType type) const
{
  switch (type) {
  case TYPE_MORROWIND:
    return 0x100;
  case TYPE_OBLIVION:
    return 0x67;
  case TYPE_FALLOUT3:
    return 0x68;
  case TYPE_SKYRIMSE:
    return 0x69;
  case TYPE_FALLOUT4:
    return 0x01;
  case TYPE_STARFIELD:
    return 0x02;
  case TYPE_STARFIELD_LZ4_TEXTURE:
    return 0x03;
  case TYPE_FALLOUT4NG_7:
    return 0x07;
  case TYPE_FALLOUT4NG_8:
    return 0x08;
  default:
    throw data_invalid_exception(makeString("invalid type %d", type));
  }
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(previewBsa, PreviewBsa)
#endif
