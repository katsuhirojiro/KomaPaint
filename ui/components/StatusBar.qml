import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    id: root
    color: KP.paneHeaderBg
    border.color: KP.border
    border.width: 1

    function baseName(path) {
        var normalized = path.replace(/\\/g, "/");
        var parts = normalized.split("/");
        return parts[parts.length - 1];
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: KP.gapM
        Label {
            text: Workspace.currentFile.length > 0
                  ? root.baseName(Workspace.currentFile) + (Workspace.document.modified ? " *" : "")
                  : qsTr("Untitled (not saved yet)")
            color: Workspace.currentFile.length > 0 ? KP.textMuted : "#d8a657"
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
