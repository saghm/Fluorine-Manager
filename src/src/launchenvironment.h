#pragma once

#include <QMap>
#include <QString>
#include <optional>

// One NAME=value per line. Values are literal, including spaces and '=';
// nothing is evaluated by a shell. Empty values deliberately override globals.
std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error = nullptr);
