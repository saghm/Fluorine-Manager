#include "ddspreview.h"
#include "ddsfile.h"
#include "ddswidget.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

using namespace MOBase;

DDSPreview::DDSPreview() {}

bool DDSPreview::init(IOrganizer* moInfo)
{
  m_organizer = moInfo;
  return true;
}

QString DDSPreview::name() const
{
  return "DDS Preview (Native)";
}

QString DDSPreview::localizedName() const
{
  return tr("DDS Preview (Native)");
}

QString DDSPreview::author() const
{
  return "AnyOldName3";
}

QString DDSPreview::description() const
{
  return tr("Displays DDS texture files using OpenGL.");
}

VersionInfo DDSPreview::version() const
{
  return VersionInfo(1, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> DDSPreview::settings() const
{
  return {};
}

std::set<QString> DDSPreview::supportedExtensions() const
{
  return {"dds"};
}

bool DDSPreview::supportsArchives() const
{
  return true;
}

QWidget* DDSPreview::genFilePreview(const QString& fileName,
                                    const QSize& maxSize) const
{
  DDSFile dds;
  if (!dds.loadFromFile(fileName)) {
    QLabel* label = new QLabel(tr("Failed to load DDS file."));
    label->setAlignment(Qt::AlignCenter);
    return label;
  }
  return buildPreview(dds, maxSize);
}

QWidget* DDSPreview::genDataPreview(const QByteArray& fileData,
                                    const QString& fileName,
                                    const QSize& maxSize) const
{
  DDSFile dds;
  if (!dds.loadFromData(fileData)) {
    QLabel* label = new QLabel(tr("Failed to load DDS data."));
    label->setAlignment(Qt::AlignCenter);
    return label;
  }
  return buildPreview(dds, maxSize);
}

QWidget* DDSPreview::buildPreview(DDSFile& dds, const QSize& maxSize) const
{
  QWidget* container    = new QWidget();
  QVBoxLayout* layout   = new QVBoxLayout(container);

  // Description label
  QLabel* descLabel = new QLabel(dds.description());
  descLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(descLabel);

  // OpenGL preview widget — we need to keep the DDSFile alive, so store
  // it in the container. We use a shared_ptr stored as a property.
  auto* ddsPtr = new DDSFile(std::move(dds));
  DDSWidget* widget = new DDSWidget(*ddsPtr, container);

  // Clean up DDSFile when container is destroyed
  QObject::connect(container, &QObject::destroyed, [ddsPtr]() {
    delete ddsPtr;
  });

  // Size the widget proportionally
  int previewW = maxSize.width();
  int previewH = maxSize.height() - 30;  // leave room for label
  if (ddsPtr->width() > 0 && ddsPtr->height() > 0) {
    float texAspect =
        static_cast<float>(ddsPtr->width()) / ddsPtr->height();
    float availAspect =
        static_cast<float>(previewW) / std::max(1, previewH);
    if (texAspect > availAspect) {
      previewH = static_cast<int>(previewW / texAspect);
    } else {
      previewW = static_cast<int>(previewH * texAspect);
    }
  }
  widget->setMinimumSize(std::min(previewW, static_cast<int>(ddsPtr->width())),
                         std::min(previewH, static_cast<int>(ddsPtr->height())));
  widget->setMaximumSize(previewW, previewH);

  layout->addWidget(widget);
  container->setLayout(layout);

  return container;
}
