#include "backup/BackupSnapshot.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

namespace {
QString keyFor(const QJsonObject& row) {
  return row.value("source").toString() + QChar::Null + row.value("runner").toString() +
         QChar::Null + row.value("app_id").toString();
}
bool captureDatabase(QSqlDatabase& database, const QJsonObject& settings, BackupPayload* output,
                     QString* error) {
  const auto fail = [&](const QString& message) {
    database.rollback();
    if (error)
      *error = message;
    return false;
  };
  if (!database.transaction())
    return fail("Could not start a consistent library snapshot.");
  BackupPayload payload;
  payload.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  payload.settings = settings;
  QSqlQuery query(database);
  if (!query.exec("SELECT name FROM sqlite_master WHERE type='table'"))
    return fail("Could not inspect the library schema.");
  QSet<QString> tables;
  while (query.next())
    tables.insert(query.value(0).toString());
  QMap<QString, QJsonObject> flags;
  qint64 metadataBytes = 0;
  const auto storeFlag = [&](const QString& key, const QJsonObject& row) {
    if (flags.contains(key))
      metadataBytes -= QJsonDocument(flags.value(key)).toJson(QJsonDocument::Compact).size() + 1;
    metadataBytes += QJsonDocument(row).toJson(QJsonDocument::Compact).size() + 1;
    if (metadataBytes > BackupArchive::MaxManifestBytes)
      return false;
    flags.insert(key, row);
    return true;
  };
  struct LegacySource {
    QString table;
    QString source;
    QString appId;
    QString runner;
  };
  const QList<LegacySource> sources{
      {"games", "'Steam'", "app_id", "''"},
      {"lutris_games", "'Lutris'", "id", "''"},
      {"heroic_games", "CASE WHEN runner='gog-direct' THEN 'GOG' ELSE 'Heroic' END", "app_id",
       "COALESCE(runner, '')"},
      {"faugus_games", "'Faugus'", "game_id", "''"},
      {"retroarch_games", "'RetroArch'", "game_id", "''"},
      {"pcsx2_games", "'PCSX2'", "'path:' || path", "COALESCE(serial, '')"},
      {"ryujinx_games", "'Ryujinx'", "game_id", "COALESCE(flatpak_app_id, '')"},
      {"battlenet_games", "'Battle.net'", "game_id", "COALESCE(runner, '')"},
      {"manual_games", "'Manual'", "id", "''"}};
  for (const auto& source : sources) {
    if (!tables.contains(source.table))
      continue;
    if (!query.exec(QStringLiteral("SELECT %1, %2, %3, favorite, hidden FROM %4")
                        .arg(source.source, source.runner, source.appId, source.table)))
      return fail("Could not read personal state for " + source.table + ".");
    while (query.next()) {
      QJsonObject row{{"source", query.value(0).toString()},
                      {"runner", query.value(1).toString()},
                      {"app_id", query.value(2).toString()},
                      {"favorite", query.value(3).toBool()},
                      {"hidden", query.value(4).toBool()}};
      if (!storeFlag(keyFor(row), row))
        return fail("Personal records exceed the backup metadata limit.");
      if (flags.size() > 100000)
        return fail("The library contains too many personal records for one backup.");
    }
  }
  qint64 recordCount = 0;
  qint64 artworkBytes = 0;
  QHash<QString, QString> assetForPath;
  const auto schemas = BackupArchive::tableColumns();
  for (auto schema = schemas.begin(); schema != schemas.end(); ++schema) {
    QJsonArray records;
    if (tables.contains(schema.key())) {
      if (!query.exec("PRAGMA table_info(" + schema.key() + ")"))
        return fail("Could not inspect a personal-data table.");
      QSet<QString> columns;
      while (query.next())
        columns.insert(query.value(1).toString());
      QStringList expressions;
      for (const auto& column : schema.value()) {
        if (columns.contains(column))
          expressions.append(column);
        else if (schema.key() == "artwork_overrides" &&
                 (column == "hero_path" || column == "logo_path"))
          expressions.append("''");
        else
          return fail("The personal-data schema is unsupported: " + schema.key() + ".");
      }
      if (!query.exec("SELECT " + expressions.join(", ") + " FROM " + schema.key()))
        return fail("Could not read the personal-data table " + schema.key() + ".");
      while (query.next()) {
        if (++recordCount > 100000)
          return fail("The library contains too many personal records for one backup.");
        QJsonObject row;
        for (int index = 0; index < schema.value().size(); ++index) {
          const QString column = schema.value().at(index);
          const auto value = query.value(index);
          if (column == "favorite" || column == "hidden" || column == "active" ||
              column == "is_primary") {
            row.insert(column, schema.key() == "user_game_flags" && value.isNull()
                                   ? QJsonValue(QJsonValue::Null)
                                   : QJsonValue(value.toBool()));
          } else if (column == "created_at" || column == "last_launched" ||
                     column == "launch_count")
            row.insert(column, double(value.toLongLong()));
          else if (schema.key() == "artwork_overrides" && column.endsWith("_path")) {
            const QString path = value.toString();
            if (path.isEmpty()) {
              row.insert(column, "");
              continue;
            }
            if (!assetForPath.contains(path)) {
              QFile image(path);
              if (!image.open(QIODevice::ReadOnly) || image.size() > BackupArchive::MaxImageBytes)
                return fail("Custom artwork is missing or too large. Repair or reset it before "
                            "backing up.");
              const auto bytes = image.read(BackupArchive::MaxImageBytes + 1);
              const auto name = BackupArchive::artworkName(bytes, error);
              if (name.isEmpty())
                return fail("Custom artwork is invalid. Repair or reset it before backing up.");
              if (!payload.artwork.contains(name)) {
                artworkBytes += bytes.size();
                if (artworkBytes > BackupArchive::MaxTotalBytes)
                  return fail("Custom artwork exceeds the backup size limit.");
                if (payload.artwork.size() >= BackupArchive::MaxArtworkFiles)
                  return fail("The backup contains too many artwork files.");
                payload.artwork.insert(name, bytes);
              }
              assetForPath.insert(path, name);
            }
            row.insert(column, assetForPath.value(path));
          } else {
            const QString text = value.toString();
            if (text.size() > 128 * 1024)
              return fail("A personal record exceeds the backup size limit.");
            row.insert(column, text);
          }
        }
        if (schema.key() == "user_game_flags") {
          const QString key = keyFor(row);
          auto merged = flags.value(key, row);
          for (const QString& flag : {QStringLiteral("favorite"), QStringLiteral("hidden")})
            if (!row.value(flag).isNull())
              merged.insert(flag, row.value(flag));
          if (!storeFlag(key, merged))
            return fail("Personal records exceed the backup metadata limit.");
        } else {
          metadataBytes += QJsonDocument(row).toJson(QJsonDocument::Compact).size() + 1;
          if (metadataBytes > BackupArchive::MaxManifestBytes)
            return fail("Personal records exceed the backup metadata limit.");
          records.append(row);
        }
      }
    }
    if (schema.key() != "user_game_flags")
      payload.library.insert(schema.key(), records);
  }
  QJsonArray personalFlags;
  for (const auto& flag : flags)
    personalFlags.append(flag);
  payload.library.insert("user_game_flags", personalFlags);
  if (!BackupArchive::validate(payload, error)) {
    database.rollback();
    return false;
  }
  if (!database.commit())
    return fail("Could not complete the library snapshot.");
  *output = std::move(payload);
  return true;
}
} // namespace

bool BackupSnapshot::capture(const QString& path, const QJsonObject& settings,
                             BackupPayload* output, QString* error) {
  if (error)
    error->clear();
  if (!output || !QFileInfo(path).isFile()) {
    if (error)
      *error = "The library database does not exist.";
    return false;
  }
  const QString connection = "omakade-backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool okay = false;
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
    database.setDatabaseName(path);
    if (database.open())
      okay = captureDatabase(database, settings, output, error);
    else if (error)
      *error = "Could not open the library database for backup.";
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  return okay;
}
