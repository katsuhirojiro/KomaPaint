import QtQuick
import QtQuick.Layouts
import "."

Rectangle {
    id: control
    property var options: []
    property int currentIndex: 0
    property bool small: false
    signal chosen(int index)
    implicitHeight: small ? 22 : 28
    radius: Theme.radiusSmall
    color: Theme.track
    border.width: 1
    border.color: Theme.hairline

    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2
        Repeater {
            model: control.options
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 2
                color: index === control.currentIndex ? Theme.elevated : "transparent"
                border.width: index === control.currentIndex ? 1 : 0
                border.color: Theme.innerHighlight
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: index === control.currentIndex ? Theme.text : Theme.secondary
                    font.family: Theme.ui
                    font.pixelSize: control.small ? Theme.micro : Theme.small
                    font.weight: index === control.currentIndex ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        control.currentIndex = index
                        control.chosen(index)
                    }
                }
            }
        }
    }
}
