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

    // The artwork's rectangle in viewport coordinates. The chrome around the
    // canvas (its edge, the shadow it casts on the desk) is drawn by QML, which
    // needs to know where the paper is; it is refreshed whenever the view or the
    // document changes rather than polled.
    property rect paperRect: Qt.rect(0, 0, 0, 0)
    readonly property bool paperVisible: paperRect.width > 2 && paperRect.height > 2

    Component.onCompleted: refreshPaper()
    onWidthChanged: refreshPaper()
    onHeightChanged: refreshPaper()
    function refreshPaper() {
        if (!canvas.document) { paperRect = Qt.rect(0, 0, 0, 0); return }
        const topLeft = canvas.documentToViewport(0, 0)
        const bottomRight = canvas.documentToViewport(canvas.document.width, canvas.document.height)
        paperRect = Qt.rect(topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y)
    }

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
        onViewChanged: { ws.rememberView(); ws.refreshPaper() }
        onStrokeError: function(message) { brushError.text = message; brushErrorDialog.open() }
        // "Fit to window" is asked of the canvas, which is the only thing that
        // knows how large the viewport currently is.
        Connections {
            target: Workspace
            function onFitRequested() { canvas.fit() }
        }
        // Runs immediately after setDocument() has updated the canvas, so the
        // view is applied against the new document's dimensions.
        Connections {
            target: canvas
            function onDocumentChanged() { ws.applyStoredView(); ws.refreshPaper() }
            function onZoomChanged() { ws.refreshPaper() }
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
    // ---- Navigation HUD ----------------------------------------------------
    // Zooming and fitting sit under the hand, at the corner of the canvas, with
    // the zoom as a real readout. It floats, so it is allowed a shadow.
    Rectangle {
        id: navHud
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: KP.gapL
        width: hudRow.implicitWidth + KP.gapM * 2
        height: 30
        radius: KP.radiusM
        color: KP.floatingBg
        border.width: 1
        border.color: KP.border
        opacity: hudMouse.containsMouse ? 1 : 0.94
        z: 3

        Behavior on opacity { NumberAnimation { duration: KP.durationFast } }
        MouseArea {
            id: hudMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        Row {
            id: hudRow
            anchors.centerIn: parent
            spacing: KP.gapXS

            IconButton {
                glyph: "zoom-out"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Zoom out")
                onActivated: Workspace.zoomOut()
            }
            AbstractButton {
                id: hudZoom
                width: 46
                height: KP.controlHeightSmall
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.name: qsTr("Zoom level")
                ToolTip.text: qsTr("Zoom level — click for presets")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                background: Rectangle {
                    radius: KP.radiusXS
                    color: hudZoom.hovered ? KP.hoverOverlay : "transparent"
                }
                contentItem: Text {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: Math.round(Workspace.zoom) + "%"
                    color: KP.text
                    font.pixelSize: KP.fontXS + 1
                    font.family: KP.fontMono
                }
                onClicked: hudZoomMenu.popup()
            }
            IconButton {
                glyph: "zoom-in"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Zoom in")
                onActivated: Workspace.zoomIn()
            }
            Rectangle { width: 1; height: 12; color: KP.hairline }
            IconButton {
                glyph: "zoom-fit"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Fit to window (Ctrl+0)")
                onActivated: Workspace.zoomFit()
            }
            IconButton {
                glyph: "actual"
                iconSize: KP.iconS
                cell: KP.controlHeightSmall
                tooltip: qsTr("Actual size (Ctrl+1)")
                onActivated: Workspace.resetZoom()
            }
        }

        KpMenu {
            id: hudZoomMenu
            x: navHud.width - width
            y: -height - KP.gapS
            KpMenuItem { text: qsTr("Fit to window\tCtrl+0"); onTriggered: Workspace.zoomFit() }
            KpMenuItem { text: qsTr("Actual size\tCtrl+1"); onTriggered: Workspace.resetZoom() }
            KpMenuSeparator { }
            KpMenuItem { text: qsTr("200%"); onTriggered: Workspace.zoom = 200 }
            KpMenuItem { text: qsTr("400%"); onTriggered: Workspace.zoom = 400 }
            KpMenuItem { text: qsTr("800%"); onTriggered: Workspace.zoom = 800 }
        }
    }

    // ---- Document facts ----------------------------------------------------
    // Small, quiet, monospaced: paper size and resolution, at the opposite
    // corner from the navigation.
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: KP.gapL
        width: facts.implicitWidth + KP.gapM * 2
        height: 22
        radius: KP.radiusS
        color: KP.isDark ? Qt.rgba(0, 0, 0, 0.28) : Qt.rgba(255, 255, 255, 0.72)
        z: 3

        Text {
            id: facts
            anchors.centerIn: parent
            text: Workspace.document
                  ? Workspace.document.width + " × " + Workspace.document.height
                    + "  ·  " + Math.round(Workspace.document.dpi) + " dpi"
                  : ""
            color: KP.isDark ? KP.textMuted : KP.textMuted
            font.family: KP.fontMono
            font.pixelSize: KP.fontXS
        }
    }
}
