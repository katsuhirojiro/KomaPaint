import QtQuick
import QtQuick.Controls
import "../themes"

// A checkbox with the same self-explanation contract as PropertySlider: the
// question mark opens the explanation on hover, keyboard focus and touch.
CheckBox {
    id: root

    property string help: ""
    property bool pinned: false

    hoverEnabled: true
    rightPadding: help.length > 0 ? 20 : 0
    Accessible.name: text
    Accessible.description: help

    Rectangle {
        id: badge
        parent: root
        visible: root.help.length > 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 15
        height: 15
        radius: width / 2
        color: helpArea.containsMouse || root.pinned ? KP.accent : "transparent"
        border.width: 1
        border.color: helpArea.containsMouse || root.pinned ? KP.accent : KP.textMuted

        Text {
            anchors.centerIn: parent
            text: "?"
            color: helpArea.containsMouse || root.pinned ? KP.textOnAccent : KP.textMuted
            font.pixelSize: 10
            font.bold: true
            font.family: KP.fontUi
        }

        MouseArea {
            id: helpArea
            anchors.fill: parent
            anchors.margins: -5
            hoverEnabled: true
            onClicked: root.pinned = !root.pinned
        }
    }

    ToolTip {
        id: tip
        text: root.help
        visible: root.help.length > 0 && (root.hovered || root.activeFocus || root.pinned)
        delay: 0
        timeout: -1
        y: root.height + 2
        contentItem: Text {
            text: tip.text
            color: KP.text
            font.pixelSize: KP.fontS
            font.family: KP.fontUi
            wrapMode: Text.Wrap
            width: 250
        }
        background: Rectangle {
            color: KP.floatingBg
            border.width: 1
            border.color: KP.border
            radius: KP.radiusS
        }
    }
}
