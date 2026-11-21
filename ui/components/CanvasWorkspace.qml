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
    function resetRotation() { canvas.resetRotation() }

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
        inputSettings: Workspace.inputSettings
        performanceSettings: Workspace.performanceSettings
        brushSettings: Workspace.brush
        toolSettings: Workspace.toolSettings
        brushColor: Workspace.colors.foreground
        activeTool: Workspace.activeTool
        zoom: Workspace.zoom
        // The desk and the transparency checkerboard are part of the theme,
        // so a theme switch re-paints the canvas chrome as well.
        deskColour: KP.canvasBg
        checkerLight: KP.transparencyLight
        checkerDark: KP.transparencyDark
        onZoomRequested: function(value) { if (canvas.document === Workspace.document) Workspace.zoom = value }
        onViewChanged: { ws.rememberView(); ws.refreshPaper() }
        onStrokeError: function(message) { brushError.text = message; brushErrorDialog.open() }
        onTextInputStateChanged: {
            if (canvas.textInputRequested) {
                inlineTextInput.text = ""
                inlineTextInput.forceActiveFocus()
            }
        }
        onEyedropperPicked: function(picked) { Workspace.colors.foreground = picked }
        onBarrelActionRequested: function(action) {
            if (action === "right_click") {
                canvasContextMenu.x = Math.max(8, Math.min(ws.width - canvasContextMenu.width - 8, canvas.cursorPos.x))
                canvasContextMenu.y = Math.max(8, Math.min(ws.height - canvasContextMenu.height - 8, canvas.cursorPos.y))
                canvasContextMenu.open()
            }
        }
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
    // Legacy tip outline. A single ellipse would misrepresent a native brush
    // with a bitmap, several tips, or per-stamp transforms.
    Item {
        id: brushOutline
        objectName: "brushOutline"
        readonly property real tiltMag: canvas.cursorHasTilt
            ? Math.min(1.0, Math.hypot(canvas.cursorTiltX, canvas.cursorTiltY) / 90.0) : 0.0
        readonly property real tiltScale: (canvas.cursorHasTilt && Workspace.brush.tiltSize > 0)
            ? (1.0 + (Workspace.brush.tiltSize / 100.0) * tiltMag * 1.5) : 1.0
        readonly property real diameter: Math.max(3, Workspace.brush.size * tiltScale * canvas.zoom / 100)
        readonly property real aspect: Math.max(1.0, (Workspace.brush.aspectRatio / 100.0)
            * ((canvas.cursorHasTilt && Workspace.brush.tiltAngle) ? (1.0 + tiltMag * 0.75) : 1.0))
        readonly property real outlineHeight: Math.max(3, diameter / aspect)
        readonly property real angleDeg: Workspace.brush.tipAngle
            + ((canvas.cursorHasTilt && Workspace.brush.tiltAngle && tiltMag > 0.02)
               ? (Math.atan2(canvas.cursorTiltY, canvas.cursorTiltX) * 180.0 / Math.PI) : 0.0)
            + (Workspace.brush.stylusRotation ? canvas.cursorRotationDeg : 0.0)
        // When the whole viewport fits inside the tip, no edge is visible.
        // Don't hand a billion-pixel rounded rectangle to the scene graph.
        readonly property real farthestCorner: Math.hypot(Math.max(Math.abs(canvas.cursorPos.x), Math.abs(canvas.width - canvas.cursorPos.x)),
                                                          Math.max(Math.abs(canvas.cursorPos.y), Math.abs(canvas.height - canvas.cursorPos.y)))
        visible: Workspace.subTools.paintTool && !Workspace.brush.nativeBrush && canvas.cursorVisible && outlineHeight / 2 <= farthestCorner + 2
        x: canvas.cursorPos.x - width / 2
        y: canvas.cursorPos.y - height / 2
        width: visible ? diameter : 0
        height: visible ? outlineHeight : 0
        rotation: angleDeg
        Rectangle {
            anchors.fill: parent
            radius: Math.min(width, height) / 2
            color: "transparent"
            border.width: 1
            border.color: canvas.cursorEraserActive ? KP.ember : "#f2f2f2"
            antialiasing: true
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.min(width, height) / 2
                color: "transparent"
                border.width: 1
                border.color: "#80000000"
                antialiasing: true
            }
        }
    }
    // ---- Stage 11 Interactive Tool HUDs -------------------------------------
    // Smart Fill Preview confirmation bar
    Rectangle {
        id: regionPreviewHud
        objectName: "regionPreviewHud"
        visible: canvas.hasPendingRegionPreview
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: KP.gapM
        width: previewHudRow.implicitWidth + KP.gapM * 2
        height: 32
        radius: KP.radiusM
        color: KP.floatingBg
        border.width: 1
        border.color: KP.accent
        z: 4
        RowLayout {
            id: previewHudRow
            anchors.centerIn: parent
            spacing: KP.gapS
            Text {
                text: canvas.regionPreviewStatus
                color: KP.text
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "commitRegionFillButton"
                small: true
                variant: "primary"
                label: qsTr("Commit Fill")
                onClicked: canvas.commitPendingRegionFill()
            }
            KpButton {
                objectName: "cancelRegionFillButton"
                small: true
                label: qsTr("Cancel")
                onClicked: canvas.cancelPendingRegionFill()
            }
        }
    }

    // Boundary-Aware Eraser protection bar
    Rectangle {
        id: boundaryEraserHud
        objectName: "boundaryEraserHud"
        visible: Workspace.activeTool === "eraser"
                 && Workspace.toolSettings
                 && Workspace.toolSettings.executableTool === "boundary_eraser"
        anchors.top: regionPreviewHud.visible ? regionPreviewHud.bottom : parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: KP.gapM
        width: boundaryHudRow.implicitWidth + KP.gapM * 2
        height: 30
        radius: KP.radiusM
        color: KP.floatingBg
        border.width: 1
        border.color: canvas.hasProtectedRegion ? KP.accent : KP.border
        z: 4
        RowLayout {
            id: boundaryHudRow
            anchors.centerIn: parent
            spacing: KP.gapS
            Text {
                text: canvas.hasProtectedRegion
                      ? canvas.protectedRegionStatus
                      : qsTr("Boundary-Aware Eraser: click inside region to protect (Alt+click to re-sample)")
                color: canvas.hasProtectedRegion ? KP.textAccent : KP.textMuted
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "clearProtectedRegionButton"
                visible: canvas.hasProtectedRegion
                small: true
                label: qsTr("Clear")
                onClicked: canvas.clearProtectedRegion()
            }
        }
    }

    // Active Selection quick-action bar
    Rectangle {
        id: selectionHud
        objectName: "selectionActionHud"
        visible: Workspace.hasSelection && !canvas.hasPendingRegionPreview
        anchors.top: boundaryEraserHud.visible ? boundaryEraserHud.bottom : parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: KP.gapM
        width: selectionHudRow.implicitWidth + KP.gapM * 2
        height: 30
        radius: KP.radiusM
        color: KP.floatingBg
        border.width: 1
        border.color: KP.border
        z: 4
        RowLayout {
            id: selectionHudRow
            anchors.centerIn: parent
            spacing: KP.gapXS
            KpButton { small: true; label: qsTr("Deselect"); onClicked: Workspace.deselect() }
            KpButton { small: true; label: qsTr("Invert"); onClicked: Workspace.invertSelection() }
            KpButton { small: true; label: qsTr("Expand"); onClicked: Workspace.expandSelection(2) }
            KpButton { small: true; label: qsTr("Contract"); onClicked: Workspace.contractSelection(2) }
            KpButton { small: true; label: qsTr("Feather"); onClicked: Workspace.featherSelection(3) }
            KpButton { small: true; label: qsTr("Clear"); onClicked: canvas.clearSelectedContent() }
        }
    }

    // Inline Text tool popover
    Rectangle {
        id: inlineTextPopover
        objectName: "inlineTextPopover"
        visible: canvas.textInputRequested
        x: Math.max(8, Math.min(ws.width - width - 8, canvas.textInputViewPos.x))
        y: Math.max(8, Math.min(ws.height - height - 8, canvas.textInputViewPos.y))
        width: 240
        height: 36
        radius: KP.radiusS
        color: KP.floatingBg
        border.width: 1
        border.color: KP.accent
        z: 5
        RowLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                height: 26
                radius: KP.radiusS
                color: KP.inputBg
                border.width: 1
                border.color: inlineTextInput.activeFocus ? KP.accent : KP.border
                TextInput {
                    id: inlineTextInput
                    objectName: "inlineTextInput"
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    color: KP.text
                    font.pixelSize: KP.fontS
                    clip: true
                    selectByMouse: true
                    onAccepted: {
                        if (text.trim().length > 0)
                            canvas.applyTextAt(canvas.textInputDocPos.x, canvas.textInputDocPos.y, text)
                        else
                            canvas.dismissTextInput()
                    }
                    Keys.onEscapePressed: canvas.dismissTextInput()
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: parent.text.length === 0
                        text: qsTr("Type text & press Enter…")
                        color: KP.textSubtle
                        font.pixelSize: KP.fontS
                        elide: Text.ElideRight
                    }
                }
            }
            KpButton {
                small: true
                variant: "primary"
                label: qsTr("OK")
                onClicked: {
                    if (inlineTextInput.text.trim().length > 0)
                        canvas.applyTextAt(canvas.textInputDocPos.x, canvas.textInputDocPos.y, inlineTextInput.text)
                    else
                        canvas.dismissTextInput()
                }
            }
        }
    }

    KpMenu {
        id: canvasContextMenu
        KpMenuItem {
            text: qsTr("Reset View Rotation")
            enabled: canvas.viewRotation !== 0
            onTriggered: canvas.resetRotation()
        }
        KpMenuItem {
            text: qsTr("Pick Colour at Cursor")
            onTriggered: {
                const picked = canvas.colorAt(canvas.cursorPos.x, canvas.cursorPos.y)
                if (picked !== undefined && picked !== null) Workspace.colors.foreground = picked
            }
        }
        KpMenuSeparator { }
        KpMenuItem {
            text: Workspace.activeTool === "eraser" ? qsTr("Switch to Brush") : qsTr("Switch to Eraser")
            onTriggered: Workspace.activeTool = Workspace.activeTool === "eraser" ? "brush" : "eraser"
        }
    }
    Dialog {
        id: brushErrorDialog
        objectName: "brushErrorDialog"
        anchors.centerIn: parent
        width: Math.min(Math.max(280, ws.width - 48), 400)
        modal: true
        title: qsTr("Unable to paint")
        standardButtons: Dialog.Ok
        contentItem: Label { id: brushError; objectName: "brushErrorLabel"; color: KP.text; wrapMode: Text.WordWrap }
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
            Rectangle { visible: canvas.viewRotation !== 0; width: 1; height: 12; color: KP.hairline }
            AbstractButton {
                visible: canvas.viewRotation !== 0
                width: 44
                height: KP.controlHeightSmall
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                ToolTip.text: qsTr("Canvas rotation — click to reset to 0°")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                background: Rectangle {
                    radius: KP.radiusXS
                    color: parent.hovered ? KP.hoverOverlay : "transparent"
                }
                contentItem: Text {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: Math.round(canvas.viewRotation) + "°"
                    color: KP.textAccent
                    font.pixelSize: KP.fontXS + 1
                    font.family: KP.fontMono
                }
                onClicked: canvas.resetRotation()
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
