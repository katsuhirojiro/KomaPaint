import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The document strip: one folded tab per open project.
//
// A tab is a sheet edge, not a button. The active tab is filled with the
// workspace surface and marked by a 2 px ember rule along its top edge, so the
// eye finds it without a colour block; inactive tabs are quiet text on the strip
// and only light up on hover. A project with unsaved changes carries a dot
// (never an asterisk in the title), and closing it asks first.
Rectangle {
    id: strip

    signal activateRequested(int index)
    signal closeRequested(int index)
    signal newRequested()

    implicitHeight: KP.tabStripHeight
    color: KP.windowBg

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: KP.hairline
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.leftMargin: KP.gapS
        anchors.rightMargin: KP.gapS
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded

        RowLayout {
            spacing: KP.gapXS

            Repeater {
                model: Workspace.projectTabs

                delegate: Item {
                    id: tab
                    required property int index
                    required property var modelData

                    readonly property bool current: index === Workspace.activeTab
                    readonly property bool hovered: hitArea.hovered

                    Layout.preferredWidth: Math.min(220, Math.max(124, label.implicitWidth + 58))
                    Layout.preferredHeight: strip.height

                    AbstractButton {
                        id: hitArea
                        objectName: "projectTabButton" + tab.index
                        anchors.fill: parent
                        // The button is the tab: its text is the project name,
                        // so accessibility, tooling and the interface tests see
                        // the same label the strip draws.
                        text: tab.modelData.title
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        Accessible.name: tab.modelData.title
                        ToolTip.text: tab.modelData.path
                        ToolTip.visible: hovered && tab.modelData.path.length > 0
                        ToolTip.delay: 700
                        onClicked: strip.activateRequested(tab.index)
                    }

                    Rectangle {
                        id: sheet
                        anchors.bottom: parent.bottom
                        width: parent.width - 2
                        height: strip.height - KP.gapS
                        radius: KP.radiusS
                        color: tab.current ? KP.paneBg
                             : tab.hovered ? KP.hoverOverlay : "transparent"
                        border.width: tab.current ? 1 : 0
                        border.color: KP.border

                        Rectangle {
                            visible: tab.current
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: -1
                            height: 2
                            radius: 1
                            color: KP.ember
                        }

                        Rectangle {
                            visible: tab.modelData.modified
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6
                            height: 6
                            radius: 3
                            color: KP.ember
                        }

                        Text {
                            id: label
                            anchors.left: parent.left
                            anchors.leftMargin: tab.modelData.modified ? 20 : KP.gapM
                            anchors.right: closeButton.left
                            anchors.rightMargin: KP.gapXS
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.title
                            color: tab.current ? KP.text : tab.hovered ? KP.text : KP.textMuted
                            font.pixelSize: KP.fontS + 1
                            font.weight: tab.current ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        AbstractButton {
                            id: closeButton
                            objectName: "projectTabClose" + tab.index
                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            // Always present so it can be clicked; faint until
                            // the tab is active, hovered or modified.
                            opacity: tab.current || tab.hovered || tab.modelData.modified ? 1 : 0.28
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: true
                            Accessible.name: qsTr("Close %1").arg(tab.modelData.title)
                            ToolTip.text: tab.modelData.modified
                                         ? qsTr("Close %1 — unsaved changes").arg(tab.modelData.title)
                                         : qsTr("Close %1").arg(tab.modelData.title)
                            ToolTip.visible: hovered
                            ToolTip.delay: 600

                            background: Rectangle {
                                radius: KP.radiusXS
                                color: closeButton.down ? KP.pressOverlay
                                     : closeButton.hovered ? KP.hoverOverlayStrong : "transparent"
                            }
                            contentItem: KpGlyph {
                                name: "close"
                                size: 11
                                color: closeButton.hovered ? KP.iconHover : KP.iconOff
                            }
                            onClicked: strip.closeRequested(tab.index)
                        }
                    }
                }
            }

            AbstractButton {
                id: newTabButton
                objectName: "newProjectTabButton"
                Layout.preferredWidth: 26
                Layout.preferredHeight: strip.height - KP.gapS
                Layout.alignment: Qt.AlignBottom
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.name: qsTr("New project")
                ToolTip.text: qsTr("New project (Ctrl+N)")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                background: Rectangle {
                    radius: KP.radiusXS
                    color: newTabButton.hovered ? KP.hoverOverlay : "transparent"
                }
                contentItem: KpGlyph {
                    name: "plus"
                    size: 13
                    color: newTabButton.hovered ? KP.iconHover : KP.iconOff
                }
                onClicked: strip.newRequested()
            }
        }
    }
}
