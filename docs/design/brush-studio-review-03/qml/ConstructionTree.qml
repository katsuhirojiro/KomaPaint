import QtQuick
import QtQuick.Controls.Basic as C
import "."

Rectangle {
    id: card
    signal tipRequested
    signal reuseRequested
    signal importRequested
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
        spacing: 7

        Row {
            width: parent.width
            height: 20
            Text {
                text: "CONSTRUCTION"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 1.0
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Math.max(0, parent.width - 180); height: 1 }
            Text {
                text: studio.counts.mark + " marks · " + studio.counts.brush + " brushes"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.hairline
        }

        Repeater {
            model: studio.tree
            Item {
                id: treeRow
                width: content.width
                height: 26
                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: modelData.selected ? Theme.raised : (treeMouse.containsMouse ? Theme.hover : "transparent")
                    border.width: modelData.selected ? 1 : 0
                    border.color: Theme.innerHighlight
                }
                Rectangle {
                    visible: modelData.depth > 0
                    x: 5 + modelData.depth * 12
                    width: 1
                    height: parent.height
                    color: Theme.hairline
                }
                LineIcon {
                    x: 12 + modelData.depth * 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13; height: 13
                    kind: modelData.kind === "group" ? "construction"
                        : (modelData.kind === "brush" ? "secondary"
                        : (modelData.kind === "texture" ? "texture"
                        : (modelData.kind === "mask" ? "render" : "tip")))
                    tint: modelData.selected ? Theme.text : Theme.secondary
                }
                Text {
                    x: 32 + modelData.depth * 12
                    width: parent.width - x - 54
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    color: modelData.visible ? (modelData.selected ? Theme.text : Theme.secondary) : Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    font.weight: modelData.selected ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    width: 42
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.kind === "brush" ? Math.round(modelData.children) + " parts" : ""
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 7
                    horizontalAlignment: Text.AlignRight
                }
                Rectangle {
                    width: 20; height: 20
                    anchors.right: parent.right
                    anchors.rightMargin: 34
                    anchors.verticalCenter: parent.verticalCenter
                    color: "transparent"
                    LineIcon {
                        anchors.centerIn: parent
                        kind: "eye"
                        width: 12; height: 12
                        tint: modelData.visible ? Theme.secondary : Theme.disabled
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: studio.toggleNodeVisible(modelData.uid)
                    }
                }
                MouseArea {
                    id: treeMouse
                    anchors.fill: parent
                    anchors.rightMargin: 64
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: studio.selectNode(modelData.uid)
                }
                objectName: "treeRow_" + modelData.uid
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.hairline
        }

        Row {
            width: parent.width
            spacing: 3
            ActionButton { objectName: "addMark"; text: "Mark"; icon: "plus"; compact: true; onClicked: studio.addNode("mark") }
            ActionButton { objectName: "addGroup"; text: "Group"; icon: "plus"; compact: true; onClicked: studio.addNode("group") }
            ActionButton { objectName: "addTexture"; text: "Texture"; icon: "plus"; compact: true; onClicked: studio.addNode("texture") }
            ActionButton { objectName: "addMask"; text: "Mask"; icon: "plus"; compact: true; onClicked: studio.addNode("mask") }
        }
        Row {
            width: parent.width
            spacing: 3
            ActionButton { objectName: "addSecondary"; text: "Secondary brush"; icon: "plus"; compact: true; onClicked: studio.addSecondary() }
            ActionButton { text: "Reuse"; icon: "secondary"; compact: true; onClicked: card.reuseRequested() }
        }

        Row {
            width: parent.width
            spacing: 3
            ActionButton { text: "Duplicate"; icon: "copy"; compact: true; onClicked: studio.duplicateNode() }
            ActionButton { text: "Up"; icon: "chevron"; compact: true; rotation: 180; onClicked: studio.moveNode(-1) }
            ActionButton { text: "Down"; icon: "chevron"; compact: true; onClicked: studio.moveNode(1) }
            ActionButton { text: "Remove"; compact: true; onClicked: studio.removeNode() }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.hairline
        }

        Text {
            width: parent.width
            text: studio.selectedNode.name + "  ·  " + studio.selectedNode.detail
            color: Theme.secondary
            font.family: Theme.ui
            font.pixelSize: Theme.small
            elide: Text.ElideRight
        }

        Item {
            visible: studio.selectedNode.kind === "mark"
            width: parent.width
            height: visible ? 58 : 0
            Flickable {
                anchors.fill: parent
                contentWidth: tipRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: tipRow
                    spacing: 4
                    Repeater {
                        model: studio.tipChoices
                        Rectangle {
                            width: 44; height: 44
                            radius: 2
                            color: studio.selectedNode.asset === modelData.id ? Theme.raised : Theme.track
                            border.width: 1
                            border.color: studio.selectedNode.asset === modelData.id ? Theme.text : Theme.hairline
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                source: modelData.source
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: studio.setNodeTip(modelData.id)
                            }
                        }
                    }
                    ActionButton { text: "Draw"; icon: "tip"; compact: true; height: 44; onClicked: card.tipRequested() }
                    ActionButton { text: "Import"; icon: "plus"; compact: true; height: 44; onClicked: card.importRequested() }
                }
            }
        }
    }
}
