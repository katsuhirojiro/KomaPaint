import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Item {
    id: root
    implicitHeight: KP.toolbarHeight
    signal newDocumentRequested()

    Rectangle {
        anchors.fill: parent
        color: KP.toolbarBg
        border.color: KP.border
        border.width: 1
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        spacing: 2

        ToolbarButton {
            iconText: "N"
            tooltip: "New document"
            onClicked: root.newDocumentRequested()
        }
        ToolbarButton {
            iconText: "O"
            tooltip: "Open..."
            onClicked: /* placeholder */  {
            }
        }
        ToolbarButton {
            iconText: "S"
            tooltip: "Save"
            onClicked: /* placeholder */  {
            }
        }
        Sep {
        }

        ToolbarButton {
            iconText: "↶"
            tooltip: "Undo"
            enabled: Workspace.history.canUndo
            onClicked: Workspace.history.undo()
        }
        ToolbarButton {
            iconText: "↷"
            tooltip: "Redo"
            enabled: Workspace.history.canRedo
            onClicked: Workspace.history.redo()
        }
        Sep {
        }

        // Zoom controls
        ToolbarButton {
            iconText: "−"
            tooltip: "Zoom out"
            onClicked: Workspace.zoomOut()
        }
        Rectangle {
            width: 58
            height: 22
            radius: KP.radiusS
            color: KP.inputBg
            border.color: KP.border
            Label {
                anchors.centerIn: parent
                text: Math.round(Workspace.zoom) + "%"
                color: KP.text
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton /* reserved for dropdown later */
            }
        }
        ToolbarButton {
            iconText: "+"
            tooltip: "Zoom in"
            onClicked: Workspace.zoomIn()
        }
        ToolbarButton {
            iconText: "1:1"
            tooltip: "100%"
            onClicked: Workspace.resetZoom()
            fontPx: KP.fontXS
        }
        ToolbarButton {
            iconText: "⤧"
            tooltip: "Fit to window"
            onClicked: Workspace.zoomFit()
        }
        Sep {
        }

        // Spacer
        Item {
            Layout.fillWidth: true
        }

        ToolbarButton {
            iconText: "▦"
            tooltip: "Workspace"
            onClicked: /* placeholder */  {
            }
        }
    }
}
