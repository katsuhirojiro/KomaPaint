import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import "."

C.Dialog {
    id: room
    objectName: "reuseRoom"
    modal: true
    title: "Reusable brushes and groups"
    width: Math.min(520, parent ? parent.width - 60 : 520)
    height: Math.min(450, parent ? parent.height - 60 : 450)
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
            text: "Reuse a construction"
            color: Theme.text
            font.family: Theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        ActionButton {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            icon: "close"; compact: true
            onClicked: room.close()
        }
    }
    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            Text {
                text: "An inserted brush is an independent copy: its marks, groups and textures are editable here without changing the source. Reuse is unlimited."
                color: Theme.secondary
                font.family: Theme.ui
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            C.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                contentWidth: availableWidth
                Column {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: studio.reusableBrushes
                        Rectangle {
                            width: parent.width
                            height: 44
                            radius: 3
                            color: reuseMouse.containsMouse ? Theme.raised : Theme.track
                            border.width: 1
                            border.color: Theme.hairline
                            Column {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1
                                Text {
                                    text: modelData.name
                                    color: Theme.text
                                    font.family: Theme.ui
                                    font.pixelSize: Theme.small
                                }
                                Text {
                                    text: modelData.source
                                    color: Theme.meta
                                    font.family: Theme.ui
                                    font.pixelSize: 8
                                }
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "INSERT"
                                color: Theme.secondary
                                font.family: Theme.ui
                                font.pixelSize: 8
                                font.letterSpacing: 0.6
                            }
                            MouseArea {
                                id: reuseMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    studio.insertReusable(modelData.id)
                                    room.close()
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                ActionButton { text: "Save selected secondary brush"; compact: true; onClicked: studio.makeReusable() }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Close"; compact: true; onClicked: room.close() }
            }
        }
    }
}
