#ifndef DDSPREVIEW_H
#define DDSPREVIEW_H

#include <set>

#include <QString>

#include <uibase/iplugin.h>
#include <uibase/ipluginpreview.h>

class DDSPreview : public MOBase::IPluginPreview
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginPreview)
  Q_PLUGIN_METADATA(IID "org.tannin.DDSPreviewNative")

public:
  DDSPreview();

public:  // IPlugin
  bool init(MOBase::IOrganizer* moInfo) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

public:  // IPluginPreview
  std::set<QString> supportedExtensions() const override;
  QWidget* genFilePreview(const QString& fileName,
                          const QSize& maxSize) const override;
  bool supportsArchives() const override;
  QWidget* genDataPreview(const QByteArray& fileData,
                          const QString& fileName,
                          const QSize& maxSize) const override;

private:
  QWidget* buildPreview(class DDSFile& dds, const QSize& maxSize) const;

  MOBase::IOrganizer* m_organizer = nullptr;
};

#endif  // DDSPREVIEW_H
