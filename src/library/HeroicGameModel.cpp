#include "library/HeroicGameModel.h"

#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <utility>
#include <utility>

namespace {
QColor colorFor(const QString& key, int offset) {
  const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

QString storeName(const QString& runner) {
  if (runner == QStringLiteral("legendary")) {
    return QStringLiteral("Epic");
  }
  if (runner == QStringLiteral("gog")) {
    return QStringLiteral("GOG");
  }
  if (runner == QStringLiteral("nile")) {
    return QStringLiteral("Amazon");
  }
  if (runner == QStringLiteral("sideload")) {
    return QStringLiteral("Sideload");
  }
  return QStringLiteral("Heroic");
}
} // namespace

HeroicGameModel::HeroicGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-heroic-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<HeroicScanResult>::finished, this,
          [this] {
            m_scanning = false;
            applyScan(m_scanWatcher.result());
            emit statusChanged();
            if (m_refreshPending) {
              m_refreshPending = false;
              refresh();
            }
          });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
    QSqlQuery roots(m_database);
    if (roots.exec(QStringLiteral("SELECT path, removed FROM gog_configured_roots"))) {
      while (roots.next()) {
        if (roots.value(1).toBool()) m_removedGogRoots.append(roots.value(0).toString());
        else m_gogLibraryPaths.append(roots.value(0).toString());
      }
    }
  }
}

HeroicGameModel::~HeroicGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int HeroicGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant HeroicGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> HeroicGameModel::roleNames() const {
  return GameRoles::names();
}

bool HeroicGameModel::heroicDetected() const { return m_heroicDetected; }
bool HeroicGameModel::gogDetected() const { return m_gogDetected; }
QString HeroicGameModel::statusText() const { return m_statusText; }
QString HeroicGameModel::errorText() const { return m_errorText; }
QStringList HeroicGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 HeroicGameModel::lastScan() const { return m_lastScan; }

void HeroicGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE heroic_games SET favorite = ? WHERE game_key = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.heroic.key);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void HeroicGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE heroic_games SET hidden = ? WHERE game_key = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.heroic.key);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void HeroicGameModel::setGogLibraryPaths(const QStringList& paths) {
  for (const QString& previous : m_gogLibraryPaths) {
    if (!paths.contains(previous)) m_removedGogRoots.append(previous);
  }
  for (const QString& path : paths) m_removedGogRoots.removeAll(path);
  m_gogLibraryPaths = paths;
  if (!m_database.isOpen() || !m_database.transaction()) return;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("INSERT OR REPLACE INTO gog_configured_roots(path, removed) VALUES(?, ?)"));
  bool okay = true;
  for (const QString& path : m_removedGogRoots) {
    query.bindValue(0, path);
    query.bindValue(1, true);
    okay = okay && query.exec();
  }
  for (const QString& path : paths) {
    query.bindValue(0, path);
    query.bindValue(1, false);
    okay = okay && query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(m_statusText, QStringLiteral("Could not save GOG folder scan state"));
  }
}

void HeroicGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    m_refreshPending = true;
    return;
  }
  m_scanning = true;
  const QStringList roots = HeroicScanner::discoverRoots(m_gogLibraryPaths);
  setStatus(QStringLiteral("Scanning Heroic and GOG libraries"));
  const QStringList removed = std::exchange(m_removedGogRoots, {});
  m_scanWatcher.setFuture(QtConcurrent::run([roots, removed] {
    auto result = HeroicScanner::scan(roots);
    result.removedGogRoots = removed;
    return result;
  }));
}
void HeroicGameModel::refreshFromRoots(const QStringList& roots) {
  auto result = HeroicScanner::scan(roots);
  result.removedGogRoots = std::exchange(m_removedGogRoots, {});
  applyScan(result);
}

bool HeroicGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    setStatus(QStringLiteral("Heroic cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool HeroicGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS heroic_games (game_key TEXT PRIMARY KEY, app_id TEXT NOT NULL, "
      "runner TEXT NOT NULL, name TEXT NOT NULL, directory TEXT, cover_path TEXT, hero_path TEXT, "
      "flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT "
      "NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    return false;
  }
  if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS gog_configured_roots "
                                 "(path TEXT PRIMARY KEY, removed INTEGER NOT NULL DEFAULT 0)"))) {
    return false;
  }
  bool hasPaths = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(source_state)"))) {
    while (query.next()) {
      hasPaths = hasPaths || query.value(1).toString() == QStringLiteral("paths");
    }
  }
  if (!hasPaths && !query.exec(QStringLiteral(
                       "ALTER TABLE source_state ADD COLUMN paths TEXT NOT NULL DEFAULT ''"))) {
    return false;
  }
  bool hasPlaytime = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(heroic_games)"))) {
    while (query.next()) {
      hasPlaytime = hasPlaytime || query.value(1).toString() == QStringLiteral("playtime_minutes");
    }
  }
  return hasPlaytime ||
         (query.exec(QStringLiteral("ALTER TABLE heroic_games ADD COLUMN playtime_minutes "
                                    "INTEGER NOT NULL DEFAULT 0")) &&
          query.exec(QStringLiteral(
              "ALTER TABLE heroic_games ADD COLUMN last_played INTEGER NOT NULL DEFAULT 0")));
}

void HeroicGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_key, app_id, runner, name, directory, cover_path, hero_path, flatpak, "
          "favorite, hidden, playtime_minutes, last_played FROM heroic_games WHERE observed_at > 0 "
          "ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Heroic games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    HeroicGameRecord record{.key = query.value(0).toString(),
                            .appId = query.value(1).toString(),
                            .runner = query.value(2).toString(),
                            .title = query.value(3).toString(),
                            .installPath = query.value(4).toString(),
                            .coverPath = query.value(5).toString(),
                            .heroPath = query.value(6).toString(),
                            .playtimeMinutes = query.value(10).toInt(),
                            .lastPlayed = query.value(11).toLongLong(),
                            .flatpak = query.value(7).toBool()};
    loaded.append({.heroic = record,
                   .favorite = query.value(8).toBool(),
                   .hidden = query.value(9).toBool(),
                   .accentStart = colorFor(record.key, 0),
                   .accentEnd = colorFor(record.key, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
  m_gogDetected = std::any_of(m_games.cbegin(), m_games.cend(), [](const Game& game) {
    return game.heroic.runner == QStringLiteral("gog-direct");
  });
  m_heroicDetected = std::any_of(m_games.cbegin(), m_games.cend(), [](const Game& game) {
    return game.heroic.runner != QStringLiteral("gog-direct");
  });
}

void HeroicGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'heroic'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_heroicDetected = std::any_of(m_games.cbegin(), m_games.cend(), [](const Game& game) {
    return game.heroic.runner != QStringLiteral("gog-direct");
  });
  if (!m_heroicDetected) {
    m_heroicDetected = std::any_of(
        m_detectedPaths.cbegin(), m_detectedPaths.cend(), [](const QString& path) {
          return path.endsWith(QStringLiteral("/heroic")) ||
                 path.contains(QStringLiteral("com.heroicgameslauncher.hgl"));
        });
  }
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Heroic library");
  }
}

void HeroicGameModel::applyScan(const HeroicScanResult& result) {
  const bool missingHeroicRoots = result.roots.isEmpty() &&
      std::any_of(m_games.cbegin(), m_games.cend(), [](const Game& game) {
        return game.heroic.runner != QStringLiteral("gog-direct");
      });
  const bool missingGogRoots = result.gogRoots.isEmpty() &&
      std::any_of(m_games.cbegin(), m_games.cend(), [](const Game& game) {
        return game.heroic.runner == QStringLiteral("gog-direct");
      });
  const bool heroicIncomplete = result.incomplete || missingHeroicRoots;
  const bool gogIncomplete = result.gogIncomplete || missingGogRoots;
  if (heroicIncomplete && gogIncomplete && result.removedGogRoots.isEmpty()) {
    setStatus(QStringLiteral("Heroic/GOG scan interrupted; kept cached results"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Heroic games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = true;
  if (!heroicIncomplete) {
    okay = query.exec(QStringLiteral(
        "UPDATE heroic_games SET observed_at = 0 WHERE runner != 'gog-direct'"));
  }
  if (!gogIncomplete) {
    okay = okay && query.exec(QStringLiteral(
                         "UPDATE heroic_games SET observed_at = 0 WHERE runner = 'gog-direct'"));
  }
  if (!gogIncomplete && !result.unavailableGogRoots.isEmpty()) {
    query.prepare(QStringLiteral("UPDATE heroic_games SET observed_at = ? WHERE game_key = ?"));
    for (const Game& cached : std::as_const(m_games)) {
      if (cached.heroic.runner != QStringLiteral("gog-direct")) continue;
      const QString path = QDir::cleanPath(cached.heroic.installPath);
      for (const QString& root : result.unavailableGogRoots) {
        if (path == root || path.startsWith(root + QLatin1Char('/'))) {
          query.bindValue(0, scanTimestamp);
          query.bindValue(1, cached.heroic.key);
          okay = okay && query.exec();
          break;
        }
      }
    }
  }
  if (!result.removedGogRoots.isEmpty()) {
    query.prepare(QStringLiteral("UPDATE heroic_games SET observed_at = 0 WHERE game_key = ?"));
    for (const Game& cached : std::as_const(m_games)) {
      if (cached.heroic.runner != QStringLiteral("gog-direct")) continue;
      const QString path = QDir::cleanPath(cached.heroic.installPath);
      for (const QString& root : result.removedGogRoots) {
        if (path == root || path.startsWith(root + QLatin1Char('/'))) {
          query.bindValue(0, cached.heroic.key);
          okay = okay && query.exec();
          break;
        }
      }
    }
  }
  QSet<QString> cachedManagedGogPaths;
  if (result.managedGogIncomplete) {
    for (const Game& cached : std::as_const(m_games)) {
      if (cached.heroic.runner == QStringLiteral("gog")) {
        cachedManagedGogPaths.insert(QDir::cleanPath(cached.heroic.installPath));
      }
    }
  }
  for (const HeroicGameRecord& game : result.games) {
    const bool directGog = game.runner == QStringLiteral("gog-direct");
    if (directGog ? gogIncomplete : heroicIncomplete) {
      continue;
    }
    if (directGog &&
        cachedManagedGogPaths.contains(QDir::cleanPath(game.installPath))) {
      continue;
    }
    query.prepare(QStringLiteral(
        "INSERT INTO heroic_games(game_key, app_id, runner, name, directory, cover_path, "
        "hero_path, flatpak, playtime_minutes, last_played, observed_at) VALUES(?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_key) DO UPDATE SET app_id = "
        "excluded.app_id, runner = excluded.runner, name = excluded.name, directory = "
        "excluded.directory, cover_path = excluded.cover_path, hero_path = excluded.hero_path, "
        "flatpak = excluded.flatpak, playtime_minutes = excluded.playtime_minutes, last_played = "
        "excluded.last_played, observed_at = excluded.observed_at"));
    query.addBindValue(game.key);
    query.addBindValue(game.appId);
    query.addBindValue(game.runner);
    query.addBindValue(game.title);
    query.addBindValue(game.installPath);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroPath);
    query.addBindValue(game.flatpak);
    query.addBindValue(game.playtimeMinutes);
    query.addBindValue(game.lastPlayed);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('heroic', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  query.prepare(QStringLiteral("DELETE FROM gog_configured_roots WHERE path = ? AND removed = 1"));
  for (const QString& root : result.removedGogRoots) {
    query.bindValue(0, root);
    okay = okay && query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Heroic games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(heroicIncomplete || gogIncomplete || !result.unavailableGogRoots.isEmpty()
                ? QStringLiteral("Imported available Heroic/GOG games; kept cached results for "
                                 "the interrupted source")
            : !result.roots.isEmpty()
                ? QStringLiteral("Imported %1 Heroic/GOG game(s)").arg(result.games.size())
                : QStringLiteral("Heroic or GOG was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant HeroicGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.heroic.title;
  case GameRoles::Subtitle:
    return game.heroic.runner == QStringLiteral("gog-direct")
               ? QStringLiteral("GOG · Direct")
               : QStringLiteral("Heroic · %1").arg(storeName(game.heroic.runner));
  case GameRoles::Description:
    return game.heroic.runner == QStringLiteral("gog-direct")
               ? QStringLiteral("Installed GOG game.")
           : game.heroic.runner == QStringLiteral("sideload")
               ? QStringLiteral("Added manually to Heroic.")
               : QStringLiteral("Installed locally through Heroic.");
  case GameRoles::Hours:
    return game.heroic.playtimeMinutes / 60;
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.heroic.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.heroic.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.heroic.title.left(1).toUpper();
  case GameRoles::AppId:
    return game.heroic.appId;
  case GameRoles::CoverPath:
    return localUrl(game.heroic.coverPath);
  case GameRoles::HeroPath:
    return localUrl(game.heroic.heroPath);
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.heroic.installPath;
  case GameRoles::Source:
    return game.heroic.runner == QStringLiteral("gog-direct") ? QStringLiteral("GOG")
                                                               : QStringLiteral("Heroic");
  case GameRoles::Runner:
    return game.heroic.runner;
  case GameRoles::Flatpak:
    return game.heroic.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void HeroicGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}
