#pragma once
#include "backup/BackupRecovery.h"
#include <QFutureWatcher>
#include <QObject>
#include <QVariantMap>
#include <memory>

class AppSettings;

// UI operations only stage a restore. Database writes happen during the next
// exclusive startup through BackupRecovery, never while source models are live.
class BackupManager final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
public:
  BackupManager(BackupRecovery::Paths paths, AppSettings* settings, bool available,
                QObject* parent = nullptr);
  ~BackupManager() override;
  bool busy() const { return m_busy; }
  bool available() const { return m_available; }
  bool hasPreview() const { return bool(m_payload); }
  QString message() const { return m_message; }
  QVariantMap preview() const { return m_preview; }
  Q_INVOKABLE void exportBackup(const QString& path);
  Q_INVOKABLE void previewBackup(const QString& path);
  Q_INVOKABLE void discardPreview();
  Q_INVOKABLE void confirmRestore(bool replace);
  static QVariantMap describe(const BackupPayload& incoming, const BackupPayload& current);
signals:
  void changed();
  void restoreQueued();

private:
  struct Result {
    QString message;
    QVariantMap preview;
    std::shared_ptr<BackupPayload> payload;
    bool queued = false;
  };
  bool begin(const QString& message);
  void run(std::function<Result()> operation);
  static QString localPath(const QString& input);
  BackupRecovery::Paths m_paths;
  AppSettings* m_settings;
  bool m_available;
  bool m_busy = false;
  QString m_message;
  QVariantMap m_preview;
  std::shared_ptr<BackupPayload> m_payload;
  QFutureWatcher<Result> m_watcher;
};
