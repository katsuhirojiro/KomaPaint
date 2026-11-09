import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../themes"

Rectangle {
    id: ws
    color: KP.canvasBg
    function fitCanvas() { canvas.fit() }
    CanvasViewport {
        id: canvas
        anchors.fill: parent
        document: Workspace.document
        zoom: Workspace.zoom
        onZoomRequested: function(value) { Workspace.zoom = value }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton | Qt.LeftButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        property real lastX
        property real lastY
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: function(mouse) {
            if (pressed) { canvas.panBy(mouse.x - lastX, mouse.y - lastY); lastX = mouse.x; lastY = mouse.y }
        }
        onWheel: function(wheel) {
            canvas.zoomAt(wheel.x, wheel.y, Math.pow(1.0015, wheel.angleDelta.y))
            wheel.accepted = true
        }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: infoText.implicitWidth + 12
        height: infoText.implicitHeight + 6
        color: "#00000066"
        radius: KP.radiusS
        Label {
            id: infoText
            anchors.centerIn: parent
            text: Workspace.document.width + " × " + Workspace.document.height + " px  •  " + Math.round(Workspace.document.dpi) + " dpi"
            color: "#e7e8ea"
            font.family: KP.fontMono
            font.pixelSize: KP.fontXS
        }
    }
}
