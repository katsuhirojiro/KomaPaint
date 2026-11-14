import QtQuick
import QtQuick.Controls
import "../themes"

// One numeric tool property: a name, an editable value and a level rail.
//
// The rail is read as a level meter that happens to be draggable: it fills from
// the left in the accent colour and ends in a round knob, which is easier to
// grab than a 2 px handle and reads as "this is a value" rather than "this is a
// scrollbar". Everything an artist expects works:
//   * click anywhere on the rail to jump there, drag to scrub,
//   * hold Shift while dragging for a tenth of the travel (fine tuning),
//   * double-click to return to `resetValue`,
//   * the mouse wheel steps by one, arrows move by one when focused,
//   * the value field takes typed input and step/Shift-step from the keyboard.
//
// `curve` above 1 gives the low end of the range more travel, which is what
// brush size needs: 2 px → 4 px is a bigger difference than 200 px → 202 px.
Item {
    id: root

    property string label: ""
    property string unit: ""
    property int from: 0
    property int to: 100
    property int value: 0
    property real curve: 1
    property int step: 1
    // Double-click returns here. Leave negative for "no reset".
    property int resetValue: -1
    // Set by the owner when the tool has no such property.
    property bool available: true

    signal edited(int value)

    implicitHeight: 40
    implicitWidth: 200
    opacity: enabled && available ? 1 : KP.disabledOpacity

    function clamp(v) { return Math.max(from, Math.min(to, Math.round(v))) }
    function positionFor(v) { return to === from ? 0 : Math.pow((clamp(v) - from) / (to - from), 1 / curve) }
    function valueFor(position) { return clamp(from + Math.pow(Math.max(0, Math.min(1, position)), curve) * (to - from)) }
    function commit(v) { const next = clamp(v); if (next !== value) edited(next) }

    Text {
        id: caption
        anchors.left: parent.left
        anchors.right: field.left
        anchors.rightMargin: KP.gapM
        anchors.top: parent.top
        height: KP.controlHeightSmall + 2
        text: root.label
        color: KP.text
        font.pixelSize: KP.fontS + 1
        font.family: KP.fontUi
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    KpNumberField {
        id: field
        anchors.right: parent.right
        anchors.top: parent.top
        width: 62
        height: KP.controlHeightSmall + 2
        value: root.value
        from: root.from
        to: root.to
        step: root.step
        unit: root.unit
        enabled: root.enabled && root.available
        onEdited: function(v) { root.commit(v) }
    }

    Item {
        id: rail
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        height: 6

        readonly property real travel: Math.max(0, width - 2)
        readonly property real knobX: 1 + travel * root.positionFor(root.value)

        Rectangle {
            id: groove
            anchors.fill: parent
            radius: height / 2
            color: KP.inputBg
            border.width: 1
            border.color: KP.border

            Rectangle {
                id: fill
                x: 1
                y: 1
                height: parent.height - 2
                width: Math.max(0, rail.knobX - 1)
                radius: height / 2
                color: KP.ember
                opacity: railMouse.pressed || railMouse.containsMouse ? 1 : 0.85
                Behavior on opacity { NumberAnimation { duration: KP.durationFast } }
            }
        }

        Rectangle {
            id: knob
            x: Math.round(rail.knobX - width / 2)
            anchors.verticalCenter: parent.verticalCenter
            width: railMouse.pressed || railMouse.containsMouse ? 11 : 9
            height: width
            radius: width / 2
            color: KP.ember
            border.width: 1.5
            border.color: KP.isDark ? "#e8e3da" : "#ffffff"
            Behavior on width { NumberAnimation { duration: KP.durationFast; easing.type: KP.easingCurve } }
        }

        Rectangle {
            // Focus ring: only shown when the rail itself has keyboard focus.
            anchors.fill: parent
            anchors.margins: -3
            radius: (height + 6) / 2
            color: "transparent"
            border.width: 1
            border.color: KP.accent
            visible: railMouse.activeFocus
        }

        MouseArea {
            id: railMouse
            anchors.fill: parent
            anchors.topMargin: -7
            anchors.bottomMargin: -4
            hoverEnabled: true
            preventStealing: true
            focus: false
            activeFocusOnTab: true

            property real dragStartX: 0
            property int dragStartValue: 0
            property bool tuning: false

            function positionAt(x) { return (x - 1) / Math.max(1, rail.travel) }

            onPressed: function(mouse) {
                dragStartX = mouse.x
                dragStartValue = root.value
                tuning = (mouse.modifiers & Qt.ShiftModifier) !== 0
                forceActiveFocus(Qt.MouseFocusReason)
                if (!tuning) root.commit(root.valueFor(positionAt(mouse.x)))
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (tuning || (mouse.modifiers & Qt.ShiftModifier)) {
                    // Fine tuning is relative so the value cannot jump on press.
                    const delta = (mouse.x - dragStartX) / Math.max(1, rail.travel) * (root.to - root.from) * 0.1
                    root.commit(dragStartValue + delta)
                } else {
                    root.commit(root.valueFor(positionAt(mouse.x)))
                }
            }
            onDoubleClicked: if (root.resetValue >= 0) root.commit(root.resetValue)
            onWheel: function(wheel) {
                root.commit(root.value + (wheel.angleDelta.y > 0 ? root.step : -root.step))
                wheel.accepted = true
            }
            Keys.onLeftPressed: function(event) { root.commit(root.value - (event.modifiers & Qt.ShiftModifier ? root.step * 10 : root.step)); event.accepted = true }
            Keys.onRightPressed: function(event) { root.commit(root.value + (event.modifiers & Qt.ShiftModifier ? root.step * 10 : root.step)); event.accepted = true }
            Keys.onUpPressed: function(event) { root.commit(root.value + root.step); event.accepted = true }
            Keys.onDownPressed: function(event) { root.commit(root.value - root.step); event.accepted = true }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home) { root.commit(root.from); event.accepted = true }
                else if (event.key === Qt.Key_End) { root.commit(root.to); event.accepted = true }
            }
        }
    }
}
