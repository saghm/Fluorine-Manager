#include "TextureManager.h"
#include "PreviewNif.h"

#include <dataarchives.h>
#include <igamefeatures.h>
#include <imodinterface.h>
#include <imodlist.h>
#include <imoinfo.h>
#include <ipluginlist.h>
#include <iplugingame.h>

#include <gli/gli.hpp>
#include <libbsarch.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector4D>

#include <exception>
#include <memory>

TextureManager::TextureManager(MOBase::IOrganizer* moInfo) : m_MOInfo{moInfo} {}

void TextureManager::cleanup()
{
    for (auto it = m_Textures.cbegin(); it != m_Textures.cend();) {
        auto texture = it->second;
        m_Textures.erase(it++);
        delete texture;
    }

    if (m_ErrorTexture) {
        delete m_ErrorTexture;
        m_ErrorTexture = nullptr;
    }

    if (m_BlackTexture) {
        delete m_BlackTexture;
        m_BlackTexture = nullptr;
    }

    if (m_WhiteTexture) {
        delete m_WhiteTexture;
        m_WhiteTexture = nullptr;
    }

    if (m_FlatNormalTexture) {
        delete m_FlatNormalTexture;
        m_FlatNormalTexture = nullptr;
    }
}

QOpenGLTexture* TextureManager::getTexture(const std::string& texturePath)
{
    return getTexture(QString::fromStdString(texturePath));
}

QOpenGLTexture* TextureManager::getTexture(QString texturePath)
{
    if (texturePath.isEmpty()) {
        return nullptr;
    }

    auto key = texturePath.toLower().toStdWString();

    auto cached = m_Textures.find(key);
    if (cached != m_Textures.end()) {
        return cached->second;
    }

    auto texture = loadTexture(texturePath);

    m_Textures[key] = texture;
    return texture;
}

QOpenGLTexture* TextureManager::getErrorTexture()
{
    if (!m_ErrorTexture) {
        m_ErrorTexture = makeSolidColor({1.0f, 0.0f, 1.0f, 1.0f});
    }

    return m_ErrorTexture;
}

QOpenGLTexture* TextureManager::getBlackTexture()
{
    if (!m_BlackTexture) {
        m_BlackTexture = makeSolidColor({0.0f, 0.0f, 0.0f, 1.0f});
    }

    return m_BlackTexture;
}

QOpenGLTexture* TextureManager::getWhiteTexture()
{
    if (!m_WhiteTexture) {
        m_WhiteTexture = makeSolidColor({1.0f, 1.0f, 1.0f, 1.0f});
    }

    return m_WhiteTexture;
}

QOpenGLTexture* TextureManager::getFlatNormalTexture()
{
    if (!m_FlatNormalTexture) {
        m_FlatNormalTexture = makeSolidColor({0.5f, 0.5f, 1.0f, 1.0f});
    }

    return m_FlatNormalTexture;
}

QOpenGLTexture* TextureManager::loadTexture(QString texturePath)
{
    if (texturePath.isEmpty()) {
        return nullptr;
    }

    auto game = m_MOInfo->managedGame();

    if (!game) {
        return nullptr;
    }

    // 1) Loose file via MO2's virtual tree (honors mod priority + Overwrite).
    auto realPath = resolvePath(game, texturePath);
    if (!realPath.isEmpty()) {
        return makeTexture(gli::load(realPath.toStdString()));
    }

    // 2) BSA fallback. Walk cached priority-sorted candidate list.
    auto tryExtract = [&](const QString& bsaPath) -> QOpenGLTexture* {
        using bsa_ptr = std::unique_ptr<void, decltype(&bsa_free)>;
        auto bsa      = bsa_ptr(bsa_create(), bsa_free);

        const std::wstring bsaPathW     = bsaPath.toStdWString();
        const std::wstring texturePathW = texturePath.toStdWString();

        bsa_result_message_t result =
            bsa_load_from_file(bsa.get(), bsaPathW.c_str());
        if (result.code == BSA_RESULT_EXCEPTION) {
            return nullptr;
        }

        auto result_buffer =
            bsa_extract_file_data_by_filename(bsa.get(), texturePathW.c_str());
        if (result_buffer.message.code == BSA_RESULT_EXCEPTION ||
            !result_buffer.buffer.data) {
            return nullptr;
        }

        auto buffer_free = [&bsa](bsa_result_buffer_t* buffer) {
            bsa_file_data_free(bsa.get(), *buffer);
        };
        using buffer_ptr =
            std::unique_ptr<bsa_result_buffer_t, decltype(buffer_free)>;
        auto buffer = buffer_ptr(&result_buffer.buffer, buffer_free);

        return makeTexture(
            gli::load(static_cast<char*>(buffer->data), buffer->size));
    };

    for (const QString& bsaPath : bsaCandidates()) {
        if (auto* tex = tryExtract(bsaPath)) {
            return tex;
        }
    }

    return nullptr;
}

// Process-wide BSA candidate cache. Keyed by profile directory path so
// switching instances invalidates automatically. Built once, reused across
// every NIF preview until the profile changes.
namespace {
    QString g_cachedProfileKey;
    QStringList g_cachedBsaCandidates;
}

const QStringList& TextureManager::bsaCandidates()
{
    QString profileKey;
    if (auto profile = m_MOInfo->profile()) {
        profileKey = profile->absolutePath();
    }

    if (profileKey == g_cachedProfileKey && !g_cachedBsaCandidates.isEmpty()) {
        return g_cachedBsaCandidates;
    }

    g_cachedBsaCandidates.clear();
    rebuildBsaCandidates();
    g_cachedProfileKey = profileKey;
    return g_cachedBsaCandidates;
}

void TextureManager::rebuildBsaCandidates()
{
    QSet<QString> seen;

    auto addCandidate = [&](const QString& p) {
        QString norm = QDir::cleanPath(p);
        if (!norm.isEmpty() && !seen.contains(norm)) {
            seen.insert(norm);
            g_cachedBsaCandidates.append(norm);
        }
    };

    try {
        auto game = m_MOInfo->managedGame();

        if (auto* modList = m_MOInfo->modList()) {
            auto profile = m_MOInfo->profile();
            QStringList modNames =
                modList->allModsByProfilePriority(profile.get());

            for (auto it = modNames.rbegin(); it != modNames.rend(); ++it) {
                if (!(modList->state(*it) & MOBase::IModList::STATE_ACTIVE))
                    continue;

                MOBase::IModInterface* mod = modList->getMod(*it);
                if (!mod) continue;

                QDir modDir(mod->absolutePath());
                if (!modDir.exists()) continue;

                const QStringList bsas =
                    modDir.entryList(QStringList{"*.bsa", "*.ba2"},
                                     QDir::Files | QDir::NoDotAndDotDot);
                for (const QString& name : bsas) {
                    addCandidate(modDir.absoluteFilePath(name));
                }
            }
        }

        // Vanilla/game archives from DataArchives ini list.
        if (auto* features = m_MOInfo->gameFeatures()) {
            if (auto gameArchives =
                    features->gameFeature<MOBase::DataArchives>()) {
                auto profile  = m_MOInfo->profile();
                auto archives = gameArchives->archives(profile.get());
                for (auto it = archives.rbegin(); it != archives.rend(); ++it) {
                    QString resolved = resolvePath(game, *it);
                    if (!resolved.isEmpty()) addCandidate(resolved);
                }
            }
        }
    } catch (...) {
    }
}


QOpenGLTexture* TextureManager::makeTexture(const gli::texture& texture)
{
    if (texture.empty()) {
        return nullptr;
    }

    gli::gl GL(gli::gl::PROFILE_GL32);
    const gli::gl::format format = GL.translate(texture.format(), texture.swizzles());
    GLenum target                = GL.translate(texture.target());

    auto f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
        QOpenGLContext::currentContext());
    QOpenGLTexture* glTexture =
        new QOpenGLTexture(static_cast<QOpenGLTexture::Target>(target));

    glTexture->create();
    glTexture->bind();
    glTexture->setMipLevels(texture.levels());
    glTexture->setMipBaseLevel(0);
    glTexture->setMipMaxLevel(texture.levels() - 1);
    glTexture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,
                                QOpenGLTexture::Linear);
    glTexture->setSwizzleMask(
        static_cast<QOpenGLTexture::SwizzleValue>(format.Swizzles[0]),
        static_cast<QOpenGLTexture::SwizzleValue>(format.Swizzles[1]),
        static_cast<QOpenGLTexture::SwizzleValue>(format.Swizzles[2]),
        static_cast<QOpenGLTexture::SwizzleValue>(format.Swizzles[3]));

    glTexture->setWrapMode(QOpenGLTexture::Repeat);

    auto extent             = texture.extent();
    const GLsizei faceTotal = texture.layers() * texture.faces();

    glTexture->setSize(extent.x, extent.y, extent.z);
    glTexture->setFormat(static_cast<QOpenGLTexture::TextureFormat>(format.Internal));
    glTexture->allocateStorage(
        static_cast<QOpenGLTexture::PixelFormat>(format.External),
        static_cast<QOpenGLTexture::PixelType>(format.Type));

    for (std::size_t layer = 0; layer < texture.layers(); layer++)
        for (std::size_t face = 0; face < texture.faces(); face++)
            for (std::size_t level = 0; level < texture.levels(); level++) {
                auto extent = texture.extent(level);

                target =
                    gli::is_target_cube(texture.target())
                        ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                        : target;

                // Qt's upload functions lag badly so we just use the GL API
                switch (texture.target()) {
                case gli::TARGET_1D:
                    if (gli::is_compressed(texture.format())) {
                        f->glCompressedTexSubImage1D(
                            target, level, 0, extent.x, format.Internal,
                            texture.size(level), texture.data(layer, face, level));
                    }
                    else {
                        f->glTexSubImage1D(target, level, 0, extent.x, format.External,
                                           format.Type,
                                           texture.data(layer, face, level));
                    }
                    break;
                case gli::TARGET_1D_ARRAY:
                case gli::TARGET_2D:
                case gli::TARGET_CUBE:
                    if (gli::is_compressed(texture.format())) {
                        f->glCompressedTexSubImage2D(
                            target, level, 0, 0, extent.x,
                            texture.target() == gli::TARGET_1D_ARRAY ? layer : extent.y,
                            format.Internal, texture.size(level),
                            texture.data(layer, face, level));
                    }
                    else {
                        f->glTexSubImage2D(
                            target, level, 0, 0, extent.x,
                            texture.target() == gli::TARGET_1D_ARRAY ? layer : extent.y,
                            format.External, format.Type,
                            texture.data(layer, face, level));
                    }
                    break;
                case gli::TARGET_2D_ARRAY:
                case gli::TARGET_3D:
                case gli::TARGET_CUBE_ARRAY:
                    if (gli::is_compressed(texture.format())) {
                        f->glCompressedTexSubImage3D(
                            target, level, 0, 0, 0, extent.x, extent.y,
                            texture.target() == gli::TARGET_3D ? extent.z : layer,
                            format.Internal, texture.size(level),
                            texture.data(layer, face, level));
                    }
                    else {
                        f->glTexSubImage3D(target, level, 0, 0, 0, extent.x, extent.y,
                                           texture.target() == gli::TARGET_3D ? extent.z
                                                                              : layer,
                                           format.External, format.Type,
                                           texture.data(layer, face, level));
                    }
                    break;
                }
            }

    glTexture->release();

    return glTexture;
}

QOpenGLTexture* TextureManager::makeSolidColor(QVector4D color)
{
    QOpenGLTexture* glTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    glTexture->create();
    glTexture->bind();

    glTexture->setSize(1, 1);
    glTexture->setFormat(QOpenGLTexture::RGBA32F);
    glTexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::Float32);

    glTexture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::Float32, &color);

    glTexture->release();

    return glTexture;
}

// Case-insensitive walk of a path relative to a root dir. Used because the
// Linux FS is case-sensitive but Bethesda NIFs reference textures with
// arbitrary case. We fix the case component-by-component against the real
// on-disk filenames.
static QString resolveCaseInsensitive(const QString& rootAbs, const QString& relPath)
{
    QStringList parts = QDir::cleanPath(relPath).split('/', Qt::SkipEmptyParts);
    QString cursor    = rootAbs;
    for (const QString& part : parts) {
        QDir cur(cursor);
        if (!cur.exists()) return "";
        const QStringList entries =
            cur.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        QString match;
        for (const QString& entry : entries) {
            if (entry.compare(part, Qt::CaseInsensitive) == 0) {
                match = entry;
                break;
            }
        }
        if (match.isEmpty()) return "";
        cursor = cur.absoluteFilePath(match);
    }
    return QFileInfo::exists(cursor) ? cursor : QString();
}

QString TextureManager::resolvePath(const MOBase::IPluginGame* game, QString path)
{
    // NIF texture paths use Windows backslashes. On Linux, IOrganizer's
    // virtual tree keys on forward-slash paths, so normalize here before
    // lookup. BSA lookups keep the raw backslash form separately.
    QString normalized = path;
    normalized.replace('\\', '/');

    // MO2's virtual tree may return a path that matches case-insensitively
    // against its internal index but doesn't match the real on-disk case.
    // Verify existence — if the returned path doesn't actually exist, do a
    // case-insensitive walk against the containing mod directory to recover
    // the correct case.
    auto virtPath = m_MOInfo->resolvePath(normalized);
    if (!virtPath.isEmpty()) {
        if (QFileInfo::exists(virtPath)) {
            return virtPath;
        }
        // Path wasn't valid — walk up to find the mod root, then re-resolve
        // case-insensitively under it. We detect the mod root by looking for
        // the "textures/" segment (NIF texture paths are always rooted at
        // textures/).
        const int texIdx = virtPath.indexOf("/textures/", 0, Qt::CaseInsensitive);
        if (texIdx > 0) {
            const QString modRoot = virtPath.left(texIdx);
            const QString rel     = virtPath.mid(texIdx + 1);
            const QString fixed   = resolveCaseInsensitive(modRoot, rel);
            if (!fixed.isEmpty()) return fixed;
        }
    }

    auto dataDir  = game->dataDirectory();
    auto dataPath = dataDir.absoluteFilePath(QDir::cleanPath(normalized));
    if (QFileInfo::exists(dataPath)) {
        return dataPath;
    }

    // Final fallback: case-insensitive walk under the game data directory.
    return resolveCaseInsensitive(dataDir.absolutePath(), normalized);
}
