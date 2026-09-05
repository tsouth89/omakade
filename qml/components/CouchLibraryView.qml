import QtQuick
import QtQuick.Layouts
import QtQuick.Window

FocusScope {
    id: root

    required property var libraryModel
    property string viewOverride: ""
    property bool scanning: false
    property int currentIndex: 0
    property bool updatingGameViews: false
    property var currentGame: ({})
    property var pendingCurrent: null
    property bool searchOpen: false
    property string searchInitial: ""
    property bool browseOpen: false
    readonly property var sourceOptions: [
        { label: "ALL SOURCES", value: "" },
        { label: "STEAM", value: "Steam", enabled: Preferences.steamEnabled },
        { label: "BATTLE.NET", value: "Battle.net", enabled: Preferences.battleNetEnabled },
        { label: "LUTRIS", value: "Lutris", enabled: Preferences.lutrisEnabled },
        { label: "HEROIC", value: "Heroic", enabled: Preferences.heroicEnabled },
        { label: "GOG", value: "GOG", enabled: Preferences.gogEnabled },
        { label: "FAUGUS", value: "Faugus", enabled: Preferences.faugusEnabled },
        { label: "RETROARCH", value: "RetroArch", enabled: Preferences.retroArchEnabled },
        { label: "PCSX2", value: "PCSX2", enabled: Preferences.pcsx2Enabled },
        { label: "RYUJINX", value: "Ryujinx", enabled: Preferences.ryujinxEnabled },
        { label: "MANUAL", value: "Manual", enabled: true }
    ].filter(function(option) { return option.enabled === undefined || option.enabled })
    readonly property bool detailView: (viewOverride.length > 0
                                        ? viewOverride : Preferences.couchLibraryView) !== "grid"
    readonly property bool gridFocused: root.activeGameView().activeFocus
    readonly property real uiScale: Math.max(0.68, Math.min(2.0,
                                                           Math.min(width / 1920,
                                                                    height / 1080)))

    signal gameActivated(int index)
    signal favoriteToggled(int index)
    signal organizeRequested()
    signal savedFiltersRequested()
    signal randomRequested()
    signal settingsRequested()
    signal desktopRequested()
    signal coverRequested(string source, string appId)

    Accessible.name: "Couch library"
    Accessible.role: Accessible.List

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function refreshCurrentGame() {
        if (libraryModel && currentIndex >= 0 && currentIndex < libraryModel.rowCount()) {
            currentGame = libraryModel.get(currentIndex)
        } else {
            currentGame = ({})
        }
    }

    function activeGameView() {
        return root.detailView ? gameStrip : gameGrid
    }

    function syncGameViews() {
        // Attaching a model initializes the view's index. Keep that temporary
        // index from replacing the selected game while switching layouts.
        const selectedIndex = root.currentIndex
        root.updatingGameViews = true
        gameStrip.model = root.detailView ? root.libraryModel : null
        gameGrid.model = root.detailView ? null : root.libraryModel
        const view = root.activeGameView()
        view.currentIndex = selectedIndex
        if (selectedIndex >= 0) {
            view.positionViewAtIndex(selectedIndex, GridView.Contain)
        }
        root.updatingGameViews = false
    }

    function focusGrid() {
        const view = root.activeGameView()
        if (view.count > 0) {
            view.forceActiveFocus(Qt.TabFocusReason)
        } else {
            settingsButton.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function toggleLibraryView() {
        Preferences.couchLibraryView = root.detailView ? "grid" : "detail"
        root.focusGrid()
    }

    function toggleControls() {
        if (searchOpen || browseOpen) {
            return
        }
        if (root.gridFocused) {
            if (root.detailView) {
                viewButton.forceActiveFocus(Qt.TabFocusReason)
            } else {
                layoutButton.forceActiveFocus(Qt.TabFocusReason)
            }
        } else {
            focusGrid()
        }
    }

    function selectMode(mode) {
        libraryModel.mode = mode
        currentIndex = libraryModel.rowCount() > 0 ? 0 : -1
        Qt.callLater(focusGrid)
    }

    function openSearch() {
        searchInitial = libraryModel.searchText
        couchKeyboard.value = searchInitial
        searchOpen = true
        Qt.callLater(couchKeyboard.focusKeyboard)
    }

    function openBrowse() {
        browseOpen = true
        Qt.callLater(couchBrowse.focusPanel)
    }

    function closeBrowse() {
        browseOpen = false
        currentIndex = libraryModel.rowCount() > 0
                       ? Math.max(0, Math.min(currentIndex,
                                             libraryModel.rowCount() - 1))
                       : -1
        refreshCurrentGame()
        Qt.callLater(function() {
            browseButton.forceActiveFocus(Qt.TabFocusReason)
        })
    }

    function closeSearch(accepted) {
        if (!accepted) {
            libraryModel.searchText = searchInitial
        }
        searchOpen = false
        currentIndex = libraryModel.rowCount() > 0 ? 0 : -1
        Qt.callLater(function() {
            if (accepted) {
                root.focusGrid()
            } else {
                searchButton.forceActiveFocus(Qt.TabFocusReason)
            }
        })
    }

    onCurrentIndexChanged: refreshCurrentGame()
    onLibraryModelChanged: {
        syncGameViews()
        refreshCurrentGame()
    }
    onDetailViewChanged: syncGameViews()

    Connections {
        target: root.libraryModel
        function onModelAboutToBeReset() {
            if (root.currentIndex >= 0 && root.currentIndex < root.libraryModel.rowCount()) {
                const game = root.libraryModel.get(root.currentIndex)
                root.pendingCurrent = { source: game.source, runner: game.runner || "",
                                        appId: game.appId }
            } else {
                root.pendingCurrent = null
            }
        }
        function onModelReset() {
            const needsInitialFocus = root.currentIndex < 0
            const pending = root.pendingCurrent
            root.pendingCurrent = null
            const matched = pending
                            ? root.libraryModel.indexOf(pending.source, pending.runner,
                                                        pending.appId)
                            : -1
            root.currentIndex = matched >= 0 ? matched
                                : root.libraryModel.rowCount() > 0
                                  ? Math.max(0, Math.min(root.currentIndex,
                                                        root.libraryModel.rowCount() - 1))
                                  : -1
            root.refreshCurrentGame()
            if (needsInitialFocus && root.currentIndex >= 0 && root.visible
                    && !root.searchOpen && !root.browseOpen) {
                Qt.callLater(root.focusGrid)
            }
        }
        function onRowsInserted() {
            const needsInitialFocus = root.currentIndex < 0
            if (needsInitialFocus && root.libraryModel.rowCount() > 0) {
                root.currentIndex = 0
            }
            root.refreshCurrentGame()
            if (needsInitialFocus && root.currentIndex >= 0 && root.visible
                    && !root.searchOpen && !root.browseOpen) {
                Qt.callLater(root.focusGrid)
            }
        }
        function onRowsRemoved() {
            root.currentIndex = root.libraryModel.rowCount() > 0
                                ? Math.max(0, Math.min(root.currentIndex,
                                                      root.libraryModel.rowCount() - 1))
                                : -1
            root.refreshCurrentGame()
        }
        function onDataChanged() { root.refreshCurrentGame() }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, Math.max(0.90, Theme.surfaceAlpha))
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.currentGame.accentStart
                       ? root.alpha(root.currentGame.accentStart, 0.38)
                       : root.alpha(Theme.accent, 0.24)
            }
            GradientStop {
                position: 0.52
                color: root.alpha(Theme.darkerBackground, 0.74)
            }
            GradientStop {
                position: 1
                color: root.alpha(Theme.darkerBackground, 0.96)
            }
        }
    }

    Image {
        id: heroArtwork
        anchors.fill: parent
        source: root.currentGame.heroPath || ""
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 128) * 128
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 128) * 128
        opacity: status === Image.Ready ? 0.58 : 0

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 220 }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: root.alpha(Theme.darkerBackground, 0.12) }
            GradientStop { position: 0.58; color: root.alpha(Theme.darkerBackground, 0.62) }
            GradientStop { position: 1; color: root.alpha(Theme.darkerBackground, 0.98) }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: root.alpha(Theme.darkerBackground, 0.94) }
            GradientStop { position: 0.58; color: root.alpha(Theme.darkerBackground, 0.34) }
            GradientStop { position: 1; color: root.alpha(Theme.darkerBackground, 0.12) }
        }
    }

    Rectangle {
        anchors.left: topBar.left
        anchors.right: topBar.right
        anchors.top: topBar.top
        anchors.bottom: topBar.bottom
        anchors.margins: -12 * root.uiScale
        radius: Math.max(12 * root.uiScale, Theme.cornerRadius * 2)
        color: root.alpha(Theme.background, Math.min(0.78, Theme.surfaceAlpha * 0.78))
        border.color: root.alpha(Theme.foreground, 0.12)
    }

    RowLayout {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 32 * root.uiScale
        anchors.leftMargin: 54 * root.uiScale
        anchors.rightMargin: 54 * root.uiScale
        spacing: 12 * root.uiScale

        Row {
            spacing: 12 * root.uiScale
            Layout.alignment: Qt.AlignVCenter

            Image {
                width: 42 * root.uiScale
                height: width
                source: "qrc:/icons/resources/icons/io.github.tsouth89.Omakade.svg"
                sourceSize: Qt.size(96, 96)
                Accessible.ignored: true
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Text {
                    text: "OMAKADE"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 18 * root.uiScale
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                }
                Text {
                    text: "COUCH MODE"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 9 * root.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.4
                }
            }
        }

        Item { Layout.fillWidth: true }

        Row {
            spacing: 7 * root.uiScale
            Layout.alignment: Qt.AlignVCenter

            GlassButton {
                id: allButton
                objectName: "couchAllButton"
                text: "ALL"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                selected: root.libraryModel.mode === 0
                onClicked: root.selectMode(0)
                KeyNavigation.right: favoritesButton
                KeyNavigation.down: root.detailView ? viewButton : gameGrid
            }
            GlassButton {
                id: favoritesButton
                objectName: "couchFavoritesFilterButton"
                text: "FAVORITES"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                selected: root.libraryModel.mode === 1
                onClicked: root.selectMode(1)
                KeyNavigation.left: allButton
                KeyNavigation.right: recentButton
                KeyNavigation.down: root.detailView ? viewButton : gameGrid
            }
            GlassButton {
                id: recentButton
                objectName: "couchRecentButton"
                text: "RECENT"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                selected: root.libraryModel.mode === 2
                onClicked: root.selectMode(2)
                KeyNavigation.left: favoritesButton
                KeyNavigation.right: layoutButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
            GlassButton {
                id: layoutButton
                objectName: "couchLayoutButton"
                text: root.detailView ? "VIEW · DETAIL" : "VIEW · GRID"
                iconText: root.detailView ? "▤" : "▦"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                selected: true
                onClicked: root.toggleLibraryView()
                KeyNavigation.left: recentButton
                KeyNavigation.right: browseButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
            GlassButton {
                id: browseButton
                objectName: "couchBrowseButton"
                text: "BROWSE"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                onClicked: root.openBrowse()
                KeyNavigation.left: layoutButton
                KeyNavigation.right: searchButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
            GlassButton {
                id: searchButton
                objectName: "couchSearchButton"
                text: root.libraryModel.searchText.length > 0
                      ? "SEARCH · "
                        + root.libraryModel.searchText.substring(0, 12).toUpperCase()
                        + (root.libraryModel.searchText.length > 12 ? "…" : "")
                      : "SEARCH"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                onClicked: root.openSearch()
                KeyNavigation.left: browseButton
                KeyNavigation.right: settingsButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
            GlassButton {
                id: settingsButton
                objectName: "couchSettingsButton"
                text: "SETTINGS"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                onClicked: root.settingsRequested()
                KeyNavigation.left: searchButton
                KeyNavigation.right: desktopButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
            GlassButton {
                id: desktopButton
                objectName: "couchDesktopButton"
                text: "DESKTOP"
                compact: true
                displayScale: Math.max(1, root.uiScale * 1.18)
                onClicked: root.desktopRequested()
                KeyNavigation.left: settingsButton
                KeyNavigation.down: root.detailView ? favoriteButton : gameGrid
            }
        }
    }

    Item {
        id: heroCoverFrame
        anchors.top: topBar.bottom
        anchors.topMargin: 44 * root.uiScale
        anchors.right: parent.right
        anchors.rightMargin: 116 * root.uiScale
        width: 330 * root.uiScale
        height: width * 1.5
        visible: root.detailView && gameStrip.count > 0 && root.width >= 1200

        Rectangle {
            anchors.fill: parent
            anchors.margins: 14 * root.uiScale
            rotation: 7
            radius: 18 * root.uiScale
            color: root.alpha(root.currentGame.accentEnd || Theme.accent, 0.18)
            border.color: root.alpha(Theme.brightForeground, 0.10)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 8 * root.uiScale
            rotation: -4
            radius: 18 * root.uiScale
            color: root.alpha(root.currentGame.accentStart || Theme.green, 0.24)
            border.color: root.alpha(Theme.brightForeground, 0.12)
        }

        Rectangle {
            id: featuredCover
            anchors.fill: parent
            radius: 16 * root.uiScale
            clip: true
            border.width: 2
            border.color: root.alpha(Theme.brightForeground, 0.22)
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.currentGame.accentStart || Theme.accent
                }
                GradientStop {
                    position: 1
                    color: root.currentGame.accentEnd || Theme.darkerBackground
                }
            }

            Image {
                anchors.fill: parent
                source: root.currentGame.coverPath || ""
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
            }

            Rectangle {
                anchors.fill: parent
                visible: !root.currentGame.coverPath
                color: "transparent"

                Rectangle {
                    width: parent.width * 0.9
                    height: width
                    radius: width / 2
                    x: parent.width * 0.44
                    y: -height * 0.2
                    color: root.alpha(Theme.brightForeground, 0.11)
                }
                Rectangle {
                    width: parent.width * 0.72
                    height: width
                    radius: width / 2
                    x: -width * 0.38
                    y: parent.height * 0.5
                    color: root.alpha(Theme.darkerBackground, 0.2)
                }
                Text {
                    anchors.centerIn: parent
                    text: root.currentGame.coverMark || "◇"
                    color: root.alpha(Theme.brightForeground, 0.88)
                    font.family: Theme.fontFamily
                    font.pixelSize: 92 * root.uiScale
                    font.weight: Font.Light
                }
            }
        }
    }

    Column {
        id: heroCopy
        anchors.left: parent.left
        anchors.leftMargin: 64 * root.uiScale
        anchors.bottom: gameStrip.top
        anchors.bottomMargin: 42 * root.uiScale
        width: Math.min(parent.width * 0.58, 920 * root.uiScale)
        spacing: 12 * root.uiScale
        visible: root.detailView && gameStrip.count > 0

        Text {
            width: parent.width
            text: ((root.currentIndex + 1) + " / " + root.libraryModel.rowCount()
                   + "  ·  " + (root.currentGame.source || "LIBRARY")
                   + (root.currentGame.year ? "  ·  " + root.currentGame.year : "")).toUpperCase()
            textFormat: Text.PlainText
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: 13 * root.uiScale
            font.weight: Font.Bold
            font.letterSpacing: 1.8
            elide: Text.ElideRight
        }

        Image {
            id: logoArtwork
            width: Math.min(parent.width * 0.72, 560 * root.uiScale)
            height: 120 * root.uiScale
            source: root.currentGame.logoPath || ""
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            visible: status === Image.Ready
            sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio))
        }

        Text {
            width: parent.width
            text: root.currentGame.title || ""
            textFormat: Text.PlainText
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: 50 * root.uiScale
            font.weight: Font.Bold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            visible: !logoArtwork.visible
        }

        Text {
            width: parent.width
            text: root.currentGame.description || root.currentGame.subtitle || ""
            textFormat: Text.PlainText
            color: root.alpha(Theme.brightForeground, 0.78)
            font.family: Theme.fontFamily
            font.pixelSize: 17 * root.uiScale
            lineHeight: 1.22
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Row {
            spacing: 10 * root.uiScale

            GlassButton {
                id: viewButton
                objectName: "couchViewButton"
                text: "VIEW GAME"
                iconText: "▶"
                primary: true
                displayScale: Math.max(1, root.uiScale * 1.2)
                enabled: root.currentIndex >= 0
                onClicked: root.gameActivated(root.currentIndex)
                KeyNavigation.up: allButton
                KeyNavigation.right: favoriteButton
                KeyNavigation.down: gameStrip
            }
            GlassButton {
                id: favoriteButton
                objectName: "couchFavoriteButton"
                text: root.currentGame.favorite ? "FAVORITED" : "FAVORITE"
                iconText: root.currentGame.favorite ? "♥" : "♡"
                displayScale: Math.max(1, root.uiScale * 1.2)
                enabled: root.currentIndex >= 0
                onClicked: root.favoriteToggled(root.currentIndex)
                KeyNavigation.up: settingsButton
                KeyNavigation.left: viewButton
                KeyNavigation.down: gameStrip
            }
        }
    }

    Column {
        objectName: "couchEmptyState"
        anchors.centerIn: parent
        spacing: 14 * root.uiScale
        visible: root.activeGameView().count === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.scanning ? "SCANNING YOUR LIBRARY" : "NO GAMES HERE"
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: 26 * root.uiScale
            font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.scanning ? "Looking for installed games and artwork."
                                : "Change the library view or rescan from Settings."
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 15 * root.uiScale
        }
    }

    ListView {
        id: gameStrip
        objectName: "couchGameStrip"
        visible: root.detailView
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: hintBar.top
        anchors.leftMargin: 52 * root.uiScale
        anchors.rightMargin: 52 * root.uiScale
        anchors.bottomMargin: 22 * root.uiScale
        height: 260 * root.uiScale
        orientation: ListView.Horizontal
        spacing: 14 * root.uiScale
        clip: true
        cacheBuffer: Math.max(0, width)
        model: null
        currentIndex: root.currentIndex
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: true
        highlight: Item {
            Rectangle {
                anchors.top: parent.top
                anchors.topMargin: root.uiScale
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                height: 233 * root.uiScale
                radius: 16 * root.uiScale
                color: root.alpha(Theme.accent, 0.18)
                border.width: 2 * root.uiScale
                border.color: root.alpha(Theme.accent, 0.72)
            }
        }
        highlightMoveDuration: Preferences.reducedMotion ? 0 : 90
        highlightResizeDuration: Preferences.reducedMotion ? 0 : 90
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: width * 0.08
        preferredHighlightEnd: width * 0.72
        boundsBehavior: Flickable.StopAtBounds

        onCurrentIndexChanged: {
            if (visible && !root.updatingGameViews) {
                root.currentIndex = currentIndex
            }
        }

        Keys.onUpPressed: function(event) {
            viewButton.forceActiveFocus(Qt.TabFocusReason)
            event.accepted = true
        }
        Keys.onReturnPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
            }
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
            }
            event.accepted = true
        }

        delegate: FocusScope {
            id: card
            required property int index
            required property string title
            required property string subtitle
            required property string coverPath
            required property string coverMark
            required property string source
            required property string appId
            required property bool favorite
            required property color accentStart
            required property color accentEnd

            width: 160 * root.uiScale
            height: gameStrip.height
            z: 1
            Accessible.name: title
            Accessible.role: Accessible.ListItem

            Component.onCompleted: {
                if (coverPath.length === 0) {
                    root.coverRequested(source, appId)
                }
            }

            Rectangle {
                id: cover
                anchors.top: parent.top
                anchors.topMargin: 8 * root.uiScale
                anchors.horizontalCenter: parent.horizontalCenter
                width: 146 * root.uiScale
                height: width * 1.5
                radius: 10 * root.uiScale
                clip: true
                border.width: gameStrip.currentIndex === card.index ? 5 : 1
                border.color: gameStrip.currentIndex === card.index
                              ? Theme.brightForeground : root.alpha(Theme.foreground, 0.16)
                gradient: Gradient {
                    GradientStop { position: 0; color: card.accentStart }
                    GradientStop { position: 1; color: card.accentEnd }
                }

                Image {
                    anchors.fill: parent
                    source: card.coverPath
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                }

                Text {
                    anchors.centerIn: parent
                    visible: card.coverPath.length === 0
                    text: card.coverMark
                    color: root.alpha(Theme.brightForeground, 0.86)
                    font.family: Theme.fontFamily
                    font.pixelSize: 42 * root.uiScale
                }

                Rectangle {
                    visible: card.favorite
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8 * root.uiScale
                    width: 28 * root.uiScale
                    height: width
                    radius: width / 2
                    color: root.alpha(Theme.darkerBackground, 0.72)

                    Text {
                        anchors.centerIn: parent
                        text: "♥"
                        color: Theme.brightForeground
                        font.pixelSize: 12 * root.uiScale
                    }
                }

                Rectangle {
                    visible: gameStrip.currentIndex === card.index
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 8 * root.uiScale
                    color: Theme.accent
                }
            }

            Text {
                anchors.top: cover.bottom
                anchors.topMargin: 9 * root.uiScale
                width: parent.width
                text: card.title
                textFormat: Text.PlainText
                color: gameStrip.currentIndex === card.index
                       ? Theme.brightForeground : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 14 * root.uiScale
                font.weight: gameStrip.currentIndex === card.index ? Font.Bold : Font.Medium
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    gameStrip.currentIndex = card.index
                    gameStrip.forceActiveFocus(Qt.MouseFocusReason)
                }
                onDoubleClicked: root.gameActivated(card.index)
            }

            opacity: gameStrip.currentIndex === card.index ? 1 : 0.58
            Behavior on opacity {
                enabled: !Preferences.reducedMotion
                NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
            }
        }
    }

    Rectangle {
        anchors.fill: gameGrid
        anchors.margins: -16 * root.uiScale
        visible: gameGrid.visible
        radius: Math.max(16 * root.uiScale, Theme.cornerRadius * 2)
        color: root.alpha(Theme.background, Math.min(0.64, Theme.surfaceAlpha * 0.64))
        border.color: root.alpha(Theme.foreground, 0.11)
    }

    GridView {
        id: gameGrid
        objectName: "couchGameGrid"
        visible: !root.detailView
        anchors.top: topBar.bottom
        anchors.bottom: hintBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 38 * root.uiScale
        anchors.bottomMargin: 24 * root.uiScale
        anchors.leftMargin: 58 * root.uiScale
        anchors.rightMargin: 58 * root.uiScale
        cellWidth: width / columnCount
        cellHeight: 350 * root.uiScale
        readonly property int columnCount: Math.max(1, Math.floor(width / (212 * root.uiScale)))
        model: null
        currentIndex: root.currentIndex
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: true
        highlight: Item {}
        highlightMoveDuration: Preferences.reducedMotion ? 0 : 90
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        cacheBuffer: Math.max(0, height)

        onCurrentIndexChanged: {
            if (visible && !root.updatingGameViews) {
                root.currentIndex = currentIndex
            }
        }

        Keys.onUpPressed: function(event) {
            if (currentIndex >= 0 && currentIndex < columnCount) {
                allButton.forceActiveFocus(Qt.TabFocusReason)
                event.accepted = true
            } else {
                event.accepted = false
            }
        }

        Keys.onReturnPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
            }
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
            }
            event.accepted = true
        }

        delegate: Item {
            id: gridCard
            required property int index
            required property string title
            required property string subtitle
            required property string coverPath
            required property string coverMark
            required property string source
            required property string appId
            required property bool favorite
            required property color accentStart
            required property color accentEnd
            readonly property bool current: gameGrid.currentIndex === index

            width: 196 * root.uiScale
            height: 330 * root.uiScale
            transform: Translate { x: (gameGrid.cellWidth - gridCard.width) / 2 }
            transformOrigin: Item.TopLeft
            scale: current ? 1.025 : 1
            z: current ? 2 : 1
            Accessible.name: title
            Accessible.role: Accessible.ListItem

            Behavior on scale {
                enabled: !Preferences.reducedMotion
                NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
            }

            Component.onCompleted: {
                if (coverPath.length === 0) {
                    root.coverRequested(source, appId)
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Math.max(12 * root.uiScale, Theme.cornerRadius * 1.5)
                color: root.alpha(Theme.background, gridCard.current ? 0.90 : 0.58)
                border.width: gridCard.current ? 4 * root.uiScale : 1
                border.color: gridCard.current
                              ? Theme.accent : root.alpha(Theme.foreground, 0.14)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: gridCard.current ? 7 * root.uiScale : 0
                    radius: Math.max(8 * root.uiScale, Theme.cornerRadius)
                    visible: gridCard.current
                    color: "transparent"
                    border.width: 1
                    border.color: root.alpha(Theme.brightForeground, 0.46)
                }
            }

            Rectangle {
                id: gridCover
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 10 * root.uiScale
                height: 258 * root.uiScale
                radius: Math.max(8 * root.uiScale, Theme.cornerRadius)
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0; color: gridCard.accentStart }
                    GradientStop { position: 1; color: gridCard.accentEnd }
                }

                Image {
                    anchors.fill: parent
                    source: gridCard.coverPath
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                }

                Text {
                    anchors.centerIn: parent
                    visible: gridCard.coverPath.length === 0
                    text: gridCard.coverMark
                    color: root.alpha(Theme.brightForeground, 0.88)
                    font.family: Theme.fontFamily
                    font.pixelSize: 48 * root.uiScale
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: gridCard.current ? 10 * root.uiScale : 4 * root.uiScale
                    color: gridCard.current ? Theme.accent
                                            : root.alpha(Theme.brightForeground, 0.20)
                }

                Rectangle {
                    visible: gridCard.favorite
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 10 * root.uiScale
                    width: 32 * root.uiScale
                    height: width
                    radius: width / 2
                    color: root.alpha(Theme.darkerBackground, 0.82)
                    border.color: root.alpha(Theme.brightForeground, 0.28)

                    Text {
                        anchors.centerIn: parent
                        text: "♥"
                        color: Theme.brightForeground
                        font.pixelSize: 14 * root.uiScale
                    }
                }

                Rectangle {
                    visible: gridCard.current
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 10 * root.uiScale
                    width: selectedText.implicitWidth + 16 * root.uiScale
                    height: 28 * root.uiScale
                    radius: height / 2
                    color: Theme.accent

                    Text {
                        id: selectedText
                        anchors.centerIn: parent
                        text: "SELECTED"
                        color: Theme.darkerBackground
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * root.uiScale
                        font.weight: Font.Bold
                        font.letterSpacing: 0.8
                    }
                }
            }

            Text {
                anchors.top: gridCover.bottom
                anchors.topMargin: 10 * root.uiScale
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 11 * root.uiScale
                anchors.rightMargin: 11 * root.uiScale
                text: gridCard.title
                textFormat: Text.PlainText
                color: gridCard.current ? Theme.brightForeground : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 16 * root.uiScale
                font.weight: gridCard.current ? Font.Bold : Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 11 * root.uiScale
                anchors.rightMargin: 11 * root.uiScale
                anchors.bottomMargin: 8 * root.uiScale
                text: (gridCard.source || gridCard.subtitle || "LIBRARY").toUpperCase()
                textFormat: Text.PlainText
                color: gridCard.current ? Theme.accent : Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 10 * root.uiScale
                font.weight: Font.DemiBold
                font.letterSpacing: 0.8
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    gameGrid.currentIndex = gridCard.index
                    gameGrid.forceActiveFocus(Qt.MouseFocusReason)
                }
                onDoubleClicked: root.gameActivated(gridCard.index)
            }

            opacity: gridCard.current ? 1 : 0.72
            Behavior on opacity {
                enabled: !Preferences.reducedMotion
                NumberAnimation { duration: 100 }
            }
        }
    }

    Row {
        id: hintBar
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 54 * root.uiScale
        anchors.bottomMargin: 22 * root.uiScale
        spacing: 22 * root.uiScale

        Repeater {
            model: [
                { glyph: Controller.primaryGlyph, label: "OPEN" },
                { glyph: Controller.favoriteGlyph, label: "FAVORITE" },
                { glyph: Controller.toolbarGlyph, label: "CONTROLS" },
                { glyph: "START", label: "DESKTOP" }
            ]

            Row {
                required property var modelData
                spacing: 7 * root.uiScale

                Rectangle {
                    width: Math.max(31 * root.uiScale, glyphText.implicitWidth + 14 * root.uiScale)
                    height: 31 * root.uiScale
                    radius: height / 2
                    color: root.alpha(Theme.foreground, 0.12)
                    border.color: root.alpha(Theme.foreground, 0.22)

                    Text {
                        id: glyphText
                        anchors.centerIn: parent
                        text: modelData.glyph
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.uiScale
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
            }
        }
    }

    CouchKeyboard {
        id: couchKeyboard
        objectName: "couchKeyboard"
        anchors.fill: parent
        visible: root.searchOpen
        enabled: visible
        z: 50

        onValueEdited: function(value) {
            root.libraryModel.searchText = value
            root.currentIndex = root.libraryModel.rowCount() > 0 ? 0 : -1
        }
        onAccepted: function(value) {
            root.libraryModel.searchText = value
            root.closeSearch(true)
        }
        onCanceled: root.closeSearch(false)
    }

    CouchBrowsePanel {
        id: couchBrowse
        objectName: "couchBrowsePanel"
        anchors.fill: parent
        visible: root.browseOpen
        enabled: visible
        z: 50
        libraryModel: root.libraryModel
        sourceOptions: root.sourceOptions

        onFiltersChanged: {
            root.currentIndex = root.libraryModel.rowCount() > 0 ? 0 : -1
            root.refreshCurrentGame()
        }
        onOrganizeRequested: { root.closeBrowse(); root.organizeRequested() }
        onSavedFiltersRequested: { root.closeBrowse(); root.savedFiltersRequested() }
        onRandomRequested: { root.closeBrowse(); root.randomRequested() }
        onClosed: root.closeBrowse()
    }

    Component.onCompleted: {
        currentIndex = libraryModel.rowCount() > 0 ? 0 : -1
        syncGameViews()
        refreshCurrentGame()
        Qt.callLater(focusGrid)
    }
}
