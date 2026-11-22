import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string title: ""
    property string description: ""
    property string iconName: "adjust"
    property bool expanded: false
    property Component inner
    spacing: 8
    Button {
        id: control
        Layout.fillWidth: true; implicitHeight: 52
        text: root.title; hoverEnabled: true
        Accessible.description: root.description + (root.expanded ? " Expanded." : " Collapsed.")
        padding: 9
        background: Rectangle { radius: 6; color: root.expanded ? Desk.active : control.hovered ? Desk.hover : Desk.raised; border.color: control.visualFocus ? Desk.accent : root.expanded ? Desk.accentLine : Desk.line }
        contentItem: RowLayout {
            spacing: 7
            Glyph { name: root.iconName; tint: root.expanded ? Desk.accent : Desk.muted; Layout.preferredWidth: 21; Layout.preferredHeight: 21 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Type { text: root.title; Layout.fillWidth: true; elide: Text.ElideRight; font.weight: Font.DemiBold; font.pixelSize: 13 }
                Type { text: root.description; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 11; color: Desk.muted }
            }
            Glyph { name: root.expanded ? "down" : "right"; tint: Desk.muted; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
        }
        onClicked: root.expanded = !root.expanded
    }
    Loader {
        Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 4
        active: root.expanded; visible: active; sourceComponent: root.inner
    }
}
