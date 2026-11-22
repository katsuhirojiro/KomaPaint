import QtQuick
import QtQuick.Controls.Basic as C

C.MenuItem {
    id: root
    property string glyph: ""
    implicitHeight: 30
    leftPadding: 9; rightPadding: 17
    font.family: Theme.family; font.pixelSize: Theme.normal
    background: Rectangle { color: root.highlighted || root.visualFocus ? Theme.itemSelected : "transparent" }
    contentItem: Row {
        spacing: 8
        Glyph { name: root.glyph; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
        LabelText { text: root.text; enabled: root.enabled; opacity: enabled ? 1 : .45; anchors.verticalCenter: parent.verticalCenter }
    }
}
