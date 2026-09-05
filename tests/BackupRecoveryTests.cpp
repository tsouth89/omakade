#include "app/AppSettings.h"
#include "backup/BackupManager.h"
#include "backup/BackupRecovery.h"
#include "backup/BackupSnapshot.h"
#include "library/BattleNetGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/GameRoles.h"
#include "library/HeroicGameModel.h"
#include "library/LutrisGameModel.h"
#include "library/ManualGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/SteamGameModel.h"
#include "library/UnifiedGameModel.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <cstdlib>

namespace {
BackupRecovery::Paths paths(const QString& base) {
  return {base + "/library.sqlite3", base + "/config.toml", base + "/recovery"};
}
BackupPayload payload(const QString& title, bool couch) {
  BackupPayload value;
  value.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  value.settings = {{"couch_mode", couch}, {"reduced_motion", true}};
  value.library = {
      {"collections", QJsonArray{QJsonObject{{"name", title}, {"created_at", 1788566400}}}}};
  return value;
}
QByteArray read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QStringList collections(const QString& path) {
  BackupPayload snapshot;
  if (!BackupSnapshot::capture(path, {}, &snapshot))
    return {"capture failed"};
  QStringList result;
  for (const auto& row : snapshot.library.value("collections").toArray())
    result << row.toObject().value("name").toString();
  result.sort();
  return result;
}
int child(const QString& base, const QString& checkpoint, bool undo = false) {
  QProcess process;
  process.start(QCoreApplication::applicationFilePath(),
                {"--worker", base, checkpoint, undo ? "undo" : "resume"});
  if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
    return -1;
  return process.exitCode();
}
} // namespace

class BackupRecoveryTests : public QObject {
  Q_OBJECT
private slots:
  void abruptRestore_data();
  void abruptRestore();
  void abruptUndo_data();
  void abruptUndo();
  void changedArchiveAndJournal();
  void missingSettingsAndFreshLibrary();
  void failedSettingsWriteKeepsRecoveryPending();
  void damagedRecoveryBlocksReplay();
  void asynchronousPreviewStagesExactPayload();
  void exportAndInvalidPreview();
  void previewCountsAndMissingPaths();
  void releasedDatabaseMigration();
};

void BackupRecoveryTests::abruptRestore_data() {
  QTest::addColumn<QString>("checkpoint");
  QTest::addColumn<bool>("merge");
  for (const auto& checkpoint : {"prepared", "database", "settings", "complete"}) {
    QTest::newRow(qPrintable(QString(checkpoint) + "-merge")) << QString(checkpoint) << true;
    QTest::newRow(qPrintable(QString(checkpoint) + "-replace")) << QString(checkpoint) << false;
  }
}
void BackupRecoveryTests::abruptRestore() {
  QFETCH(QString, checkpoint);
  QFETCH(bool, merge);
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY2(BackupDatabase::restore(p.database, payload("Local", false),
                                   BackupDatabase::Mode::Replace, &error),
           qPrintable(error));
  AppSettings settings(p.settings);
  settings.setIgdbClientId("localclient");
  settings.setSunshineGameApps(true);
  const auto originalSettings = read(p.settings);
  BackupRecovery recovery(p);
  QVERIFY2(recovery.stage(payload("Imported", true),
                          merge ? BackupDatabase::Mode::Merge : BackupDatabase::Mode::Replace,
                          &error),
           qPrintable(error));
  QCOMPARE(collections(p.database), QStringList{"Local"});
  QCOMPARE(read(p.settings), originalSettings);
  QVERIFY(!recovery.stage(payload("Other", false), BackupDatabase::Mode::Merge, &error));
  QCOMPARE(child(temp.path(), checkpoint), 73);
  QCOMPARE(recovery.status(), checkpoint == "complete" ? "complete" : "prepared");
  const auto archive = recovery.recoveryArchive();
  const auto recoveryBytes = read(archive);
  QVERIFY(!recoveryBytes.isEmpty());
  QVERIFY2(recovery.resume(&error), qPrintable(error));
  QCOMPARE(recovery.status(), "complete");
  QCOMPARE(collections(p.database),
           merge ? QStringList({"Imported", "Local"}) : QStringList{"Imported"});
  AppSettings restored(p.settings);
  QVERIFY(restored.couchModeEnabled());
  QCOMPARE(restored.igdbClientId(), "localclient");
  QVERIFY(restored.sunshineGameApps());
  QCOMPARE(read(archive), recoveryBytes);
  QVERIFY(recovery.resume(&error));
  QCOMPARE(read(archive), recoveryBytes);
  BackupPayload before;
  QVERIFY(BackupArchive::read(archive, &before, &error));
  QCOMPARE(
      before.library.value("collections").toArray().first().toObject().value("name").toString(),
      "Local");
  QVERIFY(!QJsonDocument(before.settings).toJson().contains("localclient"));
  // The completed recovery job is retained when a later request is staged.
  QVERIFY(recovery.stage(payload("Later", false), BackupDatabase::Mode::Merge, &error));
  QCOMPARE(read(archive), recoveryBytes);
  QVERIFY(recovery.undo(&error));
  QCOMPARE(recovery.status(), "reverted");
}

void BackupRecoveryTests::abruptUndo_data() {
  QTest::addColumn<QString>("checkpoint");
  QTest::newRow("database") << QString("undo-database");
  QTest::newRow("settings") << QString("undo-settings");
}
void BackupRecoveryTests::abruptUndo() {
  QFETCH(QString, checkpoint);
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  const QByteArray original =
      "# Original formatting\nigdb_client_id = \"localclient\"\nunknown_future_key = 17\n";
  QVERIFY(write(p.settings, original));
  QCOMPARE(AppSettings(p.settings).igdbClientId(), "localclient");
  BackupRecovery recovery(p);
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Replace, &error));
  QCOMPARE(child(temp.path(), "settings"), 73);
  QCOMPARE(collections(p.database), QStringList{"Imported"});
  QVERIFY(read(p.settings) != original);
  QCOMPARE(child(temp.path(), checkpoint, true), 73);
  QCOMPARE(recovery.status(), "undoing");
  // A normal startup resumes the already chosen undo direction.
  QVERIFY2(recovery.resume(&error), qPrintable(error));
  QCOMPARE(recovery.status(), "reverted");
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QCOMPARE(read(p.settings), original);
  QVERIFY(recovery.resume(&error));
  QCOMPARE(read(p.settings), original);
}

void BackupRecoveryTests::changedArchiveAndJournal() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  BackupRecovery recovery(p);
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Merge, &error));
  const QString journalPath = p.state + "/active.json";
  const auto originalJournal = read(journalPath);
  auto journal = QJsonDocument::fromJson(originalJournal).object();
  const QString archive =
      p.state + "/" + journal.value("id").toString() + "/incoming.omakade-backup";
  QVERIFY(BackupArchive::write(archive, payload("Changed", true), &error));
  QVERIFY(!recovery.resume(&error));
  QVERIFY(error.contains("changed"));
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QVERIFY(recovery.undo(&error));
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Merge, &error));
  journal = QJsonDocument::fromJson(read(journalPath)).object();
  journal.insert("id", "../escape");
  QVERIFY(write(journalPath, QJsonDocument(journal).toJson()));
  QVERIFY(!recovery.resume(&error));
  QCOMPARE(recovery.status(), "error");
  QCOMPARE(collections(p.database), QStringList{"Original"});
}

void BackupRecoveryTests::missingSettingsAndFreshLibrary() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  BackupRecovery recovery(p);
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Replace, &error));
  QCOMPARE(child(temp.path(), "settings"), 73);
  QVERIFY(QFileInfo::exists(p.settings));
  QVERIFY2(recovery.undo(&error), qPrintable(error));
  QVERIFY(!QFileInfo::exists(p.settings));
  QCOMPARE(collections(p.database), QStringList{});
  QCOMPARE(recovery.status(), "reverted");
}

void BackupRecoveryTests::failedSettingsWriteKeepsRecoveryPending() {
  QTemporaryDir temp;
  auto p = paths(temp.path());
  const QString config = temp.filePath("config");
  QVERIFY(QDir().mkpath(config));
  p.settings = config + "/config.toml";
  const QByteArray original = "igdb_client_id = \"localclient\"\n";
  QVERIFY(write(p.settings, original));
  QCOMPARE(AppSettings(p.settings).igdbClientId(), "localclient");
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  BackupRecovery recovery(p, [&](const QString& checkpoint) {
    if (checkpoint == "database") {
      QVERIFY(QFile::remove(p.settings));
      QVERIFY(QDir().rmdir(config));
      QVERIFY(write(config, "blocked"));
    }
  });
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Replace, &error));
  QVERIFY(!recovery.resume(&error));
  QVERIFY(error.contains("preferences"));
  QCOMPARE(recovery.status(), "prepared");
  QCOMPARE(collections(p.database), QStringList{"Imported"});
  QVERIFY(QFile::remove(config));
  QVERIFY(QDir().mkpath(config));
  BackupRecovery retry(p);
  QVERIFY2(retry.undo(&error), qPrintable(error));
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QCOMPARE(read(p.settings), original);
  const auto permissions = QFileInfo(retry.recoveryArchive()).permissions();
  QVERIFY(!(permissions &
            (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
}

void BackupRecoveryTests::damagedRecoveryBlocksReplay() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  BackupRecovery recovery(p);
  QVERIFY(recovery.stage(payload("Imported", true), BackupDatabase::Mode::Replace, &error));
  QCOMPARE(child(temp.path(), "prepared"), 73);
  const QString archive = recovery.recoveryArchive();
  const QByteArray original = read(archive);
  QVERIFY(BackupArchive::write(archive, payload("Unrelated", false), &error));
  QVERIFY(!recovery.resume(&error));
  QVERIFY(error.contains("changed"));
  QVERIFY(!recovery.undo(&error));
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QVERIFY(write(archive, original));
  QVERIFY(recovery.resume(&error));
  QCOMPARE(collections(p.database), QStringList{"Imported"});
}

void BackupRecoveryTests::asynchronousPreviewStagesExactPayload() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  AppSettings settings(p.settings);
  settings.setIgdbClientId("localclient");
  const QByteArray original = read(p.settings);
  const QString archive = temp.filePath("incoming.omakade-backup");
  QVERIFY(BackupArchive::write(archive, payload("Previewed", true), &error));
  BackupManager manager(p, &settings, true);
  QSignalSpy queued(&manager, &BackupManager::restoreQueued);
  manager.previewBackup(QUrl::fromLocalFile(archive).toString());
  QVERIFY(manager.busy());
  manager.confirmRestore(true); // No preview while the asynchronous read is pending.
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  QVERIFY2(manager.hasPreview(), qPrintable(manager.message()));
  QCOMPARE(manager.preview().value("path").toString(), archive);
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QCOMPARE(read(p.settings), original);
  QCOMPARE(queued.count(), 0);
  // Replacing the external file after preview does not change confirmation.
  QVERIFY(BackupArchive::write(archive, payload("Changed later", false), &error));
  manager.confirmRestore(true);
  QVERIFY(manager.busy());
  QTRY_COMPARE_WITH_TIMEOUT(queued.count(), 1, 5000);
  QVERIFY(!manager.hasPreview());
  QCOMPARE(collections(p.database), QStringList{"Original"});
  QCOMPARE(read(p.settings), original);
  BackupRecovery recovery(p);
  QCOMPARE(recovery.status(), "queued");
  QVERIFY2(recovery.resume(&error), qPrintable(error));
  QCOMPARE(collections(p.database), QStringList{"Previewed"});
  QVERIFY(AppSettings(p.settings).couchModeEnabled());
}

void BackupRecoveryTests::exportAndInvalidPreview() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  QString error;
  QVERIFY(BackupDatabase::restore(p.database, payload("Original", false),
                                  BackupDatabase::Mode::Replace, &error));
  AppSettings settings(p.settings);
  settings.setIgdbClientId("localclient");
  BackupManager manager(p, &settings, true);
  const QString archive = temp.filePath("export.omakade-backup");
  manager.exportBackup(archive);
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  BackupPayload restored;
  QVERIFY2(BackupArchive::read(archive, &restored, &error), qPrintable(manager.message()));
  QCOMPARE(
      restored.library.value("collections").toArray().first().toObject().value("name").toString(),
      "Original");
  QVERIFY(!QJsonDocument(restored.settings).toJson().contains("localclient"));
  manager.previewBackup(archive);
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  QVERIFY(manager.hasPreview());
  manager.previewBackup("https://example.invalid/backup");
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  QVERIFY(!manager.hasPreview());
  QCOMPARE(manager.message(), "Choose a local backup file.");
  QSignalSpy queued(&manager, &BackupManager::restoreQueued);
  manager.confirmRestore(true);
  QVERIFY(!manager.busy());
  QCOMPARE(queued.count(), 0);
  const auto databaseBytes = read(p.database);
  manager.exportBackup(p.database);
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  QCOMPARE(read(p.database), databaseBytes);
  QVERIFY(QDir().mkpath(p.state));
  manager.exportBackup(p.state + "/bad.omakade-backup");
  QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
  QVERIFY(!QFileInfo::exists(p.state + "/bad.omakade-backup"));
  BackupManager unavailable(p, &settings, false);
  unavailable.exportBackup(temp.filePath("disabled.omakade-backup"));
  QVERIFY(!unavailable.busy());
  QVERIFY(!QFileInfo::exists(temp.filePath("disabled.omakade-backup")));
}

void BackupRecoveryTests::previewCountsAndMissingPaths() {
  QTemporaryDir temp;
  auto incoming = payload("Shared", true);
  auto current = payload("Shared", false);
  const QJsonObject entry{{"id", "missing"},
                          {"title", "Missing game"},
                          {"executable", temp.filePath("offline/game")},
                          {"directory", temp.filePath("offline")},
                          {"arguments", QJsonArray{}}};
  incoming.library.insert(
      "manual_games",
      QJsonArray{QJsonObject{
          {"id", "missing"},
          {"entry", QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact))},
          {"favorite", false},
          {"hidden", false},
          {"active", true}}});
  incoming.settings.insert("gog_library_paths", QJsonArray{temp.filePath("GOG")});
  const QJsonObject state{{"version", 1}, {"search", ""},      {"mode", 0},
                          {"sort", 0},    {"availability", 0}, {"showHidden", false},
                          {"source", ""}, {"status", ""},      {"collection", ""},
                          {"tag", ""}};
  QJsonObject filter{
      {"id", "imported-filter"},
      {"name", "Weekend"},
      {"name_key", "weekend"},
      {"state_json", QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact))}};
  incoming.library.insert("saved_filters", QJsonArray{filter});
  filter.insert("id", "local-filter");
  current.library.insert("saved_filters", QJsonArray{filter});
  QString error;
  QVERIFY2(BackupArchive::validate(incoming, &error), qPrintable(error));
  const auto description = BackupManager::describe(incoming, current);
  QCOMPARE(description.value("missingPathCount").toInt(), 3);
  QCOMPARE(description.value("missingPaths").toStringList().size(), 3);
  QCOMPARE(description.value("settingsCount").toInt(), 3);
  QCOMPARE(description.value("settings").toList().size(), 3);
  QCOMPARE(description.value("savedFilterNameConflicts").toStringList(), QStringList{"Weekend"});
  // Replacing the same saved-filter ID does not need a renamed copy.
  filter.insert("id", "imported-filter");
  current.library.insert("saved_filters", QJsonArray{filter});
  QVERIFY(BackupManager::describe(incoming, current)
              .value("savedFilterNameConflicts")
              .toStringList()
              .isEmpty());
  bool foundCollections = false;
  for (const auto& value : description.value("counts").toList()) {
    const auto count = value.toMap();
    if (count.value("label") == "Collections") {
      foundCollections = true;
      QCOMPARE(count.value("incoming").toInt(), 1);
      QCOMPARE(count.value("current").toInt(), 1);
      QCOMPARE(count.value("matching").toInt(), 1);
    }
  }
  QVERIFY(foundCollections);
}

void BackupRecoveryTests::releasedDatabaseMigration() {
  QTemporaryDir temp;
  const auto p = paths(temp.path());
  const QByteArray schema =
      read(QStringLiteral(OMAKADE_FIXTURE_DIR) + "/released-v1.6.0/schema.sql");
  QCOMPARE(QCryptographicHash::hash(schema, QCryptographicHash::Sha256).toHex(),
           QByteArray("dceefb05d10259a3cc9a8c0df88be9635fecae5aabced92ccf3d9a861a9d30c0"));
  const QByteArray seed =
      read(QStringLiteral(OMAKADE_FIXTURE_DIR) + "/released-v1.6.0/personal-data.sql");
  QVERIFY(!seed.isEmpty());
  QImage cover(32, 48, QImage::Format_ARGB32);
  cover.fill(QColor(80, 140, 180));
  const QString coverPath = temp.filePath("original-cover.png");
  QVERIFY(cover.save(coverPath));
  const QString connection = QUuid::createUuid().toString();
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(p.database);
    QVERIFY(db.open());
    QSqlQuery query(db);
    for (const auto& script : {schema, seed}) {
      for (const auto& statement : QString::fromUtf8(script).split(';', Qt::SkipEmptyParts)) {
        if (!statement.trimmed().isEmpty())
          QVERIFY2(query.exec(statement), qPrintable(query.lastError().text()));
      }
    }
    query.prepare("INSERT INTO artwork_overrides(source,runner,app_id,cover_path) VALUES "
                  "('Steam','','42',?)");
    query.addBindValue(coverPath);
    QVERIFY(query.exec());
    QVERIFY(query.exec("PRAGMA user_version"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    QVERIFY(query.exec("PRAGMA table_info(artwork_overrides)"));
    int columns = 0;
    while (query.next())
      ++columns;
    QCOMPARE(columns, 4);
    db.close();
  }
  QSqlDatabase::removeDatabase(connection);
  const QByteArray beforeBytes = read(p.database);
  AppSettings settings(p.settings);
  settings.setIgdbClientId("migrationclient");
  QString error;
  BackupPayload before;
  QVERIFY2(BackupSnapshot::capture(p.database, settings.backupSettings(), &before, &error),
           qPrintable(error));
  QCOMPARE(read(p.database), beforeBytes);
  const auto serialized = QJsonDocument(before.library).toJson();
  QVERIFY(!serialized.contains("private scanner error"));
  QVERIFY(!serialized.contains("76561190000000000"));
  QVERIFY(!serialized.contains("Disposable metadata"));
  QCOMPARE(before.library.value("user_game_flags").toArray().size(), 10);
  QCOMPARE(before.library.value("game_link_members").toArray().size(), 2);
  QCOMPARE(before.library.value("collection_games").toArray().size(), 2);
  QCOMPARE(before.library.value("game_organization").toArray().size(), 2);
  QCOMPARE(before.library.value("launch_activity")
               .toArray()
               .first()
               .toObject()
               .value("launch_count")
               .toInt(),
           9);
  QCOMPARE(before.artwork.size(), 1);
  const auto artwork = before.library.value("artwork_overrides").toArray().first().toObject();
  QCOMPARE(artwork.value("hero_path").toString(), QString{});
  QCOMPARE(artwork.value("logo_path").toString(), QString{});
  QCOMPARE(before.artwork.value(artwork.value("cover_path").toString()), read(coverPath));
  const auto hasFlag = [&](const QString& source, const QString& runner, const QString& id,
                           bool favorite, bool hidden) {
    for (const auto& value : before.library.value("user_game_flags").toArray()) {
      const auto row = value.toObject();
      if (row.value("source") == source && row.value("runner") == runner &&
          row.value("app_id") == id)
        return row.value("favorite").toBool() == favorite && row.value("hidden").toBool() == hidden;
    }
    return false;
  };
  QVERIFY(hasFlag("Steam", "", "43", false, true));
  QVERIFY(hasFlag("GOG", "gog-direct", "gog-release", true, false));
  QVERIFY(hasFlag("Heroic", "legendary", "epic-release", false, true));
  QVERIFY(hasFlag("Faugus", "", "faugus-release", true, true));
  QVERIFY(hasFlag("Lutris", "", "7", true, false));
  QVERIFY(hasFlag("PCSX2", "TEST-0001", "path:/offline/game.iso", true, false));
  QVERIFY(hasFlag("Ryujinx", "org.ryujinx.Ryujinx", "switch-release", true, false));
  const QString archive = temp.filePath("released.omakade-backup");
  QVERIFY2(BackupArchive::write(archive, before, &error), qPrintable(error));
  BackupPayload roundtrip;
  QVERIFY(BackupArchive::read(archive, &roundtrip, &error));
  QCOMPARE(roundtrip.library, before.library);
  {
    // Run every candidate source constructor against the generated release DB.
    // No event loop or scanner refresh is run while these objects are alive.
    SteamGameModel steam(p.database);
    LutrisGameModel lutris(p.database);
    HeroicGameModel heroic(p.database);
    FaugusGameModel faugus(p.database);
    RetroArchGameModel retroarch(p.database);
    Pcsx2GameModel pcsx2(p.database);
    RyujinxGameModel ryujinx(p.database);
    BattleNetGameModel battlenet(p.database);
    ManualGameModel manual(p.database);
    UnifiedGameModel unified(p.database);
    for (QAbstractItemModel* source : QList<QAbstractItemModel*>{
             &steam, &lutris, &heroic, &faugus, &retroarch, &pcsx2, &ryujinx, &battlenet, &manual})
      unified.addSourceModel(source);
    bool found = false;
    for (int row = 0; row < unified.rowCount(); ++row) {
      const auto index = unified.index(row);
      if (index.data(GameRoles::Source).toString() == "Steam" &&
          index.data(GameRoles::AppId).toString() == "42") {
        found = true;
        QVERIFY(index.data(GameRoles::Favorite).toBool());
        QCOMPARE(unified.installations(row).size(), 2);
        QCOMPARE(index.data(GameRoles::CompletionStatus).toString(), "playing");
        QVERIFY(index.data(GameRoles::CustomCover).toBool());
        QVERIFY(!index.data(GameRoles::CustomHero).toBool());
      }
    }
    QVERIFY(found);
    BackupPayload migrated;
    QVERIFY2(BackupSnapshot::capture(p.database, settings.backupSettings(), &migrated, &error),
             qPrintable(error));
    QCOMPARE(migrated.library, before.library);
    QCOMPARE(migrated.artwork, before.artwork);
  }
  // Restore the portable pre-migration backup through the real coordinator.
  BackupRecovery recovery(p);
  QVERIFY(recovery.stage(roundtrip, BackupDatabase::Mode::Replace, &error));
  QVERIFY2(recovery.resume(&error), qPrintable(error));
  BackupPayload restored;
  QVERIFY(BackupSnapshot::capture(p.database, settings.backupSettings(), &restored, &error));
  QCOMPARE(restored.library, before.library);
  QCOMPARE(restored.artwork, before.artwork);
  QVERIFY(QFileInfo(coverPath).exists());
  QCOMPARE(AppSettings(p.settings).igdbClientId(), "migrationclient");
  QVERIFY(QFileInfo(recovery.recoveryArchive()).isFile());
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(p.database);
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec("SELECT title,favorite,hidden FROM games WHERE app_id='42'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), "Released Steam game");
    QCOMPARE(query.value(1).toInt(), 0); // Overrides now carry the restored choice.
    QCOMPARE(query.value(2).toInt(), 0);
    QVERIFY(query.exec("SELECT steam_id FROM owned_games WHERE app_id='42'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), "76561190000000000");
    QVERIFY(query.exec("SELECT title FROM game_insights WHERE app_id='42'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), "Disposable metadata");
    QVERIFY(query.exec("PRAGMA user_version"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    db.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() == 5 && args.at(1) == "--worker") {
    BackupRecovery recovery(paths(args.at(2)), [&](const QString& step) {
      if (step == args.at(3))
        std::_Exit(73);
    });
    QString error;
    const bool okay = args.at(4) == "undo" ? recovery.undo(&error) : recovery.resume(&error);
    if (!okay)
      qWarning().noquote() << error;
    return okay ? 0 : 2;
  }
  BackupRecoveryTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "BackupRecoveryTests.moc"
