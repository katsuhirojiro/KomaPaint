import QtQuick
import QtQuick.Controls.Basic as C

C.CheckBox {
    id: root
    font.family: Theme.family; font.pixelSize: Theme.normal
    implicitHeight: 28
    padding: 0
    indicator: Rectangle {
        x: 0; anchors.verticalCenter: parent.verticalCenter
        implicitHeight: 16; implicitWidth: 16
        radius: 2; color: root.checked ? Theme.itemSelected : Theme.field
        border.color: root.visualFocus ? Theme.focus : Theme.border
        Glyph { anchors.fill: parent; anchors.margins: 1; visible: root.checked; name: "check"; tint: Theme.text }
    }
    contentItem: LabelText { text: root.text; leftPadding: 23; font: root.font }
}
