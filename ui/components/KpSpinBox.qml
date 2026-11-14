import QtQuick
import QtQuick.Controls
import "../themes"

// The interface's spin box.
//
// The stock content item is kept on purpose: the box stays an ordinary SpinBox,
// with its editable field, validator, valueFromText/textFromValue hooks and
// valueModified signal, so a form wired to those keeps working. What is
// replaced is the chrome around it — the frame and the two stepper cells — and
// the ink, which the dialog palette already supplies.
SpinBox {
    id: spin

    implicitHeight: KP.controlHeight
    implicitWidth: 96
    font.pixelSize: KP.fontS

    background: Rectangle {
        color: KP.inputBg
        radius: KP.radiusS
        border.width: 1
        border.color: spin.activeFocus ? KP.accent : KP.border
        Behavior on border.color { ColorAnimation { duration: KP.durationFast } }
    }

    up.indicator: Item {
        x: spin.width - width - 2
        y: 2
        width: 16
        height: (spin.height - 4) / 2

        Rectangle {
            anchors.fill: parent
            radius: KP.radiusXS
            color: spin.up.pressed ? KP.pressOverlay
                 : spin.up.hovered ? KP.hoverOverlay : "transparent"
        }
        KpGlyph {
            anchors.centerIn: parent
            name: "chevron-up"
            size: 7
            color: spin.enabled ? (spin.up.hovered ? KP.iconHover : KP.iconNormal) : KP.iconDisabled
        }
    }

    down.indicator: Item {
        x: spin.width - width - 2
        y: spin.height / 2
        width: 16
        height: (spin.height - 4) / 2

        Rectangle {
            anchors.fill: parent
            radius: KP.radiusXS
            color: spin.down.pressed ? KP.pressOverlay
                 : spin.down.hovered ? KP.hoverOverlay : "transparent"
        }
        KpGlyph {
            anchors.centerIn: parent
            name: "chevron-down"
            size: 7
            color: spin.enabled ? (spin.down.hovered ? KP.iconHover : KP.iconNormal) : KP.iconDisabled
        }
    }
}
