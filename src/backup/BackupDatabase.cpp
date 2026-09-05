#include "backup/BackupDatabase.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

namespace {
const QMap<QString, QString> schemas{
    {"user_game_flags", "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
                        "favorite INTEGER, hidden INTEGER, PRIMARY KEY(source,runner,app_id)"},
    {"game_organization",
     "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, completion_status TEXT NOT "
     "NULL DEFAULT '', tags_json TEXT NOT NULL DEFAULT '[]', PRIMARY KEY(source,runner,app_id)"},
    {"collections", "name TEXT PRIMARY KEY COLLATE NOCASE, created_at INTEGER NOT NULL"},
    {"collection_games",
     "collection_name TEXT NOT NULL, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT "
     "NULL, PRIMARY KEY(collection_name,source,runner,app_id)"},
    {"game_link_members",
     "group_id TEXT NOT NULL, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
     "is_primary INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source,runner,app_id)"},
    {"launch_preferences",
     "group_id TEXT PRIMARY KEY, source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL"},
    {"launch_activity",
     "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, last_launched INTEGER NOT "
     "NULL, launch_count INTEGER NOT NULL DEFAULT 1, PRIMARY KEY(source,runner,app_id)"},
    {"manual_games", "id TEXT PRIMARY KEY, entry TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT "
                     "0, hidden INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1"},
    {"saved_filters", "id TEXT PRIMARY KEY, name TEXT NOT NULL, name_key TEXT NOT NULL UNIQUE, "
                      "state_json TEXT NOT NULL"},
    {"artwork_overrides", "source TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, "
                          "cover_path TEXT NOT NULL, hero_path TEXT NOT NULL DEFAULT '', logo_path "
                          "TEXT NOT NULL DEFAULT '', PRIMARY KEY(source,runner,app_id)"}};
QStringList primaryKey(const QString& table) {
  if (table == "collections")
    return {"name"};
  if (table == "manual_games" || table == "saved_filters")
    return {"id"};
  if (table == "launch_preferences")
    return {"group_id"};
  if (table == "collection_games")
    return {"collection_name", "source", "runner", "app_id"};
  return {"source", "runner", "app_id"};
}
bool stageArtwork(const QString& directory, const BackupPayload& payload, QString* error) {
  const auto fail = [&](const QString& message) {
    if (error)
      *error = message;
    return false;
  };
  if (payload.artwork.isEmpty())
    return true;
  if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory))
    return fail("Could not create the owned artwork folder.");
  for (auto asset = payload.artwork.begin(); asset != payload.artwork.end(); ++asset) {
    const QString path = directory + "/" + asset.key().mid(QStringLiteral("artwork/").size());
    const QFileInfo existing(path);
    if (existing.isSymLink() || (existing.exists() && !existing.isFile()))
      return fail("An artwork destination is not a regular file.");
    QFile old(path);
    if (old.open(QIODevice::ReadOnly) && old.size() <= BackupArchive::MaxImageBytes &&
        old.readAll() == asset.value())
      continue;
    old.close();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
      return fail("Could not stage restored artwork.");
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (output.write(asset.value()) != asset.value().size() || !output.commit())
      return fail("Could not save complete restored artwork.");
  }
  return true;
}
bool restoreDatabase(QSqlDatabase& database, const QString& artworkDirectory,
                     const BackupPayload& payload, BackupDatabase::Mode mode, QString* error) {
  const auto fail = [&](const QString& message) {
    database.rollback();
    if (error)
      *error = message;
    return false;
  };
  if (!database.transaction())
    return fail("Could not start the restore transaction.");
  QSqlQuery query(database);
  for (auto schema = schemas.begin(); schema != schemas.end(); ++schema)
    if (!query.exec("CREATE TABLE IF NOT EXISTS " + schema.key() + " (" + schema.value() + ")"))
      return fail("Could not prepare the personal-data schema.");
  if (!query.exec("PRAGMA table_info(artwork_overrides)"))
    return fail("Could not inspect the artwork schema.");
  QSet<QString> artworkColumns;
  while (query.next())
    artworkColumns.insert(query.value(1).toString());
  for (const auto& column : {QStringLiteral("hero_path"), QStringLiteral("logo_path")})
    if (!artworkColumns.contains(column) &&
        !query.exec("ALTER TABLE artwork_overrides ADD COLUMN " + column +
                    " TEXT NOT NULL DEFAULT ''"))
      return fail("Could not migrate the artwork schema.");

  if (mode == BackupDatabase::Mode::Replace) {
    for (auto schema = schemas.begin(); schema != schemas.end(); ++schema)
      if (!query.exec("DELETE FROM " + schema.key()))
        return fail("Could not replace existing personal records.");
    if (!query.exec("SELECT name FROM sqlite_master WHERE type='table'"))
      return fail("Could not inspect cached sources.");
    QSet<QString> tables;
    while (query.next())
      tables.insert(query.value(0).toString());
    for (const QString& table :
         {QStringLiteral("games"), QStringLiteral("lutris_games"), QStringLiteral("heroic_games"),
          QStringLiteral("faugus_games"), QStringLiteral("retroarch_games"),
          QStringLiteral("pcsx2_games"), QStringLiteral("ryujinx_games"),
          QStringLiteral("battlenet_games")})
      if (tables.contains(table) && !query.exec("UPDATE " + table + " SET favorite=0, hidden=0"))
        return fail("Could not reset legacy personal flags.");
  }

  const QJsonArray incomingLinks = payload.library.value("game_link_members").toArray();
  QSet<QString> incomingGroups, affectedGroups;
  for (const auto& value : incomingLinks)
    incomingGroups.insert(value.toObject().value("group_id").toString());
  for (const auto& value : incomingLinks) {
    const auto row = value.toObject();
    query.prepare(
        "SELECT group_id FROM game_link_members WHERE source=? AND runner=? AND app_id=?");
    query.addBindValue(row.value("source").toString());
    query.addBindValue(row.value("runner").toString());
    query.addBindValue(row.value("app_id").toString());
    if (!query.exec())
      return fail("Could not inspect linked-game conflicts.");
    while (query.next())
      affectedGroups.insert(query.value(0).toString());
    query.prepare("DELETE FROM game_link_members WHERE source=? AND runner=? AND app_id=?");
    query.addBindValue(row.value("source").toString());
    query.addBindValue(row.value("runner").toString());
    query.addBindValue(row.value("app_id").toString());
    if (!query.exec())
      return fail("Could not reconcile linked-game membership.");
  }
  // An incoming group's archived membership is authoritative. Other groups keep
  // their remaining members when at least two remain, with one valid primary.
  for (const auto& group : incomingGroups) {
    query.prepare("DELETE FROM game_link_members WHERE group_id=?");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not replace an imported linked group.");
    query.prepare("DELETE FROM launch_preferences WHERE group_id=?");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not reconcile a launch preference.");
  }
  for (const auto& group : affectedGroups - incomingGroups) {
    query.prepare("SELECT source, runner, app_id, is_primary FROM game_link_members WHERE "
                  "group_id=? ORDER BY is_primary DESC, source, runner, app_id");
    query.addBindValue(group);
    if (!query.exec())
      return fail("Could not inspect remaining linked installations.");
    QList<QStringList> members;
    while (query.next())
      members.append(
          {query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
    if (members.size() < 2) {
      query.prepare("DELETE FROM game_link_members WHERE group_id=?");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not unlink an incomplete group.");
      query.prepare("DELETE FROM launch_preferences WHERE group_id=?");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not remove an incomplete preference.");
    } else {
      const auto primary = members.first();
      query.prepare("UPDATE game_link_members SET is_primary=(source=? AND runner=? AND app_id=?) "
                    "WHERE group_id=?");
      for (const auto& part : primary)
        query.addBindValue(part);
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not retain a group's primary installation.");
      query.prepare("DELETE FROM launch_preferences WHERE group_id=? AND NOT EXISTS (SELECT 1 FROM "
                    "game_link_members m WHERE m.group_id=launch_preferences.group_id AND "
                    "m.source=launch_preferences.source AND m.runner=launch_preferences.runner AND "
                    "m.app_id=launch_preferences.app_id)");
      query.addBindValue(group);
      if (!query.exec())
        return fail("Could not reconcile a removed preferred installation.");
    }
  }

  QHash<QString, QString> collections;
  if (!query.exec("SELECT name FROM collections"))
    return fail("Could not inspect collections.");
  while (query.next())
    collections.insert(query.value(0).toString().toCaseFolded(), query.value(0).toString());
  // Matching IDs are replaced together, allowing archived names to swap without
  // spurious conflicts with records that are themselves about to be replaced.
  for (const auto& value : payload.library.value("saved_filters").toArray()) {
    query.prepare("DELETE FROM saved_filters WHERE id=?");
    query.addBindValue(value.toObject().value("id").toString());
    if (!query.exec())
      return fail("Could not reconcile saved-filter identities.");
  }
  QHash<QString, QString> filterOwner;
  if (!query.exec("SELECT id, name_key FROM saved_filters"))
    return fail("Could not inspect saved filters.");
  while (query.next())
    filterOwner.insert(query.value(1).toString(), query.value(0).toString());
  const auto columns = BackupArchive::tableColumns();
  const QStringList order{"collections",       "user_game_flags",   "game_organization",
                          "manual_games",      "artwork_overrides", "launch_activity",
                          "saved_filters",     "collection_games",  "game_link_members",
                          "launch_preferences"};
  for (const auto& table : order) {
    const auto key = primaryKey(table);
    const auto fields = columns.value(table);
    QStringList placeholders, assignments;
    for (const auto& column : fields) {
      placeholders.append("?");
      if (!key.contains(column))
        assignments.append(column + "=" +
                           (table == "user_game_flags"
                                ? "COALESCE(excluded." + column + ",user_game_flags." + column + ")"
                                : "excluded." + column));
    }
    const QString suffix = table == "collections" || table == "collection_games"
                               ? " DO NOTHING"
                               : " DO UPDATE SET " + assignments.join(", ");
    const QString sql = "INSERT INTO " + table + "(" + fields.join(",") + ") VALUES(" +
                        placeholders.join(",") + ") ON CONFLICT(" + key.join(",") + ")" + suffix;
    for (const auto& value : payload.library.value(table).toArray()) {
      auto row = value.toObject();
      if (table == "collections" || table == "collection_games") {
        const QString field = table == "collections" ? "name" : "collection_name";
        const QString name = row.value(field).toString();
        const QString canonical = collections.value(name.toCaseFolded(), name);
        collections.insert(name.toCaseFolded(), canonical);
        row.insert(field, canonical);
      }
      if (table == "saved_filters") {
        const QString id = row.value("id").toString();
        const QString originalName = row.value("name").toString();
        QString name = originalName;
        QString nameKey = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
        int attempt = 0;
        while (filterOwner.contains(nameKey) && filterOwner.value(nameKey) != id) {
          if (++attempt > 501)
            return fail("Could not choose a unique restored filter name.");
          name = originalName.left(70) + " (restored " + digest +
                 (attempt > 1 ? "-" + QString::number(attempt) : QString{}) + ")";
          nameKey = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        }
        for (auto owner = filterOwner.begin(); owner != filterOwner.end();) {
          if (owner.value() == id)
            owner = filterOwner.erase(owner);
          else
            ++owner;
        }
        filterOwner.insert(nameKey, id);
        if (filterOwner.size() > 500)
          return fail("Merge would exceed the 500 saved-filter limit. Remove some views or choose "
                      "replacement.");
        row.insert("name", name);
        row.insert("name_key", nameKey);
      }
      query.prepare(sql);
      for (const auto& column : fields) {
        auto field = row.value(column);
        if (table == "artwork_overrides" && column.endsWith("_path") && !field.toString().isEmpty())
          field = artworkDirectory + "/" + field.toString().mid(QStringLiteral("artwork/").size());
        query.addBindValue(field.isNull() ? QVariant{} : field.toVariant());
      }
      if (!query.exec())
        return fail("Could not import personal records into " + table +
                    ". No database changes were committed.");
    }
  }
  if (!database.commit())
    return fail("Could not commit restored personal records.");
  return true;
}
} // namespace

bool BackupDatabase::restore(const QString& path, const BackupPayload& payload, Mode mode,
                             QString* error) {
  if (mode != Mode::Merge && mode != Mode::Replace) {
    if (error)
      *error = "The restore mode is invalid.";
    return false;
  }
  if (!BackupArchive::validate(payload, error))
    return false;
  const QFileInfo file(path);
  if (!file.isAbsolute() || file.isSymLink() || !QFileInfo(file.absolutePath()).isDir()) {
    if (error)
      *error = "The restore database path is invalid.";
    return false;
  }
  const QString artwork = file.absolutePath() + "/artwork";
  if (!stageArtwork(artwork, payload, error))
    return false;
  const QString connection =
      "omakade-restore-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool okay = false;
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setDatabaseName(path);
    database.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (database.open())
      okay = restoreDatabase(database, artwork, payload, mode, error);
    else if (error)
      *error = "Could not open the restore database.";
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  return okay;
}
