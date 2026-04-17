#include "steamdetection.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <uibase/log.h>

// ============================================================================
// SteamProtonInfo
// ============================================================================

QString SteamProtonInfo::wineBinary() const
{
  for (const char* sub : {"files/bin/wine", "dist/bin/wine"}) {
    const QString p = QDir(path).filePath(QString::fromLatin1(sub));
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString SteamProtonInfo::wineserverBinary() const
{
  for (const char* sub : {"files/bin/wineserver", "dist/bin/wineserver"}) {
    const QString p = QDir(path).filePath(QString::fromLatin1(sub));
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

QString SteamProtonInfo::binDir() const
{
  const QString wine = wineBinary();
  return wine.isEmpty() ? QString() : QFileInfo(wine).absolutePath();
}

// ============================================================================
// Steam Path Detection
// ============================================================================

QString findSteamPath()
{
  const QString home = QDir::homePath();
  const QStringList candidates = {
      home + "/.steam/steam",
      home + "/.local/share/Steam",
      home + "/.var/app/com.valvesoftware.Steam/.steam/steam",
      home + "/snap/steam/common/.steam/steam",
  };
  for (const QString& p : candidates) {
    if (QFileInfo::exists(p))
      return p;
  }
  return {};
}

// ============================================================================
// Proton Detection
// ============================================================================

namespace {

/// Check if a Proton version is 10 or newer.
bool isProton10OrNewer(const SteamProtonInfo& p)
{
  if (p.is_experimental || p.name.contains(QStringLiteral("Experimental")))
    return true;

  if (p.name.contains(QStringLiteral("CachyOS")))
    return true;

  if (p.name == QStringLiteral("LegacyRuntime") ||
      p.name.contains(QStringLiteral("Runtime")))
    return false;

  auto extractMajor = [](const QString& s, QChar sep) -> int {
    bool ok = false;
    int v   = s.section(sep, 0, 0).toInt(&ok);
    return ok ? v : -1;
  };

  if (p.name.startsWith(QStringLiteral("GE-Proton"))) {
    int v = extractMajor(p.name.mid(9), '-');
    return v < 0 || v >= 10;
  }
  if (p.name.startsWith(QStringLiteral("Proton "))) {
    int v = extractMajor(p.name.mid(7), '.');
    return v < 0 || v >= 10;
  }
  if (p.name.startsWith(QStringLiteral("EM-"))) {
    int v = extractMajor(p.name.mid(3), '.');
    return v < 0 || v >= 10;
  }

  return true;  // unknown format — allow
}

/// Scan a directory for Proton installations.
void scanProtonDir(const QString& dir, bool isSteamBuiltin,
                   QVector<SteamProtonInfo>& out)
{
  QDir d(dir);
  if (!d.exists())
    return;

  const QStringList entries = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& name : entries) {
    const QString path = d.absoluteFilePath(name);

    bool hasProton = QFileInfo::exists(path + "/proton");
    bool hasVdf    = QFileInfo::exists(path + "/compatibilitytool.vdf");

    if (!hasProton && !hasVdf)
      continue;

    SteamProtonInfo info;
    info.name       = name;
    info.path       = path;
    info.is_steam_proton  = isSteamBuiltin;
    info.is_experimental  = name.contains(QStringLiteral("Experimental"));

    if (isSteamBuiltin) {
      if (info.is_experimental) {
        info.config_name = QStringLiteral("proton_experimental");
      } else {
        const QString version = QString(name).remove(QStringLiteral("Proton "));
        const QString major   = version.section('.', 0, 0);
        info.config_name      = QStringLiteral("proton_%1").arg(major);
      }
    } else {
      info.config_name = name;
    }

    out.append(std::move(info));
  }
}

}  // namespace

QVector<SteamProtonInfo> findSteamProtons()
{
  QVector<SteamProtonInfo> protons;

  const QString steamPath = findSteamPath();
  if (steamPath.isEmpty())
    return protons;

  // 1. Steam's built-in Protons (only entries starting with "Proton").
  {
    QVector<SteamProtonInfo> builtin;
    scanProtonDir(steamPath + "/steamapps/common", true, builtin);
    // Keep only entries whose folder name starts with "Proton".
    // Proton 11 is blacklisted: its current Wine tree deadlocks during
    // wineboot -u on modern kernels (ntsync / futex_wait_multiple races),
    // causing prefix init to hang indefinitely. Users should fall back to
    // Proton 10 / Proton-Experimental / GE-Proton until Proton 11 stabilizes.
    for (auto& p : builtin) {
      if (!p.name.startsWith(QStringLiteral("Proton")))
        continue;
      if (p.name.startsWith(QStringLiteral("Proton 11"))) {
        MOBase::log::warn("Skipping '{}' — known-broken on Linux, use Proton 10 or GE-Proton",
                          p.name.toStdString());
        continue;
      }
      protons.append(std::move(p));
    }
  }

  // 2. Custom Protons in user's compatibilitytools.d.
  scanProtonDir(steamPath + "/compatibilitytools.d", false, protons);

  // 3. System-level Protons.
  scanProtonDir(QStringLiteral("/usr/share/steam/compatibilitytools.d"), false, protons);

  // Filter to Proton 10+.
  protons.erase(
      std::remove_if(protons.begin(), protons.end(),
                     [](const SteamProtonInfo& p) { return !isProton10OrNewer(p); }),
      protons.end());

  // Filter to Protons with valid wine binaries.
  protons.erase(
      std::remove_if(protons.begin(), protons.end(),
                     [](const SteamProtonInfo& p) {
                       bool ok = !p.wineBinary().isEmpty();
                       if (!ok) {
                         MOBase::log::warn("Skipping Proton '{}': wine binary not found",
                                           p.name);
                       }
                       return !ok;
                     }),
      protons.end());

  // Sort: Experimental first, then by name descending (newest first).
  std::sort(protons.begin(), protons.end(),
            [](const SteamProtonInfo& a, const SteamProtonInfo& b) {
              if (a.is_experimental != b.is_experimental)
                return a.is_experimental > b.is_experimental;
              return a.name > b.name;
            });

  return protons;
}
