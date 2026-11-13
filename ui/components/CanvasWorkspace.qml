import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../themes"

Rectangle {
    id: ws
    color: KP.canvasBg
    clip: true   // the brush outline must not draw over neighbouring panels
    function fitCanvas() { canvas.fit() }

    // Zoom and viewport position belong to the project, not to the window.
    // The canvas reports every change to the active project, and each time a
    // different project is shown its own stored view is put back. A project
    // shown for the first time has no stored view and is fitted instead.
    property bool applyingView: false
    function rememberView() {
        if (applyingView || !canvas.document || canvas.document !== Workspace.document) return
        if (canvas.width <= 0 || canvas.height <= 0) return
        const center = canvas.viewCenter
        Workspace.storeActiveView(canvas.zoom, center.x, center.y)
    }
    function applyStoredView() {
        if (!canvas.document) return
        applyingView = true
        if (Workspace.activeViewStored()) {
            const center = Workspace.activeViewCenter()
            canvas.restoreView(Workspace.zoom, center.x, center.y)
        } else {
            canvas.fit()
        }
        applyingView = false
        rememberView()
    }
    CanvasViewport {
        id: canvas
        objectName: "canvasViewport"
        anchors.fill: parent
        document: Workspace.document
        history: Workspace.history
        zoom: Workspace.zoom
        // The desk and the transparency checkerboard are part of the theme,
        // so a theme switch re-paints the canvas chrome as well.
        deskColour: KP.canvasBg
        checkerLight: KP.transparencyLight
        checkerDark: KP.transparencyDark
        onZoomRequested: function(value) { if (canvas.document === Workspace.document) Workspace.zoom = value }
        onViewChanged: ws.rememberView()
        onStrokeError: function(message) { brushError.text = message; brushErrorDialog.open() }
        // Runs immediately after setDocument() has updated the canvas, so the
        // view is applied against the new document's dimensions.
        Connections {
            target: canvas
            function onDocumentChanged() { ws.applyStoredView() }
        }
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
    // View tools. Painting tools are handled by the pointer handlers below.
    //  hand        drag moves the view
    //  zoom        click zooms in around the pointer; Alt-click or right-click zooms out
    //  eyedropper  click or drag picks the drawing colour from the artwork
    MouseArea {
        id: viewTool
        objectName: "viewToolArea"
        anchors.fill: parent
        readonly property string tool: Workspace.activeTool
        enabled: tool === "hand" || tool === "zoom" || tool === "eyedropper"
        acceptedButtons: tool === "zoom" ? (Qt.LeftButton | Qt.RightButton) : Qt.LeftButton
        cursorShape: tool === "hand" ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.CrossCursor
        property real lastX
        property real lastY
        function pick(x, y) {
            const picked = canvas.colorAt(x, y)
            if (picked !== undefined && picked !== null) Workspace.colors.foreground = picked
        }
        onPressed: function(mouse) {
            lastX = mouse.x; lastY = mouse.y
            if (tool === "eyedropper") pick(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            if (tool === "hand") { canvas.panBy(mouse.x - lastX, mouse.y - lastY); lastX = mouse.x; lastY = mouse.y }
            else if (tool === "eyedropper") pick(mouse.x, mouse.y)
        }
        onClicked: function(mouse) {
            if (tool !== "zoom") return
            const out = mouse.button === Qt.RightButton || (mouse.modifiers & Qt.AltModifier)
            canvas.zoomAt(mouse.x, mouse.y, out ? 1 / 1.5 : 1.5)
        }
    }
    // Outline of the brush tip at its real size on the artwork, following the
    // pointer while a painting tool is active.
    HoverHandler {
        id: brushHover
        enabled: Workspace.subTools.paintTool
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
    }
    Rectangle {
        id: brushOutline
        objectName: "brushOutline"
        readonly property real diameter: Math.max(3, Workspace.brush.size * canvas.zoom / 100)
        visible: Workspace.subTools.paintTool && brushHover.hovered
        x: brushHover.point.position.x - diameter / 2
        y: brushHover.point.position.y - diameter / 2
        width: diameter
        height: diameter
        radius: diameter / 2
        color: "transparent"
        border.width: 1
        border.color: "#f2f2f2"
        antialiasing: true
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: "#80000000"
            antialiasing: true
        }
    }
    // Qt Quick pointer handlers deliver mouse and stylus samples independently.
    PointHandler {
        id: mouseBrush
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.LeftButton
        enabled: Workspace.subTools.paintTool
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
        enabled: Workspace.subTools.paintTool
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
