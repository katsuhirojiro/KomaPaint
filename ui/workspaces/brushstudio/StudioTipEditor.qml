import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

// Direct pixel editing for a tip, image or mask component. The painted canvas
// is a coverage/paint source, not a second renderer: Save hands the pixels to
// BrushStudioModel, which stores them as a .kb resource and the production
// Digital renderer paints them on the next stroke.
Item {
    id: editor
    required property BrushStudioModel draft
    signal importImageRequested()
    property int brushSize: 18
    property bool erasing: false
    property bool modified: false
    property bool colourTip: !!(draft.selectedComponent && draft.selectedComponent.colour)
    property string paintColour: "#ffffff"
    readonly property int sourceSize: 256
    implicitHeight: 372

    function loadPixels() {
        colourTip = !!(draft.selectedComponent && draft.selectedComponent.colour)
        const stored = draft.loadSelectedPixels()
        if (!stored.rgba || stored.width <= 0 || stored.height <= 0) {
            canvas.width = sourceSize
            canvas.height = sourceSize
            const ctx = canvas.getContext("2d")
            ctx.clearRect(0, 0, canvas.width, canvas.height)
            if (!colourTip) {
                ctx.fillStyle = "#000000"
                ctx.fillRect(0, 0, canvas.width, canvas.height)
            }
            canvas.requestPaint()
            modified = false
            return
        }
        canvas.width = stored.width
        canvas.height = stored.height
        const ctx = canvas.getContext("2d")
        const image = ctx.createImageData(stored.width, stored.height)
        for (let i = 0; i < stored.rgba.length; ++i) {
            const packed = stored.rgba[i]
            image.data[i * 4] = (packed >> 16) & 255
            image.data[i * 4 + 1] = (packed >> 8) & 255
            image.data[i * 4 + 2] = packed & 255
            image.data[i * 4 + 3] = colourTip ? ((packed >> 24) & 255) : 255
            if (!colourTip) {
                const coverage = (packed >> 24) & 255
                image.data[i * 4] = coverage
                image.data[i * 4 + 1] = coverage
                image.data[i * 4 + 2] = coverage
            }
        }
        ctx.putImageData(image, 0, 0)
        canvas.requestPaint()
        modified = false
    }

    function clearCanvas() {
        const ctx = canvas.getContext("2d")
        ctx.clearRect(0, 0, canvas.width, canvas.height)
        if (!colourTip) {
            ctx.fillStyle = "#000000"
            ctx.fillRect(0, 0, canvas.width, canvas.height)
        }
        canvas.requestPaint()
        modified = true
    }

    function paintAt(x, y) {
        const ctx = canvas.getContext("2d")
        ctx.save()
        ctx.globalCompositeOperation = (erasing && colourTip) ? "destination-out" : "source-over"
        ctx.strokeStyle = erasing ? "#000000" : (colourTip ? paintColour : "#ffffff")
        ctx.fillStyle = ctx.strokeStyle
        ctx.lineWidth = brushSize
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()
        if (lastX < 0) {
            ctx.arc(x, y, brushSize / 2, 0, Math.PI * 2)
            ctx.fill()
        } else {
            ctx.moveTo(lastX, lastY)
            ctx.lineTo(x, y)
            ctx.stroke()
        }
        ctx.restore()
        lastX = x
        lastY = y
        modified = true
        canvas.requestPaint()
    }

    function savePixels() {
        if (!modified) return
        const image = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height)
        const pixels = []
        for (let i = 0; i < image.data.length; i += 4) {
            pixels.push(((image.data[i + 3] << 24) | (image.data[i] << 16) |
                         (image.data[i + 1] << 8) | image.data[i + 2]) >>> 0)
        }
        if (draft.setTipPixels(pixels, canvas.width, canvas.height)) modified = false
    }

    property real lastX: -1
    property real lastY: -1

    Connections {
        target: editor.draft
        function onSelectionChanged() { editor.loadPixels() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: KP.gapM
        spacing: KP.gapS

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            Text {
                Layout.fillWidth: true
                text: qsTr("PAINT TIP PIXELS")
                color: KP.text
                font.family: KP.fontMono
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "brushStudioTipErase"
                small: true
                glyph: "eraser"
                tip: qsTr("Erase painted pixels instead of adding coverage")
                toggled: editor.erasing
                onClicked: editor.erasing = !editor.erasing
            }
            KpButton {
                objectName: "brushStudioTipLoad"
                small: true
                label: qsTr("Load image...")
                tip: qsTr("Replace the stored pixels with an image file")
                onClicked: editor.importImageRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            Text {
                text: qsTr("Brush size")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
                Accessible.description: qsTr("How wide the painting brush is on the tip canvas.")
            }
            Slider {
                id: sizeRail
                Layout.fillWidth: true
                from: 2
                to: 64
                value: editor.brushSize
                onMoved: editor.brushSize = Math.round(value)
                Accessible.name: qsTr("Tip paint brush size")
                Accessible.description: qsTr("How wide the painting brush is on the tip canvas.")
            }
            Repeater {
                model: ["#ffffff", "#808080", "#000000", "#e0574f", "#5aa469"]
                Rectangle {
                    required property string modelData
                    width: 16
                    height: 16
                    radius: 8
                    color: modelData
                    border.width: editor.paintColour === modelData ? 2 : 1
                    border.color: editor.paintColour === modelData ? KP.focusRing : KP.border
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { editor.paintColour = parent.modelData; editor.erasing = false }
                    }
                }
            }
        }

        // Non-destructive adjustments of the imported tip. Each action is one
        // undo step and writes a new resource; Reset returns to the pixels that
        // were imported, not to a saved approximation of them.
        Flow {
            Layout.fillWidth: true
            spacing: KP.gapXS
            visible: editor.draft.selectedIndex >= 0
            KpButton {
                objectName: "brushTipCrop"
                small: true
                label: qsTr("Crop")
                tip: qsTr("Trim fully transparent edges from this tip")
                onClicked: editor.draft.editTip("crop")
            }
            KpButton {
                objectName: "brushTipRotateLeft"
                small: true
                label: qsTr("Rotate left")
                tip: qsTr("Turn this tip a quarter turn to the left")
                onClicked: editor.draft.editTip("rotate-left")
            }
            KpButton {
                objectName: "brushTipRotateRight"
                small: true
                label: qsTr("Rotate right")
                tip: qsTr("Turn this tip a quarter turn to the right")
                onClicked: editor.draft.editTip("rotate-right")
            }
            KpButton {
                objectName: "brushTipFlipH"
                small: true
                label: qsTr("Flip across")
                tip: qsTr("Mirror this tip left to right")
                onClicked: editor.draft.editTip("flip-horizontal")
            }
            KpButton {
                objectName: "brushTipFlipV"
                small: true
                label: qsTr("Flip down")
                tip: qsTr("Mirror this tip top to bottom")
                onClicked: editor.draft.editTip("flip-vertical")
            }
            KpButton {
                objectName: "brushTipInvert"
                small: true
                label: qsTr("Invert")
                tip: qsTr("Invert the tip's colours, or its coverage for an outline tip")
                onClicked: editor.draft.editTip("invert")
            }
            KpButton {
                objectName: "brushTipGrayscale"
                small: true
                label: qsTr("Grayscale")
                tip: qsTr("Turn the tip's colours into matching greys without changing transparency")
                onClicked: editor.draft.editTip("grayscale")
            }
            KpButton {
                objectName: "brushTipAlpha"
                small: true
                label: qsTr("Keep outline")
                tip: qsTr("Turn a colour tip into an outline tip. Fully opaque artwork uses its darkness as the outline.")
                onClicked: editor.draft.editTip("alpha")
            }
            KpButton {
                objectName: "brushTipReset"
                small: true
                label: qsTr("Reset to artwork")
                tip: qsTr("Return to the pixels imported from the illustration. Earlier edits stay in undo history.")
                enabled: editor.draft.canResetTipToOriginal
                onClicked: editor.draft.resetTipToOriginal()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            visible: editor.draft.selectedIndex >= 0
            Text {
                text: qsTr("Contrast")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            Slider {
                id: contrastRail
                objectName: "brushTipContrast"
                Layout.fillWidth: true
                from: -50
                to: 100
                value: 0
                stepSize: 1
                Accessible.name: qsTr("Tip contrast")
                Accessible.description: qsTr("Strengthen or soften the tip's light and dark values. The change is applied when you let go.")
                ToolTip.text: Accessible.description
                ToolTip.visible: pressed || activeFocus
                ToolTip.delay: 500
                onPressedChanged: if (!pressed && value !== 0) {
                    editor.draft.editTip("contrast", value)
                    value = 0
                }
            }
            Text {
                text: qsTr("Fade")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            Slider {
                id: fadeRail
                objectName: "brushTipTransparency"
                Layout.fillWidth: true
                from: 0
                to: 100
                value: 0
                stepSize: 1
                Accessible.name: qsTr("Tip fade")
                Accessible.description: qsTr("Reduce the tip's opacity. The change is applied when you let go.")
                ToolTip.text: Accessible.description
                ToolTip.visible: pressed || activeFocus
                ToolTip.delay: 500
                onPressedChanged: if (!pressed && value > 0) {
                    editor.draft.editTip("transparency", value)
                    value = 0
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            visible: editor.draft.selectedIndex >= 0
            Text {
                text: qsTr("Size")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            SpinBox {
                id: tipWidth
                objectName: "brushTipResizeWidth"
                from: 1
                to: 16384
                value: editor.draft.selectedComponent.resourceWidth > 0 ? editor.draft.selectedComponent.resourceWidth : 32
                Accessible.name: qsTr("Tip width")
                Accessible.description: qsTr("Width of the stored tip image in pixels.")
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
            }
            Text {
                text: qsTr("×")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            SpinBox {
                id: tipHeight
                objectName: "brushTipResizeHeight"
                from: 1
                to: 16384
                value: editor.draft.selectedComponent.resourceHeight > 0 ? editor.draft.selectedComponent.resourceHeight : 32
                Accessible.name: qsTr("Tip height")
                Accessible.description: qsTr("Height of the stored tip image in pixels.")
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
            }
            KpButton {
                objectName: "brushTipResize"
                small: true
                label: qsTr("Resize")
                tip: qsTr("Resize the stored tip image. The brush preview updates immediately.")
                enabled: tipWidth.value > 0 && tipHeight.value > 0
                onClicked: editor.draft.resizeTip(tipWidth.value, tipHeight.value)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 148
            color: KP.canvasBg
            border.width: KP.hairlineWidth
            border.color: KP.border
            clip: true

            Canvas {
                id: canvas
                objectName: "brushStudioTipCanvas"
                width: editor.sourceSize
                height: editor.sourceSize
                anchors.centerIn: parent
                scale: Math.min(1, (parent.width - 2 * KP.gapS) / width,
                                (parent.height - 2 * KP.gapS) / height)
                transformOrigin: Item.Center
                onAvailableChanged: if (available) editor.loadPixels()
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: function(mouse) {
                    const local = canvas.mapFromItem(this, mouse.x, mouse.y)
                    editor.erasing = mouse.button === Qt.RightButton
                    editor.lastX = -1
                    editor.paintAt(local.x, local.y)
                }
                onPositionChanged: function(mouse) {
                    const local = canvas.mapFromItem(this, mouse.x, mouse.y)
                    if (Math.abs(local.x) < canvas.width * 2 && Math.abs(local.y) < canvas.height * 2)
                        editor.paintAt(local.x, local.y)
                }
                onReleased: { editor.lastX = -1; editor.lastY = -1 }
            }
            Text {
                anchors.centerIn: parent
                visible: !editor.modified && canvas.width === editor.sourceSize
                width: Math.max(0, parent.width - 2 * KP.gapL)
                text: qsTr("Draw with a mouse, pen or touch. Right-drag erases.")
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Accessible.description: text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: canvas.width + " x " + canvas.height
                color: KP.textSubtle
                font.family: KP.fontMono
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "brushStudioTipClear"
                small: true
                label: qsTr("Clear")
                tip: qsTr("Clear the canvas and start a new painted tip")
                onClicked: editor.clearCanvas()
            }
            KpButton {
                objectName: "brushStudioTipSave"
                small: true
                variant: "primary"
                label: qsTr("Save painted tip")
                tip: qsTr("Store the painted pixels on this component (undoable)")
                enabled: editor.modified
                onClicked: editor.savePixels()
            }
        }
    }

    Component.onCompleted: if (canvas.available) loadPixels()
}
