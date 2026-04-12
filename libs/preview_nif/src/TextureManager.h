#pragma once

#include <imoinfo.h>
#include <gli/gli.hpp>
#include <QOpenGLTexture>
#include <QSet>
#include <QStringList>
#include <map>

class TextureManager
{
public:
    TextureManager(MOBase::IOrganizer* organizer);
    ~TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager(TextureManager&&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    TextureManager& operator=(TextureManager&&) = delete;

    void cleanup();

    QOpenGLTexture* getTexture(const std::string& texturePath);
    QOpenGLTexture* getTexture(QString texturePath);

    QOpenGLTexture* getErrorTexture();
    QOpenGLTexture* getBlackTexture();
    QOpenGLTexture* getWhiteTexture();
    QOpenGLTexture* getFlatNormalTexture();

private:
    QOpenGLTexture* loadTexture(QString texturePath);
    QOpenGLTexture* makeTexture(const gli::texture& texture);
    QOpenGLTexture* makeSolidColor(QVector4D color);

    QString resolvePath(const MOBase::IPluginGame* game, QString path);

    // Build priority-sorted BSA candidate list. Shared process-wide, keyed by
    // the current instance's profile path so switching instances rebuilds.
    // Conflict semantics: only active mods contribute, highest profile
    // priority first, then vanilla archives. No plugin-attachment filter —
    // matches BodySlide's "load all BSAs" approach which is what MO2's
    // virtual data tree effectively does.
    const QStringList& bsaCandidates();
    void rebuildBsaCandidates();

    MOBase::IOrganizer* m_MOInfo;
    QOpenGLTexture* m_ErrorTexture = nullptr;
    QOpenGLTexture* m_BlackTexture = nullptr;
    QOpenGLTexture* m_WhiteTexture = nullptr;
    QOpenGLTexture* m_FlatNormalTexture = nullptr;

    std::map<std::wstring, QOpenGLTexture*> m_Textures;
};
