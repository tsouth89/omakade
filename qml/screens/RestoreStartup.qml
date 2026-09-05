import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "restoreStartupWindow"
    width: 900
    height: 620
    minimumWidth: 620
    minimumHeight: 450
    visible: true
    title: "Omakade · Restore"
    color: Theme.background
    property bool couchMode: false
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2, height / 700)) : 1
    onClosing: function(event) { if (Recovery.busy) event.accepted = false }

    function navigate(key) {
        if (Recovery.busy) return
        const grid = [retryButton, undoButton, folderButton, closeButton]
        const buttons = grid.filter(b => b.enabled)
        const index = buttons.indexOf(root.activeFocusItem)
        const gridIndex = grid.indexOf(root.activeFocusItem)
        if (key === Qt.Key_Up || key === Qt.Key_Down || key === Qt.Key_Left || key === Qt.Key_Right) {
            let target = gridIndex < 0 ? 0 : gridIndex ^ ((key === Qt.Key_Up || key === Qt.Key_Down) ? 2 : 1)
            if (!grid[target].enabled) target = target ^ 1
            grid[target].forceActiveFocus()
        } else if (key === Qt.Key_Backtab)
            buttons[(index + buttons.length - 1) % buttons.length].forceActiveFocus()
        else if (key === Qt.Key_Tab)
            buttons[(index + 1) % buttons.length].forceActiveFocus()
        else if (key === Qt.Key_Return || key === Qt.Key_Enter || key === Qt.Key_Space) {
            if (index >= 0) buttons[index].clicked()
        } else if (key === Qt.Key_Escape) root.close()
    }
    Connections {
        target: Recovery
        function onChanged() {
            if (!Recovery.busy) Qt.callLater(retryButton.forceActiveFocus)
        }
    }
    Connections {
        target: RecoveryController
        function onKeyRequested(key, modifiers) { root.navigate(key) }
        function onFocusDirectionRequested(key) { root.navigate(key) }
    }
    component RecoveryButton: Button {
        implicitHeight: 46 * root.uiScale
        font.family: Theme.fontFamily
        font.pixelSize: 13 * root.uiScale
        focusPolicy: Qt.StrongFocus
        Keys.onPressed: function(event) {
            if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right, Qt.Key_Return,
                 Qt.Key_Enter, Qt.Key_Escape].indexOf(event.key) >= 0) {
                root.navigate(event.key)
                event.accepted = true
            }
        }
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.foreground : Theme.mutedText
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: parent.activeFocus ? Theme.selection : Theme.darkerBackground
            border.color: parent.activeFocus ? Theme.accent : Theme.mutedText
            border.width: parent.activeFocus ? 2 : 1
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 760 * root.uiScale)
        spacing: 18 * root.uiScale
        Text {
            Layout.fillWidth: true
            text: Recovery.busy ? "Restoring your library" : "Restore needs attention"
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: 26 * root.uiScale
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: Recovery.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 15 * root.uiScale
        }
        Text {
            Layout.fillWidth: true
            text: Recovery.busy
                  ? "Keep Omakade open. Your library will open when the restore finishes."
                  : "Your library will stay closed until recovery finishes. Retry after fixing the problem, or undo the pending restore. The recovery folder contains your saved copies."
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 13 * root.uiScale
        }
        BusyIndicator { running: Recovery.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            visible: !Recovery.busy
            columnSpacing: 12 * root.uiScale
            rowSpacing: 12 * root.uiScale
            RecoveryButton {
                id: retryButton
                objectName: "restoreRetryButton"
                Layout.fillWidth: true
                text: "RETRY"
                enabled: !Recovery.busy
                onClicked: Recovery.retry()
            }
            RecoveryButton {
                id: undoButton
                objectName: "restoreUndoButton"
                Layout.fillWidth: true
                text: "UNDO RESTORE"
                enabled: !Recovery.busy && Recovery.canUndo
                onClicked: Recovery.undo()
            }
            RecoveryButton {
                id: folderButton
                objectName: "restoreFolderButton"
                Layout.fillWidth: true
                text: "RECOVERY FOLDER"
                enabled: !Recovery.busy
                onClicked: Qt.openUrlExternally(Recovery.recoveryFolder)
            }
            RecoveryButton {
                id: closeButton
                objectName: "restoreCloseButton"
                Layout.fillWidth: true
                text: "CLOSE"
                enabled: !Recovery.busy
                onClicked: root.close()
            }
        }
    }
}
