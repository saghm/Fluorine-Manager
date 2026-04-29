/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <string>

class QString;

namespace AppConfig
{

#define PARWSTRING wstring
#define APPPARAM(partype, parid, value) partype parid();
#include "appconfig.inc"

// Returns the application base directory.  On Linux, if the MO2_BASE_DIR
// environment variable is set (e.g. by an AppImage wrapper) that value is
// returned; otherwise falls back to QCoreApplication::applicationDirPath().
QString basePath();

// Returns the directory containing MO2 plugins.  On Linux, if the
// MO2_PLUGINS_DIR environment variable is set (e.g. when plugins live
// inside a read-only AppImage squashfs) that value is used; otherwise
// falls back to basePath() + "/plugins".
QString pluginsPath();

// Returns the directory containing bundled Linux libraries (7z.so, etc.).
// Respects MO2_LIBS_DIR, otherwise basePath() + "/lib".
QString libsPath();

namespace MOShared
{
#undef PARWSTRING
#undef APPPARAM

}  // namespace MOShared

}  // namespace AppConfig

#endif  // APPCONFIG_H
