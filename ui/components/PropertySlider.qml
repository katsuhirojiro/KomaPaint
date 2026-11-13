import QtQuick
import QtQuick.Controls
import "../themes"

// One numeric tool property: label, editable value and a slider bar.
//
// The slider bar fills from the left like a level meter. `curve` above 1
// gives the low end of the range more room (used for brush size, where the
// difference between 2 px and 4 px matters more than 200 px and 202 px).
Item {
    id: root

    property string label: ""
    property string unit: ""
    property int from: 0
    property int to: 100
    property int value: 0
    property real curve: 1
    // Emitted when the user changes the value; the owner writes it to the model.
    signal edited(int value)

    implicitHeight: 40
    implicitWidth: 200
    opacity: enabled ? 1 : 0.45

    function clamp(v) { return Math.max(from, Math.min(to, Math.round(v))) }
    function positionFor(v) { return to === from ? 0 : Math.pow((clamp(v) - from) / (to - from), 1 / curve) }
    function valueFor(position) { return clamp(from + Math.pow(Math.max(0, Math.min(1, position)), curve) * (to - from)) }
    function commit(v) { const next = clamp(v); if (next !== value) edited(next) }

    Label {
        id: caption
        anchors.left: parent.left
        anchors.verticalCenter: field.verticalCenter
        anchors.right: field.left
        anchors.rightMargin: KP.gapM
        text: root.label
        color: KP.text
        font.family: KP.fontUi
        font.pixelSize: KP.fontM
        elide: Text.ElideRight
    }

    Rectangle {
        id: field
        anchors.right: parent.right
        anchors.top: parent.top
        width: 62
        height: KP.controlHeight - 2
        radius: KP.radiusS
        color: KP.inputBg
        border.width: 1
        border.color: input.activeFocus ? KP.accent : KP.border

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.right: unitLabel.left
            anchors.leftMargin: 4
            anchors.rightMargin: unitLabel.text.length > 0 ? 3 : 0
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: TextInput.AlignRight
            color: KP.text
            selectionColor: KP.selection
            selectedTextColor: KP.textOnAccent
            font.family: KP.fontMono
            font.pixelSize: KP.fontM
            selectByMouse: true
            clip: true
            validator: IntValidator { bottom: 0; top: root.to }
            text: root.value
            onEditingFinished: {
                const typed = parseInt(text)
                if (!isNaN(typed)) root.commit(typed)
                text = Qt.binding(function() { return root.value })
            }
            onAccepted: focus = false
            Keys.onUpPressed: root.commit(root.value + 1)
            Keys.onDownPressed: root.commit(root.value - 1)
            Keys.onEscapePressed: { text = Qt.binding(function() { return root.value }); focus = false }
        }
        Label {
            id: unitLabel
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            text: root.unit
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
        }
    }

    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        height: 8
        radius: 2
        color: KP.inputBg
        border.width: 1
        border.color: KP.border

        Rectangle {
            x: 1
            y: 1
            height: parent.height - 2
            width: Math.max(0, (parent.width - 2) * root.positionFor(root.value))
            radius: 1
            color: drag.pressed ? KP.accent : (drag.containsMouse ? KP.accent : KP.accentSoft)
        }
        Rectangle {
            // handle
            x: Math.round((parent.width - 2) * root.positionFor(root.value)) - 1
            y: -2
            width: 3
            height: parent.height + 4
            radius: 1
            color: drag.pressed || drag.containsMouse ? KP.textOnAccent : KP.text
        }
        MouseArea {
            id: drag
            anchors.fill: parent
            anchors.topMargin: -6
            anchors.bottomMargin: -4
            hoverEnabled: true
            preventStealing: true
            function apply(x) { root.commit(root.valueFor((x - 1) / (track.width - 2))) }
            onPressed: function(mouse) { apply(mouse.x) }
            onPositionChanged: function(mouse) { if (pressed) apply(mouse.x) }
        }
    }
}
