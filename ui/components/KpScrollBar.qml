import QtQuick
import QtQuick.Controls
import "../themes"

// A thin overlay scrollbar: a rounded handle on a transparent track, with no
// stepper buttons. It appears only while there is something to scroll, so a
// panel never shows a permanent grey rail down its side.
ScrollBar {
    id: bar

    // `vertical` is inherited from ScrollBar; the handle is drawn for either
    // orientation from the same tokens.
    padding: 0
    minimumSize: 0.08

    readonly property bool isVertical: orientation === Qt.Vertical

    contentItem: Rectangle {
        implicitWidth: bar.isVertical ? 4 : 40
        implicitHeight: bar.isVertical ? 40 : 4
        radius: width / 2
        color: bar.pressed ? KP.accent
             : bar.hovered ? KP.textMuted
             : KP.textSubtle
        opacity: bar.policy === ScrollBar.AlwaysOff ? 0
               : (bar.pressed || bar.hovered ? 1 : 0.55)
        Behavior on opacity { NumberAnimation { duration: KP.durationFast } }
        Behavior on color { ColorAnimation { duration: KP.durationFast } }
    }

    background: Item { }
}
