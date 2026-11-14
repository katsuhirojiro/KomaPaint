import QtQuick
import QtQuick.Controls
import "../components"
import "../themes"

// What a collapsed dock area leaves behind: a slim bar along the workspace edge
// with a chevron that brings the area back. The panels behind it are hidden,
// never unloaded, so expanding restores the same tab group, sizes and tabs.
Item {
    id: strip

    // "left" / "right" / "top" / "bottom" — the area this strip belongs to.
    property string side: "left"
    property var workspaceRoot: null

    readonly property bool verticalBar: strip.side === "left" || strip.side === "right"
    // Points back into the workspace, the way the area will expand.
    readonly property string glyph: strip.side === "left" ? "chevron-right"
                                   : strip.side === "right" ? "chevron-left"
                                   : strip.side === "top" ? "chevron-down" : "chevron-up"

    signal revealRequested()

    objectName: "dockAreaStrip_" + strip.side

    Rectangle {
        anchors.fill: parent
        color: stripMouse.containsMouse ? KP.paneBg : KP.paneHeaderBg
    }
    // Only the edge facing the canvas is marked, so the strip reads as the
    // exposed edge of a folded area rather than as another box.
    Rectangle {
        visible: strip.verticalBar
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 1
        color: KP.hairline
    }
    Rectangle {
        visible: !strip.verticalBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: KP.hairline
    }

    KpGlyph {
        anchors.centerIn: parent
        name: strip.glyph
        size: 11
        color: stripMouse.containsMouse ? KP.iconHover : KP.iconOff
    }

    MouseArea {
        id: stripMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Never while a panel is being dragged across the workspace.
        enabled: !(strip.workspaceRoot && strip.workspaceRoot.dragging)
        onClicked: strip.revealRequested()
    }

    ToolTip.delay: 400
    ToolTip.visible: stripMouse.containsMouse
    ToolTip.text: qsTr("Show the %1 panels").arg(strip.side)

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Show the %1 panels").arg(strip.side)
    activeFocusOnTab: true
    Keys.onReturnPressed: strip.revealRequested()
    Keys.onSpacePressed: strip.revealRequested()
}
