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

#include "appconfig.h"

#include <cstdlib>

#include <QCoreApplication>
#include <QString>

namespace AppConfig
{

#define PARWSTRING wstring
#define APPPARAM(partype, parid, value)                                                \
  partype parid()                                                                      \
  {                                                                                    \
    return value;                                                                      \
  }
#include "appconfig.inc"

QString basePath()
{
#ifndef _WIN32
  const char* envBase = std::getenv("MO2_BASE_DIR");
  if (envBase && envBase[0] != '\0') {
    return QString::fromUtf8(envBase);
  }
#endif
  return QCoreApplication::applicationDirPath();
}

QString pluginsPath()
{
#ifndef _WIN32
  const char* envDir = std::getenv("MO2_PLUGINS_DIR");
  if (envDir && envDir[0] != '\0') {
    return QString::fromUtf8(envDir);
  }
#endif
  return basePath() + "/" + QString::fromStdWString(pluginPath());
}

QString dllsPath()
{
#ifndef _WIN32
  const char* envDir = std::getenv("MO2_DLLS_DIR");
  if (envDir && envDir[0] != '\0') {
    return QString::fromUtf8(envDir);
  }
#endif
  return basePath() + "/dlls";
}

namespace MOShared
{
#undef PARWSTRING
#undef APPPARAM

}  // namespace MOShared
}  // namespace AppConfig
