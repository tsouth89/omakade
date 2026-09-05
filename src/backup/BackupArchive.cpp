#include "backup/BackupArchive.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
#include <cmath>
#include <zip.h>

namespace {
QJsonObject manifestFor(const BackupPayload& payload) {
  QJsonArray artworks;
  auto names = payload.artwork.keys();
  names.sort();
  for (const auto& name : names)
    artworks.append(name);
  return {{"format", "omakade-backup"},     {"version", 1},
          {"createdAt", payload.createdAt}, {"library", payload.library},
          {"settings", payload.settings},   {"artwork", artworks}};
}
bool hasControls(const QString& value) {
  for (const auto ch : value)
    if (ch.category() == QChar::Other_Control)
      return true;
  return false;
}
bool fail(QString* error, const QString& message) {
  if (error)
    *error = message;
  return false;
}
bool integer(const QJsonValue& value, double minimum, double maximum) {
  return value.isDouble() && std::isfinite(value.toDouble()) &&
         value.toDouble() == std::floor(value.toDouble()) && value.toDouble() >= minimum &&
         value.toDouble() <= maximum;
}
bool flag(const QJsonValue& value) { return value.isBool() || integer(value, 0, 1); }
bool text(const QJsonValue& value, int limit, bool empty = true) {
  return value.isString() && value.toString().size() <= limit &&
         !value.toString().contains(QChar::Null) && (empty || !value.toString().isEmpty());
}
bool validSavedState(const QJsonObject& state) {
  if (state.size() != 10 || !integer(state.value("version"), 1, 1) ||
      !integer(state.value("mode"), 0, 3) || !integer(state.value("sort"), 0, 2) ||
      !integer(state.value("availability"), 0, 2) || !state.value("showHidden").isBool())
    return false;
  for (const QString& key :
       {QStringLiteral("search"), QStringLiteral("source"), QStringLiteral("status"),
        QStringLiteral("collection"), QStringLiteral("tag")})
    if (!text(state.value(key), 4096))
      return false;
  return QStringList{"", "backlog", "playing", "completed", "abandoned"}.contains(
      state.value("status").toString());
}
bool validManual(const QJsonObject& entry, const QString& id) {
  const QSet<QString> fields{"id", "title", "executable", "directory", "arguments"};
  if (entry.size() != fields.size() || entry.value("id").toString() != id ||
      !text(entry.value("title"), 200, false) || !text(entry.value("executable"), 4096, false) ||
      !text(entry.value("directory"), 4096, false) || !entry.value("arguments").isArray())
    return false;
  for (auto it = entry.begin(); it != entry.end(); ++it)
    if (!fields.contains(it.key()))
      return false;
  if (!QDir::isAbsolutePath(entry.value("executable").toString()) ||
      !QDir::isAbsolutePath(entry.value("directory").toString()))
    return false;
  const auto args = entry.value("arguments").toArray();
  if (args.size() > 256)
    return false;
  for (const auto& arg : args)
    if (!text(arg, 128 * 1024))
      return false;
  return true;
}
QString identity(const QString& table, const QJsonObject& row) {
  QJsonArray key;
  if (table == "collections")
    key.append(
        row.value("name").toString().normalized(QString::NormalizationForm_C).toCaseFolded());
  else if (table == "manual_games" || table == "saved_filters")
    key.append(row.value("id"));
  else if (table == "launch_preferences")
    key.append(row.value("group_id"));
  else {
    if (table == "collection_games")
      key.append(row.value("collection_name").toString().toCaseFolded());
    key.append(row.value("source"));
    key.append(row.value("runner"));
    key.append(row.value("app_id"));
  }
  return QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact));
}
} // namespace

QMap<QString, QStringList> BackupArchive::tableColumns() {
  return {{"user_game_flags", {"source", "runner", "app_id", "favorite", "hidden"}},
          {"game_organization", {"source", "runner", "app_id", "completion_status", "tags_json"}},
          {"collections", {"name", "created_at"}},
          {"collection_games", {"collection_name", "source", "runner", "app_id"}},
          {"game_link_members", {"group_id", "source", "runner", "app_id", "is_primary"}},
          {"launch_preferences", {"group_id", "source", "runner", "app_id"}},
          {"launch_activity", {"source", "runner", "app_id", "last_launched", "launch_count"}},
          {"manual_games", {"id", "entry", "favorite", "hidden", "active"}},
          {"saved_filters", {"id", "name", "name_key", "state_json"}},
          {"artwork_overrides",
           {"source", "runner", "app_id", "cover_path", "hero_path", "logo_path"}}};
}

QStringList BackupArchive::settingNames() {
  return {"reduced_motion",    "artwork_cache_limit_mb",
          "steam_enabled",     "lutris_enabled",
          "heroic_enabled",    "gog_enabled",
          "faugus_enabled",    "retroarch_enabled",
          "pcsx2_enabled",     "ryujinx_enabled",
          "pcsx2_auto",        "ryujinx_auto",
          "battlenet_enabled", "close_after_launch",
          "couch_mode",        "couch_library_view",
          "gog_library_paths"};
}

QString BackupArchive::artworkName(const QByteArray& bytes, QString* error) {
  if (bytes.isEmpty() || bytes.size() > MaxImageBytes) {
    fail(error, "Artwork exceeds the image size limit.");
    return {};
  }
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  const auto format = reader.format().toLower();
  const QSize size = reader.size();
  if (!QList<QByteArray>{"png", "jpeg", "jpg", "webp"}.contains(format) || !size.isValid() ||
      size.width() > 16384 || size.height() > 16384 ||
      qint64(size.width()) * size.height() > 64 * 1024 * 1024 || reader.read().isNull()) {
    fail(error, "Artwork is not a supported, valid image.");
    return {};
  }
  return "artwork/" +
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
         "." + QString::fromLatin1(format == "jpeg" ? QByteArray("jpg") : format);
}

bool BackupArchive::validate(const BackupPayload& payload, QString* error) {
  if (error)
    error->clear();
  if (!QDateTime::fromString(payload.createdAt, Qt::ISODate).isValid())
    return fail(error, "The backup timestamp is invalid.");
  const auto columns = tableColumns();
  qint64 rowCount = 0;
  QSet<QString> references, collectionNames, memberships, groups, preferredGroups, savedNames;
  QHash<QString, int> primaryCounts;
  QHash<QString, QString> groupForIdentity;
  QHash<QString, QString> preferredIdentity;
  for (auto table = payload.library.begin(); table != payload.library.end(); ++table) {
    if (!columns.contains(table.key()) || !table.value().isArray())
      return fail(error, "The backup contains an unsupported library table.");
    const auto rows = table.value().toArray();
    rowCount += rows.size();
    if (rowCount > 100000 || (table.key() == "saved_filters" && rows.size() > 500))
      return fail(error, "The backup contains too many personal records.");
    QSet<QString> identities;
    for (const auto& value : rows) {
      if (!value.isObject())
        return fail(error, "A personal record is not an object.");
      const auto row = value.toObject();
      if (row.size() != columns.value(table.key()).size())
        return fail(error, "A personal record has missing or extra fields.");
      for (const auto& column : columns.value(table.key())) {
        const auto field = row.value(column);
        if (field.isUndefined())
          return fail(error, "A personal record is missing a required field.");
        if (column == "favorite" || column == "hidden") {
          if (!(table.key() == "user_game_flags" && field.isNull()) && !flag(field))
            return fail(error, "A personal flag is invalid.");
        } else if (column == "is_primary" || column == "active") {
          if (!flag(field))
            return fail(error, "A personal flag is invalid.");
        } else if (column == "created_at" || column == "last_launched" ||
                   column == "launch_count") {
          if (!integer(field, 0, 9007199254740991.0))
            return fail(error, "A personal timestamp or count is invalid.");
        } else if (column == "entry" || column == "tags_json" || column == "state_json") {
          if (!text(field, column == "entry" ? 128 * 1024 : 32768))
            return fail(error, "A personal record is too large.");
          QJsonParseError parse;
          const auto doc = QJsonDocument::fromJson(field.toString().toUtf8(), &parse);
          if (parse.error != QJsonParseError::NoError)
            return fail(error, "A personal record contains invalid JSON.");
          if (column == "entry" &&
              (!doc.isObject() || !validManual(doc.object(), row.value("id").toString())))
            return fail(error, "A manual game has invalid launch details.");
          if (column == "state_json" && (!doc.isObject() || !validSavedState(doc.object())))
            return fail(error, "A saved filter has an unsupported format.");
          if (column == "tags_json") {
            if (!doc.isArray() || doc.array().size() > 20)
              return fail(error, "The tag list is invalid.");
            for (const auto& tag : doc.array())
              if (!text(tag, 32, false))
                return fail(error, "A tag is invalid.");
          }
        } else {
          const bool emptyAllowed =
              column == "runner" || column == "completion_status" || column.endsWith("_path");
          if (!text(field, 4096, emptyAllowed))
            return fail(error, "A personal identifier or value is invalid.");
        }
        if ((column == "cover_path" || column == "hero_path" || column == "logo_path") &&
            !field.toString().isEmpty())
          references.insert(field.toString());
      }
      if (table.key() == "game_organization" &&
          !QStringList{"", "backlog", "playing", "completed", "abandoned"}.contains(
              row.value("completion_status").toString()))
        return fail(error, "A completion status is invalid.");
      const auto key = identity(table.key(), row);
      if (identities.contains(key))
        return fail(error, "The backup contains duplicate personal identities.");
      identities.insert(key);
      if (table.key() == "collections") {
        const QString name = row.value("name").toString();
        if (name.size() > 48 || name.simplified() != name || hasControls(name))
          return fail(error, "A collection name is invalid.");
        collectionNames.insert(name.toCaseFolded());
      }
      if (table.key() == "collection_games")
        memberships.insert(row.value("collection_name").toString().toCaseFolded());
      if (table.key() == "game_link_members") {
        const auto group = row.value("group_id").toString();
        groups.insert(group);
        groupForIdentity.insert(key, group);
        if (row.value("is_primary").toBool() || row.value("is_primary").toInt() == 1)
          ++primaryCounts[group];
      }
      if (table.key() == "launch_preferences") {
        preferredGroups.insert(row.value("group_id").toString());
        preferredIdentity.insert(row.value("group_id").toString(),
                                 identity("game_link_members", row));
      }
      if (table.key() == "saved_filters") {
        const auto name = row.value("name").toString();
        const auto key = name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (name.trimmed() != name || name.size() > 100 || hasControls(name) ||
            row.value("name_key").toString() != key || savedNames.contains(key))
          return fail(error, "A saved filter name is invalid or duplicated.");
        savedNames.insert(key);
      }
    }
  }
  for (const auto& group : groups)
    if (primaryCounts.value(group) != 1)
      return fail(error, "A linked group does not have exactly one primary installation.");
  for (auto preference = preferredIdentity.begin(); preference != preferredIdentity.end();
       ++preference)
    if (groupForIdentity.value(preference.value()) != preference.key())
      return fail(error, "A preferred installation does not belong to its linked group.");
  if (!(memberships - collectionNames).isEmpty() || !(preferredGroups - groups).isEmpty())
    return fail(error, "A personal record references a missing collection or group.");
  const auto allowedSettings = settingNames();
  for (auto setting = payload.settings.begin(); setting != payload.settings.end(); ++setting) {
    if (!allowedSettings.contains(setting.key()))
      return fail(error, "The backup contains an unsupported setting.");
    if (setting.key() == "artwork_cache_limit_mb") {
      if (!integer(setting.value(), 128, 8192))
        return fail(error, "The artwork cache limit is invalid.");
    } else if (setting.key() == "couch_library_view") {
      if (!QStringList{"detail", "grid"}.contains(setting.value().toString()))
        return fail(error, "The library view is invalid.");
    } else if (setting.key() == "gog_library_paths") {
      if (!setting.value().isArray() || setting.value().toArray().size() > 64)
        return fail(error, "The GOG folder list is invalid.");
      for (const auto& path : setting.value().toArray())
        if (!text(path, 4096, false) || !QDir::isAbsolutePath(path.toString()) ||
            hasControls(path.toString()))
          return fail(error, "A GOG folder is invalid.");
    } else if (!setting.value().isBool())
      return fail(error, "A backup setting is invalid.");
  }
  qint64 total = QJsonDocument(manifestFor(payload)).toJson(QJsonDocument::Compact).size();
  if (total > MaxManifestBytes || payload.artwork.size() > MaxArtworkFiles)
    return fail(error, "The backup exceeds its metadata or artwork limits.");
  QSet<QString> names;
  for (auto artwork = payload.artwork.begin(); artwork != payload.artwork.end(); ++artwork) {
    total += artwork.value().size();
    if (total > MaxTotalBytes)
      return fail(error, "The backup exceeds the total size limit.");
    const auto expected = artworkName(artwork.value(), error);
    if (expected.isEmpty() || artwork.key() != expected)
      return fail(error, "Artwork does not match its content-addressed name.");
    names.insert(artwork.key());
  }
  if (names != references)
    return fail(error, "The backup has missing or unreferenced artwork.");
  return true;
}

bool BackupArchive::write(const QString& path, const BackupPayload& payload, QString* error) {
  if (!validate(payload, error))
    return false;
  const QFileInfo destination(path);
  if (!destination.isAbsolute() || destination.isSymLink())
    return fail(error, "Choose an absolute backup file path.");
  QTemporaryFile temporary(destination.absolutePath() + "/.omakade-backup-XXXXXX");
  if (!temporary.open())
    return fail(error, "Could not create the backup file.");
  const QString temporaryPath = temporary.fileName();
  temporary.close();
  int code = 0;
  zip_t* archive =
      zip_open(QFile::encodeName(temporaryPath).constData(), ZIP_CREATE | ZIP_TRUNCATE, &code);
  if (!archive)
    return fail(error, "Could not create the backup archive.");
  QStringList names = payload.artwork.keys();
  names.sort();
  const QByteArray manifest = QJsonDocument(manifestFor(payload)).toJson(QJsonDocument::Compact);
  auto add = [&](const QString& name, const QByteArray& bytes) {
    auto* source = zip_source_buffer(archive, bytes.constData(), zip_uint64_t(bytes.size()), 0);
    if (!source)
      return false;
    const auto index = zip_file_add(archive, name.toUtf8().constData(), source, ZIP_FL_ENC_UTF_8);
    if (index < 0) {
      zip_source_free(source);
      return false;
    }
    if (zip_file_set_external_attributes(archive, zip_uint64_t(index), 0, ZIP_OPSYS_UNIX,
                                         zip_uint32_t(0100600) << 16) != 0)
      return false;
    return zip_set_file_compression(archive, zip_uint64_t(index), ZIP_CM_DEFLATE, 6) == 0;
  };
  bool okay = add("manifest.json", manifest);
  for (const auto& name : names)
    if (okay)
      okay = add(name, payload.artwork.constFind(name).value());
  if (!okay) {
    zip_discard(archive);
    return fail(error, "Could not write the backup archive.");
  }
  if (zip_close(archive) != 0) {
    zip_discard(archive);
    return fail(error, "Could not finish the backup archive.");
  }
  QFile input(temporaryPath);
  QSaveFile output(path);
  if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
    return fail(error, "Could not save the backup archive.");
  output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  while (!input.atEnd()) {
    const auto chunk = input.read(1024 * 1024);
    if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
      output.cancelWriting();
      return fail(error, "Could not save the complete backup archive.");
    }
  }
  if (!output.commit())
    return fail(error, "Could not replace the backup file.");
  return true;
}

bool BackupArchive::read(const QString& path, BackupPayload* output, QString* error) {
  if (error)
    error->clear();
  if (!output || !QFileInfo(path).isFile() ||
      QFileInfo(path).size() > MaxTotalBytes + MaxManifestBytes)
    return fail(error, "The backup file is missing or too large.");
  int code = 0;
  auto* archive = zip_open(QFile::encodeName(path).constData(), ZIP_RDONLY | ZIP_CHECKCONS, &code);
  if (!archive)
    return fail(error, "The file is not a valid backup archive.");
  const auto reject = [&](const QString& message) {
    zip_discard(archive);
    return fail(error, message);
  };
  const auto count = zip_get_num_entries(archive, 0);
  if (count < 1 || count > MaxArtworkFiles + 1)
    return reject("The backup has too many files.");
  QHash<QString, QByteArray> files;
  qint64 total = 0;
  const QRegularExpression imagePath(QStringLiteral("^artwork/[0-9a-f]{64}\\.(png|jpg|webp)$"));
  for (zip_int64_t i = 0; i < count; ++i) {
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, zip_uint64_t(i), 0, &stat) != 0 || !stat.name)
      return reject("A backup file entry is invalid.");
    const QString name = QString::fromUtf8(stat.name);
    if ((name != "manifest.json" && !imagePath.match(name).hasMatch()) || files.contains(name))
      return reject("The backup contains an unexpected or duplicated path.");
    const qint64 limit = name == "manifest.json" ? MaxManifestBytes : MaxImageBytes;
    if (stat.size == 0 || stat.size > zip_uint64_t(limit) ||
        stat.encryption_method != ZIP_EM_NONE ||
        (stat.comp_method != ZIP_CM_STORE && stat.comp_method != ZIP_CM_DEFLATE))
      return reject("A backup file exceeds its limits or uses an unsupported encoding.");
    total += qint64(stat.size);
    if (total > MaxTotalBytes)
      return reject("The backup exceeds the total size limit.");
    zip_uint8_t system = 0;
    zip_uint32_t attributes = 0;
    if (zip_file_get_external_attributes(archive, zip_uint64_t(i), 0, &system, &attributes) != 0)
      return reject("A backup file entry is invalid.");
    if (system == ZIP_OPSYS_UNIX && ((attributes >> 16) & 0170000) != 0 &&
        ((attributes >> 16) & 0170000) != 0100000)
      return reject("The backup contains a non-regular file.");
    auto* file = zip_fopen_index(archive, zip_uint64_t(i), 0);
    if (!file)
      return reject("Could not read a backup file.");
    QByteArray bytes(qsizetype(stat.size), Qt::Uninitialized);
    zip_uint64_t offset = 0;
    while (offset < stat.size) {
      const auto read = zip_fread(file, bytes.data() + offset, stat.size - offset);
      if (read <= 0) {
        zip_fclose(file);
        return reject("A backup file is truncated or corrupt.");
      }
      offset += zip_uint64_t(read);
    }
    char extra;
    const auto remaining = zip_fread(file, &extra, 1);
    const auto closed = zip_fclose(file);
    if (remaining != 0 || closed != 0)
      return reject("A backup file failed its integrity check.");
    files.insert(name, bytes);
  }
  zip_discard(archive);
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(files.take("manifest.json"), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject())
    return fail(error, "The backup manifest is invalid.");
  const auto manifest = document.object();
  if (manifest.size() != 6 || manifest.value("format").toString() != "omakade-backup" ||
      !integer(manifest.value("version"), 1, 1) || !manifest.value("createdAt").isString() ||
      !manifest.value("library").isObject() || !manifest.value("settings").isObject() ||
      !manifest.value("artwork").isArray())
    return fail(error, "This backup format or version is unsupported.");
  QSet<QString> names;
  for (const auto& name : manifest.value("artwork").toArray()) {
    if (!name.isString() || !files.contains(name.toString()) || names.contains(name.toString()))
      return fail(error, "The artwork inventory is invalid.");
    names.insert(name.toString());
  }
  if (names.size() != files.size())
    return fail(error, "The backup has unlisted artwork.");
  BackupPayload payload{manifest.value("library").toObject(), manifest.value("settings").toObject(),
                        files, manifest.value("createdAt").toString()};
  if (!validate(payload, error))
    return false;
  *output = std::move(payload);
  return true;
}
