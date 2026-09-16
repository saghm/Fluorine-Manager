#pragma once

#include <QProcessEnvironment>

// Shared by installation, gallery, extraction and engine-version probes.
// Authentication is performed by Fluorine; subprocesses must not inherit it.
inline QProcessEnvironment clf3EngineEnvironment()
{
  auto environment = QProcessEnvironment::systemEnvironment();
  for (const auto& key : environment.keys()) {
    const auto name = key.toUpper();
    if (name.startsWith("NEXUS_") || name.startsWith("LOVERSLAB_")
        || name.contains("TOKEN") || name.contains("SECRET")
        || name.contains("PASSWORD") || name.contains("PASSWD")
        || name.contains("CREDENTIAL") || name.contains("API_KEY")
        || name.contains("APIKEY") || name.contains("AUTHORIZATION")
        || name.contains("COOKIE")) environment.remove(key);
  }
  return environment;
}
