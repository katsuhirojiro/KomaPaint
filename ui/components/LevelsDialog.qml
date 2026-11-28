import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// Levels: the first colour adjustment and the reusable frame for later ones.
//
// The controller owns the pixels and history. This dialog only edits values,
// explains them, draws the source histogram and switches the before/after
// view. It is used from Edit > Adjustments and from Create Brush Tip, so both
// paths run exactly the same adjustment code.
KpDialog {
    id: dialog
    objectName: "levelsDialog"
    heading: qsTr("Levels")
    bodyWidth: 520

    property var controller: Workspace.levels
    property bool applied: false

    readonly property bool ready: !!controller && controller.active
    readonly property bool imageTarget: ready && controller.targetKind === "image"

    signal appliedToTarget()

    function openForCurrentTarget() {
        applied = false
        open()
    }

    onClosed: {
        // Closing with the window button or Escape is a cancel, never a
        // silent commit of whatever preview happened to be on screen.
        if (!applied && controller && controller.active)
            controller.cancel()
    }

    body: [
        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentColumn.implicitHeight, 540)
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: contentColumn
                width: scroller.availableWidth
                spacing: KP.gapM

                Text {
                    Layout.fillWidth: true
                    text: dialog.ready ? dialog.controller.scopeText
                                       : (dialog.controller && dialog.controller.error.length > 0
                                          ? dialog.controller.error
                                          : qsTr("Open a document with an active raster layer first."))
                    color: dialog.ready ? KP.text : KP.warning
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                    wrapMode: Text.WordWrap
                }

                // ---- Histogram ------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132
                    radius: KP.radiusS
                    color: KP.inputBg
                    border.width: 1
                    border.color: KP.border
                    opacity: dialog.ready ? 1 : 0.5

                    Canvas {
                        id: histogramCanvas
                        objectName: "levelsHistogram"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        height: 92
                        visible: dialog.ready

                        Connections {
                            target: dialog.controller
                            function onHistogramChanged() { histogramCanvas.requestPaint() }
                            function onParametersChanged() { histogramCanvas.requestPaint() }
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        Component.onCompleted: requestPaint()

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const bins = dialog.controller ? dialog.controller.histogram : []
                            if (!bins || bins.length !== 256) return
                            let max = 0
                            for (let i = 0; i < bins.length; ++i)
                                if (bins[i] > max) max = bins[i]
                            if (max <= 0) return
                            const top = 4
                            const bottom = height - 2
                            const usable = Math.max(1, bottom - top)
                            const barWidth = width / 256
                            ctx.fillStyle = String(KP.textMuted)
                            for (let i = 0; i < 256; ++i) {
                                // Square-root scaling keeps the shadow and highlight
                                // tails visible without flattening the whole plot.
                                const h = Math.sqrt(bins[i] / max) * usable
                                if (h <= 0) continue
                                ctx.fillRect(i * barWidth, bottom - h, Math.max(1, barWidth - 0.2), h)
                            }
                            // Input-range markers: the same values as the sliders.
                            const blackX = width * dialog.controller.inputBlack / 255
                            const whiteX = width * dialog.controller.inputWhite / 255
                            ctx.strokeStyle = String(KP.accent)
                            ctx.lineWidth = 1.5
                            ctx.beginPath(); ctx.moveTo(blackX, 0); ctx.lineTo(blackX, height); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(whiteX, 0); ctx.lineTo(whiteX, height); ctx.stroke()
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.bottomMargin: 4
                        text: qsTr("Input range  ·  black %1  ·  white %2  ·  0 – 255")
                              .arg(dialog.ready ? dialog.controller.inputBlack : 0)
                              .arg(dialog.ready ? dialog.controller.inputWhite : 255)
                        color: KP.textSubtle
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontXS
                    }
                }

                // ---- Optional image preview (brush tips) ----------------------
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: dialog.imageTarget ? 176 : 0
                    visible: dialog.imageTarget
                    clip: true

                    // Kept in its own file and loaded only for the brush-tip
                    // target. Document Levels previews on the canvas, so the
                    // ordinary Edit > Adjustments path never needs this type.
                    Loader {
                        id: imagePreviewLoader
                        objectName: "levelsImagePreviewLoader"
                        anchors.fill: parent
                        active: dialog.imageTarget
                        source: active ? "LevelsPreviewPane.qml" : ""
                        onLoaded: {
                            item.controller = dialog.controller
                            item.borderColor = KP.border
                        }
                    }
                }

                // ---- Before / after ------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: KP.gapM
                    visible: dialog.ready

                    SegmentedChoice {
                        id: comparison
                        Layout.preferredWidth: 220
                        options: [qsTr("Before"), qsTr("After")]
                        explanations: [
                            qsTr("Shows the artwork exactly as it was when Levels opened."),
                            qsTr("Shows the result of the current settings.")
                        ]
                        currentIndex: dialog.controller && dialog.controller.showOriginal ? 0 : 1
                        onChosen: function(index) {
                            if (dialog.controller) dialog.controller.showOriginal = index === 0
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: dialog.controller && dialog.controller.showOriginal
                              ? qsTr("Original pixels")
                              : qsTr("Preview of the adjustment")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                        elide: Text.ElideRight
                    }
                }

                // ---- Simple controls -----------------------------------------
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Simple")
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                    font.weight: Font.DemiBold
                }

                PropertySlider {
                    objectName: "levelsBlackPoint"
                    Layout.fillWidth: true
                    label: qsTr("Black point")
                    help: qsTr("Everything at or below this input value becomes black. Raise it to deepen shadows and hide faint paper texture.")
                    from: 0
                    to: 254
                    value: dialog.ready ? dialog.controller.inputBlack : 0
                    enabled: dialog.ready
                    onEdited: function(value) { if (dialog.controller) dialog.controller.inputBlack = value }
                }
                PropertySlider {
                    objectName: "levelsWhitePoint"
                    Layout.fillWidth: true
                    label: qsTr("White point")
                    help: qsTr("Everything at or above this input value becomes white. Lower it to brighten highlights and clear a washed-out scan.")
                    from: 1
                    to: 255
                    value: dialog.ready ? dialog.controller.inputWhite : 255
                    enabled: dialog.ready
                    onEdited: function(value) { if (dialog.controller) dialog.controller.inputWhite = value }
                }
                PropertySlider {
                    objectName: "levelsMidpoint"
                    Layout.fillWidth: true
                    label: qsTr("Midpoint")
                    help: qsTr("Moves the middle tone without moving black or white. Below 50% darkens midtones; above 50% lightens them. It is the same curve as Gamma.")
                    from: 1
                    to: 99
                    unit: "%"
                    value: dialog.ready ? dialog.controller.midpointPercent : 50
                    enabled: dialog.ready
                    onEdited: function(value) { if (dialog.controller) dialog.controller.midpointPercent = value }
                }

                CheckBox {
                    id: advancedToggle
                    objectName: "levelsAdvanced"
                    text: qsTr("Advanced controls")
                    checked: false
                    enabled: dialog.ready
                    Accessible.name: text
                    Accessible.description: qsTr("Shows the exact input and output values and the gamma curve.")
                }

                // ---- Advanced controls ---------------------------------------
                Text {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    text: qsTr("These are the same values as the simple controls, plus the output range. Gamma and Midpoint always agree.")
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }
                PropertySlider {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    label: qsTr("Input black")
                    help: qsTr("Exact input black point, 0–254. Same value as the simple Black point.")
                    from: 0
                    to: 254
                    value: dialog.ready ? dialog.controller.inputBlack : 0
                    enabled: dialog.ready && advancedToggle.checked
                    onEdited: function(value) { if (dialog.controller) dialog.controller.inputBlack = value }
                }
                PropertySlider {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    label: qsTr("Input white")
                    help: qsTr("Exact input white point, 1–255. Same value as the simple White point.")
                    from: 1
                    to: 255
                    value: dialog.ready ? dialog.controller.inputWhite : 255
                    enabled: dialog.ready && advancedToggle.checked
                    onEdited: function(value) { if (dialog.controller) dialog.controller.inputWhite = value }
                }
                PropertySlider {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    label: qsTr("Gamma × 100")
                    help: qsTr("The midtone curve. 100 means 1.00 (neutral); above 100 lightens midtones, below 100 darkens them. The value beside the rail shows the actual gamma.")
                    from: 10
                    to: 1000
                    value: dialog.ready ? dialog.controller.gammaHundredths : 100
                    enabled: dialog.ready && advancedToggle.checked
                    onEdited: function(value) { if (dialog.controller) dialog.controller.gammaHundredths = value }
                }
                Text {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    text: qsTr("Gamma %1  ·  midpoint %2%")
                          .arg(dialog.ready ? dialog.controller.gammaText : "1.00")
                          .arg(dialog.ready ? dialog.controller.midpointPercent : 50)
                    color: KP.textMuted
                    font.family: KP.fontMono
                    font.pixelSize: KP.fontXS
                }
                PropertySlider {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    label: qsTr("Output black")
                    help: qsTr("The darkest value the result may contain. Raise it for a faded, lifted-black look; values above Output white invert the result.")
                    from: 0
                    to: 255
                    value: dialog.ready ? dialog.controller.outputBlack : 0
                    enabled: dialog.ready && advancedToggle.checked
                    onEdited: function(value) { if (dialog.controller) dialog.controller.outputBlack = value }
                }
                PropertySlider {
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    label: qsTr("Output white")
                    help: qsTr("The brightest value the result may contain. Lower it to dull highlights; values below Output black invert the result.")
                    from: 0
                    to: 255
                    value: dialog.ready ? dialog.controller.outputWhite : 255
                    enabled: dialog.ready && advancedToggle.checked
                    onEdited: function(value) { if (dialog.controller) dialog.controller.outputWhite = value }
                }

                Text {
                    Layout.fillWidth: true
                    text: dialog.imageTarget
                          ? qsTr("Apply keeps this adjusted copy for the new brush tip. Cancel discards it. The document and stored tips are never rewritten.")
                          : qsTr("Apply records one undo step. Cancel restores the pixels and the document's saved/unsaved state exactly as they were.")
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: dialog.controller && dialog.controller.error.length > 0
                    text: dialog.controller ? dialog.controller.error : ""
                    color: KP.error
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                    wrapMode: Text.WordWrap
                }
            }
        }
    ]

    actions: [
        KpButton {
            objectName: "levelsCancelButton"
            label: qsTr("Cancel")
            onClicked: dialog.reject()
        },
        KpButton {
            objectName: "levelsResetButton"
            label: qsTr("Reset")
            variant: "ghost"
            enabled: dialog.ready
            onClicked: if (dialog.controller) dialog.controller.reset()
        },
        KpButton {
            objectName: "levelsApplyButton"
            label: qsTr("Apply")
            variant: "primary"
            enabled: dialog.ready
            onClicked: {
                if (!dialog.controller) return
                // Flush a pending debounced preview first, so Apply always uses
                // the values currently shown.
                dialog.controller.previewNow()
                if (dialog.controller.apply()) {
                    dialog.applied = true
                    dialog.appliedToTarget()
                    dialog.accept()
                }
            }
        }
    ]
}
