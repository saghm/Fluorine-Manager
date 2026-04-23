#ifndef FILESYSTEMUTILIITES_H
#define FILESYSTEMUTILITIES_H

#include <QString>

#include "dllimport.h"

namespace MOBase
{
/**
 * @brief fix a directory name so it can be dealt with by windows explorer
 * @return false if there was no way to convert the name into a valid one
 **/
QDLLEXPORT bool fixDirectoryName(QString& name);

/**
 * @brief ensures a file name is valid
 *
 * @param name the file name being sanitized
 * @param replacement invalid characters are replaced with this string
 * @return the sanitized file name
 **/
QDLLEXPORT QString sanitizeFileName(const QString& name,
                                    const QString& replacement = QString(""));

/**
 * @brief checks file name validity per sanitizeFileName
 *
 * @param name the file name being checked
 * @return true if the given file name is valid
 **/
QDLLEXPORT bool validFileName(const QString& name);

/**
 * @brief Resolve a file path case-insensitively on Linux.
 *
 * On Windows (case-insensitive FS), returns the input path as-is.
 * On Linux, if the exact path doesn't exist, searches the parent directory
 * for a file matching the name case-insensitively.
 *
 * @param path the absolute file path to resolve
 * @return the resolved path (with correct case) or the original path if not found
 */
QDLLEXPORT QString resolveFileCaseInsensitive(const QString& path);

/**
 * @brief Case-insensitively resolve every component of an absolute path.
 *
 * Unlike resolveFileCaseInsensitive(), walks each directory component and
 * matches it case-insensitively against the real filesystem, so a path like
 * `mods/foo/meshes/dlc01/bar.nif` resolves correctly when the on-disk
 * directory is `meshes/DLC01/`.
 *
 * On Windows, returns the cleaned path as-is.
 * On Linux, returns the first walk that resolves each component; if any
 * component has no case-insensitive match, returns the cleaned input path.
 */
QDLLEXPORT QString resolvePathCaseInsensitive(const QString& path);

}  // namespace MOBase

#endif  // FILESYSTEM_H
