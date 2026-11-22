import QtQuick
import QtQuick.Controls.Basic as C
import "."
import "." as Local

Item {
    id: control
    property string text: ""
    property string icon: ""
    property bool selected: false
    property bool quiet: false
    property bool compact: false
    property bool enabled: true
    signal clicked
    implicitWidth: row.implicitWidth + (compact ? 14 : 20)
    implicitHeight: compact ? 24 : 28
    opacity: enabled ? 1 : 0.42

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: control.selected ? Theme.elevated : (mouse.containsMouse ? Theme.hover : "transparent")
        border.width: control.selected ? 1 : 0
        border.color: Theme.innerHighlight
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Local.LineIcon {
            visible: control.icon.length > 0
            kind: control.icon
            width: 14; height: 14
            tint: control.selected ? Theme.text : (mouse.containsMouse ? Theme.text : Theme.secondary)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            color: control.selected ? Theme.text : (mouse.containsMouse ? Theme.text : Theme.secondary)
            font.family: Theme.ui
            font.pixelSize: Theme.small
            font.weight: control.selected ? Font.Medium : Font.Normal
            font.features: {"tnum": 1}
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: control.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: if (control.enabled) control.clicked()
    }
}
