import QtQuick
import QtQuick.Layouts

FocusScope {
    id: root

    required property var libraryModel
    property var sourceOptions: []
    property int categoryIndex: 0
    property var optionModel: []
    readonly property var categories: [
        { label: "LIBRARY VIEW", kind: "mode" },
        { label: "SORT ORDER", kind: "sort" },
        { label: "AVAILABILITY", kind: "availability" },
        { label: "SOURCE", kind: "source" },
        { label: "STATUS", kind: "status" },
        { label: "COLLECTION", kind: "collection" },
        { label: "TAG", kind: "tag" }
    ]
    readonly property real uiScale: Math.max(1, Math.min(2,
                                                         Math.min(width / 1920,
                                                                  height / 1080)))

    signal organizeRequested()
    signal savedFiltersRequested()
    signal randomRequested()
    signal closed()
    signal filtersChanged()

    Accessible.name: "Browse and filter library"
    Accessible.role: Accessible.Pane

    function alpha(color, amount) {
        return Qt.rgba(color.r, color.g, color.b, amount)
    }

    function optionsFor(kind) {
        if (kind === "mode") {
            return [
                { label: "ALL GAMES", value: 0 },
                { label: "FAVORITES", value: 1 },
                { label: "RECENTLY PLAYED", value: 2 },
                { label: "HIDDEN", value: 3 }
            ]
        }
        if (kind === "sort") {
            return [
                { label: "TITLE", value: 0 },
                { label: "RECENTLY PLAYED", value: 1 },
                { label: "PLAYTIME", value: 2 }
            ]
        }
        if (kind === "availability") {
            return [
                { label: "INSTALLED", value: 0 },
                { label: "ALL GAMES", value: 1 },
                { label: "READY TO INSTALL", value: 2 }
            ]
        }
        if (kind === "source") {
            return sourceOptions
        }
        if (kind === "status") {
            return [
                { label: "ANY STATUS", value: "" },
                { label: "BACKLOG", value: "backlog" },
                { label: "PLAYING", value: "playing" },
                { label: "COMPLETED", value: "completed" },
                { label: "ABANDONED", value: "abandoned" }
            ]
        }
        const names = kind === "collection" ? libraryModel.collectionNames
                                             : libraryModel.tagNames
        const values = [{ label: kind === "collection" ? "ANY COLLECTION" : "ANY TAG",
                          value: "" }]
        for (let index = 0; index < names.length; ++index) {
            values.push({ label: names[index].toUpperCase(), value: names[index] })
        }
        return values
    }

    function rebuildOptions() {
        const kind = categories[categoryIndex].kind
        optionModel = optionsFor(kind)
        optionList.currentIndex = selectedIndex(kind)
    }

    function selectedIndex(kind) {
        const selectedValue = kind === "mode" ? libraryModel.mode
                            : kind === "sort" ? libraryModel.sortMode
                            : kind === "availability" ? libraryModel.availability
                            : kind === "source" ? libraryModel.sourceFilter
                            : kind === "status" ? libraryModel.completionFilter
                            : kind === "collection" ? libraryModel.collectionFilter
                            : libraryModel.tagFilter
        for (let index = 0; index < optionModel.length; ++index) {
            if (optionModel[index].value === selectedValue) {
                return index
            }
        }
        return 0
    }

    function isSelected(kind, value) {
        return kind === "mode" ? libraryModel.mode === value
             : kind === "sort" ? libraryModel.sortMode === value
             : kind === "availability" ? libraryModel.availability === value
             : kind === "source" ? libraryModel.sourceFilter === value
             : kind === "status" ? libraryModel.completionFilter === value
             : kind === "collection" ? libraryModel.collectionFilter === value
             : libraryModel.tagFilter === value
    }

    function applyOption(index) {
        if (index < 0 || index >= optionModel.length) {
            return
        }
        const kind = categories[categoryIndex].kind
        const value = optionModel[index].value
        if (kind === "mode") libraryModel.mode = value
        else if (kind === "sort") libraryModel.sortMode = value
        else if (kind === "availability") libraryModel.availability = value
        else if (kind === "source") libraryModel.sourceFilter = value
        else if (kind === "status") libraryModel.completionFilter = value
        else if (kind === "collection") libraryModel.collectionFilter = value
        else libraryModel.tagFilter = value
        filtersChanged()
    }

    function clearFilters() {
        libraryModel.mode = 0
        libraryModel.sortMode = 0
        libraryModel.availability = 0
        libraryModel.sourceFilter = ""
        libraryModel.completionFilter = ""
        libraryModel.collectionFilter = ""
        libraryModel.tagFilter = ""
        libraryModel.searchText = ""
        filtersChanged()
        rebuildOptions()
    }

    function focusPanel() {
        categoryList.currentIndex = categoryIndex
        categoryList.forceActiveFocus(Qt.TabFocusReason)
    }

    onCategoryIndexChanged: rebuildOptions()

    Connections {
        target: root.libraryModel
        function onOrganizationNamesChanged() { root.rebuildOptions() }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, 0.94)
        MouseArea { anchors.fill: parent }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 72 * root.uiScale
        anchors.rightMargin: 72 * root.uiScale
        anchors.topMargin: 48 * root.uiScale
        anchors.bottomMargin: 44 * root.uiScale
        spacing: 24 * root.uiScale

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3 * root.uiScale
                Text {
                    text: "BROWSE YOUR WAY"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 30 * root.uiScale
                    font.weight: Font.Bold
                }
                Text {
                    text: "Choose what appears in the library and how it is ordered."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 14 * root.uiScale
                }
            }

            GlassButton {
                id: organizeButton
                text: "ORGANIZE"
                displayScale: root.uiScale
                KeyNavigation.right: savedButton
                KeyNavigation.down: categoryList
                onClicked: root.organizeRequested()
            }
            GlassButton {
                id: savedButton
                KeyNavigation.left: organizeButton
                objectName: "couchSavedFiltersButton"
                text: "SAVED FILTERS"
                displayScale: root.uiScale
                KeyNavigation.right: randomButton
                KeyNavigation.down: categoryList
                onClicked: root.savedFiltersRequested()
            }
            GlassButton {
                id: randomButton
                KeyNavigation.left: savedButton
                objectName: "couchRandomGameButton"
                KeyNavigation.right: clearButton
                KeyNavigation.down: categoryList
                text: "PICK A GAME"
                displayScale: root.uiScale
                onClicked: root.randomRequested()
            }
            GlassButton {
                id: clearButton
                KeyNavigation.left: randomButton
                text: "CLEAR ALL"
                onClicked: root.clearFilters()
                KeyNavigation.right: doneButton
                KeyNavigation.down: categoryList
            }
            GlassButton {
                id: doneButton
                text: "DONE"
                primary: true
                onClicked: root.closed()
                KeyNavigation.left: clearButton
                KeyNavigation.down: optionList
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Math.max(12 * root.uiScale, Theme.cornerRadius * 2)
            color: root.alpha(Theme.background, 0.82)
            border.color: root.alpha(Theme.foreground, 0.16)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22 * root.uiScale
                spacing: 22 * root.uiScale

                ListView {
                    id: categoryList
                    objectName: "couchBrowseCategories"
                    Layout.preferredWidth: 340 * root.uiScale
                    Layout.fillHeight: true
                    model: root.categories
                    spacing: 8 * root.uiScale
                    clip: true
                    currentIndex: root.categoryIndex

                    onCurrentIndexChanged: root.categoryIndex = currentIndex

                    Keys.onRightPressed: function(event) {
                        optionList.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                    Keys.onUpPressed: function(event) {
                        if (currentIndex === 0) {
                            clearButton.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onReturnPressed: function(event) {
                        optionList.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        optionList.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }

                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: categoryList.width
                        height: 68 * root.uiScale
                        radius: Math.max(7 * root.uiScale, Theme.cornerRadius)
                        color: categoryList.currentIndex === index
                               ? root.alpha(Theme.accent, 0.16)
                               : root.alpha(Theme.foreground, 0.04)
                        border.width: categoryList.activeFocus
                                      && categoryList.currentIndex === index ? 3 : 1
                        border.color: categoryList.activeFocus
                                      && categoryList.currentIndex === index
                                      ? Theme.accent : root.alpha(Theme.foreground, 0.12)

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 22 * root.uiScale
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: categoryList.currentIndex === index
                                   ? Theme.brightForeground : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 16 * root.uiScale
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                categoryList.currentIndex = index
                                categoryList.forceActiveFocus(Qt.MouseFocusReason)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: Math.max(1, root.uiScale)
                    Layout.fillHeight: true
                    color: root.alpha(Theme.foreground, 0.12)
                }

                ListView {
                    id: optionList
                    objectName: "couchBrowseOptions"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.optionModel
                    spacing: 8 * root.uiScale
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Keys.onLeftPressed: function(event) {
                        categoryList.forceActiveFocus(Qt.TabFocusReason)
                        event.accepted = true
                    }
                    Keys.onUpPressed: function(event) {
                        if (currentIndex === 0) {
                            doneButton.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onReturnPressed: function(event) {
                        root.applyOption(currentIndex)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        root.applyOption(currentIndex)
                        event.accepted = true
                    }

                    delegate: Rectangle {
                        id: optionDelegate
                        required property int index
                        required property var modelData
                        readonly property bool selected:
                            root.isSelected(root.categories[root.categoryIndex].kind,
                                            modelData.value)
                        width: optionList.width
                        height: 64 * root.uiScale
                        radius: Math.max(7 * root.uiScale, Theme.cornerRadius)
                        color: optionList.currentIndex === index
                               ? root.alpha(Theme.accent, 0.15)
                               : selected ? root.alpha(Theme.foreground, 0.075)
                                          : root.alpha(Theme.foreground, 0.035)
                        border.width: optionList.activeFocus
                                      && optionList.currentIndex === index ? 3 : 1
                        border.color: optionList.activeFocus
                                      && optionList.currentIndex === index
                                      ? Theme.accent : selected
                                        ? root.alpha(Theme.accent, 0.5)
                                        : root.alpha(Theme.foreground, 0.1)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 22 * root.uiScale
                            anchors.rightMargin: 22 * root.uiScale

                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                textFormat: Text.PlainText
                                color: Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: 16 * root.uiScale
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: optionDelegate.selected
                                text: "✓"
                                color: Theme.accent
                                font.pixelSize: 22 * root.uiScale
                                font.weight: Font.Bold
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                optionList.currentIndex = index
                                root.applyOption(index)
                                optionList.forceActiveFocus(Qt.MouseFocusReason)
                            }
                        }
                    }
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignRight
            text: Controller.primaryGlyph + "  SELECT     "
                  + Controller.backGlyph + "  CLOSE"
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12 * root.uiScale
            font.weight: Font.DemiBold
        }
    }

    Component.onCompleted: {
        rebuildOptions()
        Qt.callLater(focusPanel)
    }
}
