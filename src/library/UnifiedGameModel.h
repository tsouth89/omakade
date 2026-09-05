#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QUrl>
#include <QVector>

class UnifiedGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QStringList collectionNames READ collectionNames NOTIFY collectionsChanged)
  Q_PROPERTY(QStringList tagNames READ tagNames NOTIFY collectionsChanged)

public:
  explicit UnifiedGameModel(const QString& databasePath = {}, QObject* parent = nullptr);
  ~UnifiedGameModel() override;

  void addSourceModel(QAbstractItemModel* model);
  void setSourceEnabled(const QString& source, bool enabled);
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE bool setCustomCover(int row, const QUrl& sourceUrl);
  Q_INVOKABLE bool resetCustomCover(int row);
  Q_INVOKABLE bool setCustomArtwork(int row, const QString& kind, const QUrl& sourceUrl);
  Q_INVOKABLE bool resetCustomArtwork(int row, const QString& kind);
  Q_INVOKABLE QVariantList installations(int row) const;
  Q_INVOKABLE QVariantMap preferredInstallation(int row) const;
  Q_INVOKABLE bool setPreferredInstallation(int row, const QString& source, const QString& runner,
                                            const QString& appId);
  Q_INVOKABLE QVariantList linkCandidates(int row, const QString& search) const;
  Q_INVOKABLE bool recordLaunch(int row, const QString& source, const QString& runner,
                                const QString& appId);
  Q_INVOKABLE bool linkGames(int row, const QString& source, const QString& runner,
                             const QString& appId);
  Q_INVOKABLE bool unlinkGames(int row);
  Q_INVOKABLE bool setCompletionStatus(int row, const QString& status);
  Q_INVOKABLE bool setTags(int row, const QString& tags);
  Q_INVOKABLE bool createCollection(const QString& name);
  Q_INVOKABLE bool deleteCollection(const QString& name);
  Q_INVOKABLE bool setCollectionMembership(int row, const QString& name, bool included);
  bool bulkOrganize(const QStringList& identities, const QVariantMap& changes);
  [[nodiscard]] QVariantList savedFilters() const;
  bool saveFilter(const QString& id, const QString& name, const QVariantMap& state);
  bool removeFilter(const QString& id);
  [[nodiscard]] QStringList collectionNames() const;
  [[nodiscard]] QStringList tagNames() const;

signals:
  void collectionsChanged();
  void savedFiltersChanged();

private:
  struct SourceRow {
    QAbstractItemModel* model = nullptr;
    int row = -1;
  };
  [[nodiscard]] SourceRow mapRow(int row) const;
  [[nodiscard]] QString gameKey(const SourceRow& source) const;
  [[nodiscard]] SourceRow sourceForKey(const QString& key) const;
  [[nodiscard]] bool sourceEnabled(const SourceRow& source) const;
  [[nodiscard]] QVector<SourceRow> groupRows(const SourceRow& source) const;
  [[nodiscard]] QVariantMap gameMap(const SourceRow& source) const;
  void rebuildRows();
  bool openArtworkDatabase(const QString& path);
  void loadArtworkOverrides();
  void loadLinks();
  void loadLaunchActivity();
  void loadOrganization();
  void loadUserFlags();
  void loadCollections();

  struct OrganizationState {
    QString status;
    QStringList tags;
  };

  QVector<QAbstractItemModel*> m_models;
  QSet<QString> m_disabledSources;
  QVector<SourceRow> m_rows;
  // Rebuilt with m_rows so linked-game lookups never walk every source model per role.
  QHash<QString, SourceRow> m_rowForKey;
  QHash<QString, QVector<SourceRow>> m_rowsForGroup;
  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_databasePath;
  QString m_artworkRoot;
  QHash<QString, QString> m_coverOverrides;
  QHash<QString, QString> m_heroOverrides;
  QHash<QString, QString> m_logoOverrides;
  QHash<QString, QString>* artworkOverrides(const QString& kind);
  void removeUnusedArtwork(const QString& path);
  QHash<QString, QString> m_groupForGame;
  QHash<QString, QString> m_primaryForGroup;
  QHash<QString, QString> m_preferredForGroup;
  QHash<QString, qint64> m_lastLaunchForGame;
  QHash<QString, OrganizationState> m_organizationForGame;
  QHash<QString, QVariantMap> m_userFlags;
  QHash<QString, QStringList> m_collectionsForGame;
  QStringList m_collectionNames;
};
