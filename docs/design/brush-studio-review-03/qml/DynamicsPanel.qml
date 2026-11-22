import QtQuick
import QtQuick.Layouts
import "."

Rectangle {
    id: panel
    implicitHeight: content.implicitHeight + 20
    radius: Theme.radius
    color: Theme.elevated
    border.width: 1
    border.color: Theme.innerHighlight

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 8

        Row {
            width: parent.width
            Text {
                text: "ONE DYNAMICS SYSTEM"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 1.0
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Math.max(0, parent.width - 205); height: 1 }
            Text {
                text: studio.bindings.length + " bindings"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Segmented {
            width: parent.width
            height: 24
            small: true
            options: ["Mouse", "Pen", "Touch"]
            currentIndex: studio.deviceKey === "mouse" ? 0 : (studio.deviceKey === "touch" ? 2 : 1)
            onChosen: function(index) {
                studio.setDevice(index === 0 ? "Mouse" : (index === 2 ? "Touch" : "Pen · simulated"))
            }
        }
        Text {
            width: parent.width
            text: studio.deviceNotice
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: Theme.micro
            wrapMode: Text.WordWrap
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        Repeater {
            model: studio.signals
            Row {
                width: content.width
                height: 20
                Text {
                    width: 72
                    text: modelData.name
                    color: modelData.available ? Theme.secondary : Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width - 126
                    text: modelData.available ? modelData.requirement : "unavailable · fallback " + modelData.fallback
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                    elide: Text.ElideRight
                }
                Text {
                    width: 54
                    text: modelData.count ? modelData.count + " used" : "—"
                    color: modelData.available ? Theme.meta : Theme.disabled
                    font.family: Theme.ui
                    font.pixelSize: 8
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.hairline }

        Repeater {
            model: studio.bindings
            Rectangle {
                width: content.width
                height: 32
                radius: 2
                color: modelData.selected ? Theme.raised : (bindMouse.containsMouse ? Theme.hover : "transparent")
                border.width: modelData.selected ? 1 : 0
                border.color: Theme.innerHighlight
                LineIcon {
                    x: 7
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "dynamics"
                    width: 13; height: 13
                    tint: modelData.available ? Theme.secondary : Theme.disabled
                }
                Text {
                    x: 26
                    width: parent.width - 106
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.signal + " → " + modelData.targetLabel
                    color: modelData.available ? Theme.text : Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    elide: Text.ElideRight
                }
                Text {
                    width: 62
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.available ? modelData.amount + "%" : "fallback"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 8
                    font.features: {"tnum": 1}
                    horizontalAlignment: Text.AlignRight
                }
                MouseArea {
                    id: bindMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: studio.selectBinding(modelData.id)
                }
                objectName: "binding_" + modelData.id
            }
        }

        Row {
            width: parent.width
            spacing: 4
            Text {
                text: "Reusable"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                anchors.verticalCenter: parent.verticalCenter
            }
            Repeater {
                model: studio.presets
                Rectangle {
                    height: 20
                    width: presetName.implicitWidth + 12
                    radius: 2
                    color: presetMouse.containsMouse ? Theme.raised : Theme.track
                    Text {
                        id: presetName
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
    }
}
