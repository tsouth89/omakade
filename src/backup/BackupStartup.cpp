#include "backup/BackupStartup.h"
#include <QtConcurrentRun>

BackupStartup::BackupStartup(BackupRecovery::Paths paths, QObject* parent)
    : QObject(parent), m_paths(std::move(paths)) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
    const auto result = m_watcher.result();
    m_busy = false;
    m_canUndo = result.canUndo;
    m_message = result.message;
    emit changed();
    if (result.okay)
      emit resolved();
  });
}
BackupStartup::~BackupStartup() { m_watcher.waitForFinished(); }
void BackupStartup::retry() { run(false); }
void BackupStartup::undo() {
  if (m_canUndo)
    run(true);
}
void BackupStartup::run(bool undo) {
  if (m_busy)
    return;
  m_busy = true;
  m_canUndo = false;
  m_message = undo ? "Restoring your previous library…" : "Applying your backup…";
  emit changed();
  const auto paths = m_paths;
  m_watcher.setFuture(QtConcurrent::run([paths, undo] {
    BackupRecovery recovery(paths);
    Result result;
    result.okay = undo ? recovery.undo(&result.message) : recovery.resume(&result.message);
    if (result.okay)
      result.message = "Your library is ready.";
    else {
      const auto phase = recovery.status();
      result.canUndo = phase == "queued" || phase == "prepared" || phase == "undoing";
    }
    return result;
  }));
}
