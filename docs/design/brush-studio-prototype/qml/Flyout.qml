import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: pop
    property var options: []
    signal chosen(string key)
    width: 242
    padding: 5
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle { radius: 8; color: Desk.raised; border.color: Desk.line }
    contentItem: ColumnLayout {
        spacing: 2
        Repeater {
            model: pop.options
            delegate: Button {
                id: choice
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: modelData.detail ? 55 : 41
                text: modelData.label
                objectName: modelData.key + "Action"
                enabled: modelData.enabled !== false
                hoverEnabled: true
                opacity: enabled ? 1 : .4
                leftPadding: 9; rightPadding: 9; topPadding: 3; bottomPadding: 3
                background: Rectangle {
                    radius: 5
                    color: choice.hovered || choice.down ? Desk.active : "transparent"
                    border.color: choice.visualFocus ? Desk.accent : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 9
                    Glyph { name: choice.modelData.icon || ""; visible: name.length > 0; Layout.preferredWidth: 19; Layout.preferredHeight: 19; tint: Desk.muted }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Type { text: choice.modelData.label; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight }
                        Type { visible: Boolean(choice.modelData.detail); text: choice.modelData.detail || ""; color: Desk.muted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
                onClicked: { pop.close(); pop.chosen(modelData.key) }
            }
        }
    }
}
