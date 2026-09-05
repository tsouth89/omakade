#pragma once

#include "sources/heroic/HeroicScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

class HeroicGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool heroicDetected READ heroicDetected NOTIFY statusChanged)
  Q_PROPERTY(bool gogDetected READ gogDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)

public:
  explicit HeroicGameModel(const QString& omakadeDatabasePath, QObject* parent = nullptr);
  ~HeroicGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool heroicDetected() const;
  [[nodiscard]] bool gogDetected() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  [[nodiscard]] bool scanning() const { return m_scanning; }
  void refreshFromRoots(const QStringList& roots);
  void setGogLibraryPaths(const QStringList& paths);

signals:
  void statusChanged();

private:
  struct Game {
    HeroicGameRecord heroic;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const HeroicScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<HeroicScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_refreshPending = false;
  QStringList m_gogLibraryPaths;
  QStringList m_removedGogRoots;
  bool m_heroicDetected = false;
  bool m_gogDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
};
