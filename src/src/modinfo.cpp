/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "modinfo.h"

#include "modinfobackup.h"
#include "modinfoforeign.h"
#include "modinfooverwrite.h"
#include "modinforegular.h"
#include "modinfoseparator.h"

#include "categories.h"
#include "modidrecovery.h"
#include "modinfodialog.h"
#include "modlist.h"
#include "organizercore.h"
#include "overwriteinfodialog.h"
#include "thread_utils.h"
#include "versioninfo.h"

#include "shared/appconfig.h"
#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <scriptextender.h>
#include <unmanagedmods.h>
#include <versioninfo.h>

#include <QApplication>
#include <QDirIterator>
#include <QHash>
#include <QMutexLocker>
#include <QSettings>
#include <QTimeZone>

using namespace MOBase;
using namespace MOShared;

const std::set<unsigned int> ModInfo::s_EmptySet;
std::vector<ModInfo::Ptr> ModInfo::s_Collection;
ModInfo::Ptr ModInfo::s_Overwrite;
std::map<QString, unsigned int, MOBase::FileNameComparator> ModInfo::s_ModsByName;
std::map<std::pair<QString, int>, std::vector<unsigned int>> ModInfo::s_ModsByModID;
int ModInfo::s_NextID;
QRecursiveMutex ModInfo::s_Mutex;

QString ModInfo::s_HiddenExt(".mohidden");

bool ModInfo::ByName(const ModInfo::Ptr& LHS, const ModInfo::Ptr& RHS)
{
  return QString::compare(LHS->name(), RHS->name(), Qt::CaseInsensitive) < 0;
}

bool ModInfo::isSeparatorName(const QString& name)
{
  static QRegularExpression const separatorExp(
      QRegularExpression::anchoredPattern(".*_separator"));
  return separatorExp.match(name).hasMatch();
}

bool ModInfo::isBackupName(const QString& name)
{
  static QRegularExpression const backupExp(
      QRegularExpression::anchoredPattern(".*backup[0-9]*"));
  return backupExp.match(name).hasMatch();
}

bool ModInfo::isRegularName(const QString& name)
{
  return !isSeparatorName(name) && !isBackupName(name);
}

ModInfo::Ptr ModInfo::createFrom(const QDir& dir, OrganizerCore& core)
{
  QMutexLocker const locker(&s_Mutex);
  ModInfo::Ptr result;

  if (isBackupName(dir.dirName())) {
    result = ModInfo::Ptr(new ModInfoBackup(dir, core));
  } else if (isSeparatorName(dir.dirName())) {
    result = Ptr(new ModInfoSeparator(dir, core));
  } else {
    result = ModInfo::Ptr(new ModInfoRegular(dir, core));
  }
  result->m_Index = s_Collection.size();
  s_Collection.push_back(result);
  return result;
}

ModInfo::Ptr ModInfo::createFromPlugin(const QString& modName, const QString& espName,
                                       const QStringList& bsaNames,
                                       ModInfo::EModType modType, OrganizerCore& core)
{
  QMutexLocker const locker(&s_Mutex);
  ModInfo::Ptr result =
      ModInfo::Ptr(new ModInfoForeign(modName, espName, bsaNames, modType, core));
  result->m_Index = s_Collection.size();
  s_Collection.push_back(result);
  return result;
}

ModInfo::Ptr ModInfo::createFromOverwrite(OrganizerCore& core)
{
  QMutexLocker const locker(&s_Mutex);
  ModInfo::Ptr overwrite = ModInfo::Ptr(new ModInfoOverwrite(core));
  overwrite->m_Index     = s_Collection.size();
  s_Collection.push_back(overwrite);
  return overwrite;
}

unsigned int ModInfo::getNumMods()
{
  QMutexLocker const locker(&s_Mutex);
  return static_cast<unsigned int>(s_Collection.size());
}

ModInfo::Ptr ModInfo::getByIndex(unsigned int index)
{
  QMutexLocker const locker(&s_Mutex);

  if (index == UINT_MAX)
    return s_Collection[ModInfo::getIndex("Overwrite")];
  if (index >= s_Collection.size()) {
    throw MyException(tr("invalid mod index: %1").arg(index));
  }
  return s_Collection[index];
}

std::vector<ModInfo::Ptr> ModInfo::getByModID(QString game, int modID)
{
  QMutexLocker const locker(&s_Mutex);

  std::vector<unsigned int> match;
  for (auto iter : s_ModsByModID) {
    if (iter.first.second == modID) {
      if (iter.first.first.compare(game, Qt::CaseInsensitive) == 0) {
        match.insert(match.end(), iter.second.begin(), iter.second.end());
      }
    }
  }
  if (match.empty()) {
    return {};
  }

  std::vector<ModInfo::Ptr> result;
  for (auto iter : match) {
    result.push_back(getByIndex(iter));
  }

  return result;
}

ModInfo::Ptr ModInfo::getByName(const QString& name)
{
  QMutexLocker const locker(&s_Mutex);

  return s_Collection[ModInfo::getIndex(name)];
}

bool ModInfo::removeMod(unsigned int index)
{
  QMutexLocker const locker(&s_Mutex);

  if (index >= s_Collection.size()) {
    throw Exception(tr("remove: invalid mod index %1").arg(index));
  }

  ModInfo::Ptr const modInfo = s_Collection[index];

  // remove the actual mod (this is the most likely to fail so we do this first)
  if (modInfo->isRegular()) {
    if (!shellDelete(QStringList(modInfo->absolutePath()), true)) {
      reportError(
          tr("remove: failed to delete mod '%1' directory").arg(modInfo->name()));
      return false;
    }
  }

  // update the indices
  s_ModsByName.erase(s_ModsByName.find(modInfo->name()));

  auto iter = s_ModsByModID.find(
      std::pair<QString, int>(modInfo->gameName(), modInfo->nexusId()));
  if (iter != s_ModsByModID.end()) {
    std::vector<unsigned int> indices = iter->second;
    indices.erase(std::remove(indices.begin(), indices.end(), index), indices.end());
    s_ModsByModID[std::pair<QString, int>(modInfo->gameName(), modInfo->nexusId())] =
        indices;
  }

  // finally, remove the mod from the collection
  s_Collection.erase(s_Collection.begin() + index);

  // and update the indices
  updateIndices();
  return true;
}

unsigned int ModInfo::getIndex(const QString& name)
{
  QMutexLocker const locker(&s_Mutex);

  std::map<QString, unsigned int>::iterator const iter = s_ModsByName.find(name);
  if (iter == s_ModsByName.end()) {
    return UINT_MAX;
  }

  return iter->second;
}

unsigned int ModInfo::findMod(const boost::function<bool(ModInfo::Ptr)>& filter)
{
  for (unsigned int i = 0U; i < s_Collection.size(); ++i) {
    if (filter(s_Collection[i])) {
      return i;
    }
  }
  return UINT_MAX;
}

void ModInfo::updateFromDisc(const QString& modsDirectory, OrganizerCore& core,
                             bool displayForeign, std::size_t refreshThreadCount)
{
  TimeThis const tt("ModInfo::updateFromDisc()");

  QMutexLocker const lock(&s_Mutex);
  s_Collection.clear();
  s_NextID    = 0;
  s_Overwrite = nullptr;

  {  // list all directories in the mod directory and make a mod out of each
    const QString cleanModsDir = QDir::fromNativeSeparators(modsDirectory);
    QDir mods(cleanModsDir);
    if (!mods.exists()) {
      log::error("mods directory does not exist: '{}'", cleanModsDir);
    }
    mods.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    QDirIterator modIter(mods);
    std::size_t managedCount = 0;
    while (modIter.hasNext()) {
      createFrom(QDir(modIter.next()), core);
      ++managedCount;
    }
    log::info("found {} managed mod directories in '{}'", managedCount, cleanModsDir);
    if (managedCount == 0 && mods.exists()) {
      log::warn("mods directory exists but contains no subdirectories; "
                "check path and permissions");
    }
  }

  const auto* game     = core.managedGame();
  auto& features = core.pluginContainer().gameFeatures();
  auto unmanaged = features.gameFeature<UnmanagedMods>();
  if (unmanaged != nullptr) {
    for (const QString& modName : unmanaged->mods(!displayForeign)) {
      ModInfo::EModType const modType =
          game->DLCPlugins().contains(unmanaged->referenceFile(modName).fileName(),
                                      Qt::CaseInsensitive)
              ? ModInfo::EModType::MOD_DLC
              : (game->CCPlugins().contains(
                     unmanaged->referenceFile(modName).fileName(), Qt::CaseInsensitive)
                     ? ModInfo::EModType::MOD_CC
                     : ModInfo::EModType::MOD_DEFAULT);

      createFromPlugin(unmanaged->displayName(modName),
                       unmanaged->referenceFile(modName).absoluteFilePath(),
                       unmanaged->secondaryFiles(modName), modType, core);
    }
  }

  s_Overwrite = createFromOverwrite(core);

  std::sort(s_Collection.begin(), s_Collection.end(), ModInfo::ByName);

  parallelMap(std::begin(s_Collection), std::end(s_Collection), &ModInfo::prefetch,
              refreshThreadCount);

  updateIndices();
}

void ModInfo::updateIndices()
{
  s_ModsByName.clear();
  s_ModsByModID.clear();

  for (unsigned int i = 0; i < s_Collection.size(); ++i) {
    QString const modName          = s_Collection[i]->internalName();
    QString const game             = s_Collection[i]->gameName();
    int const modID                = s_Collection[i]->nexusId();
    s_Collection[i]->m_Index = i;
    s_ModsByName[modName]    = i;
    s_ModsByModID[std::pair<QString, int>(game, modID)].push_back(i);
  }
}

ModInfo::ModInfo(OrganizerCore& core) :  m_Core(core) {}

namespace
{

// Bookkeeping for one "Check for updates" run: which per-mod nexus requests
// are still in flight and what went wrong along the way.
//
// This has its own mutex rather than s_Mutex: filteredMods() reads it from a
// QtConcurrent worker while the GUI thread records problems, and taking
// s_Mutex there would invite a lock order inversion.
struct UpdateCheckRun
{
  QMutex mutex;
  std::vector<ModInfo::UpdateCheckProblem> problems;
  std::set<std::pair<QString, int>> pending;

  // Bulk file-list requests in flight, keyed by the nexus request id
  // (unique per request, from NXMRequestInfo::s_NextID). Keying by id rather
  // than by game name is what guarantees an unrelated failed request for the
  // same game — endorsement, track, description — can never be reported as a
  // failed update check.
  QHash<int, QString> pendingBulk;
};

UpdateCheckRun& updateCheckRun()
{
  static UpdateCheckRun state;
  return state;
}

std::pair<QString, int> updateCheckKey(const QString& gameName, int modID)
{
  return {gameName.toLower(), modID};
}

}  // namespace

void ModInfo::clearUpdateCheckRun()
{
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  state.problems.clear();
  state.pending.clear();
  state.pendingBulk.clear();
}

void ModInfo::registerPendingBulkUpdateCheck(int requestID, const QString& gameName)
{
  if (requestID < 0) {
    return;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  state.pendingBulk.insert(requestID, gameName);
}

bool ModInfo::finishBulkUpdateCheckRequest(int requestID, QString& gameName)
{
  if (requestID < 0) {
    return false;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  const auto it = state.pendingBulk.constFind(requestID);
  if (it == state.pendingBulk.constEnd()) {
    return false;
  }
  gameName = it.value();
  state.pendingBulk.erase(it);
  return true;
}

void ModInfo::registerPendingUpdateCheck(const QString& gameName, int modID)
{
  if (modID <= 0) {
    return;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  state.pending.insert(updateCheckKey(gameName, modID));
}

bool ModInfo::isUpdateCheckPending(const QString& gameName, int modID)
{
  if (modID <= 0) {
    return false;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  return state.pending.find(updateCheckKey(gameName, modID)) != state.pending.end();
}

void ModInfo::finishUpdateCheckRequest(const QString& gameName, int modID)
{
  if (modID <= 0) {
    return;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  state.pending.erase(updateCheckKey(gameName, modID));
}

void ModInfo::noteUpdateCheckProblem(const QString& gameName, int modID,
                                     const QString& message, bool failure,
                                     bool alreadyShown)
{
  if (modID <= 0) {
    return;
  }
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);

  const auto key = updateCheckKey(gameName, modID);
  for (auto& problem : state.problems) {
    if (updateCheckKey(problem.gameName, problem.modID) == key) {
      // one entry per mod: the newest problem wins
      problem.message = message;
      problem.failure = failure;
      problem.shown   = alreadyShown;
      return;
    }
  }

  ModInfo::UpdateCheckProblem problem;
  problem.gameName = key.first;
  problem.modID    = modID;
  problem.message  = message;
  problem.failure  = failure;
  problem.shown    = alreadyShown;
  state.problems.push_back(problem);
}

void ModInfo::noteUpdateCheckGameProblem(const QString& gameName, const QString& message)
{
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);

  // modID 0 marks "the whole game's check", which is how a bulk file-list
  // failure is represented: it is not attributable to any single mod, and 0 can
  // never collide with a real nexus mod id (update checks require nexusId() > 0).
  const auto key = updateCheckKey(gameName, 0);
  for (auto& problem : state.problems) {
    if (updateCheckKey(problem.gameName, problem.modID) == key) {
      problem.message = message;
      problem.failure = true;
      problem.shown   = false;
      return;
    }
  }

  ModInfo::UpdateCheckProblem problem;
  problem.gameName = key.first;
  problem.modID    = 0;
  problem.message  = message;
  problem.failure  = true;
  problem.shown    = false;
  state.problems.push_back(problem);
}

void ModInfo::clearUpdateCheckProblem(const QString& gameName, int modID)
{
  if (modID <= 0) {
    return;
  }
  const auto key = updateCheckKey(gameName, modID);
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  std::erase_if(state.problems, [&](const ModInfo::UpdateCheckProblem& problem) {
    return updateCheckKey(problem.gameName, problem.modID) == key;
  });
}

bool ModInfo::updateCheckFailed(const QString& gameName, int modID)
{
  if (modID <= 0) {
    return false;
  }
  const auto key = updateCheckKey(gameName, modID);
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  for (const auto& problem : state.problems) {
    if (problem.failure && updateCheckKey(problem.gameName, problem.modID) == key) {
      return true;
    }
  }
  return false;
}

std::vector<ModInfo::UpdateCheckProblem> ModInfo::takeUnshownUpdateCheckProblems()
{
  std::vector<ModInfo::UpdateCheckProblem> unshown;
  UpdateCheckRun& state = updateCheckRun();
  QMutexLocker locker(&state.mutex);
  for (auto& problem : state.problems) {
    if (!problem.shown) {
      problem.shown = true;
      unshown.push_back(problem);
    }
  }
  return unshown;
}

void ModInfo::recoverMissingModIds()
{
  if (s_Collection.empty()) {
    return;
  }

  const QString downloadsPath = s_Collection.front()->m_Core.downloadsPath();
  bool recoveredAny           = false;

  for (const auto& mod : s_Collection) {
    if (mod->nexusId() > 0) {
      // A valid id is never overwritten (D.1).
      continue;
    }
    const QString installationFile = mod->installationFile();
    if (installationFile.isEmpty()) {
      continue;
    }

    QSettings downloadMeta(QDir(downloadsPath).filePath(installationFile + ".meta"),
                           QSettings::IniFormat);
    const int recovered = ModIdRecovery::recover(mod->url(), installationFile,
                                                 downloadMeta.value("modid").toString(),
                                                 downloadMeta.value("url").toString(),
                                                 downloadMeta.value("directURL").toString());
    if (!ModIdRecovery::canApply(mod->nexusId(), recovered)) {
      continue;
    }

    mod->setNexusID(recovered);
    mod->saveMeta();
    recoveredAny = true;
    log::info("recovered missing nexus mod id {} for \"{}\"", recovered, mod->name());
  }

  if (recoveredAny) {
    // nexusId() feeds s_ModsByModID, so the index has to be rebuilt.
    updateIndices();
  }
}

int ModInfo::requestModFileLists(const QString& gameName, const std::set<int>& modIDs,
                                 QObject* receiver)
{
  if (modIDs.empty()) {
    return -1;
  }

  QList<int>   ids;
  QVariantList idList;
  ids.reserve(static_cast<int>(modIDs.size()));
  idList.reserve(static_cast<int>(modIDs.size()));
  for (int modID : modIDs) {
    ids.append(modID);
    idList.append(modID);
  }

  // The response handler needs to know which mods were asked for, and the
  // request id needs to be recognisable as part of this game's check so a
  // failure is attributed to it and to nothing else.
  QVariantMap userData;
  userData.insert(QStringLiteral("game"), gameName);
  userData.insert(QStringLiteral("modIds"), idList);

  const int requestID = NexusInterface::instance().requestModFileLists(
      gameName, ids, receiver, userData, QString());
  if (requestID >= 0) {
    registerPendingBulkUpdateCheck(requestID, gameName);
  }
  return requestID;
}

bool ModInfo::checkAllForUpdate(PluginContainer* pluginContainer, QObject* receiver)
{
  bool updatesAvailable = true;

  // A new run supersedes whatever the previous one recorded.
  clearUpdateCheckRun();
  recoverMissingModIds();

  QDateTime earliest = QDateTime::currentDateTimeUtc();
  QDateTime latest   = QDateTime::fromMSecsSinceEpoch(0);
  // every game that has a mod we may talk to nexus about...
  std::set<QString> nexusGames;
  // ...and the subset of those whose check is due right now
  std::set<QString> games;
  for (const auto& mod : s_Collection) {
    if (mod->nexusId() > 0) {
      nexusGames.insert(mod->gameName().toLower());
    }
    if (mod->canBeUpdated()) {
      if (mod->getLastNexusUpdate() < earliest)
        earliest = mod->getLastNexusUpdate();
      if (mod->getLastNexusUpdate() > latest)
        latest = mod->getLastNexusUpdate();
      games.insert(mod->gameName().toLower());
    }
  }

  // Detect invalid source games
  auto dropInvalidSources = [&](std::set<QString>& gameNames) {
    for (auto itr = gameNames.begin(); itr != gameNames.end();) {
      auto gamePlugins        = pluginContainer->plugins<IPluginGame>();
      IPluginGame* gamePlugin = qApp->property("managed_game").value<IPluginGame*>();
      for (auto *plugin : gamePlugins) {
        if (plugin != nullptr &&
            plugin->gameShortName().compare(*itr, Qt::CaseInsensitive) == 0) {
          gamePlugin = plugin;
          break;
        }
      }
      if (gamePlugin != nullptr && gamePlugin->gameNexusName().isEmpty()) {
        log::warn("{}", tr("The update check has found a mod with a Nexus ID and source "
                           "game of %1, but this game is not a valid Nexus source.")
                            .arg(gamePlugin->gameName()));
        itr = gameNames.erase(itr);
      } else {
        ++itr;
      }
    }
  };
  dropInvalidSources(nexusGames);
  for (auto itr = games.begin(); itr != games.end();) {
    if (nexusGames.find(*itr) == nexusGames.end()) {
      itr = games.erase(itr);
    } else {
      ++itr;
    }
  }

  // One bulk request per game, as before. The nexus request id is registered so
  // that a failure can be attributed to *this game's* update check — and only to
  // it — in nxmRequestFailed(), which is an app-global signal carrying no
  // request type. A throttled request returns -1 and is not registered.
  const auto bulkCheck = [receiver](const QString& gameName,
                                    NexusInterface::UpdatePeriod period, bool markUpdated) {
    const int requestID = NexusInterface::instance().requestUpdateInfo(
        gameName, period, receiver, QVariant(markUpdated), QString());
    if (requestID >= 0) {
      ModInfo::registerPendingBulkUpdateCheck(requestID, gameName);
    }
  };

  if (latest < QDateTime::currentDateTimeUtc().addMonths(-1)) {
    std::set<std::pair<QString, int>> organizedGames;
    for (const auto& mod : s_Collection) {
      if (mod->canBeUpdated() &&
          mod->getLastNexusUpdate() < QDateTime::currentDateTimeUtc().addMonths(-1) &&
          games.find(mod->gameName().toLower()) != games.end()) {
        organizedGames.insert(
            std::make_pair<QString, int>(mod->gameName().toLower(), mod->nexusId()));
      }
    }

    if (organizedGames.empty()) {
      log::warn("{}",
                tr("All of your mods have been checked recently. We restrict update "
                   "checks to help preserve your available API requests."));
      updatesAvailable = false;

      // The click still has to do something visible: the bulk request is one
      // request per game and refreshes the dates the list is judged on.
      for (const auto& gameName : nexusGames)
        bulkCheck(gameName, NexusInterface::UpdatePeriod::DAY, false);
    } else {
      log::info("{}", tr("You have mods that haven't been checked within the last "
                         "month. Their file lists are fetched in one bulk request "
                         "as part of this check."));
    }

    if (!organizedGames.empty()) {
      // One bulk request per game: filteredMods(addOldMods=true) returns every
      // mod older than a month, so the file lists fetched from that response on
      // cover exactly these mods. Nothing is checked by a request of its own.
      for (const auto& gameName : games)
        bulkCheck(gameName, NexusInterface::UpdatePeriod::MONTH, true);
    }
  } else if (earliest < QDateTime::currentDateTimeUtc().addMonths(-1)) {
    for (const auto& gameName : games)
      bulkCheck(gameName, NexusInterface::UpdatePeriod::MONTH, true);
  } else if (earliest < QDateTime::currentDateTimeUtc().addDays(-7)) {
    for (const auto& gameName : games)
      bulkCheck(gameName, NexusInterface::UpdatePeriod::MONTH, false);
  } else if (earliest < QDateTime::currentDateTimeUtc().addDays(-1)) {
    for (const auto& gameName : games)
      bulkCheck(gameName, NexusInterface::UpdatePeriod::WEEK, false);
  } else {
    for (const auto& gameName : games)
      bulkCheck(gameName, NexusInterface::UpdatePeriod::DAY, false);
  }

  return updatesAvailable;
}

std::set<QSharedPointer<ModInfo>> ModInfo::filteredMods(QString gameName,
                                                        QVariantList updateData,
                                                        bool addOldMods,
                                                        bool markUpdated)
{
  std::set<QSharedPointer<ModInfo>> finalMods;
  for (const QVariant& result : updateData) {
    QVariantMap const update = result.toMap();
    std::copy_if(s_Collection.begin(), s_Collection.end(),
                 std::inserter(finalMods, finalMods.end()),
                 [=](QSharedPointer<ModInfo> info) -> bool {
                   if (info->nexusId() == update["mod_id"].toInt() &&
                       info->gameName().compare(gameName, Qt::CaseInsensitive) == 0)
                     if (info->getLastNexusUpdate().addSecs(-3600) <
                         QDateTime::fromSecsSinceEpoch(
                             update["latest_file_update"].toInt(), QTimeZone::UTC))
                       return true;
                   return false;
                 });
  }

  if (addOldMods)
    for (const auto& mod : s_Collection)
      if (mod->getLastNexusUpdate() < QDateTime::currentDateTimeUtc().addMonths(-1) &&
          mod->gameName().compare(gameName, Qt::CaseInsensitive) == 0)
        finalMods.insert(mod);

  // Mods whose recorded evidence predates a field this build needs are due no
  // matter what the bulk response says: their verdict would otherwise be
  // computed from evidence nobody ever collected, and lastNexusUpdate would
  // keep them stamped as checked for a month. This is the backfill path, and it
  // runs regardless of addOldMods because a mod can be freshly stamped *and*
  // still be missing everything the verdict needs.
  //
  // Deliberately does not touch lastNexusUpdate. A mod whose fetch fails keeps
  // its real check timestamp, so it falls back to the normal one-month retry
  // cadence instead of being asked again on every check, and "last checked"
  // still shows a date.
  for (const auto& mod : s_Collection) {
    if (!mod->canBeUpdated() || !mod->needsEvidenceRefresh()) {
      continue;
    }
    if (mod->gameName().compare(gameName, Qt::CaseInsensitive) == 0) {
      finalMods.insert(mod);
    }
  }

  if (markUpdated) {
    std::set<QSharedPointer<ModInfo>> updates;
    std::copy_if(s_Collection.begin(), s_Collection.end(),
                 std::inserter(updates, updates.end()),
                 [=](QSharedPointer<ModInfo> info) -> bool {
                   // A mod whose check failed this run keeps its old timestamp
                   // so the next run retries it instead of claiming it is fresh.
                   return info->gameName().compare(gameName, Qt::CaseInsensitive) == 0 &&
                       info->canBeUpdated() &&
                       !updateCheckFailed(info->gameName(), info->nexusId());
                 });
    std::set<QSharedPointer<ModInfo>> diff;
    std::set_difference(updates.begin(), updates.end(), finalMods.begin(),
                        finalMods.end(), std::inserter(diff, diff.end()));
    for (const auto& skipped : diff) {
      skipped->setLastNexusUpdate(QDateTime::currentDateTimeUtc());
    }
  }
  return finalMods;
}

void ModInfo::manualUpdateCheck(QObject* receiver, std::multimap<QString, int> IDs)
{
  std::vector<QSharedPointer<ModInfo>> mods;

  for (const auto& ID : IDs) {
    for (const auto& matchedMod : getByModID(ID.first, ID.second)) {
      bool alreadyMatched = false;
      for (const auto& mod : mods) {
        if (mod == matchedMod) {
          alreadyMatched = true;
          break;
        }
      }
      if (!alreadyMatched)
        mods.push_back(matchedMod);
    }
  }
  mods.erase(std::remove_if(mods.begin(), mods.end(),
                            [](ModInfo::Ptr mod) -> bool {
                              return mod->nexusId() <= 0;
                            }),
             mods.end());
  for (const auto& mod : mods) {
    mod->setLastNexusUpdate(QDateTime());
  }

  std::sort(mods.begin(), mods.end(),
            [](QSharedPointer<ModInfo> a, QSharedPointer<ModInfo> b) -> bool {
              return a->getLastNexusUpdate() < b->getLastNexusUpdate();
            });

  if (!mods.empty()) {
    log::info("Checking updates for {} mods...", mods.size());

    // Every selected mod of a game goes into a single request, so checking a
    // selection costs the same one request per game as checking everything.
    std::map<QString, std::set<int>> byGame;
    for (const auto& mod : mods) {
      byGame[mod->gameName().toLower()].insert(mod->nexusId());
    }

    for (const auto& game : byGame) {
      requestModFileLists(game.first, game.second, receiver);
    }
  } else {
    log::info("None of the selected mods can be updated.");
  }
}

void ModInfo::setVersion(const VersionInfo& version)
{
  m_Version = version;
}

void ModInfo::setPluginSelected(const bool& isSelected)
{
  m_PluginSelected = isSelected;
}

void ModInfo::addCategory(const QString& categoryName)
{
  int id = CategoryFactory::instance().getCategoryID(categoryName);
  if (id == -1) {
    id = CategoryFactory::instance().addCategory(
        categoryName, std::vector<CategoryFactory::NexusCategory>(), 0);
  }
  setCategory(id, true);
}

bool ModInfo::removeCategory(const QString& categoryName)
{
  int const id = CategoryFactory::instance().getCategoryID(categoryName);
  if (id == -1) {
    return false;
  }
  if (!categorySet(id)) {
    return false;
  }
  setCategory(id, false);
  return true;
}

QStringList ModInfo::categories() const
{
  QStringList result;

  CategoryFactory const& catFac = CategoryFactory::instance();
  for (int const id : m_Categories) {
    result.append(catFac.getCategoryName(catFac.getCategoryIndex(id)));
  }

  return result;
}

bool ModInfo::hasFlag(ModInfo::EFlag flag) const
{
  std::vector<EFlag> flags = getFlags();
  return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

bool ModInfo::hasAnyOfTheseFlags(std::vector<ModInfo::EFlag> flags) const
{
  std::vector<EFlag> const modFlags = getFlags();
  for (auto modFlag : modFlags) {
    for (auto flag : flags) {
      if (modFlag == flag) {
        return true;
      }
    }
  }
  return false;
}

bool ModInfo::categorySet(int categoryID) const
{
  for (std::set<int>::const_iterator iter = m_Categories.begin();
       iter != m_Categories.end(); ++iter) {
    if ((*iter == categoryID) ||
        (CategoryFactory::instance().isDescendantOf(*iter, categoryID))) {
      return true;
    }
  }

  return false;
}

QUrl ModInfo::parseCustomURL() const
{
  if (!hasCustomURL() || url().isEmpty()) {
    return {};
  }

  const auto url = QUrl::fromUserInput(this->url());

  if (!url.isValid()) {
    log::error("mod '{}' has an invalid custom url '{}'", name(), this->url());
    return {};
  }

  return url;
}
