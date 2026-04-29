#include "fluorinepaths.h"
#include "fluorineconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

static const QString OldFlatpakRoot =
    QDir::homePath() + "/.var/app/com.fluorine.manager";

QString fluorineDataDir()
{
  return QDir::homePath() + "/.local/share/fluorine";
}

void fluorineMigrateDataDir()
{
  const QString oldRoot = OldFlatpakRoot;
  const QString newRoot = fluorineDataDir();

  // Always check for config.json migration, even if data was already moved.
  // The Flatpak stored config.json under its sandboxed XDG_CONFIG_HOME
  // (~/.var/app/com.fluorine.manager/config/), but outside Flatpak
  // QStandardPaths returns ~/.config/.
  {
    const QString oldConfigJson = oldRoot + "/config/fluorine/config.json";
    QString configRoot = QStandardPaths::writableLocation(
        QStandardPaths::ConfigLocation);
    if (configRoot.isEmpty()) {
      configRoot = QDir::homePath() + "/.config";
    }
    const QString newConfigJson =
        QDir(configRoot).filePath("fluorine/config.json");
    if (QFile::exists(oldConfigJson) && !QFile::exists(newConfigJson)) {
      QDir().mkpath(QFileInfo(newConfigJson).dir().absolutePath());
      if (QFile::copy(oldConfigJson, newConfigJson)) {
        fprintf(stderr, "[fluorine] Migrated config.json to %s\n",
                qUtf8Printable(newConfigJson));
        // Update prefix_path if it references the old Flatpak root
        if (auto cfg = FluorineConfig::load()) {
          if (cfg->prefix_path.startsWith(oldRoot)) {
            cfg->prefix_path.replace(oldRoot, newRoot);
            cfg->save();
            fprintf(stderr, "[fluorine]   updated config prefix_path\n");
          }
        }
      } else {
        fprintf(stderr, "[fluorine] FAILED to copy config.json to %s\n",
                qUtf8Printable(newConfigJson));
      }
    }
  }

  // Already migrated or old path never existed
  if (QFile::exists(oldRoot + "/MOVED.txt")) {
    return;
  }
  if (!QDir(oldRoot).exists()) {
    return;
  }

  // Check if there is actually data to migrate
  const QStringList subdirs = {"logs", "bin", "config", "Prefix"};
  bool hasData = false;
  for (const QString& sub : subdirs) {
    if (QDir(oldRoot + "/" + sub).exists()) {
      hasData = true;
      break;
    }
  }
  if (!hasData) {
    return;
  }

  fprintf(stderr, "[fluorine] Migrating data from %s to %s\n",
          qUtf8Printable(oldRoot), qUtf8Printable(newRoot));

  QDir().mkpath(newRoot);

  for (const QString& sub : subdirs) {
    const QString src = oldRoot + "/" + sub;
    const QString dst = newRoot + "/" + sub;
    if (!QDir(src).exists()) {
      continue;
    }
    if (QDir(dst).exists()) {
      fprintf(stderr, "[fluorine]   skip %s (destination already exists)\n",
              qUtf8Printable(sub));
      continue;
    }
    if (QDir().rename(src, dst)) {
      fprintf(stderr, "[fluorine]   moved %s\n", qUtf8Printable(sub));
    } else {
      fprintf(stderr, "[fluorine]   FAILED to move %s\n", qUtf8Printable(sub));
    }
  }

  // Update FluorineConfig's prefix_path if it references the old root
  if (auto cfg = FluorineConfig::load()) {
    if (cfg->prefix_path.startsWith(oldRoot)) {
      cfg->prefix_path.replace(oldRoot, newRoot);
      cfg->save();
      fprintf(stderr, "[fluorine]   updated config prefix_path\n");
    }
  }

  // Write breadcrumb so we don't attempt migration again
  QFile marker(oldRoot + "/MOVED.txt");
  if (marker.open(QIODevice::WriteOnly)) {
    QTextStream ts(&marker);
    ts << "Data migrated to " << newRoot << "\n";
    marker.close();
  }

  fprintf(stderr, "[fluorine] Migration complete.\n");
}
