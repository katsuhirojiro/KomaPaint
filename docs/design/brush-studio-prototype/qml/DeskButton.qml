import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property string iconName: ""
    property string rightIcon: ""
    property string accessible: text
    property string style: "outline"
    property bool picked: false
    property string dotColor: ""
    property int size: 40
    property int textSize: 13
    property bool flatIcon: false
    implicitHeight: size
    implicitWidth: Math.max(size, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: text.length ? 13 : 0
    rightPadding: text.length ? 13 : 0
    topPadding: 0; bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: accessible
    opacity: enabled ? 1 : .41
    readonly property color foreground: style === "accent" ? "#2d201a" : picked ? Desk.accent : Desk.text
    background: Rectangle {
        radius: 6
        color: control.style === "accent" ? (control.down ? "#dc936f" : control.hovered ? "#f8b896" : Desk.accent)
             : control.down ? Desk.active : control.hovered ? Desk.hover : control.picked ? Desk.active
             : control.style === "quiet" ? "transparent" : control.style === "fill" ? Desk.raised : Desk.panel
        border.width: control.visualFocus ? 2 : control.style === "quiet" || control.style === "accent" ? 0 : 1
        border.color: control.visualFocus ? Desk.accent : control.picked ? Desk.accentLine : Desk.line
    }
    contentItem: Item {
        implicitWidth: details.implicitWidth
        implicitHeight: details.implicitHeight
        Row {
            id: details
            anchors.centerIn: parent
            spacing: 7
            Rectangle {
                visible: control.dotColor.length > 0
                width: 16; height: 16; radius: 8; color: control.dotColor || "transparent"; border.color: Desk.subtle
                anchors.verticalCenter: parent.verticalCenter
            }
            Glyph {
                visible: control.iconName.length > 0
                name: control.iconName; tint: control.foreground
                width: control.flatIcon ? 18 : 19; height: width
                anchors.verticalCenter: parent.verticalCenter
            }
            Type {
                visible: control.text.length > 0
                text: control.text; color: control.foreground; font.pixelSize: control.textSize; font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Glyph {
                visible: control.rightIcon.length > 0
                name: control.rightIcon; tint: control.foreground; width: 16; height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    ToolTip.visible: hovered && !text.length && accessible.length > 0
    ToolTip.delay: 650
    ToolTip.text: accessible
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.PointingHandCursor }
}
