#include "backup/BackupManager.h"
#include "app/AppSettings.h"
#include "backup/BackupSnapshot.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <QtConcurrentRun>

namespace {
QString recordKey(const QString& table, const QJsonObject& row) {
  QJsonArray parts;
  if (table == "collections")
    parts.append(row.value("name").toString().toCaseFolded());
  else if (table == "manual_games" || table == "saved_filters")
    parts.append(row.value("id"));
  else if (table == "launch_preferences")
    parts.append(row.value("group_id"));
  else {
    if (table == "collection_games")
      parts.append(row.value("collection_name").toString().toCaseFolded());
    for (const auto& field : {"source", "runner", "app_id"})
      parts.append(row.value(field));
  }
  return QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
}
bool within(const QString& file, const QString& folder) {
  return file == folder || file.startsWith(folder + '/');
}
} // namespace

BackupManager::BackupManager(BackupRecovery::Paths paths, AppSettings* settings, bool available,
                             QObject* parent)
    : QObject(parent), m_paths(std::move(paths)), m_settings(settings),
      m_available(available && settings) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
    const auto result = m_watcher.result();
    m_busy = false;
    m_message = result.message;
    if (result.payload) {
      m_payload = result.payload;
      m_preview = result.preview;
    }
    if (result.queued) {
      m_payload.reset();
      m_preview.clear();
    }
    emit changed();
    if (result.queued)
      emit restoreQueued();
  });
}
BackupManager::~BackupManager() { m_watcher.waitForFinished(); }

bool BackupManager::begin(const QString& message) {
  if (m_busy)
    return false;
  if (!m_available) {
    m_message = "Backup and restore are available in your normal library.";
    emit changed();
    return false;
  }
  m_busy = true;
  m_message = message;
  emit changed();
  return true;
}
void BackupManager::run(std::function<Result()> operation) {
  m_watcher.setFuture(QtConcurrent::run(std::move(operation)));
}
QString BackupManager::localPath(const QString& input) {
  QString path = input;
  if (path.startsWith("file:")) {
    const QUrl url(path);
    if (!url.isLocalFile() || !url.host().isEmpty())
      return {};
    path = url.toLocalFile();
  }
  if (path.startsWith("~/"))
    path = QDir::homePath() + path.mid(1);
  if (!QDir::isAbsolutePath(path) || path.size() > 4096)
    return {};
  for (const auto c : path)
    if (c.category() == QChar::Other_Control)
      return {};
  return QDir::cleanPath(path);
}

void BackupManager::exportBackup(const QString& input) {
  if (!begin("Saving backup…"))
    return;
  const auto settings = m_settings->backupSettings();
  const auto paths = m_paths;
  const QString path = localPath(input);
  run([path, paths, settings] {
    Result result;
    const QString parent = QFileInfo(path).dir().canonicalPath();
    const QString resolved = parent + '/' + QFileInfo(path).fileName();
    const QString artwork = QFileInfo(paths.database).absolutePath() + "/artwork";
    if (path.isEmpty() || parent.isEmpty() || !path.endsWith(".omakade-backup") ||
        path == paths.database || path == paths.settings || within(resolved, paths.state) ||
        within(resolved, artwork) || QFileInfo(path).isSymLink()) {
      result.message =
          "Choose a local .omakade-backup file outside Omakade's artwork and recovery folders.";
      return result;
    }
    BackupPayload payload;
    QString error;
    if (!BackupSnapshot::capture(paths.database, settings, &payload, &error) ||
        !BackupArchive::write(path, payload, &error))
      result.message = error;
    else
      result.message = "Backup saved: " + path;
    return result;
  });
}

void BackupManager::previewBackup(const QString& input) {
  if (!begin("Reading backup…"))
    return;
  m_payload.reset();
  m_preview.clear();
  emit changed();
  const QString path = localPath(input);
  const auto paths = m_paths;
  const auto settings = m_settings->backupSettings();
  run([path, paths, settings] {
    Result result;
    if (path.isEmpty()) {
      result.message = "Choose a local backup file.";
      return result;
    }
    auto incoming = std::make_shared<BackupPayload>();
    BackupPayload current;
    QString error;
    if (!BackupArchive::read(path, incoming.get(), &error) ||
        !BackupSnapshot::capture(paths.database, settings, &current, &error)) {
      result.message = error;
      return result;
    }
    result.preview = describe(*incoming, current);
    result.preview.insert("path", path);
    result.payload = std::move(incoming);
    result.message = "Review the contents and choose how to restore them.";
    return result;
  });
}

void BackupManager::discardPreview() {
  if (m_busy)
    return;
  m_payload.reset();
  m_preview.clear();
  m_message.clear();
  emit changed();
}

void BackupManager::confirmRestore(bool replace) {
  if (!m_payload || !begin("Preparing restore…"))
    return;
  const auto incoming = m_payload;
  const auto paths = m_paths;
  run([incoming, paths, replace] {
    Result result;
    QString error;
    BackupRecovery recovery(paths);
    result.queued = recovery.stage(
        *incoming, replace ? BackupDatabase::Mode::Replace : BackupDatabase::Mode::Merge, &error);
    result.message = result.queued
                         ? "Restore is ready. Close Omakade and reopen it to apply the backup."
                         : error;
    return result;
  });
}

QVariantMap BackupManager::describe(const BackupPayload& incoming, const BackupPayload& current) {
  const QMap<QString, QString> names{{"user_game_flags", "Favorites and hidden choices"},
                                     {"game_organization", "Completion states and tags"},
                                     {"collections", "Collections"},
                                     {"collection_games", "Collection memberships"},
                                     {"game_link_members", "Linked installations"},
                                     {"launch_preferences", "Preferred installations"},
                                     {"launch_activity", "Launch activity"},
                                     {"manual_games", "Manual games"},
                                     {"saved_filters", "Saved filters"},
                                     {"artwork_overrides", "Custom artwork choices"}};
  QVariantList counts;
  for (auto it = names.begin(); it != names.end(); ++it) {
    const auto imported = incoming.library.value(it.key()).toArray();
    const auto existing = current.library.value(it.key()).toArray();
    QSet<QString> keys;
    for (const auto& row : existing)
      keys.insert(recordKey(it.key(), row.toObject()));
    int matching = 0;
    for (const auto& row : imported)
      if (keys.contains(recordKey(it.key(), row.toObject())))
        ++matching;
    counts.append(QVariantMap{{"label", it.value()},
                              {"incoming", imported.size()},
                              {"current", existing.size()},
                              {"matching", matching}});
  }
  QStringList missingPaths;
  int missingCount = 0;
  const auto noteMissing = [&](const QString& path) {
    ++missingCount;
    if (missingPaths.size() < 100)
      missingPaths.append(path);
  };
  for (const auto& value : incoming.library.value("manual_games").toArray()) {
    const auto entry =
        QJsonDocument::fromJson(value.toObject().value("entry").toString().toUtf8()).object();
    const QString executable = entry.value("executable").toString();
    if (!QFileInfo(executable).isFile())
      noteMissing(executable);
    const QString directory = entry.value("directory").toString();
    if (!directory.isEmpty() && !QFileInfo(directory).isDir())
      noteMissing(directory);
  }
  for (const auto& value : incoming.settings.value("gog_library_paths").toArray()) {
    if (!QFileInfo(value.toString()).isDir())
      noteMissing(value.toString());
  }
  QVariantList settingChanges;
  for (auto setting = incoming.settings.begin(); setting != incoming.settings.end(); ++setting) {
    settingChanges.append(
        QVariantMap{{"name", setting.key()},
                    {"incoming", setting.value().toVariant()},
                    {"current", current.settings.value(setting.key()).toVariant()}});
  }
  QSet<QString> existingFilterNames;
  QSet<QString> incomingFilterIds;
  for (const auto& value : incoming.library.value("saved_filters").toArray())
    incomingFilterIds.insert(value.toObject().value("id").toString());
  for (const auto& value : current.library.value("saved_filters").toArray()) {
    const auto row = value.toObject();
    if (!incomingFilterIds.contains(row.value("id").toString()))
      existingFilterNames.insert(row.value("name_key").toString());
  }
  QStringList renamedFilters;
  for (const auto& value : incoming.library.value("saved_filters").toArray()) {
    const auto row = value.toObject();
    if (existingFilterNames.contains(row.value("name_key").toString()))
      renamedFilters.append(row.value("name").toString());
  }
  return {
      {"createdAt", incoming.createdAt},
      {"counts", counts},
      {"settingsCount", incoming.settings.size()},
      {"artworkCount", incoming.artwork.size()},
      {"missingPathCount", missingCount},
      {"missingPaths", missingPaths},
      {"settings", settingChanges},
      {"savedFilterNameConflicts", renamedFilters},
      {"mergeExplanation",
       "Merge keeps unrelated personal data. Imported values take precedence for matching games. "
       "Collections gain memberships; imported link groups take precedence for their members. "
       "Saved filters with a conflicting name receive a restored suffix."},
      {"replaceExplanation",
       "Replace clears current personal library choices, manual entries, and saved filters before "
       "importing the backup. Cached game records and game files stay in place."},
      {"recoveryExplanation",
       "Omakade saves a recovery copy before applying changes on the next startup. Account-service "
       "identifiers and Sunshine publishing choices remain local. Missing games stay stored for "
       "rediscovery, and missing manual paths can be repaired. Restoring does not launch games."}};
}
