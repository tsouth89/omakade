#include "library/LibraryFilterModel.h"

#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"

#include <algorithm>
#include <QRandomGenerator>
#include <QUuid>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>

LibraryFilterModel::LibraryFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  sort(0);
}

void LibraryFilterModel::setSourceModel(QAbstractItemModel* source) {
  if (sourceModel() != nullptr) {
    disconnect(sourceModel(), nullptr, this, nullptr);
  }
  clearSelection();
  QSortFilterProxyModel::setSourceModel(source);
  if (auto* games = qobject_cast<UnifiedGameModel*>(source)) {
    connect(games, &QAbstractItemModel::modelReset, this, &LibraryFilterModel::reconcileSelection);
    connect(games, &UnifiedGameModel::savedFiltersChanged, this, &LibraryFilterModel::savedFiltersChanged);
    connect(games, &UnifiedGameModel::collectionsChanged, this, [this] {
      beginFilterChange();
      endFilterChange(Direction::Rows);
      emit organizationNamesChanged();
      emit savedFiltersChanged();
    });
  }
  emit organizationNamesChanged();
  emit savedFiltersChanged();
}

namespace {
QString selectionKey(const QVariantMap& game) {
  return game.value("source").toString() + QChar::Null + game.value("runner").toString()
      + QChar::Null + game.value("appId").toString();
}
}

bool LibraryFilterModel::isSelected(int row) const {
  if (row < 0 || row >= rowCount()) return false;
  for (const auto& member : installations(row)) if (m_selectedIdentities.contains(selectionKey(member.toMap()))) return true;
  return false;
}

void LibraryFilterModel::toggleSelection(int row) {
  if (row < 0 || row >= rowCount()) return;
  if (isSelected(row)) {
    for (const auto& member : installations(row)) m_selectedIdentities.remove(selectionKey(member.toMap()));
  } else m_selectedIdentities.insert(selectionKey(get(row)));
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::selectAllFiltered() {
  for (int row = 0; row < rowCount(); ++row) if (!isSelected(row)) m_selectedIdentities.insert(selectionKey(get(row)));
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::clearSelection() {
  m_selectedIdentities.clear();
  ++m_selectionRevision;
  emit selectionChanged();
}

void LibraryFilterModel::reconcileSelection() {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games) for (int row = 0; row < games->rowCount(); ++row) {
    bool found = false;
    for (const auto& member : games->installations(row)) {
      const QString key = selectionKey(member.toMap());
      if (!m_selectedIdentities.contains(key)) continue;
      if (found) m_selectedIdentities.remove(key);
      found = true;
    }
  }
  ++m_selectionRevision;
  emit selectionChanged();
}

bool LibraryFilterModel::applyBulkChanges(const QVariantMap& changes) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const int count = selectionCount();
  const bool okay = games && games->bulkOrganize(m_selectedIdentities.values(), changes);
  m_bulkMessage = okay ? QStringLiteral("Updated %1 selected games").arg(count)
      : QStringLiteral("Nothing changed. Check the values and storage, or clear and reselect games if an entry is no longer available.");
  if (okay) clearSelection();
  emit bulkMessageChanged();
  return okay;
}

void LibraryFilterModel::setSavedFilterMessage(const QString& value) {
  m_savedFilterMessage = value;
  emit savedFilterMessageChanged();
}

QVariantMap LibraryFilterModel::filterState() const {
  return {{"version", 1}, {"search", m_searchText}, {"mode", int(m_mode)},
      {"sort", int(m_sortMode)}, {"availability", int(m_availability)}, {"showHidden", m_showHidden},
      {"source", m_sourceFilter}, {"status", m_completionFilter},
      {"collection", m_collectionFilter}, {"tag", m_tagFilter}};
}

bool LibraryFilterModel::validFilterState(const QVariantMap& state) {
  if (state.size() != 10 || state.value("version").toInt() != 1) return false;
  for (const QString& key : {QStringLiteral("version"), QStringLiteral("mode"), QStringLiteral("sort"), QStringLiteral("availability")}) {
    const auto value = state.value(key);
    if (value.metaType().id() != QMetaType::Int && value.metaType().id() != QMetaType::LongLong && value.metaType().id() != QMetaType::Double) return false;
    if (value.toDouble() != value.toInt()) return false;
  }
  if (state.value("mode").toInt() < 0 || state.value("mode").toInt() > 3 ||
      state.value("sort").toInt() < 0 || state.value("sort").toInt() > 2 ||
      state.value("availability").toInt() < 0 || state.value("availability").toInt() > 2 ||
      state.value("showHidden").metaType().id() != QMetaType::Bool) return false;
  for (const QString& key : {QStringLiteral("search"), QStringLiteral("source"), QStringLiteral("status"), QStringLiteral("collection"), QStringLiteral("tag")}) {
    const auto value = state.value(key);
    if (value.metaType().id() != QMetaType::QString || value.toString().size() > 4096 || value.toString().contains(QChar(0))) return false;
  }
  return QStringList{"", "backlog", "playing", "completed", "abandoned"}.contains(state.value("status").toString());
}

QString LibraryFilterModel::filterWarning(const QVariantMap& state) const {
  if (!validFilterState(state)) return QStringLiteral("This saved filter has an unsupported format.");
  QStringList missing;
  const QString collection = state.value("collection").toString();
  const QString tag = state.value("tag").toString();
  if (!collection.isEmpty() && !collectionNames().contains(collection, Qt::CaseInsensitive)) missing << "collection: " + collection;
  if (!tag.isEmpty() && !tagNames().contains(tag, Qt::CaseInsensitive)) missing << "tag: " + tag;
  return missing.isEmpty() ? QString{} : QStringLiteral("Not currently available (%1). These criteria remain applied.").arg(missing.join(", "));
}

QVariantList LibraryFilterModel::savedFilters() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  QVariantList result = games ? games->savedFilters() : QVariantList{};
  for (auto& value : result) {
    auto entry = value.toMap();
    entry.insert("warning", filterWarning(entry.value("state").toMap()));
    value = entry;
  }
  return result;
}

QString LibraryFilterModel::saveCurrentFilter(const QString& value) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const QString name = value.trimmed().normalized(QString::NormalizationForm_C);
  if (!games || savedFilters().size() >= 500 || name.isEmpty() || name.size() > 100 ||
      name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]"))) || !validFilterState(filterState())) {
    setSavedFilterMessage("Use a name of 1 to 100 characters; up to 500 filters can be saved."); return {};
  }
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!games->saveFilter(id, name, filterState())) {
    setSavedFilterMessage("Could not save. Choose a unique name and check available storage."); return {};
  }
  setSavedFilterMessage("Saved " + name);
  return id;
}

bool LibraryFilterModel::renameSavedFilter(const QString& id, const QString& value) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const QString name = value.trimmed().normalized(QString::NormalizationForm_C);
  if (games && !name.isEmpty() && name.size() <= 100 &&
      !name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) {
    for (const auto& entry : games->savedFilters()) {
      const auto saved = entry.toMap();
      if (saved.value("id").toString() == id && games->saveFilter(id, name, saved.value("state").toMap())) {
        setSavedFilterMessage("Renamed " + name); return true;
      }
    }
  }
  setSavedFilterMessage("Could not rename. Use a unique name of 1 to 100 characters."); return false;
}

bool LibraryFilterModel::removeSavedFilter(const QString& id) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  const bool removed = games && games->removeFilter(id);
  setSavedFilterMessage(removed ? "Saved filter deleted" : "Could not delete this saved filter");
  return removed;
}

bool LibraryFilterModel::applySavedFilter(const QString& id) {
  for (const auto& entry : savedFilters()) {
    const auto saved = entry.toMap();
    if (saved.value("id").toString() != id) continue;
    const auto state = saved.value("state").toMap();
    if (!validFilterState(state)) break;
    // Change the full query before invalidating so observers never see a partially applied view.
    m_searchText = state.value("search").toString();
    m_mode = Mode(state.value("mode").toInt());
    m_sortMode = SortMode(state.value("sort").toInt());
    m_availability = Availability(state.value("availability").toInt());
    m_showHidden = state.value("showHidden").toBool();
    m_sourceFilter = state.value("source").toString();
    m_completionFilter = state.value("status").toString();
    m_collectionFilter = state.value("collection").toString();
    m_tagFilter = state.value("tag").toString();
    invalidate();
    sort(0);
    emit searchTextChanged(); emit modeChanged(); emit sortModeChanged();
    emit availabilityChanged(); emit showHiddenChanged(); emit sourceFilterChanged();
    emit organizationFilterChanged();
    setSavedFilterMessage(saved.value("warning").toString());
    return true;
  }
  setSavedFilterMessage("This saved filter is missing or has an unsupported format.");
  return false;
}

int LibraryFilterModel::pickRandomGame() {
  QList<QPair<int, QString>> eligible;
  for (int row = 0; row < rowCount(); ++row) {
    if (!preferredInstallation(row).value(QStringLiteral("launchAvailable")).toBool()) continue;
    const QVariantMap game = get(row);
    const QString identity = QString::fromUtf8(QJsonDocument(QJsonArray{
        game.value(QStringLiteral("source")).toString(),
        game.value(QStringLiteral("runner")).toString(),
        game.value(QStringLiteral("appId")).toString()}).toJson(QJsonDocument::Compact));
    eligible.append({row, identity});
  }
  if (eligible.isEmpty()) return -1;
  if (eligible.size() > 1) {
    eligible.removeIf([this](const auto& entry) { return entry.second == m_lastRandomIdentity; });
  }
  const auto& chosen = eligible.at(QRandomGenerator::global()->bounded(int(eligible.size())));
  m_lastRandomIdentity = chosen.second;
  return chosen.first;
}

LibraryFilterModel::SortMode LibraryFilterModel::sortMode() const { return m_sortMode; }

void LibraryFilterModel::setSortMode(SortMode value) {
  if (m_sortMode == value) {
    return;
  }
  m_sortMode = value;
  invalidate();
  sort(0);
  emit sortModeChanged();
}

LibraryFilterModel::Availability LibraryFilterModel::availability() const { return m_availability; }

void LibraryFilterModel::setAvailability(Availability value) {
  if (m_availability == value) {
    return;
  }
  m_availability = value;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit availabilityChanged();
}

bool LibraryFilterModel::showHidden() const { return m_showHidden; }

void LibraryFilterModel::setShowHidden(bool value) {
  if (m_showHidden == value) {
    return;
  }
  m_showHidden = value;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit showHiddenChanged();
}

QString LibraryFilterModel::sourceFilter() const { return m_sourceFilter; }

void LibraryFilterModel::setSourceFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_sourceFilter.compare(normalized, Qt::CaseInsensitive) == 0) {
    return;
  }
  m_sourceFilter = normalized;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit sourceFilterChanged();
}

QString LibraryFilterModel::completionFilter() const { return m_completionFilter; }

void LibraryFilterModel::setCompletionFilter(const QString& value) {
  const QString normalized = value.trimmed().toLower();
  if (m_completionFilter == normalized) {
    return;
  }
  m_completionFilter = normalized;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QString LibraryFilterModel::collectionFilter() const { return m_collectionFilter; }

void LibraryFilterModel::setCollectionFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_collectionFilter.compare(normalized, Qt::CaseInsensitive) == 0) {
    return;
  }
  m_collectionFilter = normalized;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QString LibraryFilterModel::tagFilter() const { return m_tagFilter; }

void LibraryFilterModel::setTagFilter(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_tagFilter.compare(normalized, Qt::CaseInsensitive) == 0) {
    return;
  }
  m_tagFilter = normalized;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit organizationFilterChanged();
}

QStringList LibraryFilterModel::collectionNames() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games == nullptr ? QStringList{} : games->collectionNames();
}

QStringList LibraryFilterModel::tagNames() const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games == nullptr ? QStringList{} : games->tagNames();
}

QString LibraryFilterModel::searchText() const { return m_searchText; }

void LibraryFilterModel::setSearchText(const QString& value) {
  const QString normalized = value.trimmed();
  if (m_searchText == normalized) {
    return;
  }

  m_searchText = normalized;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit searchTextChanged();
}

LibraryFilterModel::Mode LibraryFilterModel::mode() const { return m_mode; }

void LibraryFilterModel::setMode(Mode value) {
  if (m_mode == value) {
    return;
  }

  m_mode = value;
  beginFilterChange();
  endFilterChange(Direction::Rows);
  emit modeChanged();
}

QVariantMap LibraryFilterModel::get(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }

  const QModelIndex sourceIndex = mapToSource(index(row, 0));
  QVariantMap result;
  const auto roles = sourceModel()->roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    result.insert(QString::fromUtf8(iterator.value()), sourceIndex.data(iterator.key()));
  }
  return result;
}

int LibraryFilterModel::indexOf(const QString& source, const QString& runner,
                                const QString& appId) const {
  if (source.isEmpty() || appId.isEmpty()) {
    return -1;
  }
  const QString normalizedRunner = runner.isNull() ? QStringLiteral("") : runner;
  for (int row = 0; row < rowCount(); ++row) {
    const QModelIndex game = index(row, 0);
    if (game.data(GameRoles::AppId).toString() != appId ||
        game.data(GameRoles::Source).toString() != source) {
      if (game.data(GameRoles::Linked).toBool()) {
        for (const QVariant& value : installations(row)) {
          const QVariantMap installation = value.toMap();
          if (installation.value(QStringLiteral("source")).toString() == source &&
              installation.value(QStringLiteral("runner")).toString() == normalizedRunner &&
              installation.value(QStringLiteral("appId")).toString() == appId) return row;
        }
      }
      continue;
    }
    const QString gameRunner = game.data(GameRoles::Runner).toString();
    if ((gameRunner.isNull() ? QStringLiteral("") : gameRunner) == normalizedRunner) {
      return row;
    }
  }
  return -1;
}

void LibraryFilterModel::toggleFavorite(int row) {
  if (row < 0 || row >= rowCount()) {
    return;
  }

  QMetaObject::invokeMethod(sourceModel(), "toggleFavorite",
                            Q_ARG(int, mapToSource(index(row, 0)).row()));
}

void LibraryFilterModel::toggleHidden(int row) {
  if (row < 0 || row >= rowCount()) {
    return;
  }
  QMetaObject::invokeMethod(sourceModel(), "toggleHidden",
                            Q_ARG(int, mapToSource(index(row, 0)).row()));
}

bool LibraryFilterModel::setCustomCover(int row, const QUrl& sourceUrl) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->setCustomCover(mapToSource(index(row, 0)).row(), sourceUrl);
}

bool LibraryFilterModel::resetCustomCover(int row) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->resetCustomCover(mapToSource(index(row, 0)).row());
}

bool LibraryFilterModel::setCustomArtwork(int row, const QString& kind, const QUrl& sourceUrl) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games && row >= 0 && row < rowCount() &&
         games->setCustomArtwork(mapToSource(index(row, 0)).row(), kind, sourceUrl);
}
bool LibraryFilterModel::resetCustomArtwork(int row, const QString& kind) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games && row >= 0 && row < rowCount() &&
         games->resetCustomArtwork(mapToSource(index(row, 0)).row(), kind);
}

QVariantList LibraryFilterModel::installations(int row) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return {};
  }
  return games->installations(mapToSource(index(row, 0)).row());
}

QVariantMap LibraryFilterModel::preferredInstallation(int row) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount()
             ? games->preferredInstallation(mapToSource(index(row, 0)).row())
             : QVariantMap{};
}

bool LibraryFilterModel::setPreferredInstallation(int row, const QString& source,
                                                  const QString& runner, const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setPreferredInstallation(mapToSource(index(row, 0)).row(), source, runner, appId);
}

QVariantList LibraryFilterModel::linkCandidates(int row, const QString& search) const {
  const auto* games = qobject_cast<const UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return {};
  }
  return games->linkCandidates(mapToSource(index(row, 0)).row(), search);
}

bool LibraryFilterModel::linkGames(int row, const QString& source, const QString& runner,
                                   const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->linkGames(mapToSource(index(row, 0)).row(), source, runner, appId);
}

bool LibraryFilterModel::recordLaunch(int row, const QString& source, const QString& runner,
                                      const QString& appId) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->recordLaunch(mapToSource(index(row, 0)).row(), source, runner, appId);
}

bool LibraryFilterModel::unlinkGames(int row) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || row < 0 || row >= rowCount()) {
    return false;
  }
  return games->unlinkGames(mapToSource(index(row, 0)).row());
}

bool LibraryFilterModel::setCompletionStatus(int row, const QString& status) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setCompletionStatus(mapToSource(index(row, 0)).row(), status);
}

bool LibraryFilterModel::setTags(int row, const QString& tags) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setTags(mapToSource(index(row, 0)).row(), tags);
}

bool LibraryFilterModel::createCollection(const QString& name) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && games->createCollection(name);
}

bool LibraryFilterModel::deleteCollection(const QString& name) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  if (games == nullptr || !games->deleteCollection(name)) {
    return false;
  }
  if (m_collectionFilter.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
    setCollectionFilter({});
  }
  return true;
}

bool LibraryFilterModel::setCollectionMembership(int row, const QString& name, bool included) {
  auto* games = qobject_cast<UnifiedGameModel*>(sourceModel());
  return games != nullptr && row >= 0 && row < rowCount() &&
         games->setCollectionMembership(mapToSource(index(row, 0)).row(), name, included);
}

bool LibraryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);

  const QVariant installedValue = sourceIndex.data(GameRoles::Installed);
  const bool installed = !installedValue.isValid() || installedValue.toBool();
  if ((m_availability == Availability::Installed && !installed) ||
      (m_availability == Availability::ReadyToInstall && installed)) {
    return false;
  }

  if (!m_sourceFilter.isEmpty()) {
    const QString primarySource = sourceIndex.data(GameRoles::Source).toString();
    const QStringList linkedSources =
        sourceIndex.data(GameRoles::LinkedSources).toString().split(QStringLiteral(" + "));
    if (primarySource.compare(m_sourceFilter, Qt::CaseInsensitive) != 0 &&
        std::none_of(linkedSources.cbegin(), linkedSources.cend(), [this](const QString& source) {
          return source.compare(m_sourceFilter, Qt::CaseInsensitive) == 0;
        })) {
      return false;
    }
  }

  const bool hidden = sourceIndex.data(GameRoles::Hidden).toBool();
  if (m_mode == Mode::Hidden && !hidden) {
    return false;
  }
  if (m_mode != Mode::Hidden && !m_showHidden && hidden) {
    return false;
  }

  if (m_mode == Mode::Favorites && !sourceIndex.data(GameRoles::Favorite).toBool()) {
    return false;
  }
  if (m_mode == Mode::Recent && !sourceIndex.data(GameRoles::Recent).toBool()) {
    return false;
  }

  if (!m_completionFilter.isEmpty() &&
      sourceIndex.data(GameRoles::CompletionStatus).toString() != m_completionFilter) {
    return false;
  }
  const auto containsCaseInsensitive = [](const QStringList& values, const QString& expected) {
    return std::any_of(values.cbegin(), values.cend(), [&expected](const QString& value) {
      return value.compare(expected, Qt::CaseInsensitive) == 0;
    });
  };
  if (!m_collectionFilter.isEmpty() &&
      !containsCaseInsensitive(sourceIndex.data(GameRoles::Collections).toStringList(),
                               m_collectionFilter)) {
    return false;
  }
  if (!m_tagFilter.isEmpty() &&
      !containsCaseInsensitive(sourceIndex.data(GameRoles::Tags).toStringList(), m_tagFilter)) {
    return false;
  }

  if (m_searchText.isEmpty()) {
    return true;
  }

  const QString title = sourceIndex.data(GameRoles::Title).toString();
  const QString subtitle = sourceIndex.data(GameRoles::Subtitle).toString();
  const QString tags = sourceIndex.data(GameRoles::Tags).toStringList().join(QLatin1Char(' '));
  return title.contains(m_searchText, Qt::CaseInsensitive) ||
         subtitle.contains(m_searchText, Qt::CaseInsensitive) ||
         tags.contains(m_searchText, Qt::CaseInsensitive);
}

bool LibraryFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  if (m_sortMode == SortMode::RecentlyPlayed) {
    const qint64 leftPlayed = left.data(GameRoles::LastPlayed).toLongLong();
    const qint64 rightPlayed = right.data(GameRoles::LastPlayed).toLongLong();
    if (leftPlayed != rightPlayed) {
      return leftPlayed > rightPlayed;
    }
  }
  if (m_sortMode == SortMode::Playtime) {
    const int leftHours = left.data(GameRoles::Hours).toInt();
    const int rightHours = right.data(GameRoles::Hours).toInt();
    if (leftHours != rightHours) {
      return leftHours > rightHours;
    }
  }
  return left.data(GameRoles::Title)
             .toString()
             .localeAwareCompare(right.data(GameRoles::Title).toString()) < 0;
}
