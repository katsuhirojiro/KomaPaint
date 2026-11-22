import QtQuick

Rectangle {
    property alias text: title.text
    implicitHeight: 30
    color: Theme.chrome
    Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
    LabelText {
        id: title
        anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 12; anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: Theme.label; font.weight: Font.DemiBold
    }
}
