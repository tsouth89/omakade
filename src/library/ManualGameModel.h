#pragma once

#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QUrl>
#include <QVariantMap>

// User-owned native launch entries. No source launcher data is changed.
class ManualGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
  Q_PROPERTY(int count READ count NOTIFY stateChanged)
public:
  explicit ManualGameModel(const QString& databasePath, QObject* parent = nullptr);
  ~ManualGameModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_entries.size(); }
  QString lastError() const { return m_error; }
  Q_INVOKABLE QVariantMap draftFromFile(const QUrl& file);
  Q_INVOKABLE QVariantMap get(const QString& id) const;
  Q_INVOKABLE QString saveEntry(const QVariantMap& draft);
  Q_INVOKABLE bool removeEntry(const QString& id);
  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  static bool validateLaunch(const QString& target, const QString& id, QString* executable,
                             QStringList* arguments, QString* directory, QString* error);
signals:
  void stateChanged();

private:
  void reload();
  void setError(const QString& error);
  void toggle(int row, bool hidden);
  QSqlDatabase m_database;
  QString m_connection;
  QString m_error;
  QVector<QVariantMap> m_entries;
};
