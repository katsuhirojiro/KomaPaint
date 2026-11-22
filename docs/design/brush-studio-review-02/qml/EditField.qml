import QtQuick
import QtQuick.Controls.Basic as C

C.TextField {
    id: root
    implicitHeight: 26
    font.family: Theme.family; font.pixelSize: Theme.normal
    font.weight: Font.Normal
    color: Theme.text; placeholderTextColor: Theme.muted
    selectionColor: Theme.itemSelected; selectedTextColor: Theme.text
    leftPadding: 7; rightPadding: 7; topPadding: 0; bottomPadding: 0
    selectByMouse: true
    background: Rectangle { radius: 2; color: Theme.field; border.color: root.activeFocus ? Theme.focus : Theme.border }
}
