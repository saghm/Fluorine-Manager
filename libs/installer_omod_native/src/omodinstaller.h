#ifndef OMODINSTALLER_H
#define OMODINSTALLER_H

#include <uibase/iplugininstallercustom.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>
#include <vector>

class OmodInstaller : public MOBase::IPluginInstallerCustom
{
  Q_OBJECT
  Q_INTERFACES(MOBase::IPlugin MOBase::IPluginInstaller MOBase::IPluginInstallerCustom)
  Q_PLUGIN_METADATA(IID "com.tannin.ModOrganizer.PluginInstallerCustom/1.0")

public:
  OmodInstaller();

  // IPlugin
  bool init(MOBase::IOrganizer* organizer) override;
  QString name() const override;
  QString localizedName() const override;
  QString author() const override;
  QString description() const override;
  MOBase::VersionInfo version() const override;
  QList<MOBase::PluginSetting> settings() const override;

  // IPluginInstaller
  unsigned int priority() const override;
  bool isManualInstaller() const override;
  bool isArchiveSupported(std::shared_ptr<const MOBase::IFileTree> tree) const override;

  // IPluginInstallerCustom
  bool isArchiveSupported(const QString& archiveName) const override;
  std::set<QString> supportedExtensions() const override;
  EInstallResult install(MOBase::GuessedValue<QString>& modName, QString gameName,
                         const QString& archiveName, const QString& version,
                         int nexusID) override;

private:
  MOBase::IOrganizer* m_organizer = nullptr;

  // OMOD config parsed from the binary "config" entry.
  struct OmodConfig
  {
    uint8_t fileVersion = 0;
    QString modName;
    int32_t major = 0;
    int32_t minor = 0;
    QString authorName;
    QString email;
    QString website;
    QString desc;
    uint8_t compressionType = 0;  // 0 = deflate, 1 = lzma
  };

  // Entry from data.crc / plugins.crc.
  struct CrcEntry
  {
    QString path;
    uint32_t crc = 0;
    int64_t size = 0;
  };

  // Binary reader helper over a QByteArray.
  class BinaryReader
  {
  public:
    explicit BinaryReader(const QByteArray& data);

    uint8_t readByte();
    int32_t readInt32LE();
    uint32_t readUInt32LE();
    int64_t readInt64LE();
    void skip(int bytes);
    int read7BitEncodedInt();
    QString readNetString();
    QByteArray readBytes(int count);
    bool atEnd() const;

  private:
    const QByteArray& m_data;
    int m_pos = 0;
  };

  OmodConfig parseConfig(const QByteArray& data);
  std::vector<CrcEntry> parseCrcFile(const QByteArray& data);
  QByteArray decompressStream(const QByteArray& data, uint8_t compressionType);

  EInstallResult doInstall(MOBase::GuessedValue<QString>& modName,
                           const QString& archiveName);

  void extractStream(const QString& omodDir, const QString& streamName,
                     const QString& crcName, uint8_t compressionType,
                     const QString& outDir);
};

#endif  // OMODINSTALLER_H
