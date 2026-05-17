#ifndef STARFIELDSAVEGAME_H
#define STARFIELDSAVEGAME_H

#include "gamebryosavegame.h"
#include "windows_compat.h"
#include <QDataStream>

class GameStarfield;

class StarfieldSaveGame : public GamebryoSaveGame
{
public:
  StarfieldSaveGame(QString const& fileName, GameStarfield const* game);

  const QStringList v122CorePlugins = {"Starfield.esm", "Constellation.esm",
                                       "OldMars.esm",   "BlueprintShips-Starfield.esm",
                                       "SFBGS003.esm",  "SFBGS006.esm",
                                       "SFBGS007.esm",  "SFBGS008.esm"};

protected:
  // Fetch easy-to-access information.
  void getData(FileWrapper& file) const;

  void fetchInformationFields(FileWrapper& file, uint32_t& saveNumber,
                              unsigned char& saveVersion, QString& playerName,
                              unsigned short& playerLevel, QString& playerLocation,
                              FILETIME& creationTime) const;

  std::unique_ptr<DataFields> fetchDataFields() const override;
};

#endif  // STARFIELDSAVEGAME_H
