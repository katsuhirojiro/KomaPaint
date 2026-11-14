import QtQuick
import QtQuick.Controls
import "../themes"

// A square command target: tool palette entries, layer row toggles, panel
// footer commands.
//
// The icon is either the authored PNG artwork (`iconName`, tools and layers) or
// a vector glyph (`glyph`, interface chrome). States are the theme's, not the
// component's: hover is an overlay, selection is a wash plus a 2 px ember bar
// down the left edge, and a button whose function the application does not have
// is drawn dimmed with a tooltip that says so (it is not `enabled: false`,
// because a disabled control cannot explain itself).
AbstractButton {
    id: btn

    property string iconName: ""
    property string glyph: ""
    property int iconSize: KP.iconM
    property int cell: 28
    property string tooltip: ""
    property bool selected: false
    property bool available: true
    // A toggle that is currently off (e.g. the eye of a hidden layer).
    property bool dimmed: false
    // Draws the selection bar. Off for buttons that are merely toggled on.
    property bool accentBar: false
    property int radius: KP.radiusS

    readonly property bool live: available && enabled

    signal activated()

    implicitWidth: cell
    implicitHeight: cell
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tooltip
    onClicked: if (live) activated()

    ToolTip.text: available ? tooltip : qsTr("%1 — not available yet").arg(tooltip)
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.delay: 500

    background: Rectangle {
        radius: btn.radius
        color: btn.selected ? (btn.accentBar ? KP.hoverOverlay : KP.accentWash)
             : !btn.live ? "transparent"
             : btn.down ? KP.pressOverlay
             : btn.hovered ? KP.hoverOverlay : "transparent"
        border.width: btn.activeFocus ? KP.focusWidth : 0
        border.color: KP.focusRing
        Behavior on color { ColorAnimation { duration: KP.durationFast; easing.type: KP.easingCurve } }

        Rectangle {
            visible: btn.accentBar && btn.selected
            anchors.left: parent.left
            anchors.leftMargin: 1
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: Math.min(parent.height - 10, 18)
            radius: 1
            color: KP.ember
        }
    }

    contentItem: Item {
        KpIcon {
            anchors.centerIn: parent
            visible: btn.iconName.length > 0
            name: btn.iconName
            size: btn.iconSize
            color: !btn.live ? KP.iconDisabled
                 : btn.selected ? KP.iconSelected
                 : btn.dimmed ? (btn.hovered ? KP.iconNormal : KP.iconOff)
                 : btn.hovered ? KP.iconHover : KP.iconNormal
        }
        KpGlyph {
            anchors.centerIn: parent
            visible: btn.glyph.length > 0
            name: btn.glyph
            size: btn.iconSize
            color: !btn.live ? KP.iconDisabled
                 : btn.selected ? KP.iconSelected
                 : btn.dimmed ? (btn.hovered ? KP.iconNormal : KP.iconOff)
                 : btn.hovered ? KP.iconHover : KP.iconNormal
        }
    }
}
