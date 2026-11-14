import QtQuick
import QtQuick.Controls
import "../themes"

// The interface's popup menu: a floating panel with a hairline, a soft shadow
// and generous hit areas. Menus are the one place a shadow is welcome, because
// they genuinely float above the workspace.
Menu {
    id: menu

    padding: KP.gapS
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: KP.durationFast; easing.type: KP.easingCurve }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: KP.durationFast }
    }

    // A floating surface: the tonal step up from the panels plus a hairline is
    // enough separation; the "shadow" is a single darker outline offset by one
    // pixel, which costs nothing on integrated graphics.
    background: Rectangle {
        radius: KP.radiusM
        color: KP.floatingBg
        border.width: 1
        border.color: KP.border

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "transparent"
            border.width: 1
            border.color: KP.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.7)
        }
    }
}
