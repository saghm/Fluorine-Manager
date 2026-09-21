#pragma once

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <optional>
#include <string>

// Legacy field: one NAME=value per line, with literal values. Kept for migration.
std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error = nullptr);

struct LaunchWrapperOptions
{
  QStringList commands;
  QMap<QString, QString> environment;
};

// Shared syntax for global and per-executable wrappers. Uses Qt command-line
// quoting, accepts NAME=value tokens and an optional %command% marker.
std::optional<LaunchWrapperOptions> parseLaunchWrapperOptions(
    const QString& text, QString* error = nullptr);

// Upgrade the old literal, one-assignment-per-line field without turning spaces
// or quotes in saved values into wrapper commands.
QString wrapperOptionsFromLegacyEnvironment(const QString& text);

// Keep Wine's Unix filenames in UTF-8 without replacing language/region
// preferences. Call after merging wrapper and executable overrides.
void prepareProtonLocale(QProcessEnvironment& environment);

// Serialize Linux argv for the legacy command-line parser and IPC forwarding.
// Decode UTF-8, and protect quotes/backslashes from boost::split_unix.
std::wstring commandLineFromUtf8Arguments(int argc, char* const argv[]);
