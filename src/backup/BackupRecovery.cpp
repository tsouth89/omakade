#include "backup/BackupRecovery.h"
#include "app/AppSettings.h"
#include "backup/BackupSnapshot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
bool fail(QString* error, const QString& message) {
  if (error)
    *error = message;
  return false;
}
bool regular(const QString& path) {
  const QFileInfo info(path);
  return !info.isSymLink() && (!info.exists() || info.isFile());
}
bool saveBytes(const QString& path, const QByteArray& bytes, QString* error) {
  if (!regular(path))
    return fail(error, "A recovery file is not a regular file.");
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit())
    return fail(error, "Could not save restore recovery state.");
  return true;
}
bool readBytes(const QString& path, qint64 limit, QByteArray* bytes, QString* error) {
  QFile file(path);
  if (!regular(path) || !file.open(QIODevice::ReadOnly) || file.size() > limit)
    return fail(error, "Could not read restore recovery state.");
  *bytes = file.read(limit + 1);
  if (bytes->size() > limit || file.error() != QFile::NoError)
    return fail(error, "Restore recovery state is unreadable or too large.");
  return true;
}
QString digest(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString fileDigest(const QString& path) {
  QFile file(path);
  if (!regular(path) || !file.open(QIODevice::ReadOnly) || file.size() > 600LL * 1024 * 1024)
    return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return {};
  return QString::fromLatin1(hash.result().toHex());
}
} // namespace

BackupRecovery::BackupRecovery(Paths paths, Checkpoint checkpoint)
    : m_paths(std::move(paths)), m_checkpoint(std::move(checkpoint)) {}

bool BackupRecovery::prepareDirectory(QString* error) const {
  if (error)
    error->clear();
  for (const auto& path : {m_paths.database, m_paths.settings, m_paths.state}) {
    if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path || QFileInfo(path).isSymLink())
      return fail(error, "Restore requires absolute, regular application paths.");
  }
  if (!regular(m_paths.database) || !regular(m_paths.settings))
    return fail(error, "The application database or settings path is not a regular file.");
  if (!QDir().mkpath(m_paths.state) ||
      !QFile::setPermissions(m_paths.state, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    return fail(error, "Could not create the private restore recovery folder.");
  return true;
}

bool BackupRecovery::readJournal(QJsonObject* journal, QString* error) const {
  *journal = {};
  const QString path = m_paths.state + "/active.json";
  if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink())
    return true;
  QByteArray bytes;
  if (!readBytes(path, 16384, &bytes, error))
    return false;
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(bytes, &parse);
  const auto object = document.object();
  const QString phase = object.value("phase").toString();
  const QString id = object.value("id").toString();
  const QStringList phases{"queued", "prepared", "undoing", "complete", "reverted"};
  if (parse.error != QJsonParseError::NoError || !document.isObject() ||
      object.value("version").toInt() != 1 || !phases.contains(phase) ||
      !QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
           .match(id)
           .hasMatch() ||
      object.value("database").toString() != m_paths.database ||
      object.value("settings").toString() != m_paths.settings ||
      !QRegularExpression("^[0-9a-f]{64}$")
           .match(object.value("incomingDigest").toString())
           .hasMatch() ||
      (object.value("mode").toString() != "merge" && object.value("mode").toString() != "replace"))
    return fail(error, "The restore journal is invalid. Keep the recovery folder for repair.");
  const QFileInfo job(m_paths.state + "/" + id);
  if (!job.isDir() || job.isSymLink())
    return fail(error, "The restore recovery folder is missing or invalid.");
  if (phase != "queued" && phase != "reverted" &&
      (!object.value("settingsExisted").isBool() ||
       !QRegularExpression("^[0-9a-f]{64}$")
            .match(object.value("settingsDigest").toString())
            .hasMatch() ||
       !QRegularExpression("^[0-9a-f]{64}$")
            .match(object.value("beforeDigest").toString())
            .hasMatch()))
    return fail(error, "The original settings recovery record is invalid.");
  *journal = object;
  return true;
}

bool BackupRecovery::writeJournal(const QJsonObject& journal, QString* error) const {
  return saveBytes(m_paths.state + "/active.json", QJsonDocument(journal).toJson(), error);
}

QString BackupRecovery::jobPath(const QJsonObject& journal, const QString& name) const {
  return m_paths.state + "/" + journal.value("id").toString() + "/" + name;
}

QString BackupRecovery::status(QString* error) const {
  if (error)
    error->clear();
  QJsonObject journal;
  if (!readJournal(&journal, error))
    return "error";
  return journal.isEmpty() ? "none" : journal.value("phase").toString();
}

QString BackupRecovery::recoveryArchive(QString* error) const {
  QJsonObject journal;
  if (!readJournal(&journal, error) || !journal.contains("settingsDigest"))
    return {};
  return jobPath(journal, "before.omakade-backup");
}

bool BackupRecovery::stage(const BackupPayload& payload, BackupDatabase::Mode mode,
                           QString* error) {
  if (!prepareDirectory(error) || !BackupArchive::validate(payload, error))
    return false;
  if (mode != BackupDatabase::Mode::Merge && mode != BackupDatabase::Mode::Replace)
    return fail(error, "Unknown restore mode.");
  QLockFile lock(m_paths.state + "/restore.lock");
  if (!lock.tryLock(0))
    return fail(error, "Another restore operation is running.");
  QJsonObject previous;
  if (!readJournal(&previous, error))
    return false;
  const QString phase = previous.value("phase").toString();
  if (!previous.isEmpty() && phase != "complete" && phase != "reverted")
    return fail(error, "Finish or undo the pending restore before starting another.");
  QJsonObject journal{{"version", 1},
                      {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                      {"phase", "queued"},
                      {"mode", mode == BackupDatabase::Mode::Merge ? "merge" : "replace"},
                      {"database", m_paths.database},
                      {"settings", m_paths.settings}};
  const QString folder = jobPath(journal, {});
  if (!QDir().mkpath(folder) ||
      !QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    return fail(error, "Could not create the restore job folder.");
  if (!BackupArchive::write(jobPath(journal, "incoming.omakade-backup"), payload, error))
    return false;
  const QString incomingDigest = fileDigest(jobPath(journal, "incoming.omakade-backup"));
  if (incomingDigest.isEmpty())
    return fail(error, "Could not verify the staged restore archive.");
  journal.insert("incomingDigest", incomingDigest);
  return writeJournal(journal, error);
}

bool BackupRecovery::resume(QString* error) { return restore(false, error); }
bool BackupRecovery::undo(QString* error) { return restore(true, error); }

bool BackupRecovery::restore(bool undoRequested, QString* error) {
  if (!prepareDirectory(error))
    return false;
  QLockFile lock(m_paths.state + "/restore.lock");
  if (!lock.tryLock(0))
    return fail(error, "Another restore operation is running.");
  QJsonObject journal;
  if (!readJournal(&journal, error))
    return false;
  QString phase = journal.value("phase").toString();
  if (journal.isEmpty() || phase == "reverted" || (phase == "complete" && !undoRequested))
    return true;
  if (phase == "complete")
    return fail(error,
                "This restore has completed. Preview its recovery archive to restore it again.");
  const auto checkpoint = [&](const QString& name) {
    if (m_checkpoint)
      m_checkpoint(name);
  };
  if (phase == "queued" && undoRequested) {
    journal.insert("phase", "reverted");
    return writeJournal(journal, error);
  }
  BackupPayload incoming;
  if (!undoRequested && phase != "undoing" &&
      fileDigest(jobPath(journal, "incoming.omakade-backup")) !=
          journal.value("incomingDigest").toString())
    return fail(
        error, "The staged restore archive has changed. Cancel this restore and preview it again.");
  if (!undoRequested && phase != "undoing" &&
      !BackupArchive::read(jobPath(journal, "incoming.omakade-backup"), &incoming, error))
    return false;
  if (phase == "queued") {
    QByteArray settingsBytes;
    const bool existed = QFileInfo::exists(m_paths.settings);
    if (existed && !readBytes(m_paths.settings, 1024 * 1024, &settingsBytes, error))
      return false;
    // Preserve malformed settings verbatim too; the recovery copy is machine-local
    // and private, never included in the portable archive.
    AppSettings settings(m_paths.settings);
    BackupPayload before;
    before.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    before.settings = settings.backupSettings();
    if (QFileInfo::exists(m_paths.database) &&
        !BackupSnapshot::capture(m_paths.database, before.settings, &before, error))
      return false;
    if (!BackupArchive::write(jobPath(journal, "before.omakade-backup"), before, error) ||
        !saveBytes(jobPath(journal, "settings.before"), settingsBytes, error))
      return false;
    journal.insert("settingsExisted", existed);
    journal.insert("settingsDigest", digest(settingsBytes));
    const QString beforeDigest = fileDigest(jobPath(journal, "before.omakade-backup"));
    if (beforeDigest.isEmpty())
      return fail(error, "Could not verify the recovery archive.");
    journal.insert("beforeDigest", beforeDigest);
    journal.insert("phase", "prepared");
    if (!writeJournal(journal, error))
      return false;
    checkpoint("prepared");
  }
  // Validate both recovery files before the first write, and on every replay.
  BackupPayload before;
  QByteArray settingsBytes;
  if (fileDigest(jobPath(journal, "before.omakade-backup")) !=
      journal.value("beforeDigest").toString())
    return fail(error, "The pre-restore archive has changed. Keep the recovery folder for repair.");
  if (!BackupArchive::read(jobPath(journal, "before.omakade-backup"), &before, error) ||
      !readBytes(jobPath(journal, "settings.before"), 1024 * 1024, &settingsBytes, error))
    return false;
  if (digest(settingsBytes) != journal.value("settingsDigest").toString())
    return fail(error, "The original settings recovery copy has changed.");
  if (undoRequested || phase == "undoing") {
    journal.insert("phase", "undoing");
    if (!writeJournal(journal, error))
      return false;
    if (!BackupDatabase::restore(m_paths.database, before, BackupDatabase::Mode::Replace, error))
      return false;
    checkpoint("undo-database");
    if (journal.value("settingsExisted").toBool()) {
      if (!saveBytes(m_paths.settings, settingsBytes, error))
        return false;
    } else if (QFileInfo::exists(m_paths.settings) && !QFile::remove(m_paths.settings)) {
      return fail(error, "Could not restore the original absence of a settings file.");
    }
    checkpoint("undo-settings");
    journal.insert("phase", "reverted");
  } else {
    const auto mode = journal.value("mode").toString() == "merge" ? BackupDatabase::Mode::Merge
                                                                  : BackupDatabase::Mode::Replace;
    if (!BackupDatabase::restore(m_paths.database, incoming, mode, error))
      return false;
    checkpoint("database");
    AppSettings settings(m_paths.settings);
    if (!settings.applyBackupSettings(incoming.settings, mode == BackupDatabase::Mode::Replace))
      return fail(error,
                  "Could not restore preferences. Retry or undo before opening the library.");
    checkpoint("settings");
    journal.insert("phase", "complete");
  }
  if (!writeJournal(journal, error))
    return false;
  checkpoint(journal.value("phase").toString());
  return true;
}
