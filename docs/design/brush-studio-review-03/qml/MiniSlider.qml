import QtQuick
import "."

Item {
    id: control
    property real value: 0
    property real minimum: 0
    property real maximum: 100
    property bool enabled: true
    signal moved(real value)
    signal pressed
    signal released
    implicitHeight: 24
    opacity: enabled ? 1 : 0.4

    readonly property real fraction: maximum > minimum
        ? Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum))) : 0

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 2
        color: Theme.track
        Rectangle {
            width: Math.max(0, parent.width * control.fraction)
            height: parent.height
            radius: 2
            color: Theme.fill
        }
        Rectangle {
            width: 12; height: 12; radius: 6
            x: Math.max(0, Math.min(parent.width - width, parent.width * control.fraction - width / 2))
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.thumb
            border.width: 1
            border.color: "#26000000"
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: width / 2
                color: "#12000000"
                z: -1
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        enabled: control.enabled
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        function apply(x) {
            var f = Math.max(0, Math.min(1, x / width))
            var raw = control.minimum + f * (control.maximum - control.minimum)
            control.moved(raw)
        }
        onPressed: { control.pressed(); apply(mouse.x) }
        onPositionChanged: if (pressed) apply(mouse.x)
        onReleased: control.released()
    }
}
