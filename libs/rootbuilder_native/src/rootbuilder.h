#ifndef ROOTBUILDER_NATIVE_H
#define ROOTBUILDER_NATIVE_H

#include <uibase/iplugintool.h>

#include <QJsonObject>
#include <QString>

class RootBuilderNative : public MOBase::IPluginTool
{
    Q_OBJECT
    Q_INTERFACES(MOBase::IPlugin MOBase::IPluginTool)
    Q_PLUGIN_METADATA(IID "com.tannin.ModOrganizer.PluginTool/1.0")

public:
    RootBuilderNative();

    // IPlugin
    bool init(MOBase::IOrganizer* organizer) override;
    QString name() const override;
    QString localizedName() const override;
    QString author() const override;
    QString description() const override;
    MOBase::VersionInfo version() const override;
    QList<MOBase::PluginSetting> settings() const override;
    bool enabledByDefault() const override;

    // IPluginTool
    QString displayName() const override;
    QString tooltip() const override;
    QIcon icon() const override;

public slots:
    void display() const override;

private:
    // Storage paths
    QString storageDir() const;
    QString backupDir() const;
    QString manifestPath() const;
    QString settingsPath() const;

    // Settings (own JSON, not pluginSetting)
    QJsonObject loadSettings() const;
    void saveSettings(const QJsonObject& settings) const;
    bool isAutoEnabled() const;

    // Manifest
    QJsonObject loadManifest() const;
    void saveManifest(const QJsonObject& manifest) const;
    void removeManifest() const;

    // Legacy migration
    void migrateLegacy() const;

    // Third-party conflict detection
    void checkThirdPartyRootBuilder() const;

    // Build / Clear
    int build() const;
    int clear() const;

    // File operations
    static QString findRootDir(const QString& modPath);
    static void reflinkCopy(const QString& src, const QString& dst);
    static void ensureReadable(const QString& path);
    static void ensureWritable(const QString& path);
    static bool forceRemove(const QString& path);
    static void cleanupEmptyDirs(const QString& baseDir,
                                 const QStringList& paths);

    // Hooks
    bool onAboutToRun(const QString& executable);
    void onFinishedRun(const QString& executable, unsigned int exitCode);

    MOBase::IOrganizer* m_organizer = nullptr;
};

#endif // ROOTBUILDER_NATIVE_H
