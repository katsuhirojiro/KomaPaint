import QtQuick

Rectangle {
    id: root
    property string text: ""
    property string glyph: "plus"
    signal clicked()
    implicitHeight: 27
    radius: 2
    color: hit.containsMouse || hit.pressed ? Theme.hover : "transparent"
    Row {
        anchors.fill: parent
        anchors.leftMargin: 6; anchors.rightMargin: 7
        spacing: 8
        Glyph {
            name: root.glyph; width: 15; height: 15; tint: Theme.mid
            anchors.verticalCenter: parent.verticalCenter
        }
        LabelText {
            text: root.text; width: parent.width - 40
            font.pixelSize: Theme.small
            anchors.verticalCenter: parent.verticalCenter
        }
        Glyph {
            name: "plus"; width: 11; height: 11; tint: Theme.muted; opacity: .7
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
