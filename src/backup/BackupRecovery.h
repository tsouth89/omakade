#pragma once

#include "backup/BackupDatabase.h"
#include <functional>

// Machine-local restore journal. stage() may run in the UI; resume()/undo()
// require exclusive app ownership before any source models or services start.
// A failed operation keeps the journal pending and the startup gate closed.
class BackupRecovery {
public:
  struct Paths {
    QString database;
    QString settings;
    QString state;
  };
  using Checkpoint = std::function<void(const QString&)>;
  explicit BackupRecovery(Paths paths, Checkpoint checkpoint = {});
  bool stage(const BackupPayload& payload, BackupDatabase::Mode mode, QString* error = nullptr);
  bool resume(QString* error = nullptr);
  bool undo(QString* error = nullptr);
  QString status(QString* error = nullptr) const;
  QString recoveryArchive(QString* error = nullptr) const;

private:
  bool prepareDirectory(QString* error) const;
  bool readJournal(QJsonObject* journal, QString* error) const;
  bool writeJournal(const QJsonObject& journal, QString* error) const;
  bool restore(bool undo, QString* error);
  QString jobPath(const QJsonObject& journal, const QString& name) const;
  Paths m_paths;
  Checkpoint m_checkpoint;
};
