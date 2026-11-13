import QtQuick
import QtQuick.Controls
import "../themes"

// Square button that shows one icon from ui/assets/icons.
//
// States: normal, hover, pressed, selected and unavailable. A button whose
// function does not exist yet is `available: false`: it is drawn dimmed, does
// nothing when clicked and says so in its tooltip. (It is not `enabled: false`
// because a disabled control cannot show a tooltip.)
AbstractButton {
    id: btn

    property string iconName: ""
    property int iconSize: 20
    property int cell: 28
    property string tooltip: ""
    property bool selected: false
    property bool available: true
    // A toggle that is currently off (e.g. the eye of a hidden layer).
    property bool dimmed: false
    readonly property bool live: available && enabled

    signal activated()

    implicitWidth: cell
    implicitHeight: cell
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: tooltip
    onClicked: if (live) activated()

    ToolTip.text: available ? tooltip : qsTr("%1 — not available yet").arg(tooltip)
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.delay: 500

    background: Rectangle {
        radius: KP.radiusM
        color: btn.selected ? KP.toolActive
             : !btn.live ? "transparent"
             : btn.down ? KP.buttonBgPress
             : btn.hovered ? KP.buttonBgHover : "transparent"
        border.width: 1
        border.color: btn.selected ? KP.accent : "transparent"
        opacity: btn.selected && !btn.live ? 0.5 : 1
    }

    contentItem: Item {
        KpIcon {
            anchors.centerIn: parent
            name: btn.iconName
            size: btn.iconSize
            color: !btn.live ? KP.iconDisabled
                 : btn.selected ? KP.iconSelected
                 : btn.dimmed ? (btn.hovered ? KP.iconNormal : KP.iconOff)
                 : btn.hovered ? KP.iconHover : KP.iconNormal
        }
    }
}
