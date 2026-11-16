import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The status rail: the facts about the current document, in one line.
//
// It is a band of small tracked labels separated by hairlines rather than a row
// of assorted widgets, so the eye can scan it and ignore it. Values are set in
// tabular figures so they do not jitter while you paint, and the zoom cluster on
// the right is directly usable (minus, readout, plus, fit) because the zoom is
// the one status value an artist changes constantly.
Rectangle {
    id: root

    property string toolLabel: ""
    property string message: ""

    implicitHeight: KP.statusbarHeight
    color: KP.statusbarBg

    function baseName(path) {
        var normalized = path.replace(/\\/g, "/")
        var parts = normalized.split("/")
        return parts[parts.length - 1]
    }
    readonly property bool saved: Workspace.currentFile.length > 0
    readonly property string documentName: saved ? baseName(Workspace.currentFile) : qsTr("Untitled")

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: KP.hairline
    }

    component Divider: Rectangle {
        implicitWidth: 1
        implicitHeight: 11
        color: KP.hairline
    }
    component Fact: Text {
        color: KP.textMuted
        font.pixelSize: KP.fontXS
        font.family: KP.fontMono
        verticalAlignment: Text.AlignVCenter
    }
    component Caption: Text {
        color: KP.textSectionLabel
        font.pixelSize: KP.fontMicro
        font.letterSpacing: KP.trackingMicro
        verticalAlignment: Text.AlignVCenter
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: KP.gapL
        anchors.rightMargin: KP.gapM

        // ---- Document identity
        RowLayout {
            id: identity
            anchors.left: parent.left
            anchors.right: zoomCluster.left
            anchors.rightMargin: KP.gapL
            anchors.verticalCenter: parent.verticalCenter
            spacing: KP.gapS

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 3.5
                color: Workspace.document && Workspace.document.modified ? KP.ember : KP.textSubtle
                opacity: Workspace.document && Workspace.document.modified ? 1 : 0.5
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 220
                text: root.documentName
                color: Workspace.document && Workspace.document.modified ? KP.text : KP.textMuted
                font.pixelSize: KP.fontXS + 1
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Divider { Layout.alignment: Qt.AlignVCenter }
            Fact {
                Layout.alignment: Qt.AlignVCenter
                text: Workspace.document ? (Workspace.document.width + " × " + Workspace.document.height) : ""
            }
            Caption { Layout.alignment: Qt.AlignVCenter; text: qsTr("PX") }

            // ---- Tool and brush
            Item { Layout.preferredWidth: KP.gapM }
            Caption { Layout.alignment: Qt.AlignVCenter; text: qsTr("TOOL") }
            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.toolLabel.length > 0 ? root.toolLabel
                                                : Workspace.activeTool.replace(/^./, function(c) { return c.toUpperCase() })
                color: KP.text
                font.pixelSize: KP.fontXS + 1
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Divider { Layout.alignment: Qt.AlignVCenter }
            Caption { Layout.alignment: Qt.AlignVCenter; text: qsTr("ENGINE") }
            Text {
                objectName: "statusBarBrushEngine"
                Layout.alignment: Qt.AlignVCenter
                text: Workspace.realisticEngine ? qsTr("Realistic") : qsTr("Digital")
                color: Workspace.realisticEngine ? KP.textAccent : KP.textMuted
                font.pixelSize: KP.fontXS + 1
                verticalAlignment: Text.AlignVCenter
            }
            Divider { Layout.alignment: Qt.AlignVCenter }
            Caption { Layout.alignment: Qt.AlignVCenter; text: qsTr("SIZE") }
            Fact { Layout.alignment: Qt.AlignVCenter; text: Workspace.brush.size + " px" }
            Caption { Layout.alignment: Qt.AlignVCenter; text: qsTr("OPACITY") }
            Fact { Layout.alignment: Qt.AlignVCenter; text: Workspace.brush.opacity + "%" }
            Caption {
                Layout.alignment: Qt.AlignVCenter
                visible: root.message.length > 0
                text: root.message
                color: KP.textSubtle
            }
        }

        // ---- Zoom
        Row {
            id: zoomCluster
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: KP.gapXS
            height: parent.height

            IconButton {
                glyph: "zoom-out"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Zoom out")
                onActivated: Workspace.zoomOut()
            }
            AbstractButton {
                id: zoomReadout
                width: 46
                height: KP.controlHeightSmall
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.name: qsTr("Zoom level")
                ToolTip.text: qsTr("Zoom level")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                onClicked: zoomMenu.popup()
                background: Rectangle {
                    radius: KP.radiusXS
                    color: zoomReadout.hovered ? KP.hoverOverlay : "transparent"
                }
                contentItem: Text {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: Math.round(Workspace.zoom) + "%"
                    color: KP.text
                    font.pixelSize: KP.fontXS + 1
                    font.family: KP.fontMono
                }
            }
            IconButton {
                glyph: "zoom-in"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Zoom in")
                onActivated: Workspace.zoomIn()
            }
            IconButton {
                glyph: "zoom-fit"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Fit to window (Ctrl+0)")
                onActivated: Workspace.zoomFit()
            }
        }

        KpMenu {
            id: zoomMenu
            x: zoomCluster.width - width
            y: -height - KP.gapS
            KpMenuItem { text: qsTr("Fit to window\tCtrl+0"); onTriggered: Workspace.zoomFit() }
            KpMenuItem { text: qsTr("Actual size\tCtrl+1"); onTriggered: Workspace.resetZoom() }
            KpMenuSeparator { }
            KpMenuItem { text: qsTr("200%"); onTriggered: Workspace.zoom = 200 }
            KpMenuItem { text: qsTr("400%"); onTriggered: Workspace.zoom = 400 }
            KpMenuItem { text: qsTr("800%"); onTriggered: Workspace.zoom = 800 }
        }
    }
}
