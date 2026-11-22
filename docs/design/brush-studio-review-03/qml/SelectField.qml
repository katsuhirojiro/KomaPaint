import QtQuick
import QtQuick.Controls.Basic as C
import "."

Rectangle {
    id: field
    property var options: []
    property string current: ""
    property string placeholder: "Choose"
    signal selected(string value)
    implicitWidth: 180
    implicitHeight: 28
    radius: Theme.radiusSmall
    color: mouse.containsMouse ? Theme.hover : Theme.track
    border.width: 1
    border.color: Theme.hairline

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: chevron.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: field.current.length ? field.current : field.placeholder
        color: field.current.length ? Theme.text : Theme.meta
        font.family: Theme.ui
        font.pixelSize: Theme.body
        elide: Text.ElideRight
    }
    LineIcon {
        id: chevron
        kind: "chevron"
        width: 12; height: 12
        anchors.right: parent.right
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        tint: Theme.secondary
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: menu.visible ? menu.close() : menu.open()
    }

    C.Popup {
        id: menu
        parent: field
        x: 0
        y: field.height + 4
        width: Math.max(field.width, 190)
        height: Math.min(330, list.implicitHeight + 8)
        padding: 4
        modal: false
        closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.elevated
            radius: Theme.radiusSmall
            border.width: 1
            border.color: Theme.hairline
        }
        contentItem: Flickable {
            id: flick
            contentWidth: width
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: list
                width: flick.width
                Repeater {
                    model: field.options
                    Rectangle {
                        width: list.width
                        height: 28
                        radius: 2
                        color: modelData === field.current ? Theme.raised : (rowMouse.containsMouse ? Theme.hover : "transparent")
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: modelData === field.current ? Theme.text : Theme.secondary
                            font.family: Theme.ui
                            font.pixelSize: Theme.body
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                field.current = modelData
                                field.selected(modelData)
                                menu.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
