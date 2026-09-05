#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

struct BackupPayload {
  QJsonObject library;
  QJsonObject settings;
  QHash<QString, QByteArray> artwork;
  QString createdAt;
};

// A local archive contains only allowlisted personal records and verified artwork.
// Reading validates into memory; it never extracts paths or modifies the library.
class BackupArchive {
public:
  static constexpr qint64 MaxImageBytes = 32 * 1024 * 1024;
  static constexpr qint64 MaxManifestBytes = 16 * 1024 * 1024;
  static constexpr qint64 MaxTotalBytes = 512 * 1024 * 1024;
  static constexpr int MaxArtworkFiles = 4096;
  static QMap<QString, QStringList> tableColumns();
  static QStringList settingNames();
  static QString artworkName(const QByteArray& bytes, QString* error = nullptr);
  static bool validate(const BackupPayload& payload, QString* error = nullptr);
  static bool write(const QString& path, const BackupPayload& payload, QString* error = nullptr);
  static bool read(const QString& path, BackupPayload* payload, QString* error = nullptr);
};
