#pragma once
#include "backup/BackupArchive.h"

class BackupSnapshot {
public:
  // Read one consistent SQLite snapshot. Old source flags are converted to the
  // portable override table without modifying the source database.
  static bool capture(const QString& databasePath, const QJsonObject& settings,
                      BackupPayload* payload, QString* error = nullptr);
};
