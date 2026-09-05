#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(
      bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
  Q_PROPERTY(int artworkCacheLimitMb READ artworkCacheLimitMb WRITE setArtworkCacheLimitMb NOTIFY
                 artworkCacheLimitMbChanged)
  Q_PROPERTY(QString steamId READ steamId WRITE setSteamId NOTIFY steamIdChanged)
  Q_PROPERTY(
      QString igdbClientId READ igdbClientId WRITE setIgdbClientId NOTIFY igdbClientIdChanged)
  Q_PROPERTY(QString retroAchievementsUsername READ retroAchievementsUsername WRITE
                 setRetroAchievementsUsername NOTIFY retroAchievementsUsernameChanged)
  Q_PROPERTY(bool steamEnabled READ steamEnabled WRITE setSteamEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool lutrisEnabled READ lutrisEnabled WRITE setLutrisEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool heroicEnabled READ heroicEnabled WRITE setHeroicEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool gogEnabled READ gogEnabled WRITE setGogEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool faugusEnabled READ faugusEnabled WRITE setFaugusEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(
      bool retroArchEnabled READ retroArchEnabled WRITE setRetroArchEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool pcsx2Enabled READ pcsx2Enabled WRITE setPcsx2Enabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool ryujinxEnabled READ ryujinxEnabled WRITE setRyujinxEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(
      bool battleNetEnabled READ battleNetEnabled WRITE setBattleNetEnabled NOTIFY sourcesChanged)
  Q_PROPERTY(bool closeAfterLaunch READ closeAfterLaunch WRITE setCloseAfterLaunch NOTIFY
                 closeAfterLaunchChanged)
  Q_PROPERTY(bool couchModeEnabled READ couchModeEnabled WRITE setCouchModeEnabled NOTIFY
                 couchModeEnabledChanged)
  Q_PROPERTY(QString couchLibraryView READ couchLibraryView WRITE setCouchLibraryView NOTIFY
                 couchLibraryViewChanged)
  Q_PROPERTY(bool sunshineOmakadeApp READ sunshineOmakadeApp WRITE setSunshineOmakadeApp NOTIFY
                 sunshineChanged)
  Q_PROPERTY(bool sunshineGameApps READ sunshineGameApps WRITE setSunshineGameApps NOTIFY
                 sunshineChanged)

  Q_PROPERTY(QStringList gogLibraryPaths READ gogLibraryPaths NOTIFY gogLibraryPathsChanged)

public:
  explicit AppSettings(const QString& path = {}, QObject* parent = nullptr);

  [[nodiscard]] QJsonObject backupSettings() const;
  bool applyBackupSettings(const QJsonObject& settings, bool replace);
  [[nodiscard]] bool reducedMotion() const;
  void setReducedMotion(bool value);
  [[nodiscard]] int artworkCacheLimitMb() const;
  void setArtworkCacheLimitMb(int value);
  [[nodiscard]] QString steamId() const;
  void setSteamId(const QString& value);
  [[nodiscard]] QString igdbClientId() const;
  void setIgdbClientId(const QString& value);
  [[nodiscard]] QString retroAchievementsUsername() const;
  void setRetroAchievementsUsername(const QString& value);
  [[nodiscard]] bool steamEnabled() const;
  void setSteamEnabled(bool value);
  [[nodiscard]] bool lutrisEnabled() const;
  void setLutrisEnabled(bool value);
  [[nodiscard]] bool heroicEnabled() const;
  void setHeroicEnabled(bool value);
  [[nodiscard]] bool gogEnabled() const;
  void setGogEnabled(bool value);
  [[nodiscard]] bool faugusEnabled() const;
  void setFaugusEnabled(bool value);
  [[nodiscard]] bool retroArchEnabled() const;
  void setRetroArchEnabled(bool value);
  [[nodiscard]] bool pcsx2Enabled() const;
  void setPcsx2Enabled(bool value);
  [[nodiscard]] bool ryujinxEnabled() const;
  void setRyujinxEnabled(bool value);
  // True while the user has not written an explicit pcsx2_enabled/ryujinx_enabled key,
  // letting the app enable the source automatically when its emulator is detected.
  [[nodiscard]] bool pcsx2AutoEnabled() const;
  [[nodiscard]] bool ryujinxAutoEnabled() const;
  void setPcsx2AutoEnabled(bool value);
  void setRyujinxAutoEnabled(bool value);
  [[nodiscard]] bool battleNetEnabled() const;
  void setBattleNetEnabled(bool value);
  [[nodiscard]] bool closeAfterLaunch() const;
  void setCloseAfterLaunch(bool value);
  [[nodiscard]] bool couchModeEnabled() const;
  void setCouchModeEnabled(bool value);
  [[nodiscard]] QString couchLibraryView() const;
  void setCouchLibraryView(const QString& value);
  [[nodiscard]] bool sunshineOmakadeApp() const;
  void setSunshineOmakadeApp(bool value);
  [[nodiscard]] bool sunshineGameApps() const;
  void setSunshineGameApps(bool value);

  [[nodiscard]] QStringList gogLibraryPaths() const;
  Q_INVOKABLE bool addGogLibraryPath(const QString& path);
  Q_INVOKABLE bool removeGogLibraryPath(const QString& path);
  Q_INVOKABLE QString gogLibraryPathStatus(const QString& path) const;

signals:
  void gogLibraryPathsChanged();
  void reducedMotionChanged();
  void artworkCacheLimitMbChanged();
  void steamIdChanged();
  void igdbClientIdChanged();
  void retroAchievementsUsernameChanged();
  void sourcesChanged();
  void closeAfterLaunchChanged();
  void couchModeEnabledChanged();
  void couchLibraryViewChanged();
  void sunshineChanged();

private:
  struct UnloadedSettings {};
  explicit AppSettings(UnloadedSettings) : QObject(nullptr) {}
  void assignBackupSettings(const QJsonObject& settings);
  [[nodiscard]] static QString defaultPath();
  void load();
  bool save() const;

  QString m_path;
  QStringList m_gogLibraryPaths;
  bool m_reducedMotion = false;
  int m_artworkCacheLimitMb = 1024;
  QString m_steamId;
  QString m_igdbClientId;
  QString m_retroAchievementsUsername;
  bool m_steamEnabled = true;
  bool m_lutrisEnabled = true;
  bool m_heroicEnabled = true;
  bool m_gogEnabled = true;
  bool m_faugusEnabled = true;
  bool m_retroArchEnabled = true;
  bool m_pcsx2Enabled = false;
  bool m_ryujinxEnabled = false;
  bool m_pcsx2Auto = true;
  bool m_ryujinxAuto = true;
  bool m_battleNetEnabled = true;
  bool m_closeAfterLaunch = false;
  bool m_couchModeEnabled = false;
  QString m_couchLibraryView = QStringLiteral("detail");
  bool m_sunshineOmakadeApp = false;
  bool m_sunshineGameApps = false;
};
