import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Rectangle {
    id: editor
    property bool couchMode: false
    property bool queued: false
    property string pendingMode: ""
    property string pendingPath: ""
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2, height / 700)) : 1
    signal dismissed()
    signal textEntryRequested(var target, string title)
    color: Theme.background
    function focusEditor() { doneButton.forceActiveFocus() }
    function dismiss() {
        if (Backups.busy || queued) return
        if (pendingMode) { pendingMode = ""; Qt.callLater(focusEditor); return }
        Backups.discardPreview()
        dismissed()
    }
    function chooseMode(mode) { pendingMode = mode; pendingPath = pathField.text; Qt.callLater(cancelButton.forceActiveFocus) }
    function navigate(current, key) {
        if (current !== previewText) return false
        const scroll = previewScroll.contentItem
        const end = Math.max(0, scroll.contentHeight - scroll.height)
        if (key === Qt.Key_Down && scroll.contentY < end - 1) scroll.contentY = Math.min(end, scroll.contentY + scroll.height * 0.6)
        else if (key === Qt.Key_Up && scroll.contentY > 1) scroll.contentY = Math.max(0, scroll.contentY - scroll.height * 0.6)
        else if (key === Qt.Key_Down || key === Qt.Key_Right) mergeButton.forceActiveFocus()
        else if (key === Qt.Key_Up || key === Qt.Key_Left) doneButton.forceActiveFocus()
        else return false
        return true
    }
    function summary() {
        const p = Backups.preview
        if (!Backups.hasPreview) return ""
        let text = "File: " + p.path + "\nCreated " + Qt.formatDateTime(new Date(p.createdAt), "MMM d, yyyy h:mm AP") + "\n\nBACKUP / CURRENT / MATCHING\n"
        for (const row of p.counts) text += row.label + ": " + row.incoming + " / " + row.current + " / " + row.matching + "\n"
        text += "\n" + p.artworkCount + " artwork files · " + p.settingsCount + " preferences\n"
        if (p.missingPathCount) text += "\nUNAVAILABLE PATHS (" + p.missingPathCount + ")\n" + p.missingPaths.join("\n") + "\nThese entries remain stored for repair or reconnection.\n"
        if (p.savedFilterNameConflicts.length) text += "\nRENAMED DURING MERGE\n" + p.savedFilterNameConflicts.join(", ") + "\n"
        const labels = {reduced_motion:"Reduced motion", artwork_cache_limit_mb:"Artwork cache limit (MB)", steam_enabled:"Steam", lutris_enabled:"Lutris", heroic_enabled:"Heroic", gog_enabled:"GOG", faugus_enabled:"Faugus", retroarch_enabled:"RetroArch", pcsx2_enabled:"PCSX2", ryujinx_enabled:"Ryujinx", pcsx2_auto:"Detect PCSX2 automatically", ryujinx_auto:"Detect Ryujinx automatically", battlenet_enabled:"Battle.net", close_after_launch:"Close after launching", couch_mode:"Couch Mode", couch_library_view:"Couch library view", gog_library_paths:"GOG folders"}
        text += "\nPREFERENCES (CURRENT → BACKUP)\n"
        for (const setting of p.settings) {
            const show = value => Array.isArray(value) ? value.join(", ") || "None" : value === true ? "On" : value === false ? "Off" : value === undefined ? "Default" : String(value)
            text += (labels[setting.name] || setting.name) + ": " + show(setting.current) + " → " + show(setting.incoming) + "\n"
        }
        return text + "\nMERGE\n" + p.mergeExplanation + "\n\nREPLACE\n" + p.replaceExplanation + " Core preferences absent from the backup return to defaults.\n\n" + p.recoveryExplanation
    }
    Connections {
        target: Backups
        function onChanged() {
            if (!Backups.busy) Qt.callLater(function() {
                if (editor.pendingMode) cancelButton.forceActiveFocus()
                else if (Backups.hasPreview) previewText.forceActiveFocus()
                else if (!editor.queued) doneButton.forceActiveFocus()
            })
        }
        function onRestoreQueued() { editor.queued = true; editor.pendingMode = ""; Qt.callLater(closeAppButton.forceActiveFocus) }
    }
    FileDialog {
        id: openDialog
        title: "Preview an Omakade backup"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Omakade backups (*.omakade-backup)"]
        onAccepted: { pathField.text = selectedFile; Backups.previewBackup(selectedFile) }
    }
    FileDialog {
        id: saveDialog
        title: "Save an Omakade backup"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "omakade-backup"
        nameFilters: ["Omakade backups (*.omakade-backup)"]
        onAccepted: { pathField.text = selectedFile; Backups.exportBackup(selectedFile) }
    }
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24 * editor.uiScale
        spacing: 12 * editor.uiScale
        RowLayout {
            Layout.fillWidth: true
            Text { text: "BACKUP & RESTORE"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 23 * editor.uiScale; Layout.fillWidth: true }
            GlassButton { id: doneButton; objectName: "backupDoneButton"; text: "DONE"; displayScale: editor.uiScale; enabled: !Backups.busy && !editor.queued; onClicked: editor.dismiss() }
        }
        Text {
            Layout.fillWidth: true
            text: Backups.message || "Keep a local copy of your library choices, manual games, artwork, and preferences. Game files and account credentials are excluded."
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 13 * editor.uiScale
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: !editor.queued && !editor.pendingMode
            enabled: !Backups.busy && Backups.available
            TextField {
                id: pathField
                objectName: "backupPathField"
                property bool controllerNavigation: editor.couchMode
                Layout.fillWidth: true
                placeholderText: "/path/to/library.omakade-backup"
                Accessible.name: "Backup file path"
                color: Theme.foreground
                placeholderTextColor: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 13 * editor.uiScale
                Keys.onReturnPressed: function(event) { if (editor.couchMode) { editor.textEntryRequested(pathField, "BACKUP FILE PATH"); event.accepted = true } }
                Keys.onEnterPressed: function(event) { if (editor.couchMode) { editor.textEntryRequested(pathField, "BACKUP FILE PATH"); event.accepted = true } }
                background: Rectangle { color: Theme.darkerBackground; radius: 5; border.color: parent.activeFocus ? Theme.accent : Theme.mutedText }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: editor.width < 1000 * editor.uiScale ? 2 : 4
                GlassButton { text: "SAVE AS…"; displayScale: editor.uiScale; onClicked: saveDialog.open() }
                GlassButton { text: "OPEN…"; displayScale: editor.uiScale; onClicked: openDialog.open() }
                GlassButton { objectName: "backupExportButton"; text: "SAVE TO PATH"; displayScale: editor.uiScale; enabled: pathField.text.length > 0; onClicked: editor.chooseMode("export") }
                GlassButton { objectName: "backupPreviewButton"; text: "PREVIEW PATH"; displayScale: editor.uiScale; enabled: pathField.text.length > 0; onClicked: Backups.previewBackup(pathField.text) }
            }
        }
        ScrollView {
            id: previewScroll
            objectName: "backupPreviewScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Backups.hasPreview && !editor.pendingMode && !editor.queued
            clip: true
            contentWidth: availableWidth
            TextArea {
                id: previewText
                objectName: "backupPreviewText"
                property bool controllerNavigation: true
                activeFocusOnTab: true
                readOnly: true
                selectByMouse: true
                text: editor.summary()
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 12 * editor.uiScale
                background: Rectangle { color: Theme.darkerBackground; border.color: previewText.activeFocus ? Theme.accent : Theme.mutedText }
            }
        }
        Text {
            visible: Backups.hasPreview && !editor.pendingMode && !editor.queued
            text: "Use ↑/↓ to read the preview, then → for restore choices."
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * editor.uiScale
        }
        RowLayout {
            visible: Backups.hasPreview && !editor.pendingMode && !editor.queued
            enabled: !Backups.busy
            GlassButton { id: mergeButton; objectName: "backupMergeButton"; text: "MERGE…"; displayScale: editor.uiScale; onClicked: editor.chooseMode("merge") }
            GlassButton { objectName: "backupReplaceButton"; text: "REPLACE…"; displayScale: editor.uiScale; onClicked: editor.chooseMode("replace") }
        }
        Text {
            Layout.fillWidth: true
            visible: editor.pendingMode !== ""
            text: editor.pendingMode === "export" ? "Save a backup to " + editor.pendingPath + "? An existing file at this path will be replaced."
                  : "Restore " + Backups.preview.path + "?\n\n" + (editor.pendingMode === "replace" ? Backups.preview.replaceExplanation + " Core preferences absent from the backup return to defaults." : Backups.preview.mergeExplanation)
                    + "\n\nOmakade will keep a recovery copy and apply this restore after you close and reopen the app."
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 15 * editor.uiScale
        }
        RowLayout {
            visible: editor.pendingMode !== ""
            enabled: !Backups.busy
            GlassButton { id: cancelButton; objectName: "backupCancelButton"; text: "CANCEL"; displayScale: editor.uiScale; onClicked: { editor.pendingMode = ""; Qt.callLater(editor.focusEditor) } }
            GlassButton {
                objectName: "backupConfirmButton"
                text: editor.pendingMode === "export" ? "SAVE BACKUP" : editor.pendingMode === "replace" ? "CONFIRM REPLACEMENT" : "CONFIRM MERGE"
                displayScale: editor.uiScale
                onClicked: {
                    if (editor.pendingMode === "export") { Backups.exportBackup(editor.pendingPath); editor.pendingMode = "" }
                    else Backups.confirmRestore(editor.pendingMode === "replace")
                }
            }
        }
        GlassButton { id: closeAppButton; objectName: "backupCloseAppButton"; visible: editor.queued; text: "CLOSE OMAKADE"; displayScale: editor.uiScale; onClicked: Qt.quit() }
        BusyIndicator { visible: Backups.busy; running: visible; Layout.alignment: Qt.AlignHCenter }
        Item { Layout.fillHeight: true; visible: !Backups.hasPreview || editor.pendingMode !== "" || editor.queued }
    }
}
