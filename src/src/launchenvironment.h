#pragma once

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <optional>

// One NAME=value per line. Values are literal, including spaces and '=';
// nothing is evaluated by a shell. Empty values deliberately override globals.
std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error = nullptr);

// Keep Wine's Unix filenames in UTF-8 without replacing language/region
// preferences. Call after merging wrapper and executable overrides.
void prepareProtonLocale(QProcessEnvironment& environment);
