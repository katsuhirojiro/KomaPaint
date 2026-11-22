import QtQuick
import QtQuick.Layouts
import "."

Rectangle {
    id: dock
    property bool expanded: true
    signal toggleRequested
    color: Theme.panel
    implicitHeight: expanded ? 230 : 30
    height: implicitHeight

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    Item {
        id: header
        width: parent.width
        height: 30
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.hairline
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 8
            Text {
                text: "RESPONSE CURVE"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 0.9
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: studio.selectedBinding.targetLabel
                    ? studio.selectedBinding.signal + " → " + studio.selectedBinding.targetLabel
                    : "No binding selected"
                color: Theme.secondary
                font.family: Theme.ui
                font.pixelSize: Theme.small
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Math.max(0, parent.width - 430); height: 1 }
            ActionButton {
                text: "Save as reusable"
                icon: "secondary"
                compact: true
                enabled: !!studio.selectedBinding.targetLabel
                onClicked: studio.savePreset()
            }
            ActionButton {
                text: "Reset"
                icon: "reset"
                compact: true
                enabled: !!studio.selectedBinding.targetLabel
                onClicked: studio.resetCurve(studio.selectedBinding.id)
            }
            ActionButton {
                icon: dock.expanded ? "chevron" : "chevron"
                compact: true
                onClicked: dock.toggleRequested()
                rotation: dock.expanded ? 0 : 180
            }
        }
    }

    RowLayout {
        visible: dock.expanded
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 10

        CurveEditor {
            visible: !!studio.selectedBinding.targetLabel
            Layout.preferredWidth: dock.width < 700 ? dock.width - 28 : Math.min(330, dock.width * 0.38)
            Layout.fillHeight: true
            bindingId: studio.selectedBinding.id || ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dock.width >= 760 && !!studio.selectedBinding.targetLabel
            spacing: 4
            Text {
                text: "Reusable dynamics"
                color: Theme.text
                font.family: Theme.ui
                font.pixelSize: Theme.small
                font.weight: Font.Medium
            }
            Text {
                text: "One curve, applied to any setting. A preset changes the selected binding, not the brush's other bindings."
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Row {
                spacing: 4
                Repeater {
                    model: studio.presets
                    Rectangle {
                        height: 23
                        width: presetText.implicitWidth + 14
                        radius: 2
                        color: presetMouse.containsMouse ? Theme.elevated : Theme.track
                        border.width: 1
                        border.color: Theme.hairline
                        Text {
                            id: presetText
                            anchors.centerIn: parent
                            text: modelData.name
                            color: Theme.secondary
                            font.family: Theme.ui
                            font.pixelSize: 8
                        }
                        MouseArea {
                            id: presetMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: studio.applyPreset(modelData.id)
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            Row {
                spacing: 10
                Text {
                    text: studio.selectedBinding.available === false
                        ? "Device cannot report " + studio.selectedBinding.signal + "; using " + studio.selectedBinding.fallback
                        : "Device reports " + studio.selectedBinding.signal + " · curve is editable"
                    color: studio.selectedBinding.available === false ? Theme.secondary : Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                }
                Text {
                    text: "Drag nodes · double-click to add"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                }
            }
        }

        Text {
            visible: !studio.selectedBinding.targetLabel
            Layout.fillWidth: true
            text: "Open the curve affordance on any setting to bind one shared signal system to it."
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }
    }
}
