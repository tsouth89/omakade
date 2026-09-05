#pragma once

#include <QSortFilterProxyModel>
#include <QUrl>
#include <QSet>

class LibraryFilterModel final : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
  Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
  Q_PROPERTY(QString bulkMessage READ bulkMessage NOTIFY bulkMessageChanged)
  Q_PROPERTY(QVariantList savedFilters READ savedFilters NOTIFY savedFiltersChanged)
  Q_PROPERTY(QString savedFilterMessage READ savedFilterMessage NOTIFY savedFilterMessageChanged)
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
  Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
  Q_PROPERTY(SortMode sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
  Q_PROPERTY(
      Availability availability READ availability WRITE setAvailability NOTIFY availabilityChanged)
  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
  Q_PROPERTY(
      QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY sourceFilterChanged)
  Q_PROPERTY(QString completionFilter READ completionFilter WRITE setCompletionFilter NOTIFY
                 organizationFilterChanged)
  Q_PROPERTY(QString collectionFilter READ collectionFilter WRITE setCollectionFilter NOTIFY
                 organizationFilterChanged)
  Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY organizationFilterChanged)
  Q_PROPERTY(QStringList collectionNames READ collectionNames NOTIFY organizationNamesChanged)
  Q_PROPERTY(QStringList tagNames READ tagNames NOTIFY organizationNamesChanged)

public:
  enum class Mode { All = 0, Favorites, Recent, Hidden };
  Q_ENUM(Mode)
  enum class SortMode { Title = 0, RecentlyPlayed, Playtime };
  Q_ENUM(SortMode)
  enum class Availability { Installed = 0, AllGames, ReadyToInstall };
  Q_ENUM(Availability)

  explicit LibraryFilterModel(QObject* parent = nullptr);
  void setSourceModel(QAbstractItemModel* sourceModel) override;

  [[nodiscard]] QString searchText() const;
  void setSearchText(const QString& value);

  [[nodiscard]] Mode mode() const;
  void setMode(Mode value);
  [[nodiscard]] SortMode sortMode() const;
  void setSortMode(SortMode value);
  [[nodiscard]] Availability availability() const;
  void setAvailability(Availability value);
  [[nodiscard]] bool showHidden() const;
  void setShowHidden(bool value);
  [[nodiscard]] QString sourceFilter() const;
  void setSourceFilter(const QString& value);
  [[nodiscard]] QString completionFilter() const;
  void setCompletionFilter(const QString& value);
  [[nodiscard]] QString collectionFilter() const;
  void setCollectionFilter(const QString& value);
  [[nodiscard]] QString tagFilter() const;
  void setTagFilter(const QString& value);
  [[nodiscard]] QStringList collectionNames() const;
  [[nodiscard]] QStringList tagNames() const;

  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE int pickRandomGame();
  int selectionCount() const { return m_selectedIdentities.size(); }
  int selectionRevision() const { return m_selectionRevision; }
  QString bulkMessage() const { return m_bulkMessage; }
  Q_INVOKABLE bool isSelected(int row) const;
  Q_INVOKABLE void toggleSelection(int row);
  Q_INVOKABLE void selectAllFiltered();
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE bool applyBulkChanges(const QVariantMap& changes);
  QVariantList savedFilters() const;
  QString savedFilterMessage() const { return m_savedFilterMessage; }
  Q_INVOKABLE QString saveCurrentFilter(const QString& name);
  Q_INVOKABLE bool renameSavedFilter(const QString& id, const QString& name);
  Q_INVOKABLE bool removeSavedFilter(const QString& id);
  Q_INVOKABLE bool applySavedFilter(const QString& id);
  QVariantMap filterState() const;
  Q_INVOKABLE int indexOf(const QString& source, const QString& runner, const QString& appId) const;
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

signals:
  void selectionChanged();
  void bulkMessageChanged();
  void savedFiltersChanged();
  void savedFilterMessageChanged();
  void searchTextChanged();
  void modeChanged();
  void sortModeChanged();
  void availabilityChanged();
  void showHiddenChanged();
  void sourceFilterChanged();
  void organizationFilterChanged();
  void organizationNamesChanged();

protected:
  [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                      const QModelIndex& sourceParent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
  void reconcileSelection();
  QSet<QString> m_selectedIdentities;
  int m_selectionRevision = 0;
  QString m_bulkMessage;
  void setSavedFilterMessage(const QString& value);
  QString filterWarning(const QVariantMap& state) const;
  static bool validFilterState(const QVariantMap& state);
  QString m_savedFilterMessage;
  QString m_lastRandomIdentity;
  QString m_searchText;
  Mode m_mode = Mode::All;
  SortMode m_sortMode = SortMode::Title;
  Availability m_availability = Availability::Installed;
  bool m_showHidden = false;
  QString m_sourceFilter;
  QString m_completionFilter;
  QString m_collectionFilter;
  QString m_tagFilter;
};
