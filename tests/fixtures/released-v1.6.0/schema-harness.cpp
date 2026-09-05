// Build this file against the frozen v1.6.0 omakade_core, not the candidate.
// Constructors create the released schema. Do not run an event loop or refresh
// scanners, so this fixture neither discovers local games nor requests artwork.
#include "library/BattleNetGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/HeroicGameModel.h"
#include "library/LutrisGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/SteamGameModel.h"
#include "library/UnifiedGameModel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (argc != 2)
    return 2;
  const QString path = QString::fromLocal8Bit(argv[1]);
  if (!QDir::isAbsolutePath(path) || QFileInfo::exists(path))
    return 2;
  SteamGameModel steam(path);
  LutrisGameModel lutris(path);
  HeroicGameModel heroic(path);
  FaugusGameModel faugus(path);
  RetroArchGameModel retroarch(path);
  Pcsx2GameModel pcsx2(path);
  RyujinxGameModel ryujinx(path);
  BattleNetGameModel battlenet(path);
  UnifiedGameModel unified(path);
  return QFileInfo(path).isFile() ? 0 : 1;
}
