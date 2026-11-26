import QtQuick
import QtQuick.Controls
import "../themes"

// A menu entry: label on the left, keyboard shortcut on the right, an optional
// check mark, and a single accent bar when highlighted.
//
// The text uses `\t` to separate the label from its shortcut, which is how the
// menus are written throughout the app ("Save\tCtrl+S"). Qt Quick's stock menu
// item prints the tab as a literal gap and the `&` of a mnemonic as an
// ampersand; both are handled here, so labels read exactly as intended.
MenuItem {
    id: item

    readonly property var parts: text.split("\t")
    readonly property string labelText: parts[0].replace(/&&/g, "\u0000").replace(/&/g, "").replace(/\u0000/g, "&")
    readonly property string shortcutText: parts.length > 1 ? parts[1] : ""
    readonly property bool destructive: /delete|clear|remove|discard|reset/i.test(labelText)
    property string explanation: ""
    ToolTip.text: explanation.length > 0 ? explanation : labelText
    ToolTip.visible: explanation.length > 0 && (hovered || highlighted || activeFocus)
    ToolTip.delay: 500

    implicitHeight: KP.controlHeight + 2
    padding: 0
    leftPadding: KP.gapM + (checkable ? 16 : 0)
    rightPadding: KP.gapM
    hoverEnabled: true

    indicator: Item {
        x: KP.gapS
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        visible: item.checkable
        KpGlyph {
            anchors.centerIn: parent
            visible: item.checked
            name: "check"
            size: 13
            color: KP.iconSelected
        }
    }

    contentItem: Item {
        implicitHeight: Math.max(labelItem.implicitHeight, shortcutItem.implicitHeight)
        implicitWidth: labelItem.implicitWidth + (shortcutItem.visible ? KP.gapXL + shortcutItem.implicitWidth : 0)

        Text {
            id: labelItem
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: item.labelText
            color: !item.enabled ? KP.textSubtle
                 : item.highlighted ? KP.text
                 : item.destructive ? KP.error : KP.text
            font.pixelSize: KP.fontS + 1
            font.family: KP.fontUi
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            id: shortcutItem
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: item.shortcutText.length > 0
            text: item.shortcutText
            color: item.highlighted ? KP.textMuted : KP.textSubtle
            font.pixelSize: KP.fontS
            font.family: KP.fontMono
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        radius: KP.radiusS
        color: item.down ? KP.pressOverlay : (item.highlighted ? KP.hoverOverlayStrong : "transparent")
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            radius: 1
            color: KP.accent
            visible: item.highlighted && item.enabled
        }
    }
}
