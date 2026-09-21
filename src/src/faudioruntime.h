#pragma once

#include <QString>
#include <QStringList>

struct FAudioPayload {
  QString version;
  QStringList dlls;
  bool changed = false;
};

// Validate the entire pack before replacing prefix-local DLLs. Both available
// variants use the same Wine 10+ compatibility fix, so no runner-name heuristic
// or separate Proton installation is needed.
bool installFAudioPayload(const QString& prefix, const QString& bundle,
                         FAudioPayload& payload, QString& error);

// Update prefixes that already have a managed FAudio installation. Preserve the
// recorded safe/latest choice unless explicitly overridden by the environment.
bool refreshFAudioPayload(const QString& prefix, const QString& bundleRoot,
                         const QString& requestedVariant, FAudioPayload& payload,
                         QString& error);
