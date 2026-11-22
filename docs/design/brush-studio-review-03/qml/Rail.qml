import QtQuick
import QtQuick.Controls.Basic as C
import "."

Rectangle {
    id: rail
    property var areas: []
    property string current: ""
    property string brushName: ""
    property string engine: ""
    property bool compact: false
    signal chosen(string key)
    signal brushRequested
    color: Theme.panel

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.hairline
    }

    Column {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.bottomMargin: 10

        Item {
            width: parent.width
            height: rail.compact ? 42 : 76
            Rectangle {
                x: rail.compact ? (parent.width - 22) / 2 : 16
                y: 0
                width: 22; height: 22
                radius: 4
                color: Theme.text
                Text {
                    anchors.centerIn: parent
                    text: "K"
                    color: Theme.panel
                    font.family: Theme.ui
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }
            Column {
                visible: !rail.compact
                x: 16
                y: 30
                spacing: 1
                Text {
                    text: "KomaPaint"
                    color: Theme.text
                    font.family: Theme.ui
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "BRUSH STUDIO · 03"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 7
                    font.letterSpacing: 1.0
                }
            }
        }

        Item { width: parent.width; height: 8 }

        Rectangle {
            visible: !rail.compact
            x: 12
            width: parent.width - 24
            height: 44
            radius: Theme.radiusSmall
            color: Theme.elevated
            border.width: 1
            border.color: Theme.innerHighlight
            Column {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: rail.brushName
                    color: Theme.text
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    text: rail.engine.toUpperCase() + " · EDITING"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 7
                    font.letterSpacing: 0.7
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: rail.brushRequested()
            }
        }
        Item { width: parent.width; height: rail.compact ? 6 : 12 }

        Flickable {
            width: parent.width
            height: parent.height - (rail.compact ? 42 + 8 : 76 + 8 + 44 + 12) - 54
            contentHeight: nav.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: nav
                width: parent.width
                Repeater {
                    model: rail.areas
                    Item {
                        id: item
                        width: nav.width
                        height: 30
                        Rectangle {
                            x: rail.compact ? 8 : 10
                            width: parent.width - (rail.compact ? 16 : 20)
                            height: 28
                            radius: 3
                            color: modelData.key === rail.current ? Theme.elevated : (navMouse.containsMouse ? Theme.hover : "transparent")
                            border.width: modelData.key === rail.current ? 1 : 0
                            border.color: Theme.innerHighlight
                        }
                        LineIcon {
                            x: rail.compact ? (parent.width - 16) / 2 : 20
                            anchors.verticalCenter: parent.verticalCenter
                            kind: modelData.key
                            width: 15; height: 15
                            tint: modelData.key === rail.current ? Theme.text : Theme.secondary
                        }
                        Text {
                            visible: !rail.compact
                            x: 47
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 58
                            text: modelData.label
                            color: modelData.key === rail.current ? Theme.text : Theme.secondary
                            font.family: Theme.ui
                            font.pixelSize: Theme.small
                            font.weight: modelData.key === rail.current ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: !rail.compact && modelData.key === "quick" && modelData.count > 0
                            anchors.right: parent.right
                            anchors.rightMargin: 15
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.count
                            color: Theme.meta
                            font.family: Theme.ui
                            font.pixelSize: 8
                            font.features: {"tnum": 1}
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: rail.chosen(modelData.key)
                        }
                        objectName: "rail_" + modelData.key
                    }
                }
            }
        }

        Item { width: parent.width; height: 4 }
        Rectangle {
            visible: !rail.compact
            x: 12
            width: parent.width - 24
            height: 1
            color: Theme.hairline
        }
        Item { width: parent.width; height: 8 }
        Column {
            visible: !rail.compact
            x: 16
            spacing: 4
            Row {
                spacing: 6
                Rectangle { width: 5; height: 5; radius: 3; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Creator definition"; color: Theme.secondary; font.family: Theme.ui; font.pixelSize: 8 }
            }
            Row {
                spacing: 6
                Rectangle { width: 5; height: 5; radius: 3; border.width: 1; border.color: Theme.secondary; color: "transparent"; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Personal overrides"; color: Theme.secondary; font.family: Theme.ui; font.pixelSize: 8 }
            }
            Row {
                spacing: 6
                Rectangle { width: 5; height: 5; radius: 3; color: Theme.meta; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Application input"; color: Theme.secondary; font.family: Theme.ui; font.pixelSize: 8 }
            }
        }
    }
}
