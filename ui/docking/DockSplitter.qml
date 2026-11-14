import QtQuick
import QtQuick.Controls
import "../components"
import "../themes"

// The handle between two children of a split. It is 6 px wide so it is easy to
// grab, but it only shows on hover or while dragging: the workspace stays
// quiet until the user reaches for it.
Item {
    id: handle

    property var owner: null
    required property int index
    // Side of the dock area this handle can collapse ("" for a plain handle).
    property string collapseSide: ""
    property bool areaCollapsed: false

    readonly property bool areaControlVisible: handle.collapseSide.length > 0 && !handle.areaCollapsed
    readonly property bool areaControlEnabled: !(handle.owner && handle.owner.workspaceRoot
                                                 && handle.owner.workspaceRoot.dragging)
    readonly property bool areaBarVertical: handle.collapseSide === "left"
                                            || handle.collapseSide === "right"
    // Points away from the workspace, the way the area will fold.
    readonly property string areaGlyph: handle.collapseSide === "left" ? "chevron-left"
                                        : handle.collapseSide === "right" ? "chevron-right"
                                        : handle.collapseSide === "top" ? "chevron-up" : "chevron-down"

    readonly property bool horizontal: handle.owner ? handle.owner.horizontal : true
    property bool hovered: false
    property bool pressed: false

    z: 5

    // The boundary is always a hairline, so the workspace's structure is
    // legible; reaching for it turns the line into a short accent bar, which is
    // the whole "you can drag this" affordance.
    Rectangle {
        anchors.centerIn: parent
        width: handle.horizontal ? (handle.hovered || handle.pressed ? 3 : 1) : 1
        height: handle.horizontal ? 1 : (handle.hovered || handle.pressed ? 3 : 1)
        radius: 1
        color: (handle.hovered || handle.pressed) ? KP.accent : KP.hairline
        Behavior on width { NumberAnimation { duration: KP.durationFast } }
        Behavior on height { NumberAnimation { duration: KP.durationFast } }
    }

    // The edge control: a chevron on the divider between a dock area and the
    // canvas that folds that area away. Small enough to stay out of the way,
    // and it never reacts while a panel is being dragged.
    Item {
        id: areaControl
        objectName: "dockAreaControl_" + handle.collapseSide
        visible: handle.areaControlVisible
        enabled: handle.areaControlEnabled
        width: handle.areaBarVertical ? 14 : 22
        height: handle.areaBarVertical ? 22 : 14
        // Straddles the handle so it stays reachable on a 6 px divider.
        x: handle.areaBarVertical ? Math.round((handle.width - width) / 2) : 6
        y: handle.areaBarVertical ? 6 : Math.round((handle.height - height) / 2)
        z: 2

        Rectangle {
            anchors.fill: parent
            radius: KP.radiusS
            color: areaMouse.containsMouse ? KP.floatingBg : "transparent"
            border.width: areaMouse.containsMouse ? 1 : 0
            border.color: KP.border
        }

        // A faint chevron on the boundary: visible enough to find, quiet enough
        // to ignore until wanted.
        KpGlyph {
            anchors.centerIn: parent
            name: handle.areaGlyph
            size: 11
            color: areaMouse.containsMouse ? KP.iconHover : KP.iconOff
            opacity: areaMouse.containsMouse ? 1 : 0.55
        }

        MouseArea {
            id: areaMouse
            // A little larger than the visible chevron: an easy target with a
            // mouse and a realistic one with a finger.
            anchors.fill: parent
            anchors.margins: -3
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            onClicked: if (handle.owner) handle.owner.toggleArea(handle.collapseSide)
        }

        ToolTip.delay: 400
        ToolTip.visible: areaMouse.containsMouse
        ToolTip.text: qsTr("Collapse the %1 panel area").arg(handle.collapseSide)

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Collapse the %1 panel area").arg(handle.collapseSide)
        activeFocusOnTab: true
        Keys.onReturnPressed: if (handle.owner) handle.owner.toggleArea(handle.collapseSide)
        Keys.onSpacePressed: if (handle.owner) handle.owner.toggleArea(handle.collapseSide)
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: handle.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
        preventStealing: true

        onEntered: handle.hovered = true
        onExited: handle.hovered = false
        onPressed: {
            handle.pressed = true
            if (handle.owner) handle.owner.beginResize(handle.index)
        }
        onPositionChanged: function (event) {
            if (!pressed || !handle.owner) return
            const at = mapToItem(handle.owner, event.x, event.y)
            handle.owner.resizeTo(handle.index, handle.horizontal ? at.x : at.y)
        }
        onReleased: {
            handle.pressed = false
            if (handle.owner) handle.owner.endResize(handle.index)
        }
        onCanceled: handle.pressed = false
    }
}
