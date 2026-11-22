import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import ReviewArt 1.0
import "."

Rectangle {
    id: pad
    property bool curveExpanded: true
    signal curveToggle
    color: Theme.canvas

    Rectangle {
        id: toolbar
        height: 44
        width: parent.width
        color: Theme.panel
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            spacing: 5

            Text {
                text: "TEST"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 1.0
                anchors.verticalCenter: parent.verticalCenter
            }
            SelectField {
                objectName: "padMode"
                width: 108
                height: 24
                options: studio.testModes
                current: studio.testMode
                onSelected: function(value) { studio.setTestMode(value) }
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: 4; height: 1 }
            Text {
                text: "PRESSURE"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 7
                font.letterSpacing: 0.7
                anchors.verticalCenter: parent.verticalCenter
            }
            MiniSlider {
                width: 64
                value: studio.padPressure * 100
                onMoved: function(value) { studio.setPadPressure(value / 100) }
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: Math.round(studio.padPressure * 100) + "%"
                color: Theme.secondary
                font.family: Theme.ui
                font.pixelSize: 8
                font.features: {"tnum": 1}
                width: 28
                anchors.verticalCenter: parent.verticalCenter
            }

            Item { width: Math.max(0, parent.width - 548); height: 1 }

            ActionButton {
                objectName: "padUndo"
                icon: "undo"
                compact: true
                enabled: studio.padCanUndo
                onClicked: studio.undoPad()
                anchors.verticalCenter: parent.verticalCenter
            }
            ActionButton {
                objectName: "padRedo"
                icon: "redo"
                compact: true
                enabled: studio.padCanRedo
                onClicked: studio.redoPad()
                anchors.verticalCenter: parent.verticalCenter
            }
            ActionButton {
                objectName: "retainInput"
                text: "Retain"
                compact: true
                selected: studio.compare
                enabled: studio.strokes.length > 0
                onClicked: studio.retainInput()
                anchors.verticalCenter: parent.verticalCenter
            }
            ActionButton {
                objectName: "clearPad"
                text: "Clear"
                compact: true
                enabled: studio.strokes.length > 0
                onClicked: studio.clearPad()
                anchors.verticalCenter: parent.verticalCenter
            }
            ActionButton {
                icon: "chevron"
                compact: true
                rotation: pad.curveExpanded ? 180 : 0
                onClicked: pad.curveToggle()
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Item {
        id: canvasWell
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: dock.top
        anchors.margins: 14

        Rectangle {
            anchors.fill: parent
            anchors.margins: -8
            radius: Theme.radius
            color: Theme.panel
            border.width: 1
            border.color: Theme.innerHighlight
        }

        Artwork {
            id: padCanvas
            objectName: "padCanvas"
            anchors.fill: parent
            anchors.margins: 1
            kind: "pad"
            clip: true
        }

        MouseArea {
            id: ink
            anchors.fill: padCanvas
            hoverEnabled: true
            cursorShape: Qt.CrossCursor
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    studio.beginStroke(mouse.x / width, mouse.y / height, -1)
                }
            }
            onPositionChanged: function(mouse) {
                if (pressed)
                    studio.moveStroke(mouse.x / width, mouse.y / height, -1)
            }
            onReleased: studio.endStroke()
            onCanceled: studio.cancelStroke()
        }

        Rectangle {
            visible: studio.compare && studio.hasRetained
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            width: retainedLabel.implicitWidth + 14
            height: 20
            radius: 2
            color: Theme.panel
            border.width: 1
            border.color: Theme.hairline
            Text {
                id: retainedLabel
                anchors.centerIn: parent
                text: "RETAINED · SAME INPUT"
                color: Theme.secondary
                font.family: Theme.ui
                font.pixelSize: 7
                font.letterSpacing: 0.6
            }
        }

        Text {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            text: "Draw with the selected tool · " + studio.testMode + " sheet"
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: 8
        }

        Text {
            visible: studio.strokes.length === 0
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 10
            text: "Preview compositor · not the production renderer"
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: 8
        }

        Text {
            visible: studio.editing
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            text: "FAST PATH DRAFT · FULL QUALITY ON RELEASE"
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: 7
            font.letterSpacing: 0.6
        }

        Text {
            visible: studio.activeStroke.length > 0
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            text: "LIVE PATH · FULL STAMP ON RELEASE"
            color: Theme.meta
            font.family: Theme.ui
            font.pixelSize: 7
            font.letterSpacing: 0.6
        }
    }

    CurveDock {
        id: dock
        width: parent.width
        anchors.bottom: parent.bottom
        expanded: pad.curveExpanded
        onToggleRequested: pad.curveToggle()
    }
}
