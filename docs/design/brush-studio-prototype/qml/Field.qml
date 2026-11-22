import QtQuick
import QtQuick.Controls

TextField {
    id: field
    color: Desk.text
    placeholderTextColor: Desk.muted
    selectionColor: Desk.accentLine
    selectedTextColor: Desk.text
    font.family: Desk.type
    font.pixelSize: 14
    implicitHeight: 42
    padding: 9
    selectByMouse: true
    background: Rectangle {
        radius: 5
        color: Desk.base
        border.color: field.activeFocus ? Desk.accent : Desk.line
        border.width: field.activeFocus ? 2 : 1
    }
}
