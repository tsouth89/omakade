import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Rectangle {
    id: editor
    property bool couchMode: false
    property string entryId: ""
    property var argumentValues: []
    property string errorText: ""
    property bool confirmingRemoval: false
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2.4, height / 600)) : 1
    signal dismissed()
    signal saved(string entryId)
    signal removed()
    signal textEntryRequested(var target, string title)
    color: Theme.background

    function loadDraft(draft) {
        entryId = draft.id || ""
        titleField.text = draft.title || ""
        executableField.text = draft.executable || ""
        directoryField.text = draft.directory || ""
        argumentValues = draft.arguments || []
        errorText = ""
        confirmingRemoval = false
        Qt.callLater(function() { titleField.forceActiveFocus() })
    }
    function collectArguments() {
        let result = []
        for (let i = 0; i < argumentsRepeater.count; ++i)
            result.push(argumentsRepeater.itemAt(i).argumentText)
        return result
    }
    function save() {
        const id = ManualLibrary.saveEntry({id: entryId, title: titleField.text,
            executable: executableField.text, directory: directoryField.text,
            arguments: collectArguments()})
        if (id) saved(id)
        else errorText = ManualLibrary.lastError
    }
    component EditorButton: GlassButton { displayScale: editor.uiScale }
    component Caption: Text {
        Layout.preferredWidth: 105 * editor.uiScale
        color: Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: 11 * editor.uiScale
        wrapMode: Text.Wrap
    }
    component EntryField: TextField {
        property string fieldTitle: ""
        property bool controllerNavigation: editor.couchMode
        Layout.fillWidth: true
        font.family: Theme.fontFamily
        font.pixelSize: 13 * editor.uiScale
        color: Theme.foreground
        placeholderTextColor: Theme.mutedText
        Accessible.name: fieldTitle
        Keys.onReturnPressed: function(event) {
            if (editor.couchMode) { editor.textEntryRequested(this, fieldTitle); event.accepted = true }
        }
        Keys.onEnterPressed: function(event) {
            if (editor.couchMode) { editor.textEntryRequested(this, fieldTitle); event.accepted = true }
        }
        background: Rectangle {
            color: Theme.background
            radius: 5
            border.color: parent.activeFocus ? Theme.accent : Theme.mutedText
            border.width: parent.activeFocus ? 2 : 1
        }
    }
    FileDialog {
        id: importDialog
        title: "Choose a native executable or desktop entry"
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const draft = ManualLibrary.draftFromFile(selectedFile)
            if (draft.executable) {
                draft.id = editor.entryId
                editor.loadDraft(draft)
            } else editor.errorText = ManualLibrary.lastError
        }
    }
    MouseArea { anchors.fill: parent }
    ScrollView {
        id: editorScroll
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 740 * editor.uiScale)
        height: parent.height - 48
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: editorScroll.availableWidth
            spacing: 12 * editor.uiScale
            Text {
                text: editor.entryId ? "EDIT MANUAL GAME" : "ADD A GAME"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 24 * editor.uiScale
            }
            Text {
                Layout.fillWidth: true
                text: "Choose a native executable or import a desktop entry, review its launch details, then save. Windows games can be added through Steam, Heroic, Lutris, or Faugus."
                wrapMode: Text.Wrap
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 12 * editor.uiScale
            }
            EditorButton {
                text: "CHOOSE FILE"
                visible: !editor.couchMode
                onClicked: importDialog.open()
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: "TITLE" }
                EntryField { id: titleField; objectName: "manualTitleField"; fieldTitle: "Game title"; placeholderText: fieldTitle }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: "EXECUTABLE" }
                EntryField { id: executableField; objectName: "manualExecutableField"; fieldTitle: "Executable path"; placeholderText: "/path/to/game" }
            }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: "WORKING FOLDER" }
                EntryField { id: directoryField; objectName: "manualDirectoryField"; fieldTitle: "Working folder"; placeholderText: "/path/to/game/folder" }
            }
            Text {
                text: "ARGUMENTS · ONE PER FIELD"
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 11 * editor.uiScale
            }
            Repeater {
                id: argumentsRepeater
                model: editor.argumentValues
                RowLayout {
                    required property int index
                    required property string modelData
                    property alias argumentText: argumentField.text
                    Layout.fillWidth: true
                    EntryField {
                        id: argumentField
                        fieldTitle: "Argument " + (index + 1)
                        text: modelData
                    }
                    EditorButton {
                        compact: true
                        text: "REMOVE"
                        Accessible.name: "Remove argument " + (index + 1)
                        onClicked: {
                            const args = editor.collectArguments()
                            args.splice(index, 1)
                            editor.argumentValues = args
                            Qt.callLater(function() { addArgumentButton.forceActiveFocus() })
                        }
                    }
                }
            }
            EditorButton {
                id: addArgumentButton
                compact: true
                text: "ADD ARGUMENT"
                enabled: argumentsRepeater.count < 256
                onClicked: {
                    const args = editor.collectArguments()
                    args.push("")
                    editor.argumentValues = args
                }
            }
            Text {
                Layout.fillWidth: true
                visible: editor.errorText !== ""
                text: editor.errorText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.yellow
                font.family: Theme.fontFamily
                font.pixelSize: 12 * editor.uiScale
            }
            RowLayout {
                EditorButton { objectName: "manualSaveButton"; text: "SAVE GAME"; primary: true; onClicked: editor.save() }
                EditorButton { text: "CANCEL"; onClicked: editor.dismissed() }
            }
            Text {
                Layout.fillWidth: true
                visible: editor.confirmingRemoval
                text: "Remove this entry from Omakade? The game files will stay where they are."
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 12 * editor.uiScale
            }
            EditorButton {
                visible: editor.entryId !== ""
                text: editor.confirmingRemoval ? "CONFIRM REMOVAL" : "REMOVE FROM OMAKADE"
                onClicked: {
                    if (!editor.confirmingRemoval) editor.confirmingRemoval = true
                    else if (ManualLibrary.removeEntry(editor.entryId)) editor.removed()
                    else editor.errorText = ManualLibrary.lastError
                }
            }
        }
    }
}
