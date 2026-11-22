import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property var choices: []
    property string current: ""
    property int buttonHeight: 37
    property int textSize: 12
    signal chosen(string key)
    color: Desk.base; radius: 7; border.color: Desk.line
    implicitHeight: buttonHeight + 6
    implicitWidth: choices.length * 74
    RowLayout {
        anchors.fill: parent; anchors.margins: 3; spacing: 3
        Repeater {
            model: root.choices
            delegate: DeskButton {
                required property var modelData
                readonly property string key: typeof modelData === "string" ? modelData : modelData.key
                objectName: key.replace(/[^a-zA-Z]/g, "") + "Choice"
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.preferredWidth: 1
                size: root.buttonHeight
                text: typeof modelData === "string" ? modelData : modelData.label
                iconName: typeof modelData === "string" ? "" : (modelData.icon || "")
                textSize: root.textSize; style: "quiet"; picked: root.current === key
                onClicked: root.chosen(key)
            }
        }
    }
}
