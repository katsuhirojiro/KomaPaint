import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// Compact edit and view commands; panel options live in each dock header.
Rectangle {
    id: bar

    signal newDocumentRequested
    signal openRequested
    signal saveRequested

    property bool showEditGroup: true
    property bool showViewGroup: true
    readonly property string settingsKey: "ui.commandBar.hiddenGroups"
    readonly property real zoomPercent: Workspace.zoom

    implicitHeight: KP.commandBarHeight
    color: KP.toolbarBg

    function loadGroups() {
        const hidden = String(Workspace.sessionValue(settingsKey, ""))
        showEditGroup = hidden.indexOf("edit") < 0
        showViewGroup = hidden.indexOf("view") < 0
    }
    function storeGroups() {
        const hidden = []
        if (!showEditGroup) hidden.push("edit")
        if (!showViewGroup) hidden.push("view")
        Workspace.setSessionValue(settingsKey, hidden.join(","))
    }

    Component.onCompleted: loadGroups()

    // A divider between command groups: a hairline that stops short of the
    // bar's height so the row reads as one continuous surface.
    component BarRule: Rectangle {
        implicitWidth: 1
        implicitHeight: 18
        color: KP.hairline
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: KP.hairline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: KP.gapM
        anchors.rightMargin: KP.gapM
        spacing: KP.gapS

        // File actions remain small icon commands; brush-specific controls do
        // not belong in this shared command bar.
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                objectName: "openToolbarButton"
                glyph: "open"
                tip: qsTr("Open project (Ctrl+O)")
                onClicked: bar.openRequested()
            }
            KpButton {
                objectName: "saveToolbarButton"
                glyph: "save"
                tip: qsTr("Save project (Ctrl+S)")
                enabled: Workspace.document !== null
                onClicked: bar.saveRequested()
            }
        }

        BarRule { Layout.alignment: Qt.AlignVCenter }

        // ---- Edit
        Row {
            visible: bar.showEditGroup
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                glyph: "undo"
                tip: Workspace.history.canUndo
                     ? qsTr("Undo %1 (Ctrl+Z)").arg(Workspace.history.undoText)
                     : qsTr("Nothing to undo")
                enabled: Workspace.history.canUndo
                onClicked: Workspace.history.undo()
            }
            KpButton {
                glyph: "redo"
                tip: Workspace.history.canRedo
                     ? qsTr("Redo %1 (Ctrl+Shift+Z)").arg(Workspace.history.redoText)
                     : qsTr("Nothing to redo")
                enabled: Workspace.history.canRedo
                onClicked: Workspace.history.redo()
            }
        }

        BarRule { visible: bar.showEditGroup && bar.showViewGroup; Layout.alignment: Qt.AlignVCenter }

        // ---- View
        Row {
            visible: bar.showViewGroup
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                glyph: "zoom-out"
                tip: qsTr("Zoom out")
                onClicked: Workspace.zoomOut()
            }
            Item {
                width: 60
                height: KP.controlHeight

                Rectangle {
                    anchors.fill: parent
                    radius: KP.radiusS
                    color: zoomMouse.containsMouse ? KP.hoverOverlay : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(bar.zoomPercent) + "%"
                        color: KP.text
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontS
                    }
                }
                MouseArea {
                    id: zoomMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: zoomMenu.popup()
                }
                KpMenu {
                    id: zoomMenu
                    y: parent.height + 2
                    KpMenuItem { text: qsTr("Fit to window\tCtrl+0"); onTriggered: Workspace.zoomFit() }
                    KpMenuItem { text: qsTr("Actual size\tCtrl+1"); onTriggered: Workspace.resetZoom() }
                    KpMenuSeparator { }
                    KpMenuItem { text: qsTr("200%"); onTriggered: Workspace.zoom = 200 }
                    KpMenuItem { text: qsTr("400%"); onTriggered: Workspace.zoom = 400 }
                    KpMenuItem { text: qsTr("800%"); onTriggered: Workspace.zoom = 800 }
                }
            }
            KpButton {
                glyph: "zoom-in"
                tip: qsTr("Zoom in")
                onClicked: Workspace.zoomIn()
            }
            KpButton {
                glyph: "zoom-fit"
                tip: qsTr("Fit to window (Ctrl+0)")
                onClicked: Workspace.zoomFit()
            }
            KpButton {
                glyph: "actual"
                tip: qsTr("Actual size (Ctrl+1)")
                onClicked: Workspace.resetZoom()
            }
        }

        Item { Layout.fillWidth: true }

    }

    KpMenu {
        id: barMenu
        KpMenuItem {
            text: qsTr("Edit group")
            checkable: true
            checked: bar.showEditGroup
            onTriggered: { bar.showEditGroup = !bar.showEditGroup; bar.storeGroups() }
        }
        KpMenuItem {
            text: qsTr("View group")
            checkable: true
            checked: bar.showViewGroup
            onTriggered: { bar.showViewGroup = !bar.showViewGroup; bar.storeGroups() }
        }
    }

    // Right-click anywhere on the bar to choose which groups it shows.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: barMenu.popup()
    }
}
