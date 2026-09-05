import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Rectangle {
    id: editor
    required property var game
    required property int gameRow
    property bool couchMode: false
    property string selectedKind: "cover"
    property string message: ""
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2.4, height / 600)) : 1
    signal dismissed()
    signal artworkChanged()
    signal textEntryRequested(var target, string title)
    color: Theme.background

    function apply(kind, path) {
        if (Library.setCustomArtwork(gameRow, kind, path)) {
            message = "Artwork updated"
            artworkChanged()
        } else message = "Choose a readable PNG, JPEG, or WebP image up to 32 MB."
    }
    function reset(kind) {
        if (Library.resetCustomArtwork(gameRow, kind)) {
            message = "Original artwork restored"
            artworkChanged()
        } else message = "Could not reset that artwork"
    }
    function focusEditor() { doneButton.forceActiveFocus() }
    FileDialog {
        id: artworkDialog
        title: "Choose " + editor.selectedKind + " artwork"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: editor.apply(editor.selectedKind, selectedFile)
    }
    MouseArea { anchors.fill: parent }
    ScrollView {
        id: scroll
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 740 * editor.uiScale)
        height: parent.height - 48
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12 * editor.uiScale
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "ARTWORK"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 24 * editor.uiScale
                }
                GlassButton {
                    id: doneButton
                    objectName: "artworkDoneButton"
                    text: "DONE"
                    displayScale: editor.uiScale
                    onClicked: editor.dismissed()
                }
            }
            Text {
                Layout.fillWidth: true
                text: editor.game.title || "Game"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 14 * editor.uiScale
            }
            Repeater {
                model: [
                    { kind: "cover", title: "COVER", note: "Portrait artwork for the library", flag: "customCover" },
                    { kind: "hero", title: "HERO", note: "Wide background for game details", flag: "customHero" },
                    { kind: "logo", title: "LOGO", note: "Title artwork, including transparent images", flag: "customLogo" }
                ]
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6 * editor.uiScale
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: modelData.title + " · " + modelData.note
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * editor.uiScale
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12 * editor.uiScale
                        Image {
                            Layout.preferredWidth: 100 * editor.uiScale
                            Layout.preferredHeight: 80 * editor.uiScale
                            source: editor.game[modelData.kind + "Path"] || ""
                            autoTransform: true
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 400
                            sourceSize.height: 320
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: pathField
                                objectName: "artworkPath_" + modelData.kind
                                property bool controllerNavigation: editor.couchMode
                                Layout.fillWidth: true
                                placeholderText: "/path/to/image.png"
                                Accessible.name: modelData.title + " image path"
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: 13 * editor.uiScale
                                placeholderTextColor: Theme.mutedText
                                Keys.onReturnPressed: function(event) {
                                    if (editor.couchMode) { editor.textEntryRequested(pathField, modelData.title + " IMAGE PATH"); event.accepted = true }
                                }
                                Keys.onEnterPressed: function(event) {
                                    if (editor.couchMode) { editor.textEntryRequested(pathField, modelData.title + " IMAGE PATH"); event.accepted = true }
                                }
                                background: Rectangle {
                                    color: Theme.background
                                    radius: 5
                                    border.width: pathField.activeFocus ? 2 : 1
                                    border.color: pathField.activeFocus ? Theme.accent : Theme.mutedText
                                }
                            }
                            RowLayout {
                                GlassButton {
                                    text: "APPLY"
                                    compact: true
                                    displayScale: editor.uiScale
                                    onClicked: editor.apply(modelData.kind, pathField.text)
                                }
                                GlassButton {
                                    text: "BROWSE"
                                    compact: true
                                    visible: !editor.couchMode
                                    displayScale: editor.uiScale
                                    onClicked: { editor.selectedKind = modelData.kind; artworkDialog.open() }
                                }
                                GlassButton {
                                    text: "RESET"
                                    compact: true
                                    enabled: editor.game[modelData.flag] || false
                                    displayScale: editor.uiScale
                                    onClicked: {
                                        editor.reset(modelData.kind)
                                        pathField.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: editor.message
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 12 * editor.uiScale
            }
        }
    }
}
