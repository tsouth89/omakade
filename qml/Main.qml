import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "screens"

ApplicationWindow {
    id: root

    property bool randomSelection: false
    property bool backupEditorOpen: false
    property bool bulkOrganizationOpen: false
    property bool savedFiltersOpen: false
    property bool artworkEditorOpen: false
    property bool manualEditorOpen: false
    property bool detailOpen: false
    property var selectedGame: ({})
    property var selectedInstallation: ({})
    property var selectedInstallations: []
    property var linkResults: []
    property int selectedIndex: -1
    property bool smokeReady: false
    property bool diagnosticsOpen: false
    property bool linkDialogOpen: false
    property bool collectionDeleteOpen: false
    // The organize filters open a picker list instead of cycling through every value.
    property bool filterPickerOpen: false
    property bool couchTextEntryOpen: false
    property var couchTextEntryTarget: null
    property string couchTextEntryTitle: "ENTER TEXT"
    property bool couchTextEntryPassword: false
    property string couchTextEntryPlaceholder: "Start typing"
    property string filterPickerKind: ""
    property var filterPickerValues: []
    property string pendingCollectionDelete: ""
    property bool couchMode: CouchModeRequested
    property int desktopVisibility: Window.Windowed
    readonly property bool libraryScanning: (SteamLibrary ? SteamLibrary.scanning : false)
                                            || (LutrisLibrary ? LutrisLibrary.scanning : false)
                                            || (HeroicLibrary ? HeroicLibrary.scanning : false)
                                            || (FaugusLibrary ? FaugusLibrary.scanning : false)
                                            || (RetroArchLibrary ? RetroArchLibrary.scanning : false)
                                            || (Pcsx2Library ? Pcsx2Library.scanning : false)
                                            || (RyujinxLibrary ? RyujinxLibrary.scanning : false)
                                            || (BattleNetLibrary ? BattleNetLibrary.scanning : false)
    readonly property int ownedGameCount: SteamAccount
                                          ? SteamAccount.ownedGameCount
                                          : OwnedGameCountOverride
    readonly property Item sourceRowEndButton:
        manualSourceButton.visible ? manualSourceButton
      : ryujinxSourceButton.visible && ryujinxSourceButton.enabled ? ryujinxSourceButton
      : pcsx2SourceButton.visible && pcsx2SourceButton.enabled ? pcsx2SourceButton
      : retroArchSourceButton.visible && retroArchSourceButton.enabled ? retroArchSourceButton
      : faugusSourceButton.visible && faugusSourceButton.enabled ? faugusSourceButton
      : gogSourceButton.visible && gogSourceButton.enabled ? gogSourceButton
      : heroicSourceButton.visible && heroicSourceButton.enabled ? heroicSourceButton
      : lutrisSourceButton.visible && lutrisSourceButton.enabled ? lutrisSourceButton
      : battleNetSourceButton.visible && battleNetSourceButton.enabled ? battleNetSourceButton
      : steamSourceButton.visible && steamSourceButton.enabled ? steamSourceButton
      : allSourcesButton

    function isWithin(item, container) {
        while (item) {
            if (item === container) {
                return true
            }
            item = item.parent
        }
        return false
    }

    function openFilterPicker(kind, values) {
        filterPickerKind = kind
        filterPickerValues = values
        filterPickerOpen = true
    }

    function filterPickerCurrent() {
        return filterPickerKind === "status" ? Library.completionFilter
             : filterPickerKind === "collection" ? Library.collectionFilter
             : Library.tagFilter
    }

    function applyFilterPick(value) {
        if (filterPickerKind === "status") {
            Library.completionFilter = value
        } else if (filterPickerKind === "collection") {
            Library.collectionFilter = value
        } else {
            Library.tagFilter = value
        }
        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
        filterPickerOpen = false
    }

    function navigationContainer() {
        if (couchTextEntryOpen) {
            return null
        }
        if (backupEditorOpen) return backupEditor
        if (bulkOrganizationOpen) return bulkOrganizationEditor
        if (savedFiltersOpen) return savedFiltersEditor
        if (artworkEditorOpen) return artworkEditor
        if (manualEditorOpen) return manualEditor
        if (filterPickerOpen) {
            return filterPickerOverlay
        }
        if (collectionDeleteOpen) {
            return collectionDeleteOverlay
        }
        if (linkDialogOpen) {
            return linkDialogOverlay
        }
        if (diagnosticsOpen) {
            return settingsOverlay
        }
        if (detailOpen && detailsLoader.item) {
            return detailsLoader.item
        }
        return null
    }

    function arrowNavigationEnabled() {
        const current = root.activeFocusItem
        return root.navigationContainer() !== null
                && (!current || current.controllerNavigation !== false)
    }

    function focusWithin(container, forward, preferred) {
        if (!container) {
            return
        }
        if (preferred && preferred.visible && preferred.enabled) {
            preferred.forceActiveFocus(forward ? Qt.TabFocusReason
                                               : Qt.BacktabFocusReason)
            revealNavigationItem(container, preferred)
            return
        }
        const current = root.activeFocusItem
        const origin = root.isWithin(current, container) ? current : container
        let candidate = origin.nextItemInFocusChain(forward)
        for (let attempts = 0; candidate && attempts < 300; ++attempts) {
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab) {
                candidate.forceActiveFocus(forward ? Qt.TabFocusReason
                                                   : Qt.BacktabFocusReason)
                revealNavigationItem(container, candidate)
                return
            }
            candidate = candidate.nextItemInFocusChain(forward)
        }
    }

    // Fallback for arrow keys that reach an overlay loader directly.
    function handleArrowKey(container, event) {
        if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down
                && event.key !== Qt.Key_Left && event.key !== Qt.Key_Right) {
            return
        }
        if (root.activeFocusItem
                && root.activeFocusItem.controllerNavigation === false) {
            return
        }
        root.focusSpatial(container, event.key)
        event.accepted = true
    }

    function focusSpatial(container, key) {
        if (!container) {
            return false
        }
        const current = root.activeFocusItem
        if (container === backupEditor && backupEditor.navigate(current, key)) return true
        if (container === bulkOrganizationEditor && bulkOrganizationEditor.navigate(current, key)) return true
        if (container === savedFiltersEditor && savedFiltersEditor.navigate(current, key)) return true
        if (!root.isWithin(current, container)) {
            root.focusWithin(container, true)
            return true
        }
        const targetProperty = key === Qt.Key_Up ? "controllerUpTarget"
                             : key === Qt.Key_Down ? "controllerDownTarget"
                             : key === Qt.Key_Left ? "controllerLeftTarget"
                             : "controllerRightTarget"
        const explicitTarget = current[targetProperty]
        if (explicitTarget && explicitTarget.visible && explicitTarget.enabled) {
            explicitTarget.forceActiveFocus(Qt.TabFocusReason)
            root.revealNavigationItem(container, explicitTarget)
            return true
        }
        const currentCenter = current.mapToItem(container, current.width / 2,
                                                current.height / 2)
        const currentLeft = currentCenter.x - current.width / 2
        const currentRight = currentCenter.x + current.width / 2
        const currentTop = currentCenter.y - current.height / 2
        const currentBottom = currentCenter.y + current.height / 2
        // Use rectangle edges to decide direction. Comparing centers alone treats a wider button
        // on the next row as being to the right of the current button when the two actually
        // overlap horizontally.
        let best = null
        let bestScore = Number.MAX_VALUE
        let candidate = current.nextItemInFocusChain(true)
        for (let attempts = 0; candidate && candidate !== current
             && attempts < 300; ++attempts) {
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab
                    && candidate["controllerNavigation"] !== false) {
                const center = candidate.mapToItem(container, candidate.width / 2,
                                                   candidate.height / 2)
                const dx = center.x - currentCenter.x
                const dy = center.y - currentCenter.y
                const candidateLeft = center.x - candidate.width / 2
                const candidateRight = center.x + candidate.width / 2
                const candidateTop = center.y - candidate.height / 2
                const candidateBottom = center.y + candidate.height / 2
                let primary = 0
                let cross = 0
                let crossGap = 0
                if (key === Qt.Key_Up) {
                    primary = currentTop - candidateBottom
                    cross = Math.abs(dx)
                    crossGap = Math.max(0, Math.max(currentLeft, candidateLeft)
                                           - Math.min(currentRight, candidateRight))
                } else if (key === Qt.Key_Down) {
                    primary = candidateTop - currentBottom
                    cross = Math.abs(dx)
                    crossGap = Math.max(0, Math.max(currentLeft, candidateLeft)
                                           - Math.min(currentRight, candidateRight))
                } else if (key === Qt.Key_Left) {
                    primary = currentLeft - candidateRight
                    cross = Math.abs(dy)
                    crossGap = Math.max(0, Math.max(currentTop, candidateTop)
                                           - Math.min(currentBottom, candidateBottom))
                } else if (key === Qt.Key_Right) {
                    primary = candidateLeft - currentRight
                    cross = Math.abs(dy)
                    crossGap = Math.max(0, Math.max(currentTop, candidateTop)
                                           - Math.min(currentBottom, candidateBottom))
                }
                if (primary >= -1) {
                    const score = Math.max(0, primary) + crossGap * 2.5 + cross * 0.01
                    if (score < bestScore) {
                        best = candidate
                        bestScore = score
                    }
                }
            }
            candidate = candidate.nextItemInFocusChain(true)
        }
        if (best) {
            best.forceActiveFocus(Qt.TabFocusReason)
            root.revealNavigationItem(container, best)
            return true
        }
        return false
    }

    function rescanLibraries() {
        if (SteamLibrary && Preferences.steamEnabled) SteamLibrary.refresh()
        if (LutrisLibrary && Preferences.lutrisEnabled) LutrisLibrary.refresh()
        if (HeroicLibrary && (Preferences.heroicEnabled || Preferences.gogEnabled)) HeroicLibrary.refresh()
        if (FaugusLibrary && Preferences.faugusEnabled) FaugusLibrary.refresh()
        if (RetroArchLibrary && Preferences.retroArchEnabled) RetroArchLibrary.refresh()
        if (Pcsx2Library && Preferences.pcsx2Enabled) Pcsx2Library.refresh()
        if (RyujinxLibrary && Preferences.ryujinxEnabled) RyujinxLibrary.refresh()
        if (BattleNetLibrary && Preferences.battleNetEnabled) BattleNetLibrary.refresh()
    }

    function focusAboveGrid() {
        if (!root.focusSpatial(librarySurface, Qt.Key_Up)) {
            sortButton.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function toggleLibraryControls() {
        if (root.navigationContainer() !== null) {
            return
        }
        if (root.couchMode) {
            couchLibraryView.toggleControls()
            return
        }
        if (libraryView.gridFocused) {
            sortButton.forceActiveFocus(Qt.TabFocusReason)
        } else {
            libraryView.focusGrid()
        }
    }

    function revealInScrollView(scrollView, item) {
        const flickable = scrollView ? scrollView.contentItem : null
        if (!flickable || !item) {
            return
        }
        const position = item.mapToItem(flickable, 0, 0)
        const margin = 16
        if (position.y < margin) {
            flickable.contentY = Math.max(flickable.originY,
                                          flickable.contentY + position.y - margin)
        } else if (position.y + item.height > flickable.height - margin) {
            flickable.contentY = Math.min(
                        flickable.originY + Math.max(0, flickable.contentHeight - flickable.height),
                        flickable.contentY + position.y + item.height - flickable.height + margin)
        }
    }

    function revealNavigationItem(container, item) {
        if (container === bulkOrganizationEditor) {
            bulkOrganizationEditor.reveal(item)
        } else if (container === savedFiltersEditor) {
            savedFiltersEditor.reveal(item)
        } else if (container === settingsOverlay) {
            root.revealInScrollView(settingsScroll, item)
        } else if (container === linkDialogOverlay && root.isWithin(item, candidateList)) {
            candidateList.positionViewAtIndex(candidateList.currentIndex, ListView.Contain)
        } else if (container === detailsLoader.item) {
            container.revealFocusedItem(item)
        }
    }

    function restoreFocus(item) {
        if (item && item.visible && item.enabled) {
            Qt.callLater(item.forceActiveFocus)
        } else if (detailOpen && detailsLoader.item) {
            Qt.callLater(function() { root.focusWithin(detailsLoader.item, true) })
        } else {
            Qt.callLater(root.focusLibrary)
        }
    }

    function openCouchTextEntry(target, title, password, placeholder) {
        if (!root.couchMode || !target) {
            return
        }
        couchTextEntryTarget = target
        couchTextEntryTitle = title || "ENTER TEXT"
        couchTextEntryPassword = password || false
        couchTextEntryPlaceholder = placeholder || "Start typing"
        couchTextEntryKeyboard.value = target.text || ""
        couchTextEntryKeyboard.keyboardMode = "upper"
        couchTextEntryOpen = true
        Qt.callLater(couchTextEntryKeyboard.focusKeyboard)
    }

    function closeCouchTextEntry(accepted) {
        const target = couchTextEntryTarget
        if (accepted && target) {
            target.text = couchTextEntryKeyboard.value
        }
        couchTextEntryOpen = false
        couchTextEntryTarget = null
        if (target) {
            Qt.callLater(function() { root.restoreFocus(target) })
        }
    }

    function handleCouchTextEntry(event, target, title, password, placeholder) {
        if (!root.couchMode) {
            return
        }
        root.openCouchTextEntry(target, title, password, placeholder)
        event.accepted = true
    }

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function scanTime(seconds) {
        if (!seconds) {
            return "Not scanned yet"
        }
        return new Date(seconds * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    function preferredInstallation(installations, fallback) {
        const preferred = Library.preferredInstallation(root.selectedIndex)
        return preferred && preferred.appId ? preferred : fallback
    }

    function pickRandomGame() {
        const index = Library.pickRandomGame()
        if (index < 0) {
            root.showToast("No available games match these filters")
            return
        }
        root.openGame(index)
        root.randomSelection = true
    }

    function openGame(index) {
        root.randomSelection = false
        selectedIndex = index
        selectedGame = Library.get(index)
        selectedInstallations = Library.installations(index)
        selectedInstallation = preferredInstallation(selectedInstallations, selectedGame)
        if (!DemoMode && selectedInstallation.source === "Steam") {
            Achievements.load(selectedInstallation.appId)
            if (SteamAccount) {
                SteamAccount.refreshAchievementsIfStale(selectedInstallation.appId)
            }
            if (Insights) {
                Insights.loadSteam(selectedInstallation.appId)
            }
        } else if (!DemoMode && selectedInstallation.source === "RetroArch") {
            Achievements.load(selectedInstallation.appId)
            if (RetroAchievements) {
                RetroAchievements.refreshAchievementsIfStale(selectedInstallation.appId)
            }
            if (Insights) {
                Insights.loadSteam("")
            }
        } else {
            Achievements.load("")
            if (Insights) {
                Insights.loadSteam("")
            }
        }
        detailOpen = true
    }

    function closeDetails() {
        detailOpen = false
        Qt.callLater(root.focusLibrary)
    }

    function focusLibrary() {
        if (root.couchMode) {
            couchLibraryView.focusGrid()
        } else {
            libraryView.focusGrid()
        }
    }

    function focusCurrentSurface() {
        const container = root.navigationContainer()
        const current = root.activeFocusItem
        if (container && root.isWithin(current, container)
                && current.visible && current.enabled) {
            root.revealNavigationItem(container, current)
        } else if (container) {
            root.focusWithin(container, true)
        } else {
            root.focusLibrary()
        }
    }

    function updateCouchMode(enabled, remember) {
        if (root.couchMode === enabled) {
            return
        }
        if (!enabled) {
            if (root.couchTextEntryOpen) {
                root.closeCouchTextEntry(false)
            }
            if (couchLibraryView.searchOpen) {
                couchLibraryView.closeSearch(false)
            }
            if (couchLibraryView.browseOpen) {
                couchLibraryView.closeBrowse()
            }
        }
        if (enabled) {
            couchLibraryView.currentIndex = libraryView.currentIndex
            root.desktopVisibility = root.visibility
        } else {
            libraryView.currentIndex = couchLibraryView.currentIndex
        }
        root.couchMode = enabled
        if (remember) {
            Preferences.couchModeEnabled = enabled
        }
        root.visibility = enabled ? Window.FullScreen : root.desktopVisibility
        Qt.callLater(root.focusCurrentSurface)
    }

    function setCouchMode(enabled) {
        root.updateCouchMode(enabled, true)
    }

    // A Sunshine activation is session-scoped. It must not change the preferred startup
    // mode just because an already-running desktop window receives the request.
    function activateCouchMode() {
        root.updateCouchMode(true, false)
    }

    function toggleCouchMode() {
        setCouchMode(!root.couchMode)
    }

    function showToast(message) {
        toast.message = message
        toastTimer.restart()
    }

    function filterLabel(prefix, value, available) {
        if (!value || value.length === 0) {
            return available && available.length > 0 ? prefix + " (" + available.length + ")" : prefix
        }
        const shortened = value.length > 16 ? value.substring(0, 15) + "…" : value
        return prefix + ": " + shortened.toUpperCase()
    }

    readonly property bool organizationFiltersActive: Library.completionFilter !== ""
                                                      || Library.collectionFilter !== ""
                                                      || Library.tagFilter !== ""

    // Names the search or filter that produced an empty library, or returns "" when the
    // library itself is empty.
    function emptyTitleForFilters() {
        if (Library.searchText !== "") {
            return "No games match \"" + Library.searchText + "\""
        }
        const active = [Library.completionFilter, Library.collectionFilter, Library.tagFilter]
                       .filter(value => value !== "").length
        if (active > 1) {
            return "No games match these filters"
        }
        if (Library.completionFilter !== "") {
            return "No games marked " + Library.completionFilter.toUpperCase()
        }
        if (Library.collectionFilter !== "") {
            return "Nothing in " + Library.collectionFilter + " yet"
        }
        if (Library.tagFilter !== "") {
            return "No games tagged " + Library.tagFilter
        }
        return ""
    }

    function clearLibraryFilters() {
        Library.completionFilter = ""
        Library.collectionFilter = ""
        Library.tagFilter = ""
        searchField.clear()
        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
        libraryView.focusGrid()
    }

    function confirmCollectionDelete() {
        const name = pendingCollectionDelete
        const source = selectedGame.source || ""
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId || ""
        if (Library.deleteCollection(name)) {
            if (detailOpen) {
                refreshSelected(source, runner, appId)
            }
            showToast("Deleted " + name)
        }
        collectionDeleteOpen = false
        pendingCollectionDelete = ""
    }

    function refreshAfterOrganization() {
        const source = selectedGame.source
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId
        if (!refreshSelected(source, runner, appId)) {
            closeDetails()
        }
    }

    function playSelected() {
        if (DemoMode) {
            showToast("Demo games cannot be launched")
        } else if (selectedInstallation.installed === false) {
            if (Launcher.install(selectedInstallation.source, selectedInstallation.appId)) {
                showToast("Opening Steam to install " + selectedGame.title)
            } else {
                showToast(Launcher.lastError)
            }
        } else if (Launcher.launch(selectedInstallation.source, selectedInstallation.appId,
                                   selectedInstallation.flatpak || false,
                                   selectedInstallation.runner || "",
                                   selectedInstallation.installPath || "",
                                   selectedInstallation.launchTarget || "")) {
            Library.recordLaunch(selectedIndex, selectedInstallation.source,
                                 selectedInstallation.runner || "", selectedInstallation.appId)
            showToast("Opening " + selectedGame.title + " in " + selectedInstallation.source)
            if (Preferences.closeAfterLaunch) {
                Qt.callLater(Qt.quit)
            }
        } else {
            showToast(Launcher.lastError)
        }
    }

    function manageSelected() {
        if (Launcher.manage(selectedInstallation.source, selectedInstallation.appId,
                            selectedInstallation.flatpak || false,
                            selectedInstallation.runner || "",
                            selectedInstallation.launchTarget || "")) {
            showToast("Opening " + selectedInstallation.source)
        } else {
            showToast(Launcher.lastError)
        }
    }

    function selectInstallation(installation) {
        selectedInstallation = installation
        if (!DemoMode && installation.source === "Steam") {
            Achievements.load(installation.appId)
            if (SteamAccount) {
                SteamAccount.refreshAchievementsIfStale(installation.appId)
            }
            if (Insights) {
                Insights.loadSteam(installation.appId)
            }
        } else if (!DemoMode && installation.source === "RetroArch") {
            Achievements.load(installation.appId)
            if (RetroAchievements) {
                RetroAchievements.refreshAchievementsIfStale(installation.appId)
            }
            if (Insights) {
                Insights.loadSteam("")
            }
        } else {
            Achievements.load("")
            if (Insights) {
                Insights.loadSteam("")
            }
        }
    }

    function refreshSelected(source, runner, appId) {
        const index = Library.indexOf(source, runner || "", appId)
        if (index < 0) {
            return false
        }
        selectedIndex = index
        selectedGame = Library.get(index)
        selectedInstallations = Library.installations(index)
        selectedInstallation = preferredInstallation(selectedInstallations, selectedGame)
        return true
    }

    function linkCandidate(candidate) {
        const source = selectedGame.source
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId
        if (Library.linkGames(selectedIndex, candidate.source,
                              candidate.runner || "", candidate.appId)) {
            refreshSelected(source, runner, appId)
            showToast("Installations linked")
            linkDialogOpen = false
        }
    }

    function openBulkOrganization() {
        Library.clearSelection()
        root.bulkOrganizationOpen = true
        Qt.callLater(bulkOrganizationEditor.focusEditor)
    }
    BulkOrganizationEditor {
        id: bulkOrganizationEditor
        objectName: "bulkOrganizationEditor"
        anchors.fill: parent
        z: 87
        visible: root.bulkOrganizationOpen
        couchMode: root.couchMode
        onDismissed: {
            Library.clearSelection()
            root.bulkOrganizationOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function openSavedFilters() {
        root.savedFiltersOpen = true
        Qt.callLater(savedFiltersEditor.focusEditor)
    }
    function applySavedFilter(id) {
        const current = Library.get(root.couchMode ? couchLibraryView.currentIndex : libraryView.currentIndex)
        if (!Library.applySavedFilter(id)) return
        searchField.text = Library.searchText
        const found = Library.indexOf(current.source || "", current.runner || "", current.appId || "")
        const index = found >= 0 ? found : Library.rowCount() > 0 ? 0 : -1
        libraryView.currentIndex = index
        couchLibraryView.currentIndex = index
        couchLibraryView.refreshCurrentGame()
        root.savedFiltersOpen = false
        if (Library.savedFilterMessage) root.showToast(Library.savedFilterMessage)
        Qt.callLater(root.focusCurrentSurface)
    }
    SavedFiltersEditor {
        id: savedFiltersEditor
        objectName: "savedFiltersEditor"
        anchors.fill: parent
        z: 86
        visible: root.savedFiltersOpen
        couchMode: root.couchMode
        onApplyRequested: id => root.applySavedFilter(id)
        onDismissed: { root.savedFiltersOpen = false; Qt.callLater(root.focusCurrentSurface) }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function openBackupEditor() {
        backupEditorOpen = true
        Qt.callLater(backupEditor.focusEditor)
    }
    function focusGogLibraryPath() {
        gogLibraryPathField.forceActiveFocus()
        root.revealInScrollView(settingsScroll, gogLibraryPathField)
    }
    function removeGogLibraryFolder(path) {
        if (!Preferences.removeGogLibraryPath(path)) root.showToast("Could not remove that folder")
        Qt.callLater(root.focusGogLibraryPath)
    }
    BackupEditor {
        id: backupEditor
        objectName: "backupEditor"
        anchors.fill: parent
        z: 89
        visible: root.backupEditorOpen
        couchMode: root.couchMode
        onDismissed: { root.backupEditorOpen = false; Qt.callLater(root.focusCurrentSurface) }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editArtwork() {
        artworkEditor.message = ""
        root.artworkEditorOpen = true
        Qt.callLater(artworkEditor.focusEditor)
    }
    ArtworkEditor {
        id: artworkEditor
        objectName: "artworkEditor"
        anchors.fill: parent
        z: 85
        visible: root.artworkEditorOpen
        game: root.selectedGame
        gameRow: root.selectedIndex
        couchMode: root.couchMode
        onDismissed: {
            root.artworkEditorOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onArtworkChanged: root.refreshAfterOrganization()
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editManualGame(id) {
        manualEditorOpen = true
        manualEditor.loadDraft(id ? ManualLibrary.get(id) : {})
    }

    ManualGameEditor {
        id: manualEditor
        objectName: "manualGameEditor"
        anchors.fill: parent
        z: 80
        visible: root.manualEditorOpen
        couchMode: root.couchMode
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
        onDismissed: {
            root.manualEditorOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onSaved: function(id) {
            root.manualEditorOpen = false
            root.diagnosticsOpen = false
            if (manualEditor.entryId === "") root.clearLibraryFilters()
            const row = Library.indexOf("Manual", "", id)
            if (row >= 0) root.openGame(row)
            else { root.detailOpen = false; Qt.callLater(root.focusLibrary) }
            root.showToast("Manual game saved")
        }
        onRemoved: {
            root.manualEditorOpen = false
            root.detailOpen = false
            Qt.callLater(root.focusCurrentSurface)
            root.showToast("Removed from Omakade. Game files were kept.")
        }
    }

    FolderDialog {
        id: gogFolderDialog
        title: "Choose a GOG library folder"
        onAccepted: {
            if (!Preferences.addGogLibraryPath(selectedFolder.toString()))
                root.showToast("Could not save that folder")
            Qt.callLater(root.focusCurrentSurface)
        }
        onRejected: Qt.callLater(root.focusCurrentSurface)
    }

    FileDialog {
        id: coverDialog
        title: "Choose cover artwork"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: {
            if (Library.setCustomCover(root.selectedIndex, selectedFile)) {
                root.refreshAfterOrganization()
                root.showToast("Cover updated")
            } else {
                root.showToast("That image could not be used")
            }
        }
    }

    visible: true
    width: 1380
    height: 880
    minimumWidth: 820
    minimumHeight: 590
    title: "Omakade"
    color: "transparent"

    font.family: Theme.fontFamily

    Shortcut {
        sequence: "Ctrl+F"
        enabled: !root.couchMode && !root.detailOpen && !root.diagnosticsOpen
                 && !root.linkDialogOpen
                 && !root.collectionDeleteOpen
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "F11"
        onActivated: root.toggleCouchMode()
    }
    Shortcut {
        sequence: "Ctrl+M"
        onActivated: {
            Preferences.reducedMotion = !Preferences.reducedMotion
            root.showToast(Preferences.reducedMotion ? "Reduced motion enabled" : "Reduced motion disabled")
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: !root.linkDialogOpen && !root.collectionDeleteOpen
        onActivated: root.diagnosticsOpen = !root.diagnosticsOpen
    }
    Shortcut {
        sequence: "F6"
        enabled: root.navigationContainer() === null
        onActivated: root.toggleLibraryControls()
    }
    Shortcut {
        sequence: "Tab"
        enabled: root.navigationContainer() !== null
        onActivated: root.focusWithin(root.navigationContainer(), true)
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.navigationContainer() !== null
        onActivated: root.focusWithin(root.navigationContainer(), false)
    }
    Shortcut {
        sequence: "Up"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Up)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Down)
    }
    Shortcut {
        sequence: "Left"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Left)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Right)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.couchTextEntryOpen) {
                root.closeCouchTextEntry(false)
            } else if (root.backupEditorOpen) {
                backupEditor.dismiss()
            } else if (root.bulkOrganizationOpen) {
                Library.clearSelection()
                root.bulkOrganizationOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.savedFiltersOpen) {
                root.savedFiltersOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.artworkEditorOpen) {
                root.artworkEditorOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.manualEditorOpen) {
                root.manualEditorOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.filterPickerOpen) {
                root.filterPickerOpen = false
            } else if (root.couchMode && couchLibraryView.searchOpen) {
                couchLibraryView.closeSearch(false)
            } else if (root.couchMode && couchLibraryView.browseOpen) {
                couchLibraryView.closeBrowse()
            } else if (root.linkDialogOpen) {
                root.linkDialogOpen = false
            } else if (root.collectionDeleteOpen) {
                root.collectionDeleteOpen = false
                root.pendingCollectionDelete = ""
            } else if (root.diagnosticsOpen) {
                root.diagnosticsOpen = false
            } else if (root.detailOpen && detailsLoader.item
                       && detailsLoader.item.collectionEditorOpen) {
                // The window shortcut sees Escape before the details page does.
                detailsLoader.item.closeCollectionEditor()
            } else if (root.detailOpen) {
                root.closeDetails()
            } else if (!root.couchMode && searchField.text.length > 0) {
                searchField.clear()
                libraryView.focusGrid()
            } else if (root.couchMode || !libraryView.gridFocused) {
                root.focusLibrary()
            }
        }
    }

    Binding {
        target: Controller
        property: "focusNavigation"
        value: !root.couchTextEntryOpen
               && (root.backupEditorOpen || root.bulkOrganizationOpen || root.savedFiltersOpen || root.artworkEditorOpen || root.manualEditorOpen || root.detailOpen || root.diagnosticsOpen || root.linkDialogOpen
               || root.collectionDeleteOpen
               || (!root.couchMode && !libraryView.gridFocused))
    }
    Shortcut {
        sequence: "Return"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Space"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }

    onActiveChanged: {
        if (active) {
            Qt.callLater(root.focusCurrentSurface)
        }
    }
    onClosing: function(close) {
        close.accepted = true
        Qt.quit()
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha) }
            GradientStop { position: 0.48; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha * 0.88) }
            GradientStop { position: 1.0; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha) }
        }
    }

    Rectangle {
        width: root.width * 0.52
        height: width
        radius: width / 2
        x: root.width * 0.62
        y: -height * 0.62
        color: root.alpha(Theme.accent, 0.10)
    }

    Rectangle {
        width: root.width * 0.42
        height: width
        radius: width / 2
        x: -width * 0.48
        y: root.height * 0.48
        color: root.alpha(Theme.green, 0.055)
    }

    Item {
        id: librarySurface
        anchors.fill: parent
        opacity: root.detailOpen ? 0 : 1
        scale: root.detailOpen ? 0.985 : 1
        visible: !root.couchMode && opacity > 0
        enabled: !root.couchMode && !root.detailOpen

        // Arrow keys move between the filters and toolbar controls, and Down with nothing
        // below drops back into the game grid. Controller directions take the same path.
        Keys.onPressed: function(event) {
            if (root.navigationContainer() !== null || libraryView.gridFocused) {
                return
            }
            if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down
                    && event.key !== Qt.Key_Left && event.key !== Qt.Key_Right) {
                return
            }
            if (!root.focusSpatial(librarySurface, event.key) && event.key === Qt.Key_Down) {
                libraryView.focusGrid()
            }
            event.accepted = true
        }

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 150 }
        }
        Behavior on scale {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: Math.max(22, root.width * 0.032)
            anchors.rightMargin: Math.max(22, root.width * 0.032)
            anchors.topMargin: 24
            anchors.bottomMargin: 16
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                Row {
                    spacing: 11
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        width: 34
                        height: 34
                        source: "qrc:/icons/resources/icons/io.github.tsouth89.Omakade.svg"
                        sourceSize: Qt.size(68, 68)
                        fillMode: Image.PreserveAspectFit
                        Accessible.ignored: true
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Text {
                            text: "OMAKADE"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                        Text {
                            text: Theme.themeName.toUpperCase()
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 8
                            font.letterSpacing: 0.7
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Row {
                    spacing: 5
                    visible: root.width >= 1040

                    GlassButton {
                        id: allModeButton
                        objectName: "allModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "ALL"
                        compact: true
                        selected: Library.mode === 0
                        onClicked: {
                            Library.mode = 0
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: favoritesModeButton
                        objectName: "favoritesModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "FAVORITES"
                        compact: true
                        selected: Library.mode === 1
                        onClicked: {
                            Library.mode = 1
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: recentModeButton
                        objectName: "recentModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "RECENT"
                        compact: true
                        selected: Library.mode === 2
                        onClicked: {
                            Library.mode = 2
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: hiddenModeButton
                        objectName: "hiddenModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "HIDDEN"
                        compact: true
                        visible: !DemoMode
                        selected: Library.mode === 3
                        onClicked: {
                            Library.mode = 3
                            libraryView.focusGrid()
                        }
                    }
                }

                TextField {
                    id: searchField
                    objectName: "searchField"
                    property bool controllerNavigation: false
                    Layout.preferredWidth: root.width < 900 ? 150 : Math.min(300, root.width * 0.26)
                    Layout.minimumWidth: root.width < 900 ? 150 : 190
                    Layout.preferredHeight: 38
                    placeholderText: "Search games"
                    color: Theme.foreground
                    placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    leftPadding: 36
                    rightPadding: 12
                    selectByMouse: true
                    focus: false
                    Accessible.name: "Search games"
                    Accessible.description: "Filter the installed game library"

                    onTextChanged: {
                        Library.searchText = text
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                    Keys.onEscapePressed: function(event) {
                        if (text.length > 0) {
                            clear()
                        }
                        libraryView.focusGrid()
                        event.accepted = true
                    }
                    Keys.onDownPressed: function(event) {
                        libraryView.focusGrid()
                        event.accepted = true
                    }

                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: root.alpha(Theme.foreground, searchField.activeFocus ? 0.075 : 0.045)
                        border.width: searchField.activeFocus ? 2 : 1
                        border.color: searchField.activeFocus
                                      ? Theme.accent
                                      : root.alpha(Theme.foreground, 0.15)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 13
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌕"
                        color: searchField.activeFocus ? Theme.accent : Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                    }
                }

                GlassButton {
                    objectName: "bulkOrganizationButton"
                    text: "ORGANIZE"
                    compact: true
                    onClicked: root.openBulkOrganization()
                }
                GlassButton {
                    objectName: "savedFiltersButton"
                    text: "SAVED FILTERS"
                    compact: true
                    onClicked: root.openSavedFilters()
                }
                GlassButton {
                    id: settingsButton
                    objectName: "settingsButton"
                    text: "SETTINGS"
                    compact: true
                    onClicked: root.diagnosticsOpen = true
                }

                GlassButton {
                    id: couchModeButton
                    objectName: "couchModeButton"
                    text: "COUCH"
                    compact: true
                    onClicked: root.setCouchMode(true)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.width < 1040
                spacing: 6
                GlassButton {
                    id: narrowAllModeButton
                    objectName: "narrowAllModeButton"
                    text: "ALL"
                    compact: true
                    selected: Library.mode === 0
                    onClicked: {
                        Library.mode = 0
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    text: "FAVORITES"
                    compact: true
                    selected: Library.mode === 1
                    onClicked: {
                        Library.mode = 1
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    text: "RECENT"
                    compact: true
                    selected: Library.mode === 2
                    onClicked: {
                        Library.mode = 2
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    id: narrowHiddenModeButton
                    objectName: "narrowHiddenModeButton"
                    property Item controllerDownTarget: root.sourceRowEndButton
                    text: "HIDDEN"
                    compact: true
                    visible: !DemoMode
                    selected: Library.mode === 3
                    onClicked: {
                        Library.mode = 3
                        libraryView.focusGrid()
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Flickable {
                    id: sourceFlickable
                    objectName: "sourceFlickable"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.preferredHeight: sourceButtonsRow.implicitHeight
                    visible: !DemoMode
                    clip: true
                    contentWidth: sourceButtonsRow.implicitWidth
                    contentHeight: sourceButtonsRow.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    function reveal(item) {
                        if (!item || !root.isWithin(item, sourceButtonsRow)
                                || contentWidth <= width) {
                            return
                        }
                        const position = item.mapToItem(sourceButtonsRow, 0, 0)
                        const margin = 5
                        if (position.x < contentX + margin) {
                            contentX = Math.max(0, position.x - margin)
                        } else if (position.x + item.width > contentX + width - margin) {
                            contentX = Math.min(contentWidth - width,
                                                position.x + item.width - width + margin)
                        }
                    }

                    Connections {
                        target: root
                        function onActiveFocusItemChanged() {
                            sourceFlickable.reveal(root.activeFocusItem)
                        }
                    }

                    Row {
                    id: sourceButtonsRow
                    spacing: 5
                    GlassButton {
                        id: allSourcesButton
                        objectName: "allSourcesButton"
                        property Item controllerDownTarget: root.ownedGameCount > 0
                                                            ? installedAvailabilityButton
                                                            : statusFilterButton
                        text: "ALL SOURCES"
                        compact: true
                        selected: Library.sourceFilter === ""
                        onClicked: {
                            Library.sourceFilter = ""
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: steamSourceButton
                        objectName: "steamSourceButton"
                        text: "STEAM"
                        compact: true
                        visible: Preferences.steamEnabled
                        selected: Library.sourceFilter === "Steam"
                        onClicked: {
                            Library.sourceFilter = "Steam"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: battleNetSourceButton
                        objectName: "battleNetSourceButton"
                        text: "BATTLE.NET"
                        compact: true
                        visible: Preferences.battleNetEnabled
                        selected: Library.sourceFilter === "Battle.net"
                        onClicked: {
                            Library.sourceFilter = "Battle.net"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: lutrisSourceButton
                        objectName: "lutrisSourceButton"
                        text: "LUTRIS"
                        compact: true
                        visible: Preferences.lutrisEnabled
                        selected: Library.sourceFilter === "Lutris"
                        onClicked: {
                            Library.sourceFilter = "Lutris"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: heroicSourceButton
                        objectName: "heroicSourceButton"
                        text: "HEROIC"
                        compact: true
                        visible: Preferences.heroicEnabled
                        selected: Library.sourceFilter === "Heroic"
                        onClicked: {
                            Library.sourceFilter = "Heroic"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: gogSourceButton
                        objectName: "gogSourceButton"
                        text: "GOG"
                        compact: true
                        visible: Preferences.gogEnabled
                        selected: Library.sourceFilter === "GOG"
                        onClicked: {
                            Library.sourceFilter = "GOG"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: faugusSourceButton
                        objectName: "faugusSourceButton"
                        text: "FAUGUS"
                        compact: true
                        visible: Preferences.faugusEnabled
                        selected: Library.sourceFilter === "Faugus"
                        onClicked: {
                            Library.sourceFilter = "Faugus"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: retroArchSourceButton
                        objectName: "retroArchSourceButton"
                        property Item controllerRightTarget: pcsx2SourceButton
                        text: "RETROARCH"
                        compact: true
                        visible: Preferences.retroArchEnabled
                        selected: Library.sourceFilter === "RetroArch"
                        onClicked: {
                            Library.sourceFilter = "RetroArch"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: pcsx2SourceButton
                        objectName: "pcsx2SourceButton"
                        property Item controllerLeftTarget: retroArchSourceButton
                        property Item controllerRightTarget: ryujinxSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "PCSX2"
                        compact: true
                        visible: Preferences.pcsx2Enabled
                        selected: Library.sourceFilter === "PCSX2"
                        onClicked: {
                            Library.sourceFilter = "PCSX2"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: ryujinxSourceButton
                        objectName: "ryujinxSourceButton"
                        property Item controllerLeftTarget: pcsx2SourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "RYUJINX"
                        compact: true
                        visible: Preferences.ryujinxEnabled
                        selected: Library.sourceFilter === "Ryujinx"
                        onClicked: {
                            Library.sourceFilter = "Ryujinx"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: manualSourceButton
                        objectName: "manualSourceButton"
                        property Item controllerDownTarget: statusFilterButton
                        text: "MANUAL"
                        compact: true
                        visible: ManualLibrary.count > 0
                        selected: Library.sourceFilter === "Manual"
                        onClicked: {
                            Library.sourceFilter = "Manual"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    }
                }

                Text {
                    visible: root.width >= 1100
                    text: Library.mode === 1 ? "FAVORITES" : Library.mode === 2 ? "RECENTLY PLAYED" : Library.mode === 3 ? "HIDDEN" : "YOUR LIBRARY"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.7
                }
                Text {
                    visible: root.width >= 1100
                    text: libraryView.count
                          + (DemoMode ? " GAMES"
                             : Library.availability === 0 ? " INSTALLED"
                             : Library.availability === 2 ? " READY TO INSTALL"
                             : " GAMES")
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                }
                Text {
                    visible: root.width >= 1100 && root.libraryScanning
                    text: "SYNCING"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    id: randomGameButton
                    property Item controllerLeftTarget: root.width < 1040
                                                         ? root.sourceRowEndButton
                                                         : hiddenModeButton
                    objectName: "randomGameButton"
                    property Item controllerRightTarget: sortButton
                    compact: true
                    text: "PICK A GAME"
                    onClicked: root.pickRandomGame()
                }
                GlassButton {
                    id: sortButton
                    objectName: "sortButton"
                    property Item controllerLeftTarget: randomGameButton
                    property Item controllerRightTarget: rescanButton
                    compact: true
                    text: Library.sortMode === 0 ? "SORT: TITLE" : Library.sortMode === 1 ? "SORT: RECENT" : "SORT: PLAYTIME"
                    onClicked: Library.sortMode = (Library.sortMode + 1) % 3
                }
                GlassButton {
                    id: rescanButton
                    objectName: "rescanButton"
                    property Item controllerUpTarget: settingsButton
                    compact: true
                    text: root.libraryScanning ? "SCANNING" : "RESCAN"
                    enabled: !root.libraryScanning
                    onClicked: root.rescanLibraries()
                }
                Text {
                    text: Controller.connected
                          ? Controller.primaryGlyph + "  OPEN   ·   " + Controller.favoriteGlyph + "  FAVORITE   ·   " + Controller.toolbarGlyph + "  CONTROLS   ·   " + Controller.backGlyph + "  BACK"
                          : "ENTER  OPEN   ·   F  FAVORITE   ·   F6  CONTROLS"
                    color: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 8
                    visible: root.width > 930
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !DemoMode && root.ownedGameCount > 0
                spacing: 6

                Text {
                    text: "AVAILABILITY"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                GlassButton {
                    id: installedAvailabilityButton
                    objectName: "installedAvailabilityButton"
                    compact: true
                    text: "INSTALLED"
                    selected: Library.availability === 0
                    onClicked: {
                        Library.availability = 0
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    compact: true
                    text: "ALL GAMES"
                    selected: Library.availability === 1
                    onClicked: {
                        Library.availability = 1
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    id: readyAvailabilityButton
                    objectName: "readyAvailabilityButton"
                    property Item controllerDownTarget: statusFilterButton
                    compact: true
                    text: "READY TO INSTALL"
                    selected: Library.availability === 2
                    onClicked: {
                        Library.availability = 2
                        libraryView.focusGrid()
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !DemoMode
                spacing: 6

                Text {
                    text: "ORGANIZE"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                GlassButton {
                    id: statusFilterButton
                    objectName: "statusFilterButton"
                    compact: true
                    text: root.filterLabel("STATUS", Library.completionFilter)
                    selected: Library.completionFilter !== ""
                    onClicked: root.openFilterPicker("status",
                                                     ["backlog", "playing", "completed", "abandoned"])
                }
                GlassButton {
                    id: collectionFilterButton
                    objectName: "collectionFilterButton"
                    compact: true
                    text: root.filterLabel("COLLECTION", Library.collectionFilter,
                                           Library.collectionNames)
                    selected: Library.collectionFilter !== ""
                    onClicked: {
                        if (Library.collectionNames.length === 0) {
                            root.showToast("No collections yet. Open a game and use + New Collection.")
                            return
                        }
                        root.openFilterPicker("collection", Library.collectionNames)
                    }
                }
                GlassButton {
                    id: tagFilterButton
                    objectName: "tagFilterButton"
                    compact: true
                    text: root.filterLabel("TAG", Library.tagFilter, Library.tagNames)
                    selected: Library.tagFilter !== ""
                    onClicked: {
                        if (Library.tagNames.length === 0) {
                            root.showToast("No tags yet. Open a game and add tags under Organize.")
                            return
                        }
                        root.openFilterPicker("tag", Library.tagNames)
                    }
                }
                GlassButton {
                    compact: true
                    visible: Library.completionFilter !== "" || Library.collectionFilter !== ""
                             || Library.tagFilter !== ""
                    text: "CLEAR"
                    onClicked: {
                        Library.completionFilter = ""
                        Library.collectionFilter = ""
                        Library.tagFilter = ""
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                Item { Layout.fillWidth: true }
            }

            LibraryView {
                id: libraryView
                objectName: "libraryView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                libraryModel: Library
                scanning: root.libraryScanning
                filtersActive: root.organizationFiltersActive || Library.searchText !== ""
                onClearFiltersRequested: root.clearLibraryFilters()
                emptyTitle: root.emptyTitleForFilters() !== "" ? root.emptyTitleForFilters()
                            : Library.sourceFilter === "GOG" && HeroicLibrary && !HeroicLibrary.gogDetected
                            ? "GOG was not found"
                            : Library.sourceFilter === "Heroic" && HeroicLibrary && !HeroicLibrary.heroicDetected
                            ? "Heroic was not found"
                            : Library.sourceFilter === "Faugus" && FaugusLibrary && !FaugusLibrary.faugusDetected
                            ? "Faugus was not found"
                            : Library.sourceFilter === "RetroArch" && RetroArchLibrary && !RetroArchLibrary.retroArchDetected
                            ? "RetroArch was not found"
                            : Library.sourceFilter === "PCSX2" && Pcsx2Library && !Pcsx2Library.pcsx2Detected
                            ? "PCSX2 was not found"
                            : Library.sourceFilter === "Ryujinx" && RyujinxLibrary && !RyujinxLibrary.ryujinxDetected
                            ? "Ryujinx was not found"
                            : Library.sourceFilter === "Battle.net" && BattleNetLibrary && !BattleNetLibrary.battleNetDetected
                            ? "Battle.net was not found"
                            : Library.sourceFilter === "Lutris" && LutrisLibrary && !LutrisLibrary.lutrisDetected
                            ? "Lutris was not found"
                            : Library.sourceFilter === "Steam" && SteamLibrary && !SteamLibrary.steamDetected
                              ? "Steam was not found"
                              : Library.mode === 3 ? "No hidden games"
                              : Library.availability === 2 ? "No games ready to install"
                              : Library.availability === 1 ? "No games in this library"
                              : "No installed games"
                emptyMessage: Library.searchText !== ""
                              ? "Try a different search, or clear it to see the whole library."
                              : root.organizationFiltersActive
                              ? "This is a filter, not your library. Clear or change it to see your games."
                              : Library.sourceFilter === "Faugus" && FaugusLibrary && FaugusLibrary.errorText.length > 0
                              ? FaugusLibrary.errorText
                              : Library.sourceFilter === "RetroArch" && RetroArchLibrary && RetroArchLibrary.errorText.length > 0
                              ? RetroArchLibrary.errorText
                              : Library.sourceFilter === "PCSX2" && Pcsx2Library && Pcsx2Library.errorText.length > 0
                              ? Pcsx2Library.errorText
                              : Library.sourceFilter === "Ryujinx" && RyujinxLibrary && RyujinxLibrary.errorText.length > 0
                              ? RyujinxLibrary.errorText
                              : Library.sourceFilter === "GOG" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : Library.sourceFilter === "Heroic" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : Library.sourceFilter === "Lutris" && LutrisLibrary && LutrisLibrary.errorText.length > 0
                              ? LutrisLibrary.errorText
                              : Library.sourceFilter === "Battle.net" && BattleNetLibrary && BattleNetLibrary.errorText.length > 0
                              ? BattleNetLibrary.errorText
                              : SteamLibrary && SteamLibrary.errorText.length > 0
                                ? SteamLibrary.errorText
                                : "Install a game in Steam, GOG, Lutris, Heroic, Faugus, RetroArch, PCSX2, Ryujinx, or Battle.net, then rescan your library."
                onGameActivated: index => root.openGame(index)
                onFavoriteToggled: index => Library.toggleFavorite(index)
                onCoverRequested: function(source, appId) {
                    if (source === "Steam" && SteamLibrary) {
                        SteamLibrary.requestCover(appId)
                    } else if (source === "Battle.net" && BattleNetLibrary) {
                        BattleNetLibrary.requestCover(appId)
                    }
                }
                onRefreshRequested: {
                    root.rescanLibraries()
                }
                onFocusAboveRequested: root.focusAboveGrid()
            }
        }
    }

    CouchLibraryView {
        id: couchLibraryView
        objectName: "couchLibrary"
        anchors.fill: parent
        visible: root.couchMode && !root.detailOpen
        enabled: visible && root.navigationContainer() === null
        libraryModel: Library
        scanning: root.libraryScanning
        viewOverride: CouchLibraryViewOverride

        onGameActivated: index => root.openGame(index)
        onFavoriteToggled: function(index) {
            Library.toggleFavorite(index)
            couchLibraryView.refreshCurrentGame()
        }
        onOrganizeRequested: root.openBulkOrganization()
        onSavedFiltersRequested: root.openSavedFilters()
        onRandomRequested: root.pickRandomGame()
        onSettingsRequested: root.diagnosticsOpen = true
        onDesktopRequested: root.setCouchMode(false)
        onCoverRequested: function(source, appId) {
            if (source === "Steam" && SteamLibrary) {
                SteamLibrary.requestCover(appId)
            } else if (source === "Battle.net" && BattleNetLibrary) {
                BattleNetLibrary.requestCover(appId)
            }
        }
    }

    Loader {
        id: detailsLoader
        anchors.fill: parent
        active: root.detailOpen
        Keys.onPressed: function(event) {
            if (item && !root.linkDialogOpen && !root.diagnosticsOpen
                    && !root.collectionDeleteOpen) {
                root.handleArrowKey(item, event)
            }
        }
        opacity: root.detailOpen ? 1 : 0
        asynchronous: false

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 170 }
        }

        sourceComponent: GameDetails {
            game: root.selectedGame
            installations: root.selectedInstallations
            selectedInstallation: root.selectedInstallation
            couchMode: root.couchMode
            navigationEnabled: !root.backupEditorOpen && !root.bulkOrganizationOpen && !root.savedFiltersOpen && !root.artworkEditorOpen && !root.manualEditorOpen && !root.linkDialogOpen && !root.diagnosticsOpen
                               && !root.collectionDeleteOpen
            onBackRequested: root.closeDetails()
            onFavoriteRequested: {
                Library.toggleFavorite(root.selectedIndex)
                // The favorite filter can drop or move the row, so find the game again by identity.
                root.refreshAfterOrganization()
            }
            onManualEditRequested: root.editManualGame(root.selectedInstallation.appId)
            onPlayRequested: root.playSelected()
            onManageRequested: root.manageSelected()
            onInstallationSelected: installation => root.selectInstallation(installation)
            onPreferredInstallationRequested: {
                const choice = root.selectedInstallation
                if (Library.setPreferredInstallation(root.selectedIndex, choice.source,
                                                     choice.runner || "", choice.appId)) {
                    root.selectedInstallations = Library.installations(root.selectedIndex)
                    root.selectInstallation(root.preferredInstallation(root.selectedInstallations,
                                                                       root.selectedGame))
                    root.showToast("Default installation saved")
                    Qt.callLater(root.focusCurrentSurface)
                } else {
                    root.showToast("Could not save the default installation")
                }
            }
            onLinkRequested: {
                linkSearch.text = root.selectedGame.title
                root.linkResults = Library.linkCandidates(root.selectedIndex, linkSearch.text)
                root.linkDialogOpen = true
            }
            onUnlinkRequested: {
                const source = root.selectedGame.source
                const runner = root.selectedGame.runner || ""
                const appId = root.selectedGame.appId
                if (Library.unlinkGames(root.selectedIndex)) {
                    if (!root.refreshSelected(source, runner, appId)) {
                        root.closeDetails()
                    }
                    root.showToast("Installations unlinked")
                }
            }
            randomSelection: root.randomSelection
            onRandomRequested: root.pickRandomGame()
            onCoverRequested: root.editArtwork()
            onCoverResetRequested: {
                if (Library.resetCustomCover(root.selectedIndex)) {
                    root.refreshAfterOrganization()
                    root.showToast("Original cover restored")
                }
            }
            onConnectRequested: root.diagnosticsOpen = true
            onHiddenRequested: {
                Library.toggleHidden(root.selectedIndex)
                root.closeDetails()
            }
            onCompletionStatusRequested: status => {
                if (Library.setCompletionStatus(root.selectedIndex, status)) {
                    root.refreshAfterOrganization()
                    root.showToast(status.length > 0 ? "Status updated" : "Status cleared")
                }
            }
            onTagsRequested: tags => {
                if (Library.setTags(root.selectedIndex, tags)) {
                    root.refreshAfterOrganization()
                    root.showToast("Tags updated")
                }
            }
            onCollectionToggled: function(name, included) {
                if (Library.setCollectionMembership(root.selectedIndex, name, included)) {
                    root.refreshAfterOrganization()
                    root.showToast(included ? "Added to " + name : "Removed from " + name)
                }
            }
            onCollectionCreateRequested: name => {
                if (Library.createCollection(name)
                        && Library.setCollectionMembership(root.selectedIndex, name, true)) {
                    root.refreshAfterOrganization()
                    root.showToast("Added to " + name)
                    detailsLoader.item.closeCollectionEditor()
                } else {
                    root.showToast("That collection already exists or is invalid")
                }
            }
            onTextEntryRequested: function(target, title, password, placeholder) {
                root.openCouchTextEntry(target, title, password, placeholder)
            }
        }
    }

    Rectangle {
        id: linkDialogOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.linkDialogOpen
        z: 25
        Keys.onPressed: function(event) { root.handleArrowKey(linkDialogOverlay, event) }
        color: root.alpha(Theme.darkerBackground, 0.72)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(linkSearch.forceActiveFocus)
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.linkDialogOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(620, root.width - 56)
            height: Math.min(560, root.height - 56)
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "LINK ANOTHER INSTALLATION"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true
                    text: "CLOSE"
                    onClicked: root.linkDialogOpen = false
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Choose only another installation of the same game. Omakade will keep every launch target."
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
            TextField {
                id: linkSearch
                property bool controllerNavigation: root.couchMode
                Layout.fillWidth: true
                placeholderText: "Search installed games"
                Accessible.name: placeholderText
                color: Theme.foreground
                font.family: Theme.fontFamily
                onTextChanged: root.linkResults = Library.linkCandidates(root.selectedIndex, text)
                Keys.onReturnPressed: function(event) {
                    root.handleCouchTextEntry(event, linkSearch,
                                              "SEARCH INSTALLATIONS", false,
                                              linkSearch.placeholderText)
                }
                Keys.onEnterPressed: function(event) {
                    root.handleCouchTextEntry(event, linkSearch,
                                              "SEARCH INSTALLATIONS", false,
                                              linkSearch.placeholderText)
                }
                Keys.onDownPressed: function(event) {
                    if (candidateList.count > 0) {
                        candidateList.currentIndex = 0
                        const candidate = candidateList.itemAtIndex(0)
                        if (candidate) {
                            candidate.forceActiveFocus(Qt.TabFocusReason)
                        }
                        event.accepted = true
                    }
                }
                background: Rectangle {
                    radius: Math.max(5, Theme.cornerRadius)
                    color: root.alpha(Theme.foreground, 0.05)
                    border.width: linkSearch.activeFocus ? 2 : 1
                    border.color: linkSearch.activeFocus
                                  ? Theme.accent : root.alpha(Theme.foreground, 0.18)
                }
            }
            ListView {
                id: candidateList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 7
                model: root.linkResults

                delegate: Button {
                    id: candidateDelegate
                    required property var modelData
                    required property int index
                    width: candidateList.width
                    height: 58
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "Link " + modelData.title + " from " + modelData.source
                    onClicked: root.linkCandidate(modelData)
                    Keys.onReturnPressed: function(event) {
                        root.linkCandidate(modelData)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        root.linkCandidate(modelData)
                        event.accepted = true
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            candidateList.currentIndex = index
                            candidateList.positionViewAtIndex(index, ListView.Contain)
                        }
                    }

                    contentItem: Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 13
                        spacing: 4
                        Text {
                            width: parent.width
                            text: modelData.title
                            textFormat: Text.PlainText
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            text: (modelData.source || "LOCAL").toUpperCase()
                                  + (modelData.runner ? "  ·  " + modelData.runner.toUpperCase() : "")
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                        }
                    }

                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: candidateDelegate.down || candidateDelegate.hovered
                               || candidateDelegate.activeFocus
                               ? root.alpha(Theme.foreground, 0.09)
                               : root.alpha(Theme.foreground, 0.04)
                        border.width: candidateDelegate.activeFocus ? 2 : 1
                        border.color: candidateDelegate.activeFocus
                                      ? Theme.accent
                                      : root.alpha(Theme.foreground, 0.14)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: candidateList.count === 0
                    text: "No matching installations"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
            }
        }
    }

    Rectangle {
        id: filterPickerOverlay
        objectName: "filterPickerOverlay"
        property var previousFocus: null
        anchors.fill: parent
        visible: root.filterPickerOpen
        z: 30
        Keys.onPressed: function(event) { root.handleArrowKey(filterPickerOverlay, event) }
        color: root.alpha(Theme.darkerBackground, 0.6)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() {
                    // Land on the current value so Enter keeps it and arrows move from it.
                    const current = root.filterPickerCurrent()
                    const index = current === "" ? 0 : root.filterPickerValues.indexOf(current) + 1
                    pickerList.currentIndex = Math.max(0, index)
                    pickerList.positionViewAtIndex(pickerList.currentIndex, ListView.Contain)
                    const item = pickerList.itemAtIndex(pickerList.currentIndex)
                    if (item) {
                        item.forceActiveFocus(Qt.TabFocusReason)
                    } else {
                        root.focusWithin(filterPickerOverlay, true)
                    }
                })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.filterPickerOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(380, root.width - 56)
            height: Math.min(pickerColumn.implicitHeight + 40, root.height - 56)
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                id: pickerColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.filterPickerKind === "status" ? "FILTER BY STATUS"
                            : root.filterPickerKind === "collection" ? "FILTER BY COLLECTION"
                            : "FILTER BY TAG"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true
                        text: "CLOSE"
                        onClicked: root.filterPickerOpen = false
                    }
                }
                ListView {
                    id: pickerList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, root.height - 160)
                    implicitHeight: Layout.preferredHeight
                    clip: true
                    spacing: 6
                    // The first row clears the filter; the rest are the available values.
                    model: [""].concat(root.filterPickerValues)
                    delegate: GlassButton {
                        required property string modelData
                        required property int index
                        width: pickerList.width
                        compact: true
                        selected: modelData === root.filterPickerCurrent()
                        text: modelData === ""
                              ? (root.filterPickerKind === "status" ? "ANY STATUS"
                                 : root.filterPickerKind === "collection" ? "ALL COLLECTIONS"
                                 : "ALL TAGS")
                              : modelData.toUpperCase()
                        onClicked: root.applyFilterPick(modelData)
                    }
                }
            }
        }
    }

    Rectangle {
        id: toast
        property string message: ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
        width: Math.min(toastText.implicitWidth + 34, parent.width - 48)
        height: 42
        // Above the settings panel and dialogs so confirmations stay readable.
        z: 40
        radius: Math.max(6, Theme.cornerRadius)
        color: root.alpha(Theme.background, 0.94)
        border.color: root.alpha(Theme.accent, 0.5)
        opacity: toastTimer.running ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 140 }
        }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: toast.width - 34
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: toast.message
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
    }

    Timer {
        id: toastTimer
        interval: 2400
    }

    Rectangle {
        id: settingsOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.diagnosticsOpen
        z: 20
        Keys.onPressed: function(event) { root.handleArrowKey(settingsOverlay, event) }
        color: root.alpha(Theme.darkerBackground, 0.72)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() { root.focusWithin(settingsOverlay, true) })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.diagnosticsOpen = false
        }

        Rectangle {
            id: settingsPanel
            anchors.centerIn: parent
            readonly property real layoutScale: root.couchMode
                                                    ? Math.max(1, Math.min(2,
                                                                          root.height / 1080))
                                                    : 1
            readonly property real uiScale: root.couchMode ? 1.25 * layoutScale : 1
            width: Math.min(root.couchMode ? 1280 * layoutScale : 610,
                            parent.width - (root.couchMode ? 96 : 48))
            height: Math.min(root.couchMode ? 900 * layoutScale : 760,
                             parent.height - (root.couchMode ? 72 : 48))
            radius: Math.max(root.couchMode ? 14 * layoutScale : 8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.2)

            MouseArea { anchors.fill: parent }

            ScrollView {
                id: settingsScroll
                objectName: "settingsScroll"
                readonly property real navigationContentY: contentItem ? contentItem.contentY : 0
                anchors.fill: parent
                anchors.margins: root.couchMode ? 42 * settingsPanel.layoutScale : 28
                anchors.bottomMargin: root.couchMode ? 70 * settingsPanel.layoutScale : 28
                rightPadding: 18
                contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 14

                Text {
                    text: "SETTINGS & SOURCES"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 20 * settingsPanel.uiScale
                    font.weight: Font.Bold
                }
                RowLayout {
                    GlassButton {
                        objectName: "addManualGameButton"
                        text: "ADD A GAME"
                        onClicked: root.editManualGame("")
                    }
                    GlassButton {
                        text: "MANUAL GAMES · " + ManualLibrary.count
                        onClicked: {
                            Library.sourceFilter = "Manual"
                            root.diagnosticsOpen = false
                            root.focusLibrary()
                        }
                    }
                }
                GlassButton {
                    objectName: "backupSettingsButton"
                    text: "BACKUP & RESTORE"
                    enabled: Backups.available
                    onClicked: root.openBackupEditor()
                }
                Text {
                    text: "GAME SOURCES"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: DemoMode ? [] : [
                        { name: "STEAM", enabled: Preferences.steamEnabled,
                          status: SteamLibrary ? SteamLibrary.statusText : "Unavailable",
                          error: SteamLibrary ? SteamLibrary.errorText : "",
                          paths: SteamLibrary ? SteamLibrary.detectedPaths : [],
                          lastScan: SteamLibrary ? SteamLibrary.lastScan : 0 },
                        { name: "BATTLE.NET", enabled: Preferences.battleNetEnabled,
                          status: BattleNetLibrary ? BattleNetLibrary.statusText : "Unavailable",
                          error: BattleNetLibrary ? BattleNetLibrary.errorText : "",
                          paths: BattleNetLibrary ? BattleNetLibrary.detectedPaths : [],
                          lastScan: BattleNetLibrary ? BattleNetLibrary.lastScan : 0 },
                        { name: "LUTRIS", enabled: Preferences.lutrisEnabled,
                          status: LutrisLibrary ? LutrisLibrary.statusText : "Unavailable",
                          error: LutrisLibrary ? LutrisLibrary.errorText : "",
                          paths: LutrisLibrary ? LutrisLibrary.detectedPaths : [],
                          lastScan: LutrisLibrary ? LutrisLibrary.lastScan : 0 },
                        { name: "HEROIC", enabled: Preferences.heroicEnabled,
                          status: HeroicLibrary ? HeroicLibrary.statusText : "Unavailable",
                          error: HeroicLibrary ? HeroicLibrary.errorText : "",
                          paths: HeroicLibrary ? HeroicLibrary.detectedPaths : [],
                          lastScan: HeroicLibrary ? HeroicLibrary.lastScan : 0 },
                        { name: "GOG", enabled: Preferences.gogEnabled,
                          status: HeroicLibrary ? HeroicLibrary.statusText : "Unavailable",
                          error: HeroicLibrary ? HeroicLibrary.errorText : "",
                          paths: HeroicLibrary ? HeroicLibrary.detectedPaths : [],
                          lastScan: HeroicLibrary ? HeroicLibrary.lastScan : 0 },
                        { name: "FAUGUS", enabled: Preferences.faugusEnabled,
                          status: FaugusLibrary ? FaugusLibrary.statusText : "Unavailable",
                          error: FaugusLibrary ? FaugusLibrary.errorText : "",
                          paths: FaugusLibrary ? FaugusLibrary.detectedPaths : [],
                          lastScan: FaugusLibrary ? FaugusLibrary.lastScan : 0 },
                        { name: "RETROARCH", enabled: Preferences.retroArchEnabled,
                          status: RetroArchLibrary ? RetroArchLibrary.statusText : "Unavailable",
                          error: RetroArchLibrary ? RetroArchLibrary.errorText : "",
                          paths: RetroArchLibrary ? RetroArchLibrary.detectedPaths : [],
                          lastScan: RetroArchLibrary ? RetroArchLibrary.lastScan : 0 },
                        { name: "PCSX2", enabled: Preferences.pcsx2Enabled,
                          status: Pcsx2Library ? Pcsx2Library.statusText : "Unavailable",
                          error: Pcsx2Library ? Pcsx2Library.errorText : "",
                          paths: Pcsx2Library ? Pcsx2Library.detectedPaths : [],
                          lastScan: Pcsx2Library ? Pcsx2Library.lastScan : 0 },
                        { name: "RYUJINX", enabled: Preferences.ryujinxEnabled,
                          status: RyujinxLibrary ? RyujinxLibrary.statusText : "Unavailable",
                          error: RyujinxLibrary ? RyujinxLibrary.errorText : "",
                          paths: RyujinxLibrary ? RyujinxLibrary.detectedPaths : [],
                          lastScan: RyujinxLibrary ? RyujinxLibrary.lastScan : 0 }
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: modelData.enabled ? Theme.accent : Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 11 * settingsPanel.uiScale
                                font.weight: Font.Bold
                            }
                            GlassButton {
                                compact: true
                                text: modelData.enabled ? "ENABLED" : "DISABLED"
                                selected: modelData.enabled
                                onClicked: {
                                    let nowEnabled = false
                                    if (modelData.name === "STEAM") {
                                        Preferences.steamEnabled = !Preferences.steamEnabled
                                        nowEnabled = Preferences.steamEnabled
                                        if (Preferences.steamEnabled) SteamLibrary.refresh()
                                    } else if (modelData.name === "BATTLE.NET") {
                                        Preferences.battleNetEnabled = !Preferences.battleNetEnabled
                                        nowEnabled = Preferences.battleNetEnabled
                                        if (Preferences.battleNetEnabled && BattleNetLibrary) BattleNetLibrary.refresh()
                                    } else if (modelData.name === "LUTRIS") {
                                        Preferences.lutrisEnabled = !Preferences.lutrisEnabled
                                        nowEnabled = Preferences.lutrisEnabled
                                        if (Preferences.lutrisEnabled) LutrisLibrary.refresh()
                                    } else if (modelData.name === "HEROIC") {
                                        Preferences.heroicEnabled = !Preferences.heroicEnabled
                                        nowEnabled = Preferences.heroicEnabled
                                        if (Preferences.heroicEnabled) HeroicLibrary.refresh()
                                    } else if (modelData.name === "GOG") {
                                        Preferences.gogEnabled = !Preferences.gogEnabled
                                        nowEnabled = Preferences.gogEnabled
                                        if (Preferences.gogEnabled) HeroicLibrary.refresh()
                                    } else if (modelData.name === "FAUGUS") {
                                        Preferences.faugusEnabled = !Preferences.faugusEnabled
                                        nowEnabled = Preferences.faugusEnabled
                                        if (Preferences.faugusEnabled) FaugusLibrary.refresh()
                                    } else if (modelData.name === "PCSX2") {
                                        Preferences.pcsx2Enabled = !Preferences.pcsx2Enabled
                                        nowEnabled = Preferences.pcsx2Enabled
                                        if (Preferences.pcsx2Enabled) Pcsx2Library.refresh()
                                    } else if (modelData.name === "RYUJINX") {
                                        Preferences.ryujinxEnabled = !Preferences.ryujinxEnabled
                                        nowEnabled = Preferences.ryujinxEnabled
                                        if (Preferences.ryujinxEnabled) RyujinxLibrary.refresh()
                                    } else {
                                        Preferences.retroArchEnabled = !Preferences.retroArchEnabled
                                        nowEnabled = Preferences.retroArchEnabled
                                        if (Preferences.retroArchEnabled) RetroArchLibrary.refresh()
                                    }
                                    if (!nowEnabled && Library.sourceFilter.toUpperCase() === modelData.name) {
                                        Library.sourceFilter = ""
                                    }
                                }
                            }
                            GlassButton {
                                compact: true
                                text: "RESCAN"
                                enabled: modelData.enabled
                                onClicked: {
                                    if (modelData.name === "STEAM") SteamLibrary.refresh()
                                    else if (modelData.name === "BATTLE.NET" && BattleNetLibrary) BattleNetLibrary.refresh()
                                    else if (modelData.name === "LUTRIS") LutrisLibrary.refresh()
                                    else if (modelData.name === "HEROIC") HeroicLibrary.refresh()
                                    else if (modelData.name === "GOG") HeroicLibrary.refresh()
                                    else if (modelData.name === "FAUGUS") FaugusLibrary.refresh()
                                    else if (modelData.name === "PCSX2") Pcsx2Library.refresh()
                                    else if (modelData.name === "RYUJINX") RyujinxLibrary.refresh()
                                    else RetroArchLibrary.refresh()
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.status + " · " + root.scanTime(modelData.lastScan)
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 10 * settingsPanel.uiScale
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.paths.length > 0
                            text: modelData.paths.join("\n")
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9 * settingsPanel.uiScale
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.enabled && modelData.error.length > 0
                            text: modelData.error
                            color: Theme.yellow
                            font.family: Theme.fontFamily
                            font.pixelSize: 9 * settingsPanel.uiScale
                            wrapMode: Text.Wrap
                        }
                    }
                }
                ColumnLayout {
                    objectName: "gogFoldersSection"
                    Layout.fillWidth: true
                    visible: !DemoMode || GogSettingsFixture
                    spacing: 8
                    Text {
                        text: "EXTRA GOG FOLDERS"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Standard folders are discovered automatically. Add a folder containing GOG installations. Removing it here never deletes game files."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: Preferences.gogLibraryPaths
                        ColumnLayout {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11 * settingsPanel.uiScale
                                    wrapMode: Text.WrapAnywhere
                                }
                                GlassButton {
                                    objectName: "gogRemoveFolder_" + index
                                    compact: true
                                    text: "REMOVE"
                                    Accessible.name: "Remove GOG folder " + modelData
                                    onClicked: root.removeGogLibraryFolder(modelData)
                                }
                            }
                            Text {
                                objectName: "gogFolderStatus_" + index
                                Layout.fillWidth: true
                                readonly property string scanState: HeroicLibrary
                                    ? HeroicLibrary.statusText + HeroicLibrary.errorText : ""
                                text: { scanState; return Preferences.gogLibraryPathStatus(modelData) }
                                color: Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 10 * settingsPanel.uiScale
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    TextField {
                        id: gogLibraryPathField
                        objectName: "gogLibraryPathField"
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        placeholderText: "/path/to/GOG games"
                        Accessible.name: "GOG library folder path"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.pixelSize: 13 * settingsPanel.uiScale
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, gogLibraryPathField, "GOG FOLDER", false,
                                                      gogLibraryPathField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, gogLibraryPathField, "GOG FOLDER", false,
                                                      gogLibraryPathField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: gogLibraryPathField.activeFocus ? 2 : 1
                            border.color: gogLibraryPathField.activeFocus ? Theme.accent : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    RowLayout {
                        GlassButton {
                            objectName: "gogAddFolderButton"
                            compact: true
                            text: "ADD FOLDER"
                            onClicked: {
                                if (Preferences.addGogLibraryPath(gogLibraryPathField.text)) {
                                    gogLibraryPathField.clear()
                                    root.showToast("GOG folder saved")
                                } else {
                                    root.showToast("Enter an absolute folder path. Up to 64 extra folders can be saved.")
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            visible: !root.couchMode
                            text: "BROWSE"
                            onClicked: gogFolderDialog.open()
                        }
                    }
                }
                Text {
                    visible: DemoMode
                    text: "Demo library"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * settingsPanel.uiScale
                }
                Repeater {
                    model: [
                        { label: "LIBRARY", value: libraryView.count + " visible games" },
                        { label: "LOCAL ARTWORK", value: SteamLibrary ? SteamLibrary.artworkCount + " covers" : "Procedural demo art" },
                        { label: "CONTROLLER", value: Controller.connected ? Controller.name : "Not connected" },
                        { label: "DATABASE", value: SteamLibrary ? SteamLibrary.databasePath : "Not used in demo mode" },
                        { label: "ACHIEVEMENT ART", value: (Achievements.cacheBytes / 1048576).toFixed(1) + " MB / " + Preferences.artworkCacheLimitMb + " MB" },
                        { label: "VERSION", value: AppVersion }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.preferredWidth: 130
                            text: modelData.label
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 10 * settingsPanel.uiScale
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * settingsPanel.uiScale
                            elide: Text.ElideMiddle
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "OPTIONAL STEAM CONNECTION"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: SteamAccount
                          ? SteamAccount.statusText
                          : "Local Steam data is used in demo mode."
                    color: SteamAccount && (SteamAccount.state === "invalid-key"
                                            || SteamAccount.state === "private"
                                            || SteamAccount.state === "rate-limited")
                           ? Theme.yellow : Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: SteamAccount !== null
                    TextField {
                        id: steamIdField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        placeholderText: "Steam ID (17 digits, starts with 7656119)"
                        Accessible.name: "Steam ID"
                        // Copy the saved value in instead of binding so a keyring lookup
                        // finishing mid-edit cannot overwrite what is being typed.
                        readonly property string savedText: SteamAccount ? SteamAccount.steamId : ""
                        onSavedTextChanged: if (!activeFocus) text = savedText
                        Component.onCompleted: text = savedText
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        inputMethodHints: Qt.ImhDigitsOnly
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, steamIdField, "STEAM ID", false,
                                                      steamIdField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, steamIdField, "STEAM ID", false,
                                                      steamIdField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: steamIdField.activeFocus ? 2 : 1
                            border.color: steamIdField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE ID"
                        onClicked: SteamAccount.setSteamId(steamIdField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: SteamAccount !== null && !SteamAccount.busy
                    TextField {
                        id: apiKeyField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        Accessible.name: "Steam Web API key"
                        placeholderText: SteamAccount && SteamAccount.hasApiKey
                                         ? "API key stored securely" : "Steam Web API key"
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, apiKeyField, "STEAM WEB API KEY", true,
                                                      apiKeyField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, apiKeyField, "STEAM WEB API KEY", true,
                                                      apiKeyField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: apiKeyField.activeFocus ? 2 : 1
                            border.color: apiKeyField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE KEY"
                        onClicked: {
                            SteamAccount.storeApiKey(apiKeyField.text)
                            apiKeyField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: SteamAccount ? SteamAccount.hasApiKey : false
                        text: "REMOVE"
                        onClicked: SteamAccount.removeApiKey()
                    }
                }
                GlassButton {
                    compact: true
                    text: "GET A KEY FROM STEAM"
                    onClicked: Qt.openUrlExternally("https://steamcommunity.com/dev/apikey")
                }
                RowLayout {
                    Layout.fillWidth: true
                    GlassButton {
                        compact: true
                        enabled: SteamAccount !== null && !SteamAccount.busy
                                 && SteamAccount.hasApiKey
                                 && SteamAccount.steamId.length > 0
                        text: SteamAccount && SteamAccount.busy
                              ? "SYNCING STEAM LIBRARY" : "SYNC OWNED STEAM LIBRARY"
                        onClicked: SteamAccount.refreshOwnedGames()
                    }
                    Text {
                        visible: SteamAccount && SteamAccount.ownedGameCount > 0
                        text: SteamAccount
                              ? SteamAccount.ownedGameCount + " OWNED GAMES CACHED" : ""
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9 * settingsPanel.uiScale
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    text: "OWNED LIBRARY SYNC REQUIRES PUBLIC STEAM GAME DETAILS"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 8 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "OPTIONAL RETROACHIEVEMENTS CONNECTION"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: RetroAchievements
                          ? RetroAchievements.statusText
                          : "RetroAchievements is unavailable in demo mode."
                    color: RetroAchievements && (RetroAchievements.state === "invalid-key"
                                                 || RetroAchievements.state === "unsupported"
                                                 || RetroAchievements.state === "rate-limited")
                           ? Theme.yellow : Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: RetroAchievements !== null && !RetroAchievements.busy
                    TextField {
                        id: retroAchievementsUsernameField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        placeholderText: "RetroAchievements username"
                        text: RetroAchievements ? RetroAchievements.username : ""
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, retroAchievementsUsernameField,
                                                      "RETROACHIEVEMENTS USERNAME", false,
                                                      retroAchievementsUsernameField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, retroAchievementsUsernameField,
                                                      "RETROACHIEVEMENTS USERNAME", false,
                                                      retroAchievementsUsernameField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: retroAchievementsUsernameField.activeFocus ? 2 : 1
                            border.color: retroAchievementsUsernameField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE USERNAME"
                        onClicked: RetroAchievements.setUsername(retroAchievementsUsernameField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: RetroAchievements !== null && !RetroAchievements.busy
                    TextField {
                        id: retroAchievementsKeyField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        placeholderText: RetroAchievements && RetroAchievements.hasApiKey
                                         ? "API key stored securely" : "RetroAchievements Web API key"
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, retroAchievementsKeyField,
                                                      "RETROACHIEVEMENTS API KEY", true,
                                                      retroAchievementsKeyField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, retroAchievementsKeyField,
                                                      "RETROACHIEVEMENTS API KEY", true,
                                                      retroAchievementsKeyField.placeholderText)
                        }
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: retroAchievementsKeyField.activeFocus ? 2 : 1
                            border.color: retroAchievementsKeyField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE KEY"
                        onClicked: {
                            RetroAchievements.storeApiKey(retroAchievementsKeyField.text)
                            retroAchievementsKeyField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: RetroAchievements ? RetroAchievements.hasApiKey : false
                        text: "REMOVE"
                        onClicked: RetroAchievements.removeApiKey()
                    }
                }
                GlassButton {
                    compact: true
                    text: "GET A KEY FROM RETROACHIEVEMENTS"
                    onClicked: Qt.openUrlExternally("https://retroachievements.org/settings")
                }
                Text {
                    Layout.fillWidth: true
                    text: "SUPPORTS NES, SNES, GENESIS, GAME BOY AND OTHER CARTRIDGE SYSTEMS FIRST; DISC-BASED SYSTEMS ARE NOT MATCHED YET"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 8 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "OPTIONAL GAME INSIGHTS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: Insights ? Insights.statusText : "IGDB is unavailable in demo mode."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "TWITCH SETUP · Create Application, not Extension · Redirect: http://localhost · Client type: Confidential · Manage → New Secret"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbClientIdField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        placeholderText: "Twitch developer client ID"
                        Accessible.name: placeholderText
                        readonly property string savedText: Insights ? Insights.clientId : ""
                        onSavedTextChanged: if (!activeFocus) text = savedText
                        Component.onCompleted: text = savedText
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, igdbClientIdField,
                                                      "TWITCH CLIENT ID", false,
                                                      igdbClientIdField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, igdbClientIdField,
                                                      "TWITCH CLIENT ID", false,
                                                      igdbClientIdField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: igdbClientIdField.activeFocus ? 2 : 1
                            border.color: igdbClientIdField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE ID"
                        onClicked: Insights.setClientId(igdbClientIdField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbSecretField
                        property bool controllerNavigation: root.couchMode
                        Layout.fillWidth: true
                        Accessible.name: "Twitch developer client secret"
                        placeholderText: Insights && Insights.hasClientSecret
                                         ? "Client secret stored securely" : "Twitch developer client secret"
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: function(event) {
                            root.handleCouchTextEntry(event, igdbSecretField,
                                                      "TWITCH CLIENT SECRET", true,
                                                      igdbSecretField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            root.handleCouchTextEntry(event, igdbSecretField,
                                                      "TWITCH CLIENT SECRET", true,
                                                      igdbSecretField.placeholderText)
                        }
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: igdbSecretField.activeFocus ? 2 : 1
                            border.color: igdbSecretField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE SECRET"
                        onClicked: {
                            Insights.storeClientSecret(igdbSecretField.text)
                            igdbSecretField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: Insights ? Insights.configured : false
                        text: "REMOVE"
                        onClicked: Insights.removeCredentials()
                    }
                }
                GlassButton {
                    compact: true
                    text: "OPEN TWITCH APPLICATIONS"
                    onClicked: Qt.openUrlExternally("https://dev.twitch.tv/console/apps")
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "STREAM WITH SUNSHINE AND MOONLIGHT"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: Sunshine ? Sunshine.statusText : "Sunshine export is unavailable in demo mode."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "Moonlight shows Sunshine's app list. Omakade can add itself next to Steam Big Picture and one app per installed game with its cover. Sunshine reads the list when it starts, so restart it after changes."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Sunshine !== null && Sunshine.detected
                    Text {
                        Layout.fillWidth: true
                        text: "OMAKADE IN MOONLIGHT"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * settingsPanel.uiScale
                    }
                    GlassButton {
                        objectName: "sunshineOmakadeButton"
                        compact: true
                        text: Preferences.sunshineOmakadeApp ? "ENABLED" : "DISABLED"
                        onClicked: Preferences.sunshineOmakadeApp = !Preferences.sunshineOmakadeApp
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Sunshine !== null && Sunshine.detected
                    Text {
                        Layout.fillWidth: true
                        text: Sunshine && Sunshine.exportedGames > 0
                              ? "ONE APP PER INSTALLED GAME · " + Sunshine.exportedGames + " EXPORTED"
                              : "ONE APP PER INSTALLED GAME"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * settingsPanel.uiScale
                    }
                    GlassButton {
                        objectName: "sunshineGamesButton"
                        compact: true
                        text: Preferences.sunshineGameApps ? "ENABLED" : "DISABLED"
                        onClicked: Preferences.sunshineGameApps = !Preferences.sunshineGameApps
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Sunshine !== null && Sunshine.detected
                    GlassButton {
                        compact: true
                        text: "UPDATE APP LIST"
                        enabled: Sunshine && !Sunshine.busy
                        onClicked: Sunshine.sync()
                    }
                    GlassButton {
                        compact: true
                        visible: Sunshine && Sunshine.restartNeeded && !Sunshine.streaming
                        enabled: Sunshine && !Sunshine.busy
                        text: "RESTART SUNSHINE"
                        onClicked: Sunshine.restartSunshine()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "LIBRARY COLLECTIONS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: Library.collectionNames.length === 0
                    text: "Create collections from a game's details."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                }
                Repeater {
                    model: Library.collectionNames
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * settingsPanel.uiScale
                            elide: Text.ElideRight
                        }
                        GlassButton {
                            compact: true
                            text: "DELETE"
                            onClicked: {
                                root.pendingCollectionDelete = modelData
                                root.collectionDeleteOpen = true
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.topMargin: 8
                    spacing: 8
                    GlassButton {
                        compact: true
                        text: "PROJECT"
                        onClicked: Qt.openUrlExternally("https://github.com/btsouth/omakade")
                    }
                    GlassButton {
                        compact: true
                        text: "REPORT ISSUE"
                        onClicked: Qt.openUrlExternally("https://github.com/btsouth/omakade/issues/new/choose")
                    }
                    Item { Layout.fillWidth: true }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columnSpacing: 8
                    rowSpacing: 8
                    columns: 3
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: Preferences.reducedMotion ? "MOTION OFF" : "MOTION ON"
                        selected: Preferences.reducedMotion
                        onClicked: Preferences.reducedMotion = !Preferences.reducedMotion
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "AUTO-CLOSE: " + (Preferences.closeAfterLaunch ? "ON" : "OFF")
                        selected: Preferences.closeAfterLaunch
                        onClicked: Preferences.closeAfterLaunch = !Preferences.closeAfterLaunch
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE -"
                        onClicked: Preferences.artworkCacheLimitMb -= 128
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE +"
                        onClicked: Preferences.artworkCacheLimitMb += 128
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CLEAR ART"
                        onClicked: Achievements.clearCache()
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        text: "CLOSE"
                        primary: true
                        onClicked: root.diagnosticsOpen = false
                    }
                }
            }
            }

            Text {
                visible: root.couchMode
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 42
                anchors.bottomMargin: 24
                text: Controller.primaryGlyph + "  SELECT     "
                      + Controller.backGlyph + "  CLOSE"
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 12 * settingsPanel.uiScale
                font.weight: Font.DemiBold
            }
        }
    }

    Rectangle {
        id: collectionDeleteOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.collectionDeleteOpen
        z: 35
        Keys.onPressed: function(event) { root.handleArrowKey(collectionDeleteOverlay, event) }
        color: root.alpha(Theme.darkerBackground, 0.76)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() {
                    root.focusWithin(collectionDeleteOverlay, true, collectionCancelButton)
                })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.collectionDeleteOpen = false
                root.pendingCollectionDelete = ""
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(460, parent.width - 48)
            height: 210
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                Text {
                    text: "DELETE COLLECTION?"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: "Remove “" + root.pendingCollectionDelete
                          + "” and its game memberships? This does not remove any games."
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    GlassButton {
                        id: collectionCancelButton
                        text: "CANCEL"
                        onClicked: {
                            root.collectionDeleteOpen = false
                            root.pendingCollectionDelete = ""
                        }
                    }
                    GlassButton {
                        text: "DELETE"
                        primary: true
                        onClicked: root.confirmCollectionDelete()
                    }
                }
            }
        }
    }

    CouchKeyboard {
        id: couchTextEntryKeyboard
        objectName: "couchTextEntryKeyboard"
        anchors.fill: parent
        visible: root.couchTextEntryOpen
        enabled: visible
        z: 100
        title: root.couchTextEntryTitle
        placeholder: root.couchTextEntryPlaceholder
        passwordMode: root.couchTextEntryPassword
        gridObjectName: "couchTextEntryGrid"
        onAccepted: root.closeCouchTextEntry(true)
        onCanceled: root.closeCouchTextEntry(false)
    }

    Component.onCompleted: {
        smokeReady = true
        root.focusLibrary()
    }

    Connections {
        target: SteamAccount
        enabled: SteamAccount !== null
        function onAchievementsUpdated(appId) {
            if (root.detailOpen && root.selectedInstallation.appId === appId) {
                root.selectedGame = Library.get(root.selectedIndex)
            }
        }
        function onOwnedGamesUpdated() {
            if (SteamAccount.ownedGameCount === 0) {
                Library.availability = 0
            }
            if (libraryView.currentIndex < 0 && Library.rowCount() > 0) {
                libraryView.currentIndex = 0
                couchLibraryView.currentIndex = 0
            }
            if (root.detailOpen
                    && !root.refreshSelected(root.selectedGame.source,
                                             root.selectedGame.runner || "",
                                             root.selectedGame.appId)) {
                root.closeDetails()
            }
        }
    }

    Connections {
        // Background rescans reset the library model. Re-resolve the open game by identity so
        // detail actions never land on whichever game now occupies the old row index.
        target: Library
        function onModelReset() {
            if (!root.detailOpen || !root.selectedGame || !root.selectedGame.appId) {
                return
            }
            if (!root.refreshSelected(root.selectedGame.source,
                                      root.selectedGame.runner || "",
                                      root.selectedGame.appId)) {
                root.closeDetails()
            }
        }
    }

    Connections {
        target: Controller
        function onControllerChanged() {
            if (Controller.connected && root.couchMode) {
                Qt.callLater(root.focusCurrentSurface)
            }
        }
        function onFocusDirectionRequested(key) {
            const container = root.navigationContainer()
            if (!root.couchMode && !container && !libraryView.gridFocused
                    && !root.focusSpatial(librarySurface, key)
                    && key === Qt.Key_Down) {
                libraryView.focusGrid()
            }
            if (container) {
                root.focusSpatial(container, key)
            }
        }
        function onToolbarRequested() {
            root.toggleLibraryControls()
        }
        function onFavoriteRequested() {
            if (root.detailOpen && !root.diagnosticsOpen && !root.linkDialogOpen
                    && !root.collectionDeleteOpen) {
                Library.toggleFavorite(root.selectedIndex)
                root.refreshAfterOrganization()
            } else if (root.couchMode && !root.detailOpen
                       && root.navigationContainer() === null
                       && !couchLibraryView.searchOpen
                       && !couchLibraryView.browseOpen
                       && couchLibraryView.currentIndex >= 0) {
                Library.toggleFavorite(couchLibraryView.currentIndex)
                couchLibraryView.refreshCurrentGame()
            } else if (!root.detailOpen && root.navigationContainer() === null
                       && libraryView.gridFocused && libraryView.currentIndex >= 0) {
                Library.toggleFavorite(libraryView.currentIndex)
            }
        }
    }
}
