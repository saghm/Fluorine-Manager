#ifndef MOTOOLS_LOOT_H
#define MOTOOLS_LOOT_H

// LOOT integration stubbed out for Fluorine port.
// LootDialog/Loot impl/LootGroups removed (depended on Qt WebEngine + lootcli).
// Only the data types needed by ILootCache / PluginList::m_LootInfo remain.

#include <QString>
#include <vector>

namespace MOTools
{

class ILootCache;

class Loot
{
public:
  struct Message
  {
    int type = 0;
    QString text;
  };

  struct File
  {
    QString name;
    QString displayName;
  };

  struct Dirty
  {
    qint64 crc               = 0;
    qint64 itm               = 0;
    qint64 deletedReferences = 0;
    qint64 deletedNavmesh    = 0;
    QString cleaningUtility;
    QString info;

    QString toString(bool isClean) const
    {
      Q_UNUSED(isClean);
      return {};
    }
    QString cleaningString() const { return {}; }
  };

  struct Plugin
  {
    QString name;
    std::vector<File> incompatibilities;
    std::vector<Message> messages;
    std::vector<Dirty> dirty, clean;
    std::vector<QString> missingMasters;
    bool loadsArchive  = false;
    bool isMaster      = false;
    bool isLightMaster = false;
  };
};

}  // namespace MOTools

#endif  // MOTOOLS_LOOT_H
