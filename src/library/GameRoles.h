#pragma once

#include <QByteArray>
#include <QHash>
#include <Qt>

namespace GameRoles {
enum Role {
  Title = Qt::UserRole + 1,
  Subtitle,
  Description,
  Hours,
  Progress,
  AchievementsUnlocked,
  AchievementsTotal,
  Favorite,
  Recent,
  LastPlayed,
  AccentStart,
  AccentEnd,
  CoverMark,
  Year,
  AppId,
  CoverPath,
  HeroPath,
  LogoPath,
  InstallPath,
  Source,
  Runner,
  Flatpak,
  Hidden,
  CustomCover,
  Linked,
  LinkedSources,
  CompletionStatus,
  Tags,
  Collections,
  LaunchTarget,
  Installed,
  CustomHero,
  CustomLogo,
};

inline QHash<int, QByteArray> names() {
  return {
      {Title, "title"},
      {Subtitle, "subtitle"},
      {Description, "description"},
      {Hours, "hours"},
      {Progress, "progress"},
      {AchievementsUnlocked, "achievementsUnlocked"},
      {AchievementsTotal, "achievementsTotal"},
      {Favorite, "favorite"},
      {Recent, "recent"},
      {LastPlayed, "lastPlayed"},
      {AccentStart, "accentStart"},
      {AccentEnd, "accentEnd"},
      {CoverMark, "coverMark"},
      {Year, "year"},
      {AppId, "appId"},
      {CoverPath, "coverPath"},
      {HeroPath, "heroPath"},
      {LogoPath, "logoPath"},
      {InstallPath, "installPath"},
      {Source, "source"},
      {Runner, "runner"},
      {Flatpak, "flatpak"},
      {Hidden, "hidden"},
  };
}
} // namespace GameRoles
