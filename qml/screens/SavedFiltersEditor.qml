import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: editor
    property bool couchMode: false
    property string selectedId: ""
    property string pendingDelete: ""
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2.4, height / 600)) : 1
    signal dismissed()
    signal applyRequested(string id)
    signal textEntryRequested(var target, string title)
    color: Theme.background
    function focusEditor() { nameField.forceActiveFocus() }
    readonly property int focusedSavedRow: views.currentIndex
    function focusSavedRow(index, applyColumn) {
        views.currentIndex = index
        views.positionViewAtIndex(index, ListView.Contain)
        views.forceLayout()
        const row = views.itemAtIndex(index)
        if (row) row.focusControl(applyColumn)
    }
    function navigate(current, key) {
        if (!current || current.savedFilterRow === undefined || (key !== Qt.Key_Up && key !== Qt.Key_Down)) return false
        const next = current.savedFilterRow + (key === Qt.Key_Down ? 1 : -1)
        if (next < 0) return false
        if (next < views.count) focusSavedRow(next, current.savedFilterApply)
        return true
    }
    function reveal(item) {
        if (item && item.savedFilterRow !== undefined) {
            views.currentIndex = item.savedFilterRow
            views.positionViewAtIndex(views.currentIndex, ListView.Contain)
        }
    }
    function saveCurrent() {
        const id = Library.saveCurrentFilter(nameField.text)
        if (id) { selectedId = id; pendingDelete = "" }
    }
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 800 * editor.uiScale)
        height: parent.height - 48
        spacing: 12 * editor.uiScale
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "SAVED FILTERS"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 24 * editor.uiScale
            }
            GlassButton {
                objectName: "savedFiltersDone"
                text: "DONE"
                displayScale: editor.uiScale
                onClicked: editor.dismissed()
            }
        }
        Text {
            Layout.fillWidth: true
            text: "Save the current search, filters, and sort order as a named view."
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12 * editor.uiScale
        }
        TextField {
            id: nameField
            objectName: "savedFilterName"
            property bool controllerNavigation: editor.couchMode
            Layout.fillWidth: true
            placeholderText: "Name this view"
            Accessible.name: "Saved filter name"
            maximumLength: 100
            color: Theme.foreground
            placeholderTextColor: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 14 * editor.uiScale
            Keys.onReturnPressed: function(event) {
                if (editor.couchMode) editor.textEntryRequested(nameField, "FILTER NAME")
                else editor.saveCurrent()
                event.accepted = true
            }
            Keys.onEnterPressed: function(event) {
                if (editor.couchMode) editor.textEntryRequested(nameField, "FILTER NAME")
                else editor.saveCurrent()
                event.accepted = true
            }
            background: Rectangle {
                color: Theme.darkerBackground
                radius: 5
                border.width: nameField.activeFocus ? 2 : 1
                border.color: nameField.activeFocus ? Theme.accent : Theme.mutedText
            }
        }
        RowLayout {
            GlassButton {
                objectName: "saveCurrentFilterButton"
                text: "SAVE CURRENT"
                displayScale: editor.uiScale
                onClicked: editor.saveCurrent()
            }
            GlassButton {
                text: "RENAME SELECTED"
                enabled: editor.selectedId.length > 0
                displayScale: editor.uiScale
                onClicked: Library.renameSavedFilter(editor.selectedId, nameField.text)
            }
            GlassButton {
                text: editor.pendingDelete === editor.selectedId && editor.selectedId ? "CONFIRM DELETE" : "DELETE"
                enabled: editor.selectedId.length > 0
                displayScale: editor.uiScale
                onClicked: {
                    if (editor.pendingDelete !== editor.selectedId) editor.pendingDelete = editor.selectedId
                    else if (Library.removeSavedFilter(editor.selectedId)) {
                        editor.selectedId = ""
                        editor.pendingDelete = ""
                    }
                }
            }
        }
        ListView {
            id: views
            objectName: "savedFilterList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Library.savedFilters
            clip: true
            spacing: 6 * editor.uiScale
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                required property int index
                id: savedRow
                function focusControl(applyColumn) { (applyColumn ? applyButton : selectButton).forceActiveFocus() }
                width: views.width
                spacing: 8 * editor.uiScale
                GlassButton {
                    id: selectButton
                    property int savedFilterRow: savedRow.index
                    property bool savedFilterApply: false
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitWidth: 100 * editor.uiScale
                    text: modelData.name
                    contentItem: Text {
                        text: selectButton.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 12 * editor.uiScale
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.name
                    selected: editor.selectedId === modelData.id
                    displayScale: editor.uiScale
                    onClicked: {
                        editor.selectedId = modelData.id
                        editor.pendingDelete = ""
                        nameField.text = modelData.name
                    }
                }
                GlassButton {
                    id: applyButton
                    property int savedFilterRow: savedRow.index
                    property bool savedFilterApply: true
                    text: "APPLY"
                    displayScale: editor.uiScale
                    onClicked: editor.applyRequested(modelData.id)
                }
            }
            Text {
                anchors.centerIn: parent
                visible: views.count === 0
                text: "No saved filters yet"
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 14 * editor.uiScale
            }
        }
        Text {
            Layout.fillWidth: true
            text: Library.savedFilterMessage
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12 * editor.uiScale
        }
    }
}
