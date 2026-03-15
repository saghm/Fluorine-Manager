#ifndef PROTONLAUNCHER_H
#define PROTONLAUNCHER_H

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <utility>

class ProtonLauncher
{
public:
  ProtonLauncher();

  ProtonLauncher& setBinary(const QString& path);
  ProtonLauncher& setArguments(const QStringList& args);
  ProtonLauncher& setWorkingDir(const QString& dir);
  ProtonLauncher& setProtonPath(const QString& path);
  ProtonLauncher& setPrefix(const QString& path);
  ProtonLauncher& setSteamAppId(uint32_t id);
  ProtonLauncher& setWrapper(const QString& wrapperCmd);
  ProtonLauncher& setSteamDrm(bool useSteamDrm);
  ProtonLauncher& setUseSLR(bool useSLR);
  ProtonLauncher& setStoreVariant(const QString& variant);
  ProtonLauncher& addEnvVar(const QString& key, const QString& value);
  ProtonLauncher& setUseTerminal(bool useTerminal);

  // Launch dispatch: Proton -> Direct
  std::pair<bool, qint64> launch() const;

private:
  bool launchWithProton(qint64& pid) const;
  bool launchDirect(qint64& pid) const;
  static bool ensureSteamRunning();

  QString m_binary;
  QStringList m_arguments;
  QString m_workingDir;
  QString m_protonPath;
  QString m_prefixPath;
  uint32_t m_steamAppId;
  QStringList m_wrapperCommands;
  bool m_useSteamDrm;
  bool m_useSLR = true;
  QString m_storeVariant; // "GOG", "Epic", or empty for Steam
  QMap<QString, QString> m_envVars;
  QMap<QString, QString> m_wrapperEnvVars;
  bool m_useTerminal = false;
};

#endif  // PROTONLAUNCHER_H
