import QtQuick
import QtQuick.Controls.Basic as C

C.ScrollBar {
    id: root
    policy: C.ScrollBar.AsNeeded
    width: 8
    padding: 2
    background: Rectangle { color: "transparent" }
    contentItem: Rectangle { implicitWidth: 4; implicitHeight: 12; radius: 1; color: root.pressed ? Theme.ink : "#767676" }
}
