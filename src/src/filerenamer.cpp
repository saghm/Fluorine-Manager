#include "filerenamer.h"
#include <QFileInfo>
#include <QMessageBox>
#include <filesystemutilities.h>
#include <log.h>
#include <utility.h>

using namespace MOBase;

FileRenamer::FileRenamer(QWidget* parent, QFlags<RenameFlags> flags)
    : m_parent(parent), m_flags(flags)
{
  // sanity check for flags
  if ((m_flags & (HIDE | UNHIDE)) == 0) {
    log::error("renameFile() missing hide flag");
    // doesn't really matter, it's just for text
    m_flags = HIDE;
  }
}

FileRenamer::RenameResults FileRenamer::rename(const QString& oldName,
                                               const QString& newName)
{
  log::debug("renaming {} to {}", oldName, newName);

  // Case-sensitivity safety net: if oldName doesn't resolve on disk but a
  // case-folded variant does, retarget the rename to the real path. This
  // hits when the source path comes from MO2's case-normalized
  // DirectoryEntry tree (e.g. Conflict tab bulk-hide) while the real
  // filesystem has different casing for parent dirs.
  QString effectiveOld = oldName;
  QString effectiveNew = newName;
  if (!QFileInfo::exists(effectiveOld)) {
    const QString resolved = MOBase::resolvePathCaseInsensitive(oldName);
    if (resolved != oldName && QFileInfo::exists(resolved)) {
      log::debug("resolved case-mismatched source '{}' -> '{}'", oldName,
                 resolved);
      effectiveOld = resolved;

      // Keep the destination's directory component aligned with the resolved
      // source so we don't create a parallel lowercase directory tree.
      const QFileInfo oldInfo(resolved);
      const QFileInfo newInfo(newName);
      effectiveNew = oldInfo.absolutePath() + '/' + newInfo.fileName();
    }
  }

  if (QFileInfo(effectiveNew).exists()) {
    log::debug("{} already exists", newName);

    // target file already exists, confirm replacement
    auto answer = confirmReplace(newName);

    switch (answer) {
    case DECISION_SKIP: {
      // user wants to skip this file
      log::debug("skipping {}", oldName);
      return RESULT_SKIP;
    }

    case DECISION_REPLACE: {
      log::debug("removing {}", newName);

      // user wants to replace the file, so remove it
      const auto r = shell::Delete(QFileInfo(newName));

      if (!r.success()) {
        log::error("failed to remove '{}': {}", newName, r.toString());

        // removal failed, warn the user and allow canceling
        if (!removeFailed(newName, r)) {
          log::debug("canceling {}", oldName);
          // user wants to cancel
          return RESULT_CANCEL;
        }

        // ignore this file and continue on
        log::debug("skipping {}", oldName);
        return RESULT_SKIP;
      }

      break;
    }

    case DECISION_CANCEL:  // fall-through
    default: {
      // user wants to stop
      log::debug("canceling");
      return RESULT_CANCEL;
    }
    }
  }

  // target either didn't exist or was removed correctly
  const auto r =
      shell::Rename(QFileInfo(effectiveOld), QFileInfo(effectiveNew));

  if (!r.success()) {
    log::error("failed to rename '{}' to '{}': {}", effectiveOld, effectiveNew,
               r.toString());

    // renaming failed, warn the user and allow canceling
    if (!renameFailed(effectiveOld, effectiveNew, r)) {
      // user wants to cancel
      log::debug("canceling");
      return RESULT_CANCEL;
    }

    // ignore this file and continue on
    log::debug("skipping {}", effectiveOld);
    return RESULT_SKIP;
  }

  // everything worked
  log::debug("successfully renamed {} to {}", effectiveOld, effectiveNew);
  return RESULT_OK;
}

FileRenamer::RenameDecision FileRenamer::confirmReplace(const QString& newName)
{
  if (m_flags & REPLACE_ALL) {
    // user wants to silently replace all
    log::debug("user has selected replace all");
    return DECISION_REPLACE;
  } else if (m_flags & REPLACE_NONE) {
    // user wants to silently skip all
    log::debug("user has selected replace none");
    return DECISION_SKIP;
  }

  QString text;

  if (m_flags & HIDE) {
    text =
        QObject::tr("The hidden file \"%1\" already exists. Replace it?").arg(newName);
  } else if (m_flags & UNHIDE) {
    text =
        QObject::tr("The visible file \"%1\" already exists. Replace it?").arg(newName);
  }

  auto buttons = QMessageBox::Yes | QMessageBox::No;
  if (m_flags & MULTIPLE) {
    // only show these buttons when there are multiple files to replace
    buttons |= QMessageBox::YesToAll | QMessageBox::NoToAll | QMessageBox::Cancel;
  }

  const auto answer =
      QMessageBox::question(m_parent, QObject::tr("Replace file?"), text, buttons);

  switch (answer) {
  case QMessageBox::Yes:
    log::debug("user wants to replace");
    return DECISION_REPLACE;

  case QMessageBox::No:
    log::debug("user wants to skip");
    return DECISION_SKIP;

  case QMessageBox::YesToAll:
    log::debug("user wants to replace all");
    // remember the answer
    m_flags |= REPLACE_ALL;
    return DECISION_REPLACE;

  case QMessageBox::NoToAll:
    log::debug("user wants to replace none");
    // remember the answer
    m_flags |= REPLACE_NONE;
    return DECISION_SKIP;

  case QMessageBox::Cancel:  // fall-through
  default:
    log::debug("user wants to cancel");
    return DECISION_CANCEL;
  }
}

bool FileRenamer::removeFailed(const QString& name, const shell::Result& r)
{
  QMessageBox::StandardButtons buttons = QMessageBox::Ok;
  if (m_flags & MULTIPLE) {
    // only show cancel for multiple files
    buttons |= QMessageBox::Cancel;
  }

  const auto answer = QMessageBox::critical(
      m_parent, QObject::tr("File operation failed"),
      QObject::tr("Failed to remove \"%1\": %2").arg(name).arg(r.toString()), buttons);

  if (answer == QMessageBox::Cancel) {
    // user wants to stop
    log::debug("user wants to cancel");
    return false;
  }

  // skip this one and continue
  log::debug("user wants to skip");
  return true;
}

bool FileRenamer::renameFailed(const QString& oldName, const QString& newName,
                               const shell::Result& r)
{
  QMessageBox::StandardButtons buttons = QMessageBox::Ok;
  if (m_flags & MULTIPLE) {
    // only show cancel for multiple files
    buttons |= QMessageBox::Cancel;
  }

  const auto answer =
      QMessageBox::critical(m_parent, QObject::tr("File operation failed"),
                            QObject::tr("Failed to rename file: %1.\r\n\r\n"
                                        "Source:\r\n\"%2\"\r\n\r\n"
                                        "Destination:\r\n\"%3\"")
                                .arg(r.toString())
                                .arg(QDir::toNativeSeparators(oldName))
                                .arg(QDir::toNativeSeparators(newName)),
                            buttons);

  if (answer == QMessageBox::Cancel) {
    // user wants to stop
    log::debug("user wants to cancel");
    return false;
  }

  // skip this one and continue
  log::debug("user wants to skip");
  return true;
}
