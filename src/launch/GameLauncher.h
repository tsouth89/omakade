#pragma once

#include "launch/LaunchCommand.h"

#include <QObject>
#include <QStringList>

class GameLauncher final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
  explicit GameLauncher(QObject* parent = nullptr);

  [[nodiscard]] QString lastError() const;
  [[nodiscard]] static LaunchCommand lutrisCommand(const QString& id, bool flatpak);
  [[nodiscard]] static LaunchCommand heroicCommand(const QString& id, const QString& runner,
                                                   bool flatpak);
  [[nodiscard]] static LaunchCommand faugusCommand(const QString& id, bool flatpak);
  [[nodiscard]] static LaunchCommand retroArchCommand(const QString& contentPath,
                                                      const QString& corePath, bool flatpak);
  [[nodiscard]] static LaunchCommand pcsx2Command(const QString& id, bool isElf, bool flatpak);
  [[nodiscard]] static LaunchCommand ryujinxCommand(const QString& id,
                                                    const QString& nativeExecutable,
                                                    const QString& flatpakAppId =
                                                        QStringLiteral("io.github.ryubing.Ryujinx"));
  [[nodiscard]] static LaunchCommand battleNetCommand(const QString& id, const QString& prefix,
                                                      const QString& runner, bool flatpak);
  [[nodiscard]] static LaunchCommand gogCommand(const QString& id, const QString& installPath,
                                                const QString& winePrefix = {});
  Q_INVOKABLE bool launch(const QString& source, const QString& id, bool flatpak = false,
                          const QString& runner = {}, const QString& installPath = {},
                          const QString& launchTarget = {});
  Q_INVOKABLE bool manage(const QString& source, const QString& id, bool flatpak = false,
                          const QString& runner = {}, const QString& launchTarget = {});
  Q_INVOKABLE bool install(const QString& source, const QString& id);

signals:
  void lastErrorChanged();

private:
  bool launchLutris(const QString& id, bool flatpak, bool manageOnly);
  bool launchHeroic(const QString& id, const QString& runner, bool flatpak, bool manageOnly);
  bool launchFaugus(const QString& id, bool flatpak, bool manageOnly);
  bool launchRetroArch(const QString& contentPath, const QString& corePath, bool flatpak,
                       bool manageOnly);
  bool launchPcsx2(const QString& id, bool isElf, bool flatpak, bool manageOnly);
  bool launchRyujinx(const QString& id, bool flatpak, const QString& flatpakAppId,
                     bool manageOnly);
  bool launchBattleNet(const QString& id, const QString& prefix, const QString& runner,
                       bool flatpak, bool manageOnly);
  bool launchGog(const QString& id, const QString& installPath, bool manageOnly);
  [[nodiscard]] QString flatpakError(const QString& appId, const QString& launcherName) const;
  void setError(const QString& error);
  QString m_lastError;
};
