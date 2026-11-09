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
        objectName: "canvasViewport"
        anchors.fill: parent
        document: Workspace.document
        history: Workspace.history
        zoom: Workspace.zoom
        onZoomRequested: function(value) { Workspace.zoom = value }
        onStrokeError: function(message) { brushError.text = message; brushErrorDialog.open() }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
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
    // Qt Quick pointer handlers deliver mouse and stylus samples independently.
    PointHandler {
        id: mouseBrush
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.LeftButton
        enabled: Workspace.activeTool === "brush"
        property bool drawing: false
        property point lastPosition
        function finish() { if (drawing) { canvas.endStroke(lastPosition.x, lastPosition.y, 1); drawing = false } }
        onActiveChanged: {
            if (active) { finish(); lastPosition = point.position; drawing = canvas.beginStroke(lastPosition.x, lastPosition.y, 1, Workspace.brush, Workspace.colors.foreground) }
            else if (drawing) Qt.callLater(finish)
        }
        onPointChanged: if (active && drawing) { lastPosition = point.position; canvas.continueStroke(lastPosition.x, lastPosition.y, 1) }
        onCanceled: { canvas.cancelStroke(); drawing = false }
    }
    PointHandler {
        id: penBrush
        acceptedDevices: PointerDevice.Stylus | PointerDevice.Airbrush
        acceptedPointerTypes: PointerDevice.Pen
        acceptedButtons: Qt.LeftButton
        enabled: Workspace.activeTool === "brush"
        property bool drawing: false
        property point lastPosition
        property real lastPressure: 0
        function finish() { if (drawing) { canvas.endStroke(lastPosition.x, lastPosition.y, lastPressure); drawing = false } }
        onActiveChanged: {
            if (active) { finish(); lastPosition = point.position; lastPressure = point.pressure; drawing = canvas.beginStroke(lastPosition.x, lastPosition.y, lastPressure, Workspace.brush, Workspace.colors.foreground) }
            else if (drawing) Qt.callLater(finish)
        }
        onPointChanged: if (active && drawing) { lastPosition = point.position; lastPressure = point.pressure; canvas.continueStroke(lastPosition.x, lastPosition.y, lastPressure) }
        onCanceled: { canvas.cancelStroke(); drawing = false }
    }
    Dialog {
        id: brushErrorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Unable to paint")
        standardButtons: Dialog.Ok
        contentItem: Label { id: brushError; color: KP.text; wrapMode: Text.WordWrap; width: 360 }
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
