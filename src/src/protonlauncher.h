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
  ProtonLauncher& setUmu(bool useUmu);
  ProtonLauncher& setPreferSystemUmu(bool preferSystemUmu);
  ProtonLauncher& setUseSteamRun(bool useSteamRun);
  ProtonLauncher& setSteamDrm(bool useSteamDrm);
  ProtonLauncher& setStoreVariant(const QString& variant);
  ProtonLauncher& addEnvVar(const QString& key, const QString& value);

  // Launch dispatch: UMU -> Proton -> Direct
  std::pair<bool, qint64> launch() const;

private:
  bool launchWithProton(qint64& pid) const;
  bool launchWithUmu(qint64& pid) const;
  bool launchDirect(qint64& pid) const;
  static bool ensureSteamRunning();

  QString m_binary;
  QStringList m_arguments;
  QString m_workingDir;
  QString m_protonPath;
  QString m_prefixPath;
  uint32_t m_steamAppId;
  QStringList m_wrapperCommands;
  bool m_useUmu;
  bool m_preferSystemUmu;
  bool m_useSteamRun;
  bool m_useSteamDrm;
  QString m_storeVariant; // "GOG", "Epic", or empty for Steam
  QMap<QString, QString> m_envVars;
  QMap<QString, QString> m_wrapperEnvVars;
};

#endif  // PROTONLAUNCHER_H
