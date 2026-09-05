#include "library/UnifiedGameModel.h"

#include "library/GameRoles.h"
#include "library/ManualGameModel.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace {
constexpr qint64 kMaximumArtworkBytes = 32 * 1024 * 1024;
constexpr qint64 kMaximumArtworkPixels = 64 * 1024 * 1024;

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

QString runnerFor(const QModelIndex& game) {
  const QString runner = game.data(GameRoles::Runner).toString();
  return runner.isNull() ? QStringLiteral("") : runner;
}

QString normalizedStatus(const QString& status) {
  const QString value = status.trimmed().toLower();
  static const QSet<QString> allowed = {QString{}, QStringLiteral("backlog"),
                                        QStringLiteral("playing"), QStringLiteral("completed"),
                                        QStringLiteral("abandoned")};
  return allowed.contains(value) ? value : QString{};
}

QStringList normalizedTags(const QString& input) {
  QStringList result;
  for (QString tag : input.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    tag = tag.trimmed().simplified().left(32);
    if (tag.isEmpty()) {
      continue;
    }
    const bool duplicate = std::any_of(result.cbegin(), result.cend(), [&tag](const QString& item) {
      return item.compare(tag, Qt::CaseInsensitive) == 0;
    });
    if (!duplicate) {
      result.append(tag);
    }
    if (result.size() == 20) {
      break;
    }
  }
  return result;
}

QString normalizedCollectionName(const QString& input) {
  QString name = input.trimmed().simplified();
  if (name.isEmpty() || name.size() > 48) {
    return {};
  }
  for (const QChar character : name) {
    if (character.category() == QChar::Other_Control) {
      return {};
    }
  }
  return name;
}

} // namespace

UnifiedGameModel::UnifiedGameModel(const QString& databasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-artwork-%1").arg(QUuid::createUuid().toString())) {
  if (!databasePath.isEmpty()) {
    openArtworkDatabase(databasePath);
  }
}

UnifiedGameModel::~UnifiedGameModel() {
  if (m_database.isValid()) {
    m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

void UnifiedGameModel::addSourceModel(QAbstractItemModel* model) {
  if (model == nullptr || m_models.contains(model)) {
    return;
  }
  m_models.append(model);
  rebuildRows();

  connect(model, &QAbstractItemModel::modelReset, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::rowsInserted, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::dataChanged, this,
          [this, model](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                        const QList<int>& roles) {
            // Forward only the affected rows so the proxy does not re-filter and re-sort the
            // whole library for every cover download or favorite toggle.
            if (!topLeft.isValid() || !bottomRight.isValid()) {
              if (!m_rows.isEmpty()) {
                emit dataChanged(index(0), index(m_rows.size() - 1));
              }
              return;
            }
            QSet<QString> changedGroups;
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
              const QString groupId = m_groupForGame.value(gameKey({.model = model, .row = row}));
              if (!groupId.isEmpty()) {
                changedGroups.insert(groupId);
              }
            }
            for (int row = 0; row < m_rows.size(); ++row) {
              const SourceRow& source = m_rows.at(row);
              const bool direct = source.model == model && source.row >= topLeft.row() &&
                                  source.row <= bottomRight.row();
              if (direct || (!changedGroups.isEmpty() &&
                             changedGroups.contains(m_groupForGame.value(gameKey(source))))) {
                emit dataChanged(index(row), index(row), roles);
              }
            }
          });
}

void UnifiedGameModel::setSourceEnabled(const QString& source, bool enabled) {
  const bool changed =
      enabled ? m_disabledSources.remove(source) > 0 : !m_disabledSources.contains(source);
  if (!enabled) {
    m_disabledSources.insert(source);
  }
  if (changed) {
    rebuildRows();
  }
}

int UnifiedGameModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_rows.size();
}

QVariant UnifiedGameModel::data(const QModelIndex& index, int role) const {
  const SourceRow source = mapRow(index.row());
  if (source.model == nullptr) {
    return {};
  }
  const QVector<SourceRow> members = groupRows(source);
  if (role == GameRoles::Linked) {
    return members.size() > 1;
  }
  if (role == GameRoles::LinkedSources) {
    QStringList sources;
    for (const SourceRow& member : members) {
      const QString name = member.model->index(member.row, 0).data(GameRoles::Source).toString();
      if (!sources.contains(name)) {
        sources.append(name);
      }
    }
    return sources.join(QStringLiteral(" + "));
  }
  if (role == GameRoles::CompletionStatus) {
    for (const SourceRow& member : members) {
      const QString status = m_organizationForGame.value(gameKey(member)).status;
      if (!status.isEmpty()) {
        return status;
      }
    }
    return QString{};
  }
  if (role == GameRoles::Tags || role == GameRoles::Collections) {
    QStringList values;
    for (const SourceRow& member : members) {
      const QStringList memberValues = role == GameRoles::Tags
                                           ? m_organizationForGame.value(gameKey(member)).tags
                                           : m_collectionsForGame.value(gameKey(member));
      for (const QString& value : memberValues) {
        if (std::none_of(values.cbegin(), values.cend(), [&value](const QString& item) {
              return item.compare(value, Qt::CaseInsensitive) == 0;
            })) {
          values.append(value);
        }
      }
    }
    values.sort(Qt::CaseInsensitive);
    return values;
  }
  if (role == GameRoles::Favorite || role == GameRoles::Hidden) {
    bool value = role == GameRoles::Hidden;
    for (const SourceRow& member : members) {
      const QString flag = role == GameRoles::Favorite ? QStringLiteral("favorite") : QStringLiteral("hidden");
      const auto overrides = m_userFlags.value(gameKey(member));
      const bool memberValue = overrides.contains(flag) ? overrides.value(flag).toBool()
          : member.model->index(member.row, 0).data(role).toBool();
      value = role == GameRoles::Hidden ? value && memberValue : value || memberValue;
    }
    return value;
  }
  if (role == GameRoles::Recent || role == GameRoles::LastPlayed) {
    qint64 lastPlayed = 0;
    for (const SourceRow& member : members) {
      const QModelIndex game = member.model->index(member.row, 0);
      lastPlayed = std::max(lastPlayed, game.data(GameRoles::LastPlayed).toLongLong());
      lastPlayed = std::max(lastPlayed, m_lastLaunchForGame.value(gameKey(member)));
    }
    return role == GameRoles::Recent ? lastPlayed > 0 : lastPlayed;
  }
  if (role == GameRoles::Hours) {
    int hours = 0;
    for (const SourceRow& member : members) {
      hours = std::max(hours, member.model->index(member.row, 0).data(role).toInt());
    }
    return hours;
  }
  if (role == GameRoles::Installed) {
    for (const SourceRow& member : members) {
      const QModelIndex game = member.model->index(member.row, 0);
      const QVariant installed = game.data(role);
      if (!installed.isValid() || installed.toBool()) {
        return true;
      }
    }
    return false;
  }
  const QString override = m_coverOverrides.value(gameKey(source));
  if (role == GameRoles::CoverPath && QFileInfo::exists(override)) {
    return localUrl(override);
  }
  if (role == GameRoles::CustomCover) {
    return QFileInfo::exists(override);
  }
  const QString hero = m_heroOverrides.value(gameKey(source));
  const QString logo = m_logoOverrides.value(gameKey(source));
  if (role == GameRoles::CustomHero) return QFileInfo::exists(hero);
  if (role == GameRoles::CustomLogo) return QFileInfo::exists(logo);
  if (role == GameRoles::HeroPath && QFileInfo::exists(hero)) return localUrl(hero);
  if (role == GameRoles::LogoPath && QFileInfo::exists(logo)) return localUrl(logo);
  return source.model->index(source.row, 0).data(role);
}

QHash<int, QByteArray> UnifiedGameModel::roleNames() const {
  QHash<int, QByteArray> roles =
      m_models.isEmpty() ? QHash<int, QByteArray>{} : m_models.constFirst()->roleNames();
  roles.insert(GameRoles::CustomCover, "customCover");
  roles.insert(GameRoles::CustomHero, "customHero");
  roles.insert(GameRoles::CustomLogo, "customLogo");
  roles.insert(GameRoles::Linked, "linked");
  roles.insert(GameRoles::LinkedSources, "linkedSources");
  roles.insert(GameRoles::CompletionStatus, "completionStatus");
  roles.insert(GameRoles::Tags, "tags");
  roles.insert(GameRoles::Collections, "collections");
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  roles.insert(GameRoles::Installed, "installed");
  return roles;
}

void UnifiedGameModel::toggleFavorite(int row) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr) {
    return;
  }
  const bool desired = !data(index(row), GameRoles::Favorite).toBool();
  for (const SourceRow& member : groupRows(source)) {
    if (m_userFlags.value(gameKey(member)).contains("favorite")) {
      bulkOrganize({gameKey(source)}, {{"favorite", desired}});
      return;
    }
  }
  for (const SourceRow& member : groupRows(source)) {
    if (member.model->index(member.row, 0).data(GameRoles::Favorite).toBool() != desired) {
      QMetaObject::invokeMethod(member.model, "toggleFavorite", Q_ARG(int, member.row));
    }
  }
}

void UnifiedGameModel::toggleHidden(int row) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr) {
    return;
  }
  const bool desired = !data(index(row), GameRoles::Hidden).toBool();
  for (const SourceRow& member : groupRows(source)) {
    if (m_userFlags.value(gameKey(member)).contains("hidden")) {
      bulkOrganize({gameKey(source)}, {{"hidden", desired}});
      return;
    }
  }
  for (const SourceRow& member : groupRows(source)) {
    if (member.model->index(member.row, 0).data(GameRoles::Hidden).toBool() != desired) {
      QMetaObject::invokeMethod(member.model, "toggleHidden", Q_ARG(int, member.row));
    }
  }
}

QHash<QString, QString>* UnifiedGameModel::artworkOverrides(const QString& kind) {
  if (kind == QStringLiteral("cover")) return &m_coverOverrides;
  if (kind == QStringLiteral("hero")) return &m_heroOverrides;
  if (kind == QStringLiteral("logo")) return &m_logoOverrides;
  return nullptr;
}

void UnifiedGameModel::removeUnusedArtwork(const QString& path) {
  // Only remove files owned by this library, and never a file another slot uses.
  if (path.isEmpty() || QFileInfo(path).absolutePath() != QDir(m_artworkRoot).absolutePath()) return;
  for (const auto* overrides : {&m_coverOverrides, &m_heroOverrides, &m_logoOverrides}) {
    if (overrides->values().contains(path)) return;
  }
  QFile::remove(path);
}

bool UnifiedGameModel::setCustomCover(int row, const QUrl& sourceUrl) {
  return setCustomArtwork(row, QStringLiteral("cover"), sourceUrl);
}

bool UnifiedGameModel::resetCustomCover(int row) {
  return resetCustomArtwork(row, QStringLiteral("cover"));
}

bool UnifiedGameModel::setCustomArtwork(int row, const QString& kind, const QUrl& sourceUrl) {
  auto* overrides = artworkOverrides(kind);
  const SourceRow source = mapRow(row);
  if (!sourceUrl.scheme().isEmpty() && !sourceUrl.isLocalFile()) return false;
  const QString sourcePath = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : sourceUrl.path(QUrl::FullyDecoded);
  if (!overrides || source.model == nullptr || !m_database.isOpen() ||
      !QDir::isAbsolutePath(sourcePath) || !sourceUrl.host().isEmpty()) return false;
  QFile input(sourcePath);
  if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 || input.size() > kMaximumArtworkBytes)
    return false;
  QByteArray contents = input.read(kMaximumArtworkBytes + 1);
  if (contents.isEmpty() || contents.size() > kMaximumArtworkBytes) return false;
  QBuffer buffer(&contents);
  if (!buffer.open(QIODevice::ReadOnly)) return false;
  QImageReader reader(&buffer);
  const QSize size = reader.size();
  const QByteArray format = reader.format().toLower();
  const bool supported = format == "jpg" || format == "jpeg" || format == "png" || format == "webp";
  if (!supported || !size.isValid() || size.width() > 16384 || size.height() > 16384 ||
      static_cast<qint64>(size.width()) * size.height() > kMaximumArtworkPixels || reader.read().isNull())
    return false;
  const QString key = gameKey(source);
  if (key.isEmpty() || !QDir().mkpath(m_artworkRoot)) return false;
  const QString extension = format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format);
  const QString digest = QString::fromLatin1(
      QCryptographicHash::hash(key.toUtf8() + kind.toUtf8() + contents, QCryptographicHash::Sha256).toHex());
  const QString destination = m_artworkRoot + QLatin1Char('/') + digest + QLatin1Char('.') + extension;
  const bool alreadyStored = QFileInfo::exists(destination);
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() || !output.commit())
    return false;
  const QModelIndex game = source.model->index(source.row, 0);
  QSqlQuery query(m_database);
  // kind is restricted by artworkOverrides above, never arbitrary SQL text.
  const QString column = kind + QStringLiteral("_path");
  query.prepare(QStringLiteral(
      "INSERT INTO artwork_overrides(source, runner, app_id, cover_path, hero_path, logo_path) "
      "VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT(source, runner, app_id) DO UPDATE SET %1=excluded.%1").arg(column));
  query.addBindValue(game.data(GameRoles::Source).toString());
  query.addBindValue(runnerFor(game));
  query.addBindValue(game.data(GameRoles::AppId).toString());
  for (const QString& slot : {QStringLiteral("cover"), QStringLiteral("hero"), QStringLiteral("logo")})
    query.addBindValue(kind == slot ? destination : QStringLiteral(""));
  if (!query.exec()) {
    if (!alreadyStored) removeUnusedArtwork(destination);
    return false;
  }
  const QString previous = overrides->value(key);
  overrides->insert(key, destination);
  if (previous != destination) removeUnusedArtwork(previous);
  emit dataChanged(index(row), index(row), {GameRoles::CoverPath, GameRoles::CustomCover,
      GameRoles::HeroPath, GameRoles::CustomHero, GameRoles::LogoPath, GameRoles::CustomLogo});
  return true;
}

bool UnifiedGameModel::resetCustomArtwork(int row, const QString& kind) {
  auto* overrides = artworkOverrides(kind);
  const SourceRow source = mapRow(row);
  const QString key = gameKey(source);
  if (!overrides || source.model == nullptr || !m_database.isOpen() || !overrides->contains(key))
    return false;
  const QModelIndex game = source.model->index(source.row, 0);
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE artwork_overrides SET %1_path='' "
      "WHERE source=? AND runner=? AND app_id=?").arg(kind));
  query.addBindValue(game.data(GameRoles::Source).toString());
  query.addBindValue(runnerFor(game));
  query.addBindValue(game.data(GameRoles::AppId).toString());
  if (!query.exec()) return false;
  removeUnusedArtwork(overrides->take(key));
  emit dataChanged(index(row), index(row), {GameRoles::CoverPath, GameRoles::CustomCover,
      GameRoles::HeroPath, GameRoles::CustomHero, GameRoles::LogoPath, GameRoles::CustomLogo});
  return true;
}

QVariantList UnifiedGameModel::installations(int row) const {
  QVariantList result;
  const SourceRow source = mapRow(row);
  const QString preferred = m_preferredForGroup.value(m_groupForGame.value(gameKey(source)));
  bool preferredAvailable = preferred.isEmpty();
  for (const SourceRow& member : groupRows(source)) {
    QVariantMap installation = gameMap(member);
    const bool isPreferred = !preferred.isEmpty() && gameKey(member) == preferred;
    installation.insert(QStringLiteral("preferred"), isPreferred);
    const QString installPath = installation.value(QStringLiteral("installPath")).toString();
    bool available = (!installation.contains(QStringLiteral("installed")) ||
                            installation.value(QStringLiteral("installed")).toBool()) &&
                           (installPath.isEmpty() || QFileInfo::exists(installPath));
    if (available && installation.value(QStringLiteral("source")).toString() == QStringLiteral("Manual")) {
      QString executable, directory, error;
      QStringList arguments;
      available = ManualGameModel::validateLaunch(installation.value(QStringLiteral("launchTarget")).toString(),
          installation.value(QStringLiteral("appId")).toString(), &executable, &arguments, &directory, &error);
    }
    installation.insert(QStringLiteral("launchAvailable"), available);
    if (isPreferred && available) {
      preferredAvailable = true;
    }
    result.append(installation);
  }
  for (QVariant& value : result) {
    QVariantMap installation = value.toMap();
    installation.insert(QStringLiteral("preferredUnavailable"), !preferredAvailable);
    value = installation;
  }
  return result;
}

QVariantMap UnifiedGameModel::preferredInstallation(int row) const {
  const QVariantList entries = installations(row);
  for (const QVariant& entry : entries) {
    const QVariantMap game = entry.toMap();
    if (game.value(QStringLiteral("preferred")).toBool() &&
        game.value(QStringLiteral("launchAvailable")).toBool()) {
      return game;
    }
  }
  for (const QVariant& entry : entries) {
    if (entry.toMap().value(QStringLiteral("launchAvailable")).toBool()) {
      return entry.toMap();
    }
  }
  // Retain the usual repair action for installed entries before offering installation.
  for (const QVariant& entry : entries) {
    const QVariantMap game = entry.toMap();
    if (!game.contains(QStringLiteral("installed")) || game.value(QStringLiteral("installed")).toBool()) {
      return game;
    }
  }
  return entries.isEmpty() ? QVariantMap{} : entries.first().toMap();
}

bool UnifiedGameModel::setPreferredInstallation(int row, const QString& sourceName,
                                                const QString& runner, const QString& appId) {
  const SourceRow source = mapRow(row);
  const QString group = m_groupForGame.value(gameKey(source));
  if (group.isEmpty() || !m_database.isOpen()) {
    return false;
  }
  for (const SourceRow& member : groupRows(source)) {
    const QModelIndex game = member.model->index(member.row, 0);
    if (game.data(GameRoles::Source).toString() != sourceName || runnerFor(game) != runner ||
        game.data(GameRoles::AppId).toString() != appId) {
      continue;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO launch_preferences(group_id, source, runner, app_id) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(group);
    query.addBindValue(sourceName);
    query.addBindValue(runnerFor(game));
    query.addBindValue(appId);
    if (!query.exec()) {
      return false;
    }
    m_preferredForGroup.insert(group, gameKey(member));
    emit dataChanged(index(row), index(row));
    return true;
  }
  return false;
}

QVariantList UnifiedGameModel::linkCandidates(int row, const QString& search) const {
  QVariantList result;
  const SourceRow selected = mapRow(row);
  if (selected.model == nullptr) {
    return result;
  }
  QString query = search.trimmed();
  const QString selectedGroup = m_groupForGame.value(gameKey(selected));
  for (const SourceRow& candidate : m_rows) {
    const QString candidateKey = gameKey(candidate);
    if (candidateKey == gameKey(selected) ||
        (!selectedGroup.isEmpty() && m_groupForGame.value(candidateKey) == selectedGroup)) {
      continue;
    }
    const QModelIndex game = candidate.model->index(candidate.row, 0);
    if (!query.isEmpty() &&
        !game.data(GameRoles::Title).toString().contains(query, Qt::CaseInsensitive)) {
      continue;
    }
    result.append(gameMap(candidate));
    if (result.size() == 50) {
      break;
    }
  }
  return result;
}

bool UnifiedGameModel::linkGames(int row, const QString& sourceName, const QString& runner,
                                 const QString& appId) {
  const SourceRow selected = mapRow(row);
  SourceRow target;
  for (QAbstractItemModel* model : m_models) {
    for (int sourceRow = 0; sourceRow < model->rowCount(); ++sourceRow) {
      const QModelIndex game = model->index(sourceRow, 0);
      if (game.data(GameRoles::Source).toString() == sourceName && runnerFor(game) == runner &&
          game.data(GameRoles::AppId).toString() == appId) {
        target = {.model = model, .row = sourceRow};
        break;
      }
    }
    if (target.model != nullptr) {
      break;
    }
  }
  const QString selectedKey = gameKey(selected);
  const QString targetKey = gameKey(target);
  if (selectedKey.isEmpty() || targetKey.isEmpty() || selectedKey == targetKey ||
      !m_database.isOpen() ||
      (!m_groupForGame.value(selectedKey).isEmpty() &&
       m_groupForGame.value(selectedKey) == m_groupForGame.value(targetKey))) {
    return false;
  }

  const QString selectedGroup = m_groupForGame.value(selectedKey);
  const QString targetGroup = m_groupForGame.value(targetKey);
  const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!m_database.transaction()) {
    return false;
  }
  QSqlQuery merge(m_database);
  merge.prepare(QStringLiteral(
      "UPDATE game_link_members SET group_id = ?, is_primary = 0 WHERE group_id = ?"));
  for (const QString& existingGroup : {selectedGroup, targetGroup}) {
    if (existingGroup.isEmpty()) {
      continue;
    }
    merge.bindValue(0, groupId);
    merge.bindValue(1, existingGroup);
    if (!merge.exec()) {
      m_database.rollback();
      return false;
    }
  }
  QSqlQuery insert(m_database);
  insert.prepare(QStringLiteral(
      "INSERT OR REPLACE INTO game_link_members(group_id, source, runner, app_id, is_primary) "
      "VALUES(?, ?, ?, ?, ?)"));
  for (const SourceRow& member : {selected, target}) {
    const QModelIndex game = member.model->index(member.row, 0);
    insert.bindValue(0, groupId);
    insert.bindValue(1, game.data(GameRoles::Source).toString());
    insert.bindValue(2, runnerFor(game));
    insert.bindValue(3, game.data(GameRoles::AppId).toString());
    insert.bindValue(4, gameKey(member) == selectedKey);
    if (!insert.exec()) {
      m_database.rollback();
      return false;
    }
  }
  QSqlQuery preference(m_database);
  preference.prepare(QStringLiteral(
      "INSERT INTO launch_preferences(group_id, source, runner, app_id) "
      "SELECT ?, source, runner, app_id FROM launch_preferences "
      "WHERE group_id IN (?, ?) ORDER BY CASE WHEN group_id = ? THEN 0 ELSE 1 END LIMIT 1"));
  preference.addBindValue(groupId);
  preference.addBindValue(selectedGroup);
  preference.addBindValue(targetGroup);
  preference.addBindValue(selectedGroup);
  if (!preference.exec()) {
    m_database.rollback();
    return false;
  }
  preference.prepare(QStringLiteral("DELETE FROM launch_preferences WHERE group_id IN (?, ?)"));
  preference.addBindValue(selectedGroup);
  preference.addBindValue(targetGroup);
  if (!preference.exec() || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  loadLinks();
  rebuildRows();
  return true;
}

bool UnifiedGameModel::unlinkGames(int row) {
  const SourceRow source = mapRow(row);
  const QString groupId = m_groupForGame.value(gameKey(source));
  if (groupId.isEmpty() || !m_database.isOpen()) {
    return false;
  }
  if (!m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM launch_preferences WHERE group_id = ?"));
  query.addBindValue(groupId);
  if (!query.exec()) {
    m_database.rollback();
    return false;
  }
  query.prepare(QStringLiteral("DELETE FROM game_link_members WHERE group_id = ?"));
  query.addBindValue(groupId);
  if (!query.exec() || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  loadLinks();
  rebuildRows();
  return true;
}

bool UnifiedGameModel::recordLaunch(int row, const QString& sourceName, const QString& runner,
                                    const QString& appId) {
  const SourceRow selected = mapRow(row);
  if (selected.model == nullptr || !m_database.isOpen()) {
    return false;
  }
  SourceRow launched;
  const QString normalizedRunner = runner.isNull() ? QStringLiteral("") : runner;
  for (const SourceRow& member : groupRows(selected)) {
    const QModelIndex game = member.model->index(member.row, 0);
    if (game.data(GameRoles::Source).toString() == sourceName &&
        runnerFor(game) == normalizedRunner && game.data(GameRoles::AppId).toString() == appId) {
      launched = member;
      break;
    }
  }
  const QString key = gameKey(launched);
  if (key.isEmpty()) {
    return false;
  }
  const qint64 launchedAt = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO launch_activity(source, runner, app_id, last_launched, launch_count) "
      "VALUES(?, ?, ?, ?, 1) ON CONFLICT(source, runner, app_id) DO UPDATE SET last_launched = "
      "excluded.last_launched, launch_count = launch_count + 1"));
  query.addBindValue(sourceName);
  query.addBindValue(normalizedRunner);
  query.addBindValue(appId);
  query.addBindValue(launchedAt);
  if (!query.exec()) {
    return false;
  }
  m_lastLaunchForGame.insert(key, launchedAt);
  emit dataChanged(index(row), index(row), {GameRoles::Recent, GameRoles::LastPlayed});
  return true;
}

bool UnifiedGameModel::setCompletionStatus(int row, const QString& status) {
  const SourceRow source = mapRow(row);
  const QString normalized = normalizedStatus(status);
  if (source.model == nullptr || !m_database.isOpen() ||
      (!status.trimmed().isEmpty() && normalized.isEmpty()) || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json) "
      "VALUES(?, ?, ?, ?, '[]') ON CONFLICT(source, runner, app_id) DO UPDATE SET "
      "completion_status = excluded.completion_status"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, game.data(GameRoles::Source).toString());
    query.bindValue(1, runnerFor(game));
    query.bindValue(2, game.data(GameRoles::AppId).toString());
    query.bindValue(3, normalized);
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  for (const SourceRow& member : members) {
    m_organizationForGame[gameKey(member)].status = normalized;
  }
  emit dataChanged(index(row), index(row), {GameRoles::CompletionStatus});
  return true;
}

bool UnifiedGameModel::setTags(int row, const QString& tags) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr || !m_database.isOpen()) {
    return false;
  }
  const QStringList normalized = normalizedTags(tags);
  QJsonArray jsonTags;
  for (const QString& tag : normalized) {
    jsonTags.append(tag);
  }
  const QString json = QString::fromUtf8(QJsonDocument(jsonTags).toJson(QJsonDocument::Compact));
  if (!m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json) "
      "VALUES(?, ?, ?, '', ?) ON CONFLICT(source, runner, app_id) DO UPDATE SET tags_json = "
      "excluded.tags_json"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, game.data(GameRoles::Source).toString());
    query.bindValue(1, runnerFor(game));
    query.bindValue(2, game.data(GameRoles::AppId).toString());
    query.bindValue(3, json);
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  for (const SourceRow& member : members) {
    m_organizationForGame[gameKey(member)].tags = normalized;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Tags});
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::createCollection(const QString& name) {
  const QString normalized = normalizedCollectionName(name);
  if (normalized.isEmpty() || !m_database.isOpen()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("INSERT INTO collections(name, created_at) VALUES(?, ?)"));
  query.addBindValue(normalized);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  if (!query.exec()) {
    return false;
  }
  loadCollections();
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::deleteCollection(const QString& name) {
  QString storedName;
  for (const QString& collection : m_collectionNames) {
    if (collection.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      storedName = collection;
      break;
    }
  }
  if (storedName.isEmpty() || !m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM collection_games WHERE collection_name = ?"));
  query.addBindValue(storedName);
  bool okay = query.exec();
  query.prepare(QStringLiteral("DELETE FROM collections WHERE name = ?"));
  query.addBindValue(storedName);
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  loadCollections();
  if (!m_rows.isEmpty()) {
    emit dataChanged(index(0), index(m_rows.size() - 1), {GameRoles::Collections});
  }
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::setCollectionMembership(int row, const QString& name, bool included) {
  const SourceRow source = mapRow(row);
  QString storedName;
  for (const QString& collection : m_collectionNames) {
    if (collection.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      storedName = collection;
      break;
    }
  }
  if (source.model == nullptr || storedName.isEmpty() || !m_database.isOpen() ||
      !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(included
                    ? QStringLiteral("INSERT OR IGNORE INTO collection_games(collection_name, "
                                     "source, runner, app_id) VALUES(?, ?, ?, ?)")
                    : QStringLiteral("DELETE FROM collection_games WHERE collection_name = ? AND "
                                     "source = ? AND runner = ? AND app_id = ?"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, storedName);
    query.bindValue(1, game.data(GameRoles::Source).toString());
    query.bindValue(2, runnerFor(game));
    query.bindValue(3, game.data(GameRoles::AppId).toString());
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  loadCollections();
  emit dataChanged(index(row), index(row), {GameRoles::Collections});
  return true;
}

QStringList UnifiedGameModel::collectionNames() const { return m_collectionNames; }

QStringList UnifiedGameModel::tagNames() const {
  QStringList result;
  for (const OrganizationState& state : m_organizationForGame) {
    for (const QString& tag : state.tags) {
      if (std::none_of(result.cbegin(), result.cend(), [&tag](const QString& item) {
            return item.compare(tag, Qt::CaseInsensitive) == 0;
          })) {
        result.append(tag);
      }
    }
  }
  result.sort(Qt::CaseInsensitive);
  return result;
}

UnifiedGameModel::SourceRow UnifiedGameModel::mapRow(int row) const {
  if (row < 0 || row >= m_rows.size()) {
    return {};
  }
  return m_rows.at(row);
}

QString UnifiedGameModel::gameKey(const SourceRow& source) const {
  if (source.model == nullptr) {
    return {};
  }
  const QModelIndex game = source.model->index(source.row, 0);
  const QString gameSource = game.data(GameRoles::Source).toString();
  const QString appId = game.data(GameRoles::AppId).toString();
  if (gameSource.isEmpty() || appId.isEmpty()) {
    return {};
  }
  return gameSource + QChar::Null + runnerFor(game) + QChar::Null + appId;
}

UnifiedGameModel::SourceRow UnifiedGameModel::sourceForKey(const QString& key) const {
  return m_rowForKey.value(key);
}

bool UnifiedGameModel::sourceEnabled(const SourceRow& source) const {
  if (source.model == nullptr) {
    return false;
  }
  return !m_disabledSources.contains(
      source.model->index(source.row, 0).data(GameRoles::Source).toString());
}

QVector<UnifiedGameModel::SourceRow> UnifiedGameModel::groupRows(const SourceRow& source) const {
  QVector<SourceRow> rows;
  if (source.model == nullptr) {
    return rows;
  }
  rows.append(source);
  const QString key = gameKey(source);
  const QString groupId = m_groupForGame.value(key);
  if (groupId.isEmpty()) {
    return rows;
  }
  for (const SourceRow& candidate : m_rowsForGroup.value(groupId)) {
    if (candidate.model != source.model || candidate.row != source.row) {
      rows.append(candidate);
    }
  }
  return rows;
}

QVariantMap UnifiedGameModel::gameMap(const SourceRow& source) const {
  QVariantMap result;
  if (source.model == nullptr) {
    return result;
  }
  const QModelIndex game = source.model->index(source.row, 0);
  const auto roles = source.model->roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    result.insert(QString::fromUtf8(iterator.value()), game.data(iterator.key()));
  }
  const auto flags = m_userFlags.value(gameKey(source));
  for (auto flag = flags.cbegin(); flag != flags.cend(); ++flag) result.insert(flag.key(), flag.value());
  const QString override = m_coverOverrides.value(gameKey(source));
  if (QFileInfo::exists(override)) {
    result.insert(QStringLiteral("coverPath"), localUrl(override));
    result.insert(QStringLiteral("customCover"), true);
  } else {
    result.insert(QStringLiteral("customCover"), false);
  }
  const QString hero = m_heroOverrides.value(gameKey(source));
  const QString logo = m_logoOverrides.value(gameKey(source));
  result.insert(QStringLiteral("customHero"), QFileInfo::exists(hero));
  result.insert(QStringLiteral("customLogo"), QFileInfo::exists(logo));
  if (QFileInfo::exists(hero)) result.insert(QStringLiteral("heroPath"), localUrl(hero));
  if (QFileInfo::exists(logo)) result.insert(QStringLiteral("logoPath"), localUrl(logo));
  const qint64 lastPlayed = std::max(game.data(GameRoles::LastPlayed).toLongLong(),
                                     m_lastLaunchForGame.value(gameKey(source)));
  result.insert(QStringLiteral("lastPlayed"), lastPlayed);
  result.insert(QStringLiteral("recent"), lastPlayed > 0);
  result.insert(QStringLiteral("completionStatus"),
                m_organizationForGame.value(gameKey(source)).status);
  result.insert(QStringLiteral("tags"), m_organizationForGame.value(gameKey(source)).tags);
  result.insert(QStringLiteral("collections"), m_collectionsForGame.value(gameKey(source)));
  return result;
}

void UnifiedGameModel::rebuildRows() {
  beginResetModel();
  m_rows.clear();
  m_rowForKey.clear();
  m_rowsForGroup.clear();
  for (QAbstractItemModel* model : m_models) {
    for (int row = 0; row < model->rowCount(); ++row) {
      const SourceRow source{.model = model, .row = row};
      if (!sourceEnabled(source)) {
        continue;
      }
      const QString key = gameKey(source);
      if (key.isEmpty()) {
        continue;
      }
      m_rowForKey.insert(key, source);
      const QString groupId = m_groupForGame.value(key);
      if (!groupId.isEmpty()) {
        m_rowsForGroup[groupId].append(source);
      }
    }
  }
  QSet<QString> addedGroups;
  for (QAbstractItemModel* model : m_models) {
    for (int row = 0; row < model->rowCount(); ++row) {
      const SourceRow source{.model = model, .row = row};
      if (!sourceEnabled(source)) {
        continue;
      }
      const QString groupId = m_groupForGame.value(gameKey(source));
      if (groupId.isEmpty()) {
        m_rows.append(source);
      } else if (!addedGroups.contains(groupId)) {
        SourceRow representative = sourceForKey(m_primaryForGroup.value(groupId));
        m_rows.append(representative.model == nullptr ? source : representative);
        addedGroups.insert(groupId);
      }
    }
  }
  endResetModel();
}

void UnifiedGameModel::loadUserFlags() {
  m_userFlags.clear();
  QSqlQuery query(m_database);
  if (!query.exec("SELECT source, runner, app_id, favorite, hidden FROM user_game_flags")) return;
  while (query.next()) {
    QVariantMap flags;
    if (!query.value(3).isNull()) flags.insert("favorite", query.value(3).toBool());
    if (!query.value(4).isNull()) flags.insert("hidden", query.value(4).toBool());
    m_userFlags.insert(query.value(0).toString() + QChar::Null + query.value(1).toString()
        + QChar::Null + query.value(2).toString(), flags);
  }
}

bool UnifiedGameModel::bulkOrganize(const QStringList& identities, const QVariantMap& changes) {
  static const QSet<QString> allowed{"favorite", "hidden", "status", "tagsAdd", "tagsRemove", "collection", "collectionIncluded"};
  if (!m_database.isOpen() || identities.isEmpty() || changes.isEmpty()) return false;
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
    if (!allowed.contains(it.key())) return false;
    const bool boolean = it.key() == "favorite" || it.key() == "hidden" || it.key() == "collectionIncluded";
    if (it.value().metaType().id() != (boolean ? QMetaType::Bool : QMetaType::QString)) return false;
    if (!boolean && (it.value().toString().size() > 4096 || it.value().toString().contains(QChar::Null))) return false;
  }
  const QString status = normalizedStatus(changes.value("status").toString());
  if (changes.contains("status") && !changes.value("status").toString().trimmed().isEmpty() && status.isEmpty()) return false;
  QString collection;
  if (changes.contains("collection") != changes.contains("collectionIncluded")) return false;
  if (changes.contains("collection")) {
    collection = normalizedCollectionName(changes.value("collection").toString());
    if (collection.isEmpty()) return false;
    for (const auto& existing : m_collectionNames) {
      if (existing.compare(collection, Qt::CaseInsensitive) == 0) { collection = existing; break; }
    }
    if (!changes.value("collectionIncluded").toBool() && !m_collectionNames.contains(collection)) return false;
  }
  for (const QString& field : {QStringLiteral("tagsAdd"), QStringLiteral("tagsRemove")}) {
    QStringList unique;
    for (const auto& raw : changes.value(field).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const QString tag = raw.trimmed().simplified();
      if (tag.size() > 32) return false;
      for (const auto ch : raw) if (ch.category() == QChar::Other_Control) return false;
      if (!tag.isEmpty() && !unique.contains(tag, Qt::CaseInsensitive)) unique.append(tag);
    }
    if (unique.size() > 20) return false;
  }
  const QStringList addTags = normalizedTags(changes.value("tagsAdd").toString());
  const QStringList removeTags = normalizedTags(changes.value("tagsRemove").toString());
  if ((changes.contains("tagsAdd") && addTags.isEmpty()) ||
      (changes.contains("tagsRemove") && removeTags.isEmpty())) return false;
  QHash<QString, SourceRow> members;
  for (const auto& key : identities) {
    const auto source = sourceForKey(key);
    if (!source.model || !sourceEnabled(source)) return false;
    for (const auto& member : groupRows(source)) members.insert(gameKey(member), member);
  }
  if (members.isEmpty() || !m_database.transaction()) return false;
  const auto fail = [this] { m_database.rollback(); return false; };
  QSqlQuery query(m_database);
  if (!collection.isEmpty() && changes.value("collectionIncluded").toBool()) {
    query.prepare("INSERT OR IGNORE INTO collections(name, created_at) VALUES(?, ?)");
    query.addBindValue(collection); query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) return fail();
  }
  // All writes use this connection. Source scanners and their launcher data are not modified.
  for (auto member = members.cbegin(); member != members.cend(); ++member) {
    const auto game = member.value().model->index(member.value().row, 0);
    const QString source = game.data(GameRoles::Source).toString();
    const QString runner = runnerFor(game);
    const QString appId = game.data(GameRoles::AppId).toString();
    for (const QString& flag : {QStringLiteral("favorite"), QStringLiteral("hidden")}) {
      if (!changes.contains(flag)) continue;
      query.prepare(QStringLiteral("INSERT INTO user_game_flags(source, runner, app_id, %1) VALUES(?, ?, ?, ?) "
          "ON CONFLICT(source, runner, app_id) DO UPDATE SET %1=excluded.%1").arg(flag));
      query.addBindValue(source); query.addBindValue(runner); query.addBindValue(appId); query.addBindValue(changes.value(flag).toBool());
      if (!query.exec()) return fail();
    }
    if (changes.contains("status") || changes.contains("tagsAdd") || changes.contains("tagsRemove")) {
      auto organization = m_organizationForGame.value(member.key());
      if (changes.contains("status")) organization.status = status;
      for (const auto& tag : removeTags) organization.tags.removeIf([&tag](const QString& value) { return value.compare(tag, Qt::CaseInsensitive) == 0; });
      for (const auto& tag : addTags) if (!organization.tags.contains(tag, Qt::CaseInsensitive)) organization.tags.append(tag);
      if (organization.tags.size() > 20) return fail();
      QJsonArray tags;
      for (const auto& tag : organization.tags) tags.append(tag);
      query.prepare("INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json) VALUES(?, ?, ?, ?, ?) "
          "ON CONFLICT(source, runner, app_id) DO UPDATE SET completion_status=excluded.completion_status, tags_json=excluded.tags_json");
      query.addBindValue(source); query.addBindValue(runner); query.addBindValue(appId); query.addBindValue(organization.status.isNull() ? QStringLiteral("") : organization.status);
      query.addBindValue(QString::fromUtf8(QJsonDocument(tags).toJson(QJsonDocument::Compact)));
      if (!query.exec()) return fail();
    }
    if (!collection.isEmpty()) {
      query.prepare(changes.value("collectionIncluded").toBool()
          ? "INSERT OR IGNORE INTO collection_games(collection_name, source, runner, app_id) VALUES(?, ?, ?, ?)"
          : "DELETE FROM collection_games WHERE collection_name=? AND source=? AND runner=? AND app_id=?");
      query.addBindValue(collection); query.addBindValue(source); query.addBindValue(runner); query.addBindValue(appId);
      if (!query.exec()) return fail();
    }
  }
  if (!m_database.commit()) return fail();
  loadUserFlags(); loadOrganization(); loadCollections();
  if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1),
      {GameRoles::Favorite, GameRoles::Hidden, GameRoles::CompletionStatus, GameRoles::Tags, GameRoles::Collections});
  emit collectionsChanged();
  return true;
}

QVariantList UnifiedGameModel::savedFilters() const {
  QVariantList result;
  if (!m_database.isOpen()) return result;
  QSqlQuery query(m_database);
  if (!query.exec("SELECT id, name, state_json FROM saved_filters ORDER BY name_key")) return result;
  while (query.next()) {
    result.append(QVariantMap{{"id", query.value(0)}, {"name", query.value(1)},
        {"state", QJsonDocument::fromJson(query.value(2).toByteArray()).object().toVariantMap()}});
  }
  return result;
}

bool UnifiedGameModel::saveFilter(const QString& id, const QString& name, const QVariantMap& state) {
  if (!m_database.isOpen() || id.isEmpty() || name.isEmpty()) return false;
  const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact);
  if (json.size() > 32768) return false;
  QSqlQuery query(m_database);
  query.prepare("INSERT INTO saved_filters(id, name, name_key, state_json) VALUES(?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name, name_key=excluded.name_key, state_json=excluded.state_json");
  query.addBindValue(id);
  query.addBindValue(name);
  query.addBindValue(name.normalized(QString::NormalizationForm_C).toCaseFolded());
  query.addBindValue(json);
  if (!query.exec()) return false;
  emit savedFiltersChanged();
  return true;
}

bool UnifiedGameModel::removeFilter(const QString& id) {
  if (!m_database.isOpen()) return false;
  QSqlQuery query(m_database);
  query.prepare("DELETE FROM saved_filters WHERE id=?");
  query.addBindValue(id);
  if (!query.exec() || query.numRowsAffected() != 1) return false;
  emit savedFiltersChanged();
  return true;
}

bool UnifiedGameModel::openArtworkDatabase(const QString& path) {
  m_databasePath = path;
  m_artworkRoot = QFileInfo(path).absolutePath() + QStringLiteral("/artwork");
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    return false;
  }
  QSqlQuery query(m_database);
  if (!query.exec("CREATE TABLE IF NOT EXISTS user_game_flags (source TEXT NOT NULL, runner TEXT NOT NULL, "
                  "app_id TEXT NOT NULL, favorite INTEGER, hidden INTEGER, PRIMARY KEY(source, runner, app_id))")) return false;
  if (!query.exec("CREATE TABLE IF NOT EXISTS saved_filters (id TEXT PRIMARY KEY, name TEXT NOT NULL, "
                  "name_key TEXT NOT NULL UNIQUE, state_json TEXT NOT NULL)")) return false;
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS artwork_overrides (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, cover_path TEXT NOT NULL, PRIMARY KEY(source, runner, "
          "app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS game_link_members (group_id TEXT NOT NULL, source TEXT NOT "
          "NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, is_primary INTEGER NOT NULL DEFAULT "
          "0, PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS launch_activity (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, last_launched INTEGER NOT NULL, launch_count INTEGER NOT "
          "NULL DEFAULT 1, PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS game_organization (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, completion_status TEXT NOT NULL DEFAULT '', tags_json TEXT "
          "NOT NULL DEFAULT '[]', PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS collections (name TEXT PRIMARY KEY COLLATE NOCASE, "
          "created_at INTEGER NOT NULL)"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS collection_games (collection_name TEXT NOT NULL, source "
          "TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, PRIMARY KEY(collection_name, "
          "source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS launch_preferences (group_id TEXT PRIMARY KEY, "
          "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL)"))) {
    return false;
  }
  QSet<QString> artworkColumns;
  if (!query.exec(QStringLiteral("PRAGMA table_info(artwork_overrides)"))) return false;
  while (query.next()) artworkColumns.insert(query.value(1).toString());
  for (const QString& column : {QStringLiteral("hero_path"), QStringLiteral("logo_path")}) {
    if (!artworkColumns.contains(column) && !query.exec(QStringLiteral(
        "ALTER TABLE artwork_overrides ADD COLUMN %1 TEXT NOT NULL DEFAULT ''").arg(column))) return false;
  }
  // SteamGameModel owns PRAGMA user_version for the shared database. The organization tables
  // above are created idempotently, so nothing here depends on a version stamp.
  loadArtworkOverrides();
  loadLinks();
  loadLaunchActivity();
  loadOrganization();
  loadUserFlags();
  loadCollections();
  return true;
}

void UnifiedGameModel::loadArtworkOverrides() {
  QSqlQuery query(m_database);
  if (!query.exec(
          QStringLiteral("SELECT source, runner, app_id, cover_path, hero_path, logo_path FROM artwork_overrides"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    if (!query.value(3).toString().isEmpty()) m_coverOverrides.insert(key, query.value(3).toString());
    if (!query.value(4).toString().isEmpty()) m_heroOverrides.insert(key, query.value(4).toString());
    if (!query.value(5).toString().isEmpty()) m_logoOverrides.insert(key, query.value(5).toString());
  }
}

void UnifiedGameModel::loadLinks() {
  m_groupForGame.clear();
  m_primaryForGroup.clear();
  m_preferredForGroup.clear();
  QSqlQuery query(m_database);
  if (query.exec(
          QStringLiteral("SELECT group_id, source, runner, app_id FROM launch_preferences"))) {
    while (query.next()) {
      m_preferredForGroup.insert(query.value(0).toString(),
                                 query.value(1).toString() + QChar::Null +
                                     query.value(2).toString() + QChar::Null +
                                     query.value(3).toString());
    }
  }
  if (!query.exec(QStringLiteral(
          "SELECT group_id, source, runner, app_id, is_primary FROM game_link_members"))) {
    return;
  }
  while (query.next()) {
    const QString groupId = query.value(0).toString();
    const QString key = query.value(1).toString() + QChar::Null + query.value(2).toString() +
                        QChar::Null + query.value(3).toString();
    m_groupForGame.insert(key, groupId);
    if (query.value(4).toBool()) {
      m_primaryForGroup.insert(groupId, key);
    }
  }
}

void UnifiedGameModel::loadLaunchActivity() {
  m_lastLaunchForGame.clear();
  QSqlQuery query(m_database);
  if (!query.exec(
          QStringLiteral("SELECT source, runner, app_id, last_launched FROM launch_activity"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    m_lastLaunchForGame.insert(key, query.value(3).toLongLong());
  }
}

void UnifiedGameModel::loadOrganization() {
  m_organizationForGame.clear();
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT source, runner, app_id, completion_status, tags_json FROM game_organization"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    QStringList tags;
    const QJsonArray values = QJsonDocument::fromJson(query.value(4).toByteArray()).array();
    for (const QJsonValue& value : values) {
      if (value.isString()) {
        tags.append(value.toString());
      }
    }
    m_organizationForGame.insert(
        key, {.status = normalizedStatus(query.value(3).toString()), .tags = tags});
  }
}

void UnifiedGameModel::loadCollections() {
  m_collectionNames.clear();
  m_collectionsForGame.clear();
  QSqlQuery query(m_database);
  if (query.exec(QStringLiteral("SELECT name FROM collections ORDER BY name COLLATE NOCASE"))) {
    while (query.next()) {
      m_collectionNames.append(query.value(0).toString());
    }
  }
  if (!query.exec(QStringLiteral(
          "SELECT collection_name, source, runner, app_id FROM collection_games ORDER BY "
          "collection_name COLLATE NOCASE"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(1).toString() + QChar::Null + query.value(2).toString() +
                        QChar::Null + query.value(3).toString();
    m_collectionsForGame[key].append(query.value(0).toString());
  }
}
