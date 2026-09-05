import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../components"

Item {
    id: root
    objectName: "gameDetails"

    Accessible.name: (game.title || "Game") + " details"
    Accessible.role: Accessible.Pane

    required property var game
    required property var installations
    required property var selectedInstallation
    property bool collectionEditorOpen: false
    property bool couchMode: false
    readonly property real uiScale: couchMode
                                    ? Math.max(1, Math.min(2.4,
                                                          Math.min(width / 1920,
                                                                   height / 1080) * 1.18))
                                    : 1

    // Closing the editor hides the focused field, so hand focus back to the button that
    // opened it and drop the draft instead of showing it again next time.
    function closeCollectionEditor() {
        collectionEditorOpen = false
        collectionField.clear()
        newCollectionButton.forceActiveFocus()
    }
    property bool navigationEnabled: true
    readonly property bool achievementSourceIsRetroArch: selectedInstallation.source === "RetroArch"
    readonly property var achievementAccount: achievementSourceIsRetroArch ? RetroAchievements : SteamAccount
    property bool randomSelection: false
    signal randomRequested()
    signal backRequested()
    signal favoriteRequested()
    signal playRequested()
    signal manageRequested()
    signal hiddenRequested()
    signal connectRequested()
    signal coverRequested()
    signal coverResetRequested()
    signal installationSelected(var installation)
    signal preferredInstallationRequested()
    signal manualEditRequested()
    signal linkRequested()
    signal unlinkRequested()
    signal completionStatusRequested(string status)
    signal tagsRequested(string tags)
    signal collectionToggled(string name, bool included)
    signal collectionCreateRequested(string name)
    signal textEntryRequested(var target, string title, bool password, string placeholder)

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function revealFocusedItem(item) {
        const flickable = detailsScroll.navigationFlickable
        if (!item || !flickable) {
            return
        }
        let ancestor = item
        while (ancestor) {
            if (ancestor === coverSidebar || ancestor === backButton) {
                return
            }
            ancestor = ancestor.parent
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

    Keys.onPressed: function(event) {
        if (root.navigationEnabled && event.key === Qt.Key_F) {
            root.favoriteRequested()
            event.accepted = true
        }
    }

    Connections {
        target: root.Window.window
        enabled: root.navigationEnabled && target !== null
        function onActiveFocusItemChanged() {
            Qt.callLater(function() {
                const window = root.Window.window
                if (window) {
                    root.revealFocusedItem(window.activeFocusItem)
                }
            })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, root.couchMode ? 0.88 : 0.76)
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.couchMode ? parent.height * 0.68
                               : Math.min(parent.height * 0.58, 500)
        opacity: 0.42
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: root.game.accentStart || Theme.accent }
            GradientStop { position: 1.0; color: root.game.accentEnd || Theme.blue }
        }
    }

    Image {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.couchMode ? parent.height * 0.68
                               : Math.min(parent.height * 0.58, 500)
        source: root.game.heroPath || ""
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectCrop
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        opacity: status === Image.Ready ? 0.48 : 0
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.couchMode ? parent.height * 0.74
                               : Math.min(parent.height * 0.62, 540)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.darkerBackground }
        }
    }

    GlassButton {
        id: backButton
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: root.couchMode ? 42 * root.uiScale : 24
        text: "BACK"
        iconText: "←"
        compact: true
        onClicked: root.backRequested()
    }

    Item {
        id: detailsArea
        anchors.fill: parent
        anchors.topMargin: root.couchMode ? 112 * root.uiScale : 80
        anchors.leftMargin: root.couchMode ? 64 * root.uiScale
                                           : Math.max(28, parent.width * 0.055)
        anchors.rightMargin: root.couchMode ? 64 * root.uiScale
                                            : Math.max(28, parent.width * 0.055)
        anchors.bottomMargin: root.couchMode ? 64 * root.uiScale : 22
        readonly property real columnSpacing: Math.max(28, width * 0.045)

        ColumnLayout {
            id: coverSidebar
            anchors.top: parent.top
            anchors.left: parent.left
            width: Math.max(0, Math.min(root.width * (root.couchMode ? 0.24 : 0.28),
                                        (detailsArea.height - reservedControlHeight) / 1.5,
                                        detailsArea.width * 0.44))
            spacing: 8
            readonly property real reservedControlHeight:
                (coverActions.visible ? coverActions.implicitHeight + spacing : 0)
                + (linkActions.visible ? linkActions.implicitHeight + spacing : 0)

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width * 1.5
                radius: Math.max(6, Theme.cornerRadius)
                clip: true
                border.color: root.alpha(Theme.foreground, 0.22)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: root.game.accentStart || Theme.accent }
                    GradientStop { position: 1.0; color: root.game.accentEnd || Theme.blue }
                }

                Image {
                    id: coverArtwork
                    anchors.fill: parent
                    source: root.game.coverPath || ""
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    opacity: status === Image.Ready ? 1 : 0
                }

                Rectangle {
                    visible: coverArtwork.status !== Image.Ready
                    width: parent.width * 0.95
                    height: width
                    radius: width / 2
                    x: parent.width * 0.44
                    y: -height * 0.18
                    color: root.alpha(Theme.brightForeground, 0.10)
                }

                Text {
                    visible: coverArtwork.status !== Image.Ready
                    anchors.centerIn: parent
                    text: root.game.coverMark || "◇"
                    color: root.alpha(Theme.brightForeground, 0.9)
                    font.family: Theme.fontFamily
                    font.pixelSize: Math.max(74, parent.width * 0.34)
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * 0.34
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: root.alpha(Theme.darkerBackground, 0.84) }
                    }
                }
            }

            GlassButton {
                objectName: "pickAnotherButton"
                visible: root.randomSelection
                Layout.fillWidth: true
                displayScale: root.uiScale
                text: "PICK ANOTHER"
                onClicked: root.randomRequested()
            }
            RowLayout {
                id: coverActions
                Layout.fillWidth: true
                visible: !DemoMode
                spacing: 8
                GlassButton {
                    Layout.fillWidth: true
                    compact: true
                    text: "ARTWORK"
                    onClicked: root.coverRequested()
                }
                GlassButton {
                    visible: root.game.customCover || false
                    compact: true
                    text: "RESET"
                    onClicked: root.coverResetRequested()
                }
            }

            RowLayout {
                id: linkActions
                Layout.fillWidth: true
                visible: !DemoMode
                spacing: 8
                GlassButton {
                    Layout.fillWidth: true
                    compact: true
                    text: root.game.linked ? "UNLINK INSTALLATIONS" : "LINK INSTALLATION"
                    onClicked: root.game.linked ? root.unlinkRequested() : root.linkRequested()
                }
            }
        }

        ScrollView {
            id: detailsScroll
            objectName: "detailsScroll"
            readonly property var navigationFlickable: contentItem
            readonly property real navigationContentY: contentItem ? contentItem.contentY : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: coverSidebar.right
            anchors.leftMargin: detailsArea.columnSpacing
            anchors.right: parent.right
            rightPadding: 18
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: detailsContent
                width: detailsScroll.availableWidth
                spacing: root.couchMode ? 20 * root.uiScale : 16

                Image {
                    id: gameLogo
                    objectName: "gameDetailsLogo"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.couchMode ? 110 * root.uiScale : 90
                    visible: status === Image.Ready
                    source: root.game.logoPath || ""
                    sourceSize.width: 1200
                    sourceSize.height: 360
                    asynchronous: true
                    autoTransform: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignLeft
                }
                Text {
                    Layout.fillWidth: true
                    visible: gameLogo.status !== Image.Ready
                    text: root.game.title || "Unknown game"
                    textFormat: Text.PlainText
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: root.couchMode
                                    ? Math.max(42, Math.min(68, width * 0.075)) * root.uiScale
                                    : Math.max(28, Math.min(54, width * 0.07))
                    font.weight: Font.Bold
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    spacing: 10
                    Text {
                        text: (root.game.linked
                               ? root.game.linkedSources
                               : (root.game.subtitle || "GAME")).toUpperCase()
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        visible: root.game.year > 0
                        text: "·"
                        color: root.alpha(Theme.foreground, 0.4)
                    }
                    Text {
                        visible: root.game.year > 0
                        text: root.game.year || ""
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    spacing: 8
                    visible: !DemoMode

                    GlassButton {
                        visible: root.selectedInstallation.source === "Steam"
                        compact: true
                        text: "PROTONDB"
                        onClicked: Qt.openUrlExternally(
                            "https://www.protondb.com/app/" + root.selectedInstallation.appId)
                    }

                    GlassButton {
                        compact: true
                        text: "PCGAMINGWIKI"
                        onClicked: Qt.openUrlExternally(
                            "https://www.pcgamingwiki.com/w/index.php?search="
                            + encodeURIComponent(root.game.title || ""))
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 720
                    text: root.game.description || ""
                    color: Theme.foreground
                    opacity: 0.84
                    font.family: Theme.fontFamily
                    font.pixelSize: root.couchMode ? 17 * root.uiScale : 13
                    lineHeight: 1.45
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.installations.length > 1
                    spacing: 7
                    Text {
                        text: "LAUNCH WITH"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Math.max(1, Math.floor(detailsContent.width / 160))
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: root.installations
                            GlassButton {
                                required property var modelData
                                required property int index
                                objectName: "installationChoice_" + index
                                compact: true
                                text: (modelData.source || "LOCAL").toUpperCase()
                                      + (modelData.runner ? " · " + modelData.runner.toUpperCase() : "")
                                      + (modelData.preferred ? " · DEFAULT" : "")
                                selected: root.selectedInstallation.source === modelData.source
                                          && (root.selectedInstallation.runner || "") === (modelData.runner || "")
                                          && root.selectedInstallation.appId === modelData.appId
                                onClicked: root.installationSelected(modelData)
                            }
                        }
                    }
                }

                GlassButton {
                    objectName: "editManualGameButton"
                    visible: root.selectedInstallation.source === "Manual"
                    text: "EDIT MANUAL GAME"
                    compact: true
                    onClicked: root.manualEditRequested()
                }
                GlassButton {
                    objectName: "preferredInstallationButton"
                    visible: root.installations.length > 1
                    compact: true
                    text: root.selectedInstallation.preferred ? "DEFAULT INSTALLATION" : "MAKE DEFAULT"
                    enabled: !root.selectedInstallation.preferred
                    onClicked: root.preferredInstallationRequested()
                }
                Text {
                    objectName: "preferredUnavailableText"
                    Layout.fillWidth: true
                    visible: root.selectedInstallation.preferredUnavailable === true
                    text: "Your default installation is unavailable. Choose another installation or reconnect its drive."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 16 : 11) * root.uiScale
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    id: gameActions
                    objectName: "gameActions"
                    Layout.fillWidth: true
                    columns: detailsContent.width < 620 ? 2 : 4
                    columnSpacing: 10
                    rowSpacing: 8

                    GlassButton {
                        id: playButton
                        objectName: "playButton"
                        property Item controllerRightTarget: favoriteButton
                        property Item controllerDownTarget:
                            gameActions.columns === 2 ? manageButton : null
                        text: root.selectedInstallation.installed === false
                              ? "INSTALL IN STEAM" : "PLAY"
                        iconText: root.selectedInstallation.installed === false ? "↓" : "▶"
                        primary: true
                        onClicked: root.playRequested()
                        Component.onCompleted: forceActiveFocus()
                    }

                    GlassButton {
                        id: favoriteButton
                        objectName: "favoriteButton"
                        property Item controllerLeftTarget: playButton
                        property Item controllerRightTarget:
                            gameActions.columns === 4 ? manageButton : null
                        property Item controllerDownTarget:
                            gameActions.columns === 2 ? hideButton : null
                        text: root.game.favorite ? "FAVORITE" : "ADD FAVORITE"
                        iconText: root.game.favorite ? "♥" : "♡"
                        onClicked: root.favoriteRequested()
                    }

                    GlassButton {
                        id: manageButton
                        objectName: "manageButton"
                        property Item controllerLeftTarget:
                            gameActions.columns === 4 ? favoriteButton : null
                        property Item controllerRightTarget: hideButton
                        property Item controllerUpTarget:
                            gameActions.columns === 2 ? playButton : null
                        visible: root.selectedInstallation.source === "Steam"
                                 || root.selectedInstallation.source === "Lutris"
                                 || root.selectedInstallation.source === "Heroic"
                                 || root.selectedInstallation.source === "GOG"
                                 || root.selectedInstallation.source === "Faugus"
                                 || root.selectedInstallation.source === "RetroArch"
                                 || root.selectedInstallation.source === "PCSX2"
                                 || root.selectedInstallation.source === "Ryujinx"
                                 || root.selectedInstallation.source === "Battle.net"
                        text: "MANAGE IN " + (root.selectedInstallation.source || "LAUNCHER").toUpperCase()
                        onClicked: root.manageRequested()
                    }

                    GlassButton {
                        id: hideButton
                        objectName: "hideButton"
                        property Item controllerLeftTarget: manageButton
                        property Item controllerUpTarget:
                            gameActions.columns === 2 ? favoriteButton : null
                        text: root.game.hidden ? "UNHIDE" : "HIDE"
                        onClicked: root.hiddenRequested()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    visible: !DemoMode
                    spacing: 9

                    Text {
                        text: "ORGANIZE"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.6
                    }

                    GridLayout {
                        id: statusLayout
                        Layout.fillWidth: true
                        columns: detailsContent.width < 560 ? 2 : 5
                        columnSpacing: 6
                        rowSpacing: 6
                        Text {
                            text: "STATUS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: 76
                            Layout.columnSpan: statusLayout.columns === 2 ? 2 : 1
                        }
                        Repeater {
                            model: ["backlog", "playing", "completed", "abandoned"]
                            GlassButton {
                                required property string modelData
                                compact: true
                                Layout.fillWidth: true
                                text: modelData.toUpperCase()
                                selected: (root.game.completionStatus || "") === modelData
                                onClicked: root.completionStatusRequested(
                                               selected ? "" : modelData)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "TAGS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: 76
                        }
                        TextField {
                            id: tagsField
                            property bool controllerNavigation: root.couchMode
                            Layout.fillWidth: true
                            placeholderText: "Co-op, cozy, difficult"
                            Accessible.name: "Tags"
                            // Copy the saved tags in instead of binding so an achievement
                            // refresh or rescan mid-edit cannot overwrite what is being typed.
                            readonly property string savedText: root.game.tags ? root.game.tags.join(", ") : ""
                            onSavedTextChanged: if (!activeFocus) text = savedText
                            Component.onCompleted: text = savedText
                            color: Theme.foreground
                            placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                            font.family: Theme.fontFamily
                            selectByMouse: true
                            background: Rectangle {
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: tagsField.activeFocus
                                              ? Theme.accent
                                              : root.alpha(Theme.foreground, 0.15)
                            }
                            Keys.onReturnPressed: function(event) {
                                if (root.couchMode) {
                                    root.textEntryRequested(tagsField, "EDIT TAGS", false,
                                                            tagsField.placeholderText)
                                    event.accepted = true
                                } else {
                                    root.tagsRequested(text)
                                }
                            }
                            Keys.onEnterPressed: function(event) {
                                if (root.couchMode) {
                                    root.textEntryRequested(tagsField, "EDIT TAGS", false,
                                                            tagsField.placeholderText)
                                    event.accepted = true
                                } else {
                                    root.tagsRequested(text)
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            text: "SAVE"
                            onClicked: root.tagsRequested(tagsField.text)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "COLLECTIONS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: 76
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            contentHeight: availableHeight
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                            Row {
                                spacing: 6
                                Repeater {
                                    model: Library.collectionNames
                                    GlassButton {
                                        required property string modelData
                                        compact: true
                                        text: modelData.toUpperCase()
                                        selected: root.game.collections
                                                  ? root.game.collections.indexOf(modelData) >= 0
                                                  : false
                                        onClicked: root.collectionToggled(modelData, !selected)
                                    }
                                }
                                GlassButton {
                                    id: newCollectionButton
                                    objectName: "newCollectionButton"
                                    property Item controllerDownTarget:
                                        insightRefreshButton.visible && insightRefreshButton.enabled
                                        ? insightRefreshButton
                                        : achievementSortButton.visible && achievementSortButton.enabled
                                          ? achievementSortButton
                                          : achievementRefreshButton.visible && achievementRefreshButton.enabled
                                            ? achievementRefreshButton : null
                                    compact: true
                                    text: "+ NEW COLLECTION"
                                    onClicked: {
                                        root.collectionEditorOpen = true
                                        Qt.callLater(function() {
                                            if (root.couchMode) {
                                                root.textEntryRequested(
                                                    collectionField, "NEW COLLECTION", false,
                                                    collectionField.placeholderText)
                                            } else {
                                                collectionField.forceActiveFocus()
                                            }
                                        })
                                    }
                                }
                            }
                        }
                    }

                    GridLayout {
                        id: collectionEditor
                        Layout.fillWidth: true
                        visible: root.collectionEditorOpen
                        columns: detailsContent.width < 600 ? 2 : 4
                        columnSpacing: 8
                        rowSpacing: 8
                        Text {
                            text: "NEW"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: 76
                            Layout.columnSpan: collectionEditor.columns === 2 ? 2 : 1
                        }
                        TextField {
                            id: collectionField
                            property bool controllerNavigation: root.couchMode
                            Layout.fillWidth: true
                            Layout.maximumWidth: 360
                            Layout.columnSpan: collectionEditor.columns === 2 ? 2 : 1
                            placeholderText: "New collection"
                            Accessible.name: placeholderText
                            color: Theme.foreground
                            placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                            font.family: Theme.fontFamily
                            selectByMouse: true
                            background: Rectangle {
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: collectionField.activeFocus
                                              ? Theme.accent
                                              : root.alpha(Theme.foreground, 0.15)
                            }
                            Keys.onReturnPressed: {
                                if (root.couchMode) {
                                    root.textEntryRequested(collectionField, "NEW COLLECTION",
                                                            false, collectionField.placeholderText)
                                } else {
                                    root.collectionCreateRequested(text)
                                    clear()
                                }
                            }
                            Keys.onEnterPressed: {
                                if (root.couchMode) {
                                    root.textEntryRequested(collectionField, "NEW COLLECTION",
                                                            false, collectionField.placeholderText)
                                } else {
                                    root.collectionCreateRequested(text)
                                    clear()
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            Layout.fillWidth: collectionEditor.columns === 2
                            text: "CREATE + ADD"
                            onClicked: {
                                root.collectionCreateRequested(collectionField.text)
                                collectionField.clear()
                            }
                        }
                        GlassButton {
                            compact: true
                            Layout.fillWidth: collectionEditor.columns === 2
                            text: "CANCEL"
                            onClicked: root.closeCollectionEditor()
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    columns: detailsContent.width < 520 ? 1 : 3
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: root.selectedInstallation.source === "Steam"
                               ? [
                                   { label: "PLAYTIME", value: (root.game.hours || 0) + " HOURS" },
                                   { label: "ACHIEVEMENTS", value: (Achievements.unlocked || root.game.achievementsUnlocked || 0) + " / " + (Achievements.total || root.game.achievementsTotal || 0) },
                                   { label: "COMPLETION", value: Achievements.total > 0 ? Math.round(Achievements.unlocked * 100 / Achievements.total) + "%" : (root.game.progress || 0) + "%" }
                               ]
                               : [
                                   { label: "PLAYTIME", value: (root.game.hours || 0) + " HOURS" },
                                   { label: "SOURCE", value: (root.selectedInstallation.source || "LOCAL").toUpperCase() },
                                   { label: "LAUNCHER", value: (root.selectedInstallation.subtitle || root.selectedInstallation.source || "LOCAL").toUpperCase() }
                               ]

                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 150
                            Layout.preferredHeight: 88
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.color: root.alpha(Theme.foreground, 0.13)

                            Column {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 16
                                spacing: 7
                                Text {
                                    text: modelData.label
                                    color: Theme.mutedText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: modelData.value
                                    color: Theme.brightForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: insightsSection
                    objectName: "insightsSection"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 10
                    visible: root.selectedInstallation.source === "Steam" && Insights !== null
                    readonly property var metrics: {
                        if (!Insights) {
                            return []
                        }
                        const values = []
                        if (Insights.criticScore >= 0) {
                            values.push({ label: "IGDB CRITIC",
                                          value: Insights.criticScore + " / 100" })
                        }
                        if (Insights.rushedHours > 0) {
                            values.push({ label: "RUSHED",
                                          value: Insights.rushedHours + " H" })
                        }
                        if (Insights.normalHours > 0) {
                            values.push({ label: "MAIN + EXTRAS",
                                          value: Insights.normalHours + " H" })
                        }
                        if (Insights.completeHours > 0) {
                            values.push({ label: "COMPLETIONIST",
                                          value: Insights.completeHours + " H" })
                        }
                        return values
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "GAME INSIGHTS · IGDB"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                        }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            id: insightRefreshButton
                            objectName: "insightRefreshButton"
                            property Item controllerUpTarget: newCollectionButton
                            property Item controllerDownTarget:
                                achievementSortButton.visible && achievementSortButton.enabled
                                ? achievementSortButton
                                : achievementRefreshButton.visible && achievementRefreshButton.enabled
                                  ? achievementRefreshButton : null
                            compact: true
                            text: Insights && Insights.configured
                                  ? (Insights.busy ? "REFRESHING" : "REFRESH")
                                  : "CONNECT IGDB"
                            enabled: Insights && !Insights.busy
                            onClicked: {
                                if (Insights.configured) {
                                    Insights.refreshSteam(root.selectedInstallation.appId)
                                } else {
                                    root.connectRequested()
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Insights ? Insights.statusText : ""
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        id: insightsGrid
                        Layout.fillWidth: true
                        visible: insightsSection.metrics.length > 0
                        readonly property real minimumMetricWidth: 130
                        columns: Math.max(1, Math.min(
                                              insightsSection.metrics.length,
                                              Math.floor((detailsContent.width + columnSpacing)
                                                         / (minimumMetricWidth + columnSpacing))))
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: insightsSection.metrics

                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: insightsGrid.minimumMetricWidth
                                Layout.maximumWidth: 340
                                Layout.preferredHeight: 72
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: root.alpha(Theme.foreground, 0.13)

                                Column {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14
                                    spacing: 6
                                    Text {
                                        text: modelData.label
                                        color: Theme.mutedText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 8
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: modelData.value
                                        color: Theme.brightForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: insightsSection.metrics.length > 0
                        text: "Critic aggregate and time estimates provided by IGDB"
                        color: root.alpha(Theme.foreground, 0.48)
                        font.family: Theme.fontFamily
                        font.pixelSize: 8
                    }
                }

                ColumnLayout {
                    id: achievementListSection
                    objectName: "achievementListSection"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 9
                    visible: root.selectedInstallation.source === "Steam"
                             || root.selectedInstallation.source === "RetroArch"

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "ACHIEVEMENT PROGRESS"
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: Achievements.total > 0 ? Math.round(Achievements.unlocked * 100 / Achievements.total) + "%" : (root.game.progress || 0) + "%"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        radius: 3
                        color: root.alpha(Theme.foreground, 0.1)

                        Rectangle {
                            width: parent.width * (Achievements.total > 0
                                                   ? Achievements.unlocked / Achievements.total
                                                   : (root.game.progress || 0) / 100)
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    spacing: 10
                    visible: root.selectedInstallation.source === "Steam"
                             || root.selectedInstallation.source === "RetroArch"

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "ACHIEVEMENTS"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            id: achievementSortButton
                            objectName: "achievementSortButton"
                            property Item controllerUpTarget:
                                insightRefreshButton.visible && insightRefreshButton.enabled
                                ? insightRefreshButton : newCollectionButton
                            property Item controllerRightTarget:
                                achievementRefreshButton.visible && achievementRefreshButton.enabled
                                ? achievementRefreshButton : null
                            visible: Achievements.total > 1
                            compact: true
                            text: Achievements.sortMode === 0
                                  ? "SORT: STATUS" : "SORT: UNLOCK DATE"
                            onClicked: Achievements.sortMode = (Achievements.sortMode + 1) % 2
                        }
                        GlassButton {
                            id: achievementRefreshButton
                            objectName: "achievementRefreshButton"
                            property Item controllerUpTarget:
                                insightRefreshButton.visible && insightRefreshButton.enabled
                                ? insightRefreshButton : newCollectionButton
                            property Item controllerLeftTarget:
                                achievementSortButton.visible && achievementSortButton.enabled
                                ? achievementSortButton : null
                            visible: root.achievementAccount !== null
                            compact: true
                            text: root.achievementAccount && root.achievementAccount.hasApiKey
                                  ? (root.achievementAccount.busy ? "REFRESHING"
                                     : root.achievementSourceIsRetroArch ? "REFRESH RETROACHIEVEMENTS"
                                     : "REFRESH STEAM")
                                  : root.achievementSourceIsRetroArch ? "CONNECT RETROACHIEVEMENTS"
                                                                       : "CONNECT STEAM"
                            enabled: !root.achievementAccount || !root.achievementAccount.busy
                            onClicked: {
                                if (root.achievementAccount.hasApiKey) {
                                    root.achievementAccount.refreshAchievements(
                                                root.selectedInstallation.appId)
                                } else {
                                    root.connectRequested()
                                }
                            }
                        }
                        Text {
                            text: Achievements.unlocked + " / " + Achievements.total
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Achievements.statusText
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.achievementAccount && root.achievementAccount.statusText.length > 0
                        text: root.achievementAccount ? root.achievementAccount.statusText : ""
                        color: root.achievementAccount && (root.achievementAccount.state === "invalid-key"
                                                || root.achievementAccount.state === "private"
                                                || root.achievementAccount.state === "unsupported"
                                                || root.achievementAccount.state === "rate-limited")
                               ? Theme.yellow : Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        id: achievementGrid
                        Layout.fillWidth: true
                        visible: Achievements.total > 0
                        columns: detailsContent.width < 620 ? 1 : 2
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: Achievements

                            Rectangle {
                                required property int index
                                required property string title
                                required property string description
                                required property string iconPath
                                required property bool unlocked
                                required property double unlockTime
                                required property real rarity
                                required property bool hidden
                                objectName: "achievementCard" + index
                                activeFocusOnTab: true
                                Accessible.name: title
                                Accessible.role: Accessible.ListItem
                                Accessible.focused: activeFocus
                                Layout.fillWidth: true
                                Layout.minimumWidth: 260
                                Layout.preferredHeight: 82
                                radius: Math.max(6, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, unlocked ? 0.075 : 0.035)
                                border.width: activeFocus ? 2 : 1
                                border.color: activeFocus
                                              ? Theme.accent
                                              : unlocked
                                                ? root.alpha(Theme.accent, 0.34)
                                                : root.alpha(Theme.foreground, 0.10)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 11
                                    spacing: 12

                                    Rectangle {
                                        Layout.preferredWidth: 54
                                        Layout.preferredHeight: 54
                                        radius: 5
                                        color: root.alpha(Theme.darkerBackground, 0.54)
                                        border.color: root.alpha(Theme.foreground, 0.12)
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            source: iconPath
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                            opacity: unlocked ? 1 : 0.42
                                        }
                                        Text {
                                            visible: iconPath.length === 0
                                            anchors.centerIn: parent
                                            text: unlocked ? "◆" : "◇"
                                            color: unlocked ? Theme.accent : Theme.mutedText
                                            font.pixelSize: 19
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: hidden && !unlocked ? "Hidden achievement" : title
                                            textFormat: Text.PlainText
                                            color: unlocked ? Theme.brightForeground : Theme.foreground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: hidden && !unlocked ? "Unlock to reveal details" : description
                                            textFormat: Text.PlainText
                                            color: Theme.mutedText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: (unlocked && unlockTime > 0
                                                   ? "UNLOCKED " + Qt.formatDateTime(new Date(unlockTime * 1000), "MMM d, yyyy").toUpperCase() + "  ·  "
                                                   : "")
                                                  + (rarity > 0 ? rarity.toFixed(1) + "% OF PLAYERS"
                                                     : root.achievementSourceIsRetroArch ? "RETROACHIEVEMENTS" : "STEAM")
                                            color: unlocked ? Theme.accent : root.alpha(Theme.foreground, 0.45)
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 54 * root.uiScale
        anchors.bottomMargin: 20 * root.uiScale
        spacing: 20 * root.uiScale
        visible: root.couchMode
        z: 20

        Repeater {
            model: [
                { glyph: Controller.primaryGlyph, label: "SELECT" },
                { glyph: Controller.backGlyph, label: "BACK" },
                { glyph: Controller.favoriteGlyph, label: "FAVORITE" },
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
}
}
