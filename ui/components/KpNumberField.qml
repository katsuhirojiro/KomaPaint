import QtQuick
import QtQuick.Controls
import "../themes"

// A compact numeric entry: right-aligned tabular digits in a well, with an
// optional unit and an optional pair of steppers.
//
// Behaviour that matters to an artist: the whole value is selected on focus so
// typing replaces it, Up/Down step by `step` (Shift for a tenth), Enter or
// leaving the field commits, Escape restores, and a value outside the range is
// clamped rather than rejected. `value` stays authoritative — the owner writes
// it to the model and the field follows.
Item {
    id: root

    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1
    property string unit: ""
    property bool steppers: false
    property alias horizontalAlignment: input.horizontalAlignment
    // True while the digits are being typed into.
    readonly property alias editing: input.activeFocus

    signal edited(int value)

    implicitWidth: 64
    implicitHeight: KP.controlHeight
    opacity: enabled ? 1 : KP.disabledOpacity

    function clamp(v) { return Math.max(root.from, Math.min(root.to, Math.round(v))) }
    function commit(v) {
        const next = clamp(v)
        if (next !== root.value) root.edited(next)
    }

    Rectangle {
        id: well
        anchors.fill: parent
        radius: KP.radiusS
        color: input.activeFocus ? KP.inputBgHover : KP.inputBg
        border.width: 1
        border.color: !root.enabled ? KP.border : input.activeFocus ? KP.accent : (mouse.containsMouse ? KP.borderLight : KP.border)
        Behavior on color { ColorAnimation { duration: KP.durationFast } }
        Behavior on border.color { ColorAnimation { duration: KP.durationFast } }

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.leftMargin: 5
            anchors.right: root.unit.length > 0 ? unitLabel.left : (root.steppers ? steppersColumn.left : parent.right)
            anchors.rightMargin: root.unit.length > 0 ? 2 : 4
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignRight
            color: KP.text
            clip: true
            selectByMouse: true
            selectionColor: KP.selection
            selectedTextColor: KP.textOnAccent
            font.family: KP.fontMono
            font.pixelSize: KP.fontS + 1
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
            validator: IntValidator { bottom: root.from; top: root.to }
            text: root.value

            onActiveFocusChanged: if (activeFocus) selectAll()
            onEditingFinished: {
                const typed = parseInt(text)
                if (!isNaN(typed)) root.commit(typed)
                text = Qt.binding(function() { return root.value })
            }
            Keys.onUpPressed: function(event) {
                root.commit(root.value + (event.modifiers & Qt.ShiftModifier ? Math.max(1, Math.round(root.step / 10)) : root.step))
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                root.commit(root.value - (event.modifiers & Qt.ShiftModifier ? Math.max(1, Math.round(root.step / 10)) : root.step))
                event.accepted = true
            }
            Keys.onReturnPressed: function(event) { focus = false; event.accepted = true }
            Keys.onEnterPressed: function(event) { focus = false; event.accepted = true }
            Keys.onEscapePressed: function(event) {
                text = Qt.binding(function() { return root.value })
                focus = false
                event.accepted = true
            }
        }

        Text {
            id: unitLabel
            anchors.right: root.steppers ? steppersColumn.left : parent.right
            anchors.rightMargin: root.steppers ? 0 : 6
            anchors.verticalCenter: parent.verticalCenter
            visible: text.length > 0
            text: root.unit
            color: KP.textSubtle
            font.pixelSize: KP.fontMicro + 1
        }

        Column {
            id: steppersColumn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            visible: root.steppers
            Repeater {
                model: ["up", "down"]
                Item {
                    required property string modelData
                    width: 14
                    height: (root.height - 2) / 2
                    KpGlyph {
                        anchors.centerIn: parent
                        name: modelData === "up" ? "chevron-up" : "chevron-down"
                        size: 10
                        color: stepMouse.containsMouse ? KP.iconHover : KP.iconOff
                    }
                    MouseArea {
                        id: stepMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.commit(root.value + (modelData === "up" ? root.step : -root.step))
                    }
                }
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            cursorShape: Qt.IBeamCursor
        }
    }
}
