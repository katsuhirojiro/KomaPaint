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
    property bool animateExpansion: true
    // Loaded forms can provide their implicit height directly, avoiding a
    // dependency on childrenRect while their delegate layout is being built.
    property real contentHeight: body.childrenRect.height

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

            Rectangle {
                anchors.fill: parent
                visible: headerMouse.activeFocus
                color: "transparent"
                border.width: KP.focusWidth
                border.color: KP.focusRing
            }

            MouseArea {
                id: headerMouse
                objectName: root.objectName.length > 0 ? root.objectName + "Toggle" : ""
                anchors.fill: parent
                activeFocusOnTab: root.collapsible
                Accessible.ignored: !root.collapsible
                Accessible.role: Accessible.Button
                Accessible.name: root.title
                Accessible.description: root.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                Accessible.onPressAction: if (root.collapsible) toggle()
                function toggle() { root.expanded = !root.expanded }
                Keys.onSpacePressed: if (root.collapsible) toggle()
                Keys.onReturnPressed: if (root.collapsible) toggle()
                enabled: root.collapsible
                hoverEnabled: root.collapsible
                cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: toggle()
            }
        }

        Item {
            id: body
            width: parent.width
            height: root.expanded ? Math.max(0, root.contentHeight) : 0
            clip: true
            visible: height > 0
            Behavior on height {
                enabled: root.animateExpansion
                NumberAnimation { duration: KP.durationBase; easing.type: KP.easingCurve }
            }
        }
    }
}
