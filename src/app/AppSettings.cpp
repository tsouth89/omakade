#include "app/AppSettings.h"
#include "backup/BackupArchive.h"
#include <QDateTime>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString normalizedLibraryPath(QString path) {
  if (path.startsWith(QStringLiteral("file:"))) {
    const QUrl url(path);
    if (!url.isLocalFile() || !url.host().isEmpty()) return {};
    path = url.toLocalFile();
  }
  if (path.startsWith(QStringLiteral("~/"))) path = QDir::homePath() + path.mid(1);
  if (!QDir::isAbsolutePath(path) || path.size() > 4096) return {};
  for (QChar c : path) {
    if (c.category() == QChar::Other_Control) return {};
  }
  return QDir::cleanPath(path);
}
}

QStringList AppSettings::gogLibraryPaths() const { return m_gogLibraryPaths; }

bool AppSettings::addGogLibraryPath(const QString& value) {
  const QString path = normalizedLibraryPath(value);
  if (path.isEmpty() || m_gogLibraryPaths.size() >= 64) return false;
  if (m_gogLibraryPaths.contains(path)) return true;
  m_gogLibraryPaths.append(path);
  if (!save()) {
    m_gogLibraryPaths.removeLast();
    return false;
  }
  emit gogLibraryPathsChanged();
  return true;
}

bool AppSettings::removeGogLibraryPath(const QString& path) {
  const int index = m_gogLibraryPaths.indexOf(path);
  if (index < 0) return false;
  m_gogLibraryPaths.removeAt(index);
  if (!save()) {
    m_gogLibraryPaths.insert(index, path);
    return false;
  }
  emit gogLibraryPathsChanged();
  return true;
}

QString AppSettings::gogLibraryPathStatus(const QString& path) const {
  const QFileInfo directory(path);
  if (!directory.exists()) return QStringLiteral("Unavailable. Reconnect the drive to scan this folder.");
  if (!directory.isDir()) return QStringLiteral("This path is not a folder.");
  if (!QDir(path).isReadable()) return QStringLiteral("This folder cannot be read. Check its permissions.");
  return QStringLiteral("Available");
}

AppSettings::AppSettings(const QString& path, QObject* parent)
    : QObject(parent), m_path(path.isEmpty() ? defaultPath() : path) {
  load();
}

QJsonObject AppSettings::backupSettings() const {
  return {{"reduced_motion", m_reducedMotion}, {"artwork_cache_limit_mb", m_artworkCacheLimitMb},
      {"steam_enabled", m_steamEnabled}, {"lutris_enabled", m_lutrisEnabled},
      {"heroic_enabled", m_heroicEnabled}, {"gog_enabled", m_gogEnabled},
      {"faugus_enabled", m_faugusEnabled}, {"retroarch_enabled", m_retroArchEnabled},
      {"pcsx2_enabled", m_pcsx2Enabled}, {"ryujinx_enabled", m_ryujinxEnabled},
      {"pcsx2_auto", m_pcsx2Auto}, {"ryujinx_auto", m_ryujinxAuto},
      {"battlenet_enabled", m_battleNetEnabled}, {"close_after_launch", m_closeAfterLaunch},
      {"couch_mode", m_couchModeEnabled}, {"couch_library_view", m_couchLibraryView},
      {"gog_library_paths", QJsonArray::fromStringList(m_gogLibraryPaths)}};
}

void AppSettings::assignBackupSettings(const QJsonObject& settings) {
  m_reducedMotion = settings.value("reduced_motion").toBool();
  m_artworkCacheLimitMb = settings.value("artwork_cache_limit_mb").toInt();
  m_steamEnabled = settings.value("steam_enabled").toBool();
  m_lutrisEnabled = settings.value("lutris_enabled").toBool();
  m_heroicEnabled = settings.value("heroic_enabled").toBool();
  m_gogEnabled = settings.value("gog_enabled").toBool();
  m_faugusEnabled = settings.value("faugus_enabled").toBool();
  m_retroArchEnabled = settings.value("retroarch_enabled").toBool();
  m_pcsx2Enabled = settings.value("pcsx2_enabled").toBool();
  m_ryujinxEnabled = settings.value("ryujinx_enabled").toBool();
  m_pcsx2Auto = settings.value("pcsx2_auto").toBool();
  m_ryujinxAuto = settings.value("ryujinx_auto").toBool();
  m_battleNetEnabled = settings.value("battlenet_enabled").toBool();
  m_closeAfterLaunch = settings.value("close_after_launch").toBool();
  m_couchModeEnabled = settings.value("couch_mode").toBool();
  m_couchLibraryView = settings.value("couch_library_view").toString();
  m_gogLibraryPaths.clear();
  for (const auto& path : settings.value("gog_library_paths").toArray()) {
    const QString normalized = normalizedLibraryPath(path.toString());
    if (!normalized.isEmpty() && !m_gogLibraryPaths.contains(normalized)) m_gogLibraryPaths.append(normalized);
  }
}

bool AppSettings::applyBackupSettings(const QJsonObject& settings, bool replace) {
  BackupPayload check;
  check.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  check.settings = settings;
  if (!BackupArchive::validate(check)) return false;
  const auto before = backupSettings();
  AppSettings defaults(UnloadedSettings{});
  auto merged = replace ? defaults.backupSettings() : before;
  for (auto value = settings.begin(); value != settings.end(); ++value) merged.insert(value.key(), value.value());
  assignBackupSettings(merged);
  if (!save()) { assignBackupSettings(before); return false; }
  emit reducedMotionChanged(); emit artworkCacheLimitMbChanged(); emit sourcesChanged();
  emit closeAfterLaunchChanged(); emit couchModeEnabledChanged(); emit couchLibraryViewChanged();
  emit gogLibraryPathsChanged();
  return true;
}

bool AppSettings::reducedMotion() const { return m_reducedMotion; }

void AppSettings::setReducedMotion(bool value) {
  if (m_reducedMotion == value) {
    return;
  }
  m_reducedMotion = value;
  save();
  emit reducedMotionChanged();
}

int AppSettings::artworkCacheLimitMb() const { return m_artworkCacheLimitMb; }

void AppSettings::setArtworkCacheLimitMb(int value) {
  value = qBound(128, value, 8192);
  if (m_artworkCacheLimitMb == value) {
    return;
  }
  m_artworkCacheLimitMb = value;
  save();
  emit artworkCacheLimitMbChanged();
}

QString AppSettings::steamId() const { return m_steamId; }

void AppSettings::setSteamId(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^7656119[0-9]{10}$"));
  const QString normalized = value.trimmed();
  if ((!normalized.isEmpty() && !valid.match(normalized).hasMatch()) || m_steamId == normalized) {
    return;
  }
  m_steamId = normalized;
  save();
  emit steamIdChanged();
}

QString AppSettings::igdbClientId() const { return m_igdbClientId; }

void AppSettings::setIgdbClientId(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9]{5,64}$"));
  const QString normalized = value.trimmed();
  if ((!normalized.isEmpty() && !valid.match(normalized).hasMatch()) ||
      m_igdbClientId == normalized) {
    return;
  }
  m_igdbClientId = normalized;
  save();
  emit igdbClientIdChanged();
}

QString AppSettings::retroAchievementsUsername() const { return m_retroAchievementsUsername; }

void AppSettings::setRetroAchievementsUsername(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{2,20}$"));
  const QString normalized = value.trimmed();
  if ((!normalized.isEmpty() && !valid.match(normalized).hasMatch()) ||
      m_retroAchievementsUsername == normalized) {
    return;
  }
  m_retroAchievementsUsername = normalized;
  save();
  emit retroAchievementsUsernameChanged();
}

bool AppSettings::steamEnabled() const { return m_steamEnabled; }

void AppSettings::setSteamEnabled(bool value) {
  if (m_steamEnabled == value) {
    return;
  }
  m_steamEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::lutrisEnabled() const { return m_lutrisEnabled; }

void AppSettings::setLutrisEnabled(bool value) {
  if (m_lutrisEnabled == value) {
    return;
  }
  m_lutrisEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::heroicEnabled() const { return m_heroicEnabled; }

void AppSettings::setHeroicEnabled(bool value) {
  if (m_heroicEnabled == value) {
    return;
  }
  m_heroicEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::gogEnabled() const { return m_gogEnabled; }

void AppSettings::setGogEnabled(bool value) {
  if (m_gogEnabled == value) {
    return;
  }
  m_gogEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::faugusEnabled() const { return m_faugusEnabled; }

void AppSettings::setFaugusEnabled(bool value) {
  if (m_faugusEnabled == value) {
    return;
  }
  m_faugusEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::retroArchEnabled() const { return m_retroArchEnabled; }

void AppSettings::setRetroArchEnabled(bool value) {
  if (m_retroArchEnabled == value) {
    return;
  }
  m_retroArchEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::pcsx2Enabled() const { return m_pcsx2Enabled; }

void AppSettings::setPcsx2Enabled(bool value) {
  const bool wasAuto = m_pcsx2Auto;
  m_pcsx2Auto = false;  // an explicit user choice disables automatic detection
  if (m_pcsx2Enabled == value && !wasAuto) {
    return;
  }
  m_pcsx2Enabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::battleNetEnabled() const { return m_battleNetEnabled; }

void AppSettings::setBattleNetEnabled(bool value) {
  if (m_battleNetEnabled == value) {
    return;
  }
  m_battleNetEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::ryujinxEnabled() const { return m_ryujinxEnabled; }

void AppSettings::setRyujinxEnabled(bool value) {
  const bool wasAuto = m_ryujinxAuto;
  m_ryujinxAuto = false;  // an explicit user choice disables automatic detection
  if (m_ryujinxEnabled == value && !wasAuto) {
    return;
  }
  m_ryujinxEnabled = value;
  save();
  emit sourcesChanged();
}

bool AppSettings::pcsx2AutoEnabled() const { return m_pcsx2Auto; }

void AppSettings::setPcsx2AutoEnabled(bool value) { m_pcsx2Auto = value; }

bool AppSettings::ryujinxAutoEnabled() const { return m_ryujinxAuto; }

void AppSettings::setRyujinxAutoEnabled(bool value) { m_ryujinxAuto = value; }

bool AppSettings::closeAfterLaunch() const { return m_closeAfterLaunch; }

void AppSettings::setCloseAfterLaunch(bool value) {
  if (m_closeAfterLaunch == value) {
    return;
  }
  m_closeAfterLaunch = value;
  save();
  emit closeAfterLaunchChanged();
}

bool AppSettings::couchModeEnabled() const { return m_couchModeEnabled; }

void AppSettings::setCouchModeEnabled(bool value) {
  if (m_couchModeEnabled == value) {
    return;
  }
  m_couchModeEnabled = value;
  save();
  emit couchModeEnabledChanged();
}

QString AppSettings::couchLibraryView() const { return m_couchLibraryView; }

void AppSettings::setCouchLibraryView(const QString& value) {
  const QString normalized = value == QStringLiteral("grid") ? value : QStringLiteral("detail");
  if (m_couchLibraryView == normalized) {
    return;
  }
  m_couchLibraryView = normalized;
  save();
  emit couchLibraryViewChanged();
}

QString AppSettings::defaultPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/omakade/config.toml");
}

void AppSettings::load() {
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QString contents = QString::fromUtf8(file.readAll());
  const QRegularExpression pathsExpression(QStringLiteral("(?m)^gog_library_paths[ \t]*=[ \t]*(\\[[^\\r\\n]*\\])[ \t]*$"));
  const auto pathsMatch = pathsExpression.match(contents);
  if (pathsMatch.hasMatch()) {
    const auto paths = QJsonDocument::fromJson(pathsMatch.captured(1).toUtf8()).array();
    for (const auto& value : paths) {
      if (!value.isString()) continue;
      const QString path = normalizedLibraryPath(value.toString());
      if (!path.isEmpty() && !m_gogLibraryPaths.contains(path) && m_gogLibraryPaths.size() < 64)
        m_gogLibraryPaths.append(path);
    }
  }
  const QRegularExpression motion(QStringLiteral("(?m)^reduced_motion\\s*=\\s*(true|false)\\s*$"));
  const QRegularExpressionMatch motionMatch = motion.match(contents);
  if (motionMatch.hasMatch()) {
    m_reducedMotion = motionMatch.captured(1) == QStringLiteral("true");
  }
  const QRegularExpression limit(QStringLiteral("(?m)^artwork_cache_limit_mb\\s*=\\s*(\\d+)\\s*$"));
  const QRegularExpressionMatch limitMatch = limit.match(contents);
  if (limitMatch.hasMatch()) {
    m_artworkCacheLimitMb = qBound(128, limitMatch.captured(1).toInt(), 8192);
  }
  const QRegularExpression steamId(
      QStringLiteral("(?m)^steam_id\\s*=\\s*\"(7656119[0-9]{10})\"\\s*$"));
  const QRegularExpressionMatch steamIdMatch = steamId.match(contents);
  if (steamIdMatch.hasMatch()) {
    m_steamId = steamIdMatch.captured(1);
  }
  const QRegularExpression igdbClientId(
      QStringLiteral("(?m)^igdb_client_id\\s*=\\s*\"([A-Za-z0-9]{5,64})\"\\s*$"));
  const QRegularExpressionMatch igdbClientIdMatch = igdbClientId.match(contents);
  if (igdbClientIdMatch.hasMatch()) {
    m_igdbClientId = igdbClientIdMatch.captured(1);
  }
  const QRegularExpression retroAchievementsUsername(
      QStringLiteral("(?m)^retroachievements_username\\s*=\\s*\"([A-Za-z0-9_-]{2,20})\"\\s*$"));
  const QRegularExpressionMatch retroAchievementsUsernameMatch =
      retroAchievementsUsername.match(contents);
  if (retroAchievementsUsernameMatch.hasMatch()) {
    m_retroAchievementsUsername = retroAchievementsUsernameMatch.captured(1);
  }
  const auto readEnabled = [&contents](const QString& key, bool fallback) {
    const QRegularExpression expression(
        QStringLiteral("(?m)^%1\\s*=\\s*(true|false)\\s*$").arg(key));
    const QRegularExpressionMatch match = expression.match(contents);
    return match.hasMatch() ? match.captured(1) == QStringLiteral("true") : fallback;
  };
  m_steamEnabled = readEnabled(QStringLiteral("steam_enabled"), true);
  m_lutrisEnabled = readEnabled(QStringLiteral("lutris_enabled"), true);
  m_heroicEnabled = readEnabled(QStringLiteral("heroic_enabled"), true);
  m_gogEnabled = readEnabled(QStringLiteral("gog_enabled"), true);
  m_faugusEnabled = readEnabled(QStringLiteral("faugus_enabled"), true);
  m_retroArchEnabled = readEnabled(QStringLiteral("retroarch_enabled"), true);
  const QRegularExpression pcsx2Key(
      QStringLiteral("(?m)^pcsx2_enabled\\s*=\\s*(true|false)\\s*$"));
  m_pcsx2Auto = !pcsx2Key.match(contents).hasMatch();
  m_pcsx2Enabled = readEnabled(QStringLiteral("pcsx2_enabled"), false);
  const QRegularExpression ryujinxKey(
      QStringLiteral("(?m)^ryujinx_enabled\\s*=\\s*(true|false)\\s*$"));
  m_ryujinxAuto = !ryujinxKey.match(contents).hasMatch();
  m_ryujinxEnabled = readEnabled(QStringLiteral("ryujinx_enabled"), false);
  m_battleNetEnabled = readEnabled(QStringLiteral("battlenet_enabled"), true);
  m_closeAfterLaunch = readEnabled(QStringLiteral("close_after_launch"), false);
  m_couchModeEnabled = readEnabled(QStringLiteral("couch_mode_enabled"), false);
  const QRegularExpression couchLibraryView(
      QStringLiteral("(?m)^couch_library_view\\s*=\\s*\"(detail|grid)\"\\s*$"));
  const QRegularExpressionMatch couchLibraryViewMatch = couchLibraryView.match(contents);
  if (couchLibraryViewMatch.hasMatch()) {
    m_couchLibraryView = couchLibraryViewMatch.captured(1);
  }
  m_sunshineOmakadeApp = readEnabled(QStringLiteral("sunshine_omakade_app"), false);
  m_sunshineGameApps = readEnabled(QStringLiteral("sunshine_game_apps"), false);
}

bool AppSettings::sunshineOmakadeApp() const { return m_sunshineOmakadeApp; }

void AppSettings::setSunshineOmakadeApp(bool value) {
  if (m_sunshineOmakadeApp == value) {
    return;
  }
  m_sunshineOmakadeApp = value;
  save();
  emit sunshineChanged();
}

bool AppSettings::sunshineGameApps() const { return m_sunshineGameApps; }

void AppSettings::setSunshineGameApps(bool value) {
  if (m_sunshineGameApps == value) {
    return;
  }
  m_sunshineGameApps = value;
  save();
  emit sunshineChanged();
}

bool AppSettings::save() const {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }
  // Emulator source keys are written only once their state is explicit (detection
  // completed or the user chose a value); while auto-detection is pending the keys
  // stay absent so a later emulator installation can still enable its source.
  QString contents =
      QStringLiteral("reduced_motion = %1\nartwork_cache_limit_mb = %2\nsteam_id = \"%3\"\n"
                     "igdb_client_id = \"%4\"\nretroachievements_username = \"%5\"\n"
                     "steam_enabled = %6\nlutris_enabled = %7\nheroic_enabled = %8\n"
                     "gog_enabled = %9\nfaugus_enabled = %10\nretroarch_enabled = %11\n"
                     "battlenet_enabled = %12\n")
          .arg(m_reducedMotion ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_artworkCacheLimitMb)
          .arg(m_steamId)
          .arg(m_igdbClientId)
          .arg(m_retroAchievementsUsername)
          .arg(m_steamEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_lutrisEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_heroicEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_gogEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_faugusEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_retroArchEnabled ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(m_battleNetEnabled ? QStringLiteral("true") : QStringLiteral("false"));
  if (!m_pcsx2Auto) {
    contents += QStringLiteral("pcsx2_enabled = %1\n")
                    .arg(m_pcsx2Enabled ? QStringLiteral("true") : QStringLiteral("false"));
  }
  if (!m_ryujinxAuto) {
    contents += QStringLiteral("ryujinx_enabled = %1\n")
                    .arg(m_ryujinxEnabled ? QStringLiteral("true") : QStringLiteral("false"));
  }
  contents += QStringLiteral("close_after_launch = %1\n"
                             "couch_mode_enabled = %2\n"
                             "couch_library_view = \"%3\"\n"
                             "sunshine_omakade_app = %4\nsunshine_game_apps = %5\n")
                  .arg(m_closeAfterLaunch ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(m_couchModeEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(m_couchLibraryView)
                  .arg(m_sunshineOmakadeApp ? QStringLiteral("true") : QStringLiteral("false"))
                  .arg(m_sunshineGameApps ? QStringLiteral("true") : QStringLiteral("false"));
  contents += QStringLiteral("gog_library_paths = ") +
              QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(m_gogLibraryPaths))
                                   .toJson(QJsonDocument::Compact)) + QLatin1Char('\n');
  const QByteArray encoded = contents.toUtf8();
  return file.write(encoded) == encoded.size() && file.commit();
}
