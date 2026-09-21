#pragma once

#include <QString>

// Extract only xrandr from a Debian package, without requiring binutils/ar.
// The existing helper is replaced atomically only after successful extraction.
QString installXrandrFromDeb(const QString& package, const QString& destination,
                            const int* cancelFlag = nullptr);
