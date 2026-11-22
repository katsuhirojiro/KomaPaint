import QtQuick
import QtQuick.Controls.Basic as C

C.Button {
    id: root
    property string glyph: ""
    property string hint: ""
    property bool quiet: false
    property bool selected: false
    property bool strong: false
    property bool tab: false
    property bool leftAligned: false
    property bool drop: false
    readonly property color face: strong ? "#3b4249" : selected ? (tab ? Theme.panel : Theme.itemSelected)
                                  : down || hovered ? Theme.hover : quiet || tab ? "transparent" : Theme.control
    font.family: Theme.family
    font.pixelSize: Theme.normal
    padding: text.length ? 9 : 6
    leftPadding: padding
    rightPadding: padding
    spacing: 6
    topPadding: 0; bottomPadding: 0
    implicitHeight: 26
    implicitWidth: Math.max(28, implicitContentWidth + leftPadding + rightPadding)
    opacity: enabled ? 1 : .44
    Accessible.name: text || hint
    C.ToolTip.visible: hovered && hint.length > 0
    C.ToolTip.delay: 650
    C.ToolTip.text: hint
    contentItem: Item {
        implicitWidth: (root.glyph ? (root.text.length ? 21 : 16) : 0) + (root.drop ? 15 : 0) + textLabel.implicitWidth
        implicitHeight: 24
        readonly property color fg: root.strong || !root.tab || root.selected ? Theme.text : Theme.mid
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: root.leftAligned ? 0 : Math.max(0, Math.round((parent.width - width)/2))
            spacing: 5
            Glyph { name: root.glyph; tint: parent.parent.fg; width: 16; height: 16; visible: root.glyph.length > 0; anchors.verticalCenter: parent.verticalCenter }
            LabelText {
                id: textLabel
                text: root.text; color: parent.parent.fg; visible: text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.selected || root.strong ? Font.DemiBold : Font.Medium
            }
            Glyph { name: "down"; tint: parent.parent.fg; width: 12; height: 13; visible: root.drop; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    background: Rectangle {
        color: root.face
        radius: root.tab ? 0 : 2
        border.width: root.visualFocus || root.strong || (!root.quiet && !root.tab && !root.strong) ? 1 : 0
        border.color: root.visualFocus ? Theme.focus : root.strong ? "#6c7781" : Theme.border
        Rectangle {
            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2; height: 2; color: Theme.ink
            visible: root.tab && root.selected
        }
    }
}
