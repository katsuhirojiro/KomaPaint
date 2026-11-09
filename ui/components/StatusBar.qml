import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    color: KP.paneHeaderBg
    border.color: KP.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: KP.gapM
        Label {
            text: "Artwork cannot yet be saved or undone"
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontXS
        }
        Item {
            Layout.fillWidth: true
        }
        Label {
            text: Workspace.document.width + " × " + Workspace.document.height + " px"
            color: KP.textSubtle
            font.family: KP.fontMono
            font.pixelSize: KP.fontXS
        }
        Label {
            text: Math.round(Workspace.zoom) + "%"
            color: KP.textSubtle
            font.family: KP.fontMono
            font.pixelSize: KP.fontXS
        }
        Label {
            text: "Tool: " + Workspace.activeTool
            color: KP.textSubtle
            font.family: KP.fontUi
            font.pixelSize: KP.fontXS
        }
    }
}
