import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import ReviewArt 1.0
import "."

C.Dialog {
    id: room
    objectName: "tipRoom"
    modal: true
    title: "Draw a tip"
    width: Math.min(660, parent ? parent.width - 60 : 660)
    height: Math.min(470, parent ? parent.height - 60 : 470)
    x: parent ? Math.max(0, (parent.width - width) / 2) : 0
    y: parent ? Math.max(0, (parent.height - height) / 2) : 0
    padding: 0
    closePolicy: C.Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.elevated
        radius: Theme.radius
        border.width: 1
        border.color: Theme.hairline
    }
    header: Rectangle {
        height: 44
        color: "transparent"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }
        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "Draw a tip"
            color: Theme.text
            font.family: Theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        ActionButton {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            icon: "close"
            compact: true
            onClicked: room.close()
        }
    }

    contentItem: Item {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Item {
                Layout.preferredWidth: 390
                Layout.fillHeight: true
                Rectangle {
                    anchors.fill: parent
                    color: Theme.track
                    radius: 3
                    border.width: 1
                    border.color: Theme.hairline
                }
                Artwork {
                    id: tipCanvas
                    objectName: "tipCanvas"
                    anchors.fill: parent
                    anchors.margins: 1
                    kind: "tip"
                }
                MouseArea {
                    anchors.fill: tipCanvas
                    cursorShape: Qt.CrossCursor
                    onPressed: studio.beginTip(mouse.x / width, mouse.y / height)
                    onPositionChanged: if (pressed) studio.moveTip(mouse.x / width, mouse.y / height)
                    onReleased: studio.endTip()
                }
                Text {
                    visible: studio.tipStrokes.length === 0
                    anchors.centerIn: parent
                    text: "Draw several strokes.\nThe fitted silhouette becomes the mark."
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.3
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Text {
                    text: "A hand-drawn tip is an ordinary alpha image. It is stored once, shared by marks, and can be replaced without touching the brush's settings."
                    color: Theme.secondary
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    text: "BRUSH WIDTH"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                    font.letterSpacing: 0.7
                }
                MiniSlider {
                    Layout.fillWidth: true
                    value: studio.tipSize
                    minimum: 2
                    maximum: 60
                    onMoved: function(value) { studio.setTipSize(value) }
                }
                Text {
                    text: Math.round(studio.tipSize) + " px"
                    color: Theme.secondary
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    font.features: {"tnum": 1}
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    ActionButton { text: "Undo"; icon: "undo"; compact: true; onClicked: studio.undoTip() }
                    ActionButton { text: "Clear"; compact: true; onClicked: studio.clearTip() }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "useTip"
                        text: "Use as tip"
                        icon: "tip"
                        selected: true
                        onClicked: {
                            studio.makeDrawnTip()
                            room.close()
                        }
                    }
                }
            }
        }
    }
}
