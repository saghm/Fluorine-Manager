#ifndef ICONEXTRACTOR_H
#define ICONEXTRACTOR_H

#include <QByteArray>
#include <QString>

/// Extract the best (largest) icon from a Windows PE executable as raw ICO bytes.
/// Returns an empty QByteArray if extraction fails.
__attribute__((visibility("default"))) QByteArray extractExeIcon(const QString& exePath);

#endif // ICONEXTRACTOR_H
