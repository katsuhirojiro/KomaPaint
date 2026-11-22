import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    minimumSize: .09
    implicitWidth: 10
    x: parent ? parent.width - width : 0
    y: 0
    height: parent ? parent.height : 0
    leftPadding: 3; rightPadding: 2; topPadding: 2; bottomPadding: 2
    background: Rectangle { visible: bar.size < 1; radius: 4; color: bar.hovered ? Desk.base : "transparent" }
    contentItem: Rectangle { radius: 3; implicitWidth: 4; implicitHeight: 10; color: bar.pressed ? Desk.accent : bar.hovered ? Desk.muted : "#5d564f"; visible: bar.size < 1 }
}
