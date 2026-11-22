import QtQuick
import QtQuick.Controls

Switch {
    id: control
    implicitHeight: Math.max(40, implicitContentHeight + 10)
    spacing: 10; padding: 0
    hoverEnabled: true
    indicator: Rectangle {
        implicitWidth: 36; implicitHeight: 20; radius: 10
        x: control.width - width; y: (control.height - height) / 2
        color: control.checked ? Desk.accentLine : Desk.base
        border.color: control.activeFocus ? Desk.accent : Desk.line
        Rectangle {
            width: 14; height: 14; radius: 7; y: 3; x: control.checked ? 19 : 3
            color: control.checked ? Desk.text : Desk.muted
        }
    }
    contentItem: Type {
        text: control.text
        rightPadding: 44
        wrapMode: Text.WordWrap
        font.pixelSize: 13
        color: control.hovered ? Desk.text : Desk.body
    }
}
