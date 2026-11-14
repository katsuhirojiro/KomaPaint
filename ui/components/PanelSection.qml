import QtQuick
import QtQuick.Controls
import "../themes"

// A titled group of related controls inside a panel.
//
// Panels are not boxes inside boxes: a section marks the start of a group with
// a small tracked capital label and a hairline, and lets the content breathe
// underneath. That is the whole visual separation — no card, no border, no
// background change. Use `hint` for a short right-aligned note (a unit, a
// count, the current value) and `collapsible` only where the group has enough
// content that hiding it is genuinely useful.
Item {
    id: root

    property string title: ""
    property string hint: ""
    property bool collapsible: false
    property bool expanded: true
    property bool showRule: true
    // Space between the label and the content, and after the content.
    property int contentTopMargin: KP.gapM
    property int contentBottomMargin: KP.gapL
    property int headerHeight: 18

    default property alias content: body.data

    implicitWidth: 200
    implicitHeight: header.height + (expanded ? body.height : 0)

    Column {
        width: parent.width
        spacing: 0

        Item {
            id: header
            width: parent.width
            height: root.headerHeight

            Rectangle {
                id: rule
                visible: root.showRule
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: KP.hairline
            }

            Text {
                id: label
                anchors.left: parent.left
                anchors.top: rule.bottom
                anchors.bottom: parent.bottom
                anchors.right: hintLabel.left
                anchors.rightMargin: KP.gapS
                text: root.title.toUpperCase()
                color: KP.textSectionLabel
                font.pixelSize: KP.fontMicro
                font.letterSpacing: KP.trackingMicro
                font.capitalization: Font.AllUppercase
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            Text {
                id: hintLabel
                anchors.right: root.collapsible ? chevron.left : parent.right
                anchors.rightMargin: root.collapsible ? KP.gapXS : 0
                anchors.verticalCenter: label.verticalCenter
                text: root.hint
                visible: text.length > 0
                color: KP.textSubtle
                font.pixelSize: KP.fontMicro
                font.letterSpacing: 0.2
                verticalAlignment: Text.AlignVCenter
            }

            KpGlyph {
                id: chevron
                anchors.right: parent.right
                anchors.verticalCenter: label.verticalCenter
                visible: root.collapsible
                name: root.expanded ? "chevron-up" : "chevron-down"
                size: KP.iconS - 2
                color: headerMouse.containsMouse ? KP.iconHover : KP.iconOff
            }

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                enabled: root.collapsible
                hoverEnabled: root.collapsible
                cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.expanded = !root.expanded
            }
        }

        Item {
            id: body
            width: parent.width
            height: root.expanded ? Math.max(0, childrenRect.height) : 0
            clip: true
            visible: height > 0
            Behavior on height { NumberAnimation { duration: KP.durationBase; easing.type: KP.easingCurve } }
        }
    }
}
