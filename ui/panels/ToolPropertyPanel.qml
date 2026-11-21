import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// The properties of the active tool.
//
// For painting tools these are the fields of Workspace.brush, which the canvas
// hands to the brush engine at the start of every stroke — nothing here is
// decorative. The panel is organised by what the value does to the stroke
// (Stroke / Tip / Correction) rather than by widget type, and the stroke preview
// at the top repaints as any value changes, so the effect of a slider is visible
// without touching the artwork.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var brush: Workspace.brush
    readonly property var ts: Workspace.toolSettings
    readonly property var library: Workspace.subTools
    readonly property var doc: Workspace.document
    readonly property bool painting: library.paintTool
    readonly property bool hasSubToolProps: library.hasCollections && !painting
    readonly property bool realisticEngine: Workspace.realisticEngine
    function hint(tool) {
        if (tool === "zoom") return qsTr("Click to zoom in. Alt-click or right-click to zoom out.")
        if (tool === "hand") return qsTr("Drag to move the view.")
        if (tool === "eyedropper") return qsTr("Click or drag on the artwork to pick the drawing colour.")
        return qsTr("Select a tool to adjust its properties.")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- The stroke this tool makes
        Item {
            Layout.fillWidth: true
            implicitHeight: 52
            visible: panel.painting && !panel.brush.nativeBrush

            Rectangle {
                id: previewWell
                anchors.fill: parent
                anchors.leftMargin: KP.panelInset
                anchors.rightMargin: KP.panelInset
                anchors.topMargin: KP.gapS
                anchors.bottomMargin: KP.gapS
                radius: KP.radiusS
                color: KP.panelInsetBg
                border.width: 1
                border.color: KP.hairline

                BrushStrokePreview {
                    anchors.fill: parent
                    anchors.margins: 2
                    brushSize: panel.brush.size
                    hardness: panel.brush.hardness
                    brushOpacity: panel.brush.opacity
                    flow: panel.brush.flow
                    spacing: panel.brush.spacing
                    textured: panel.brush.textured
                    tipStyle: panel.brush.tipStyle
                    aspectRatio: panel.brush.aspectRatio
                    tipAngle: panel.brush.tipAngle
                    pressureSize: panel.brush.pressureSize
                    blendStrength: panel.brush.blendStrength
                    scaleHint: 0.5
                    ink: KP.text
                }

                RowLayout {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: KP.gapS
                    spacing: KP.gapS
                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: panel.library.currentName
                        color: KP.textMuted
                        font.pixelSize: KP.fontMicro
                        font.weight: Font.Medium
                    }
                    IconButton {
                        objectName: "resetSubToolButton"
                        glyph: "reset"
                        iconSize: 12
                        cell: 18
                        tooltip: qsTr("Return this preset to its default settings")
                        enabled: panel.library.modified && (!panel.library.brushTool || panel.library.currentBrushBuiltIn)
                        onActivated: panel.library.resetCurrent()
                    }
                }
            }
        }

        Text {
            visible: panel.painting && panel.brush.nativeBrush
            Layout.fillWidth: true
            Layout.margins: KP.panelInset
            text: panel.brush.nativeBrushIssue.length > 0 ? panel.brush.nativeBrushIssue
                : qsTr("Native .kb brush · Edit its settings in Brush Studio.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: panel.brush.nativeBrushIssue.length > 0 ? KP.error : KP.textMuted
            font.pixelSize: KP.fontS
        }
        Item { Layout.fillHeight: true; visible: panel.painting && panel.brush.nativeBrush }

        Flickable {
            id: scroll
            objectName: "toolPropertyScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.painting && !panel.brush.nativeBrush
            clip: true
            contentWidth: width
            contentHeight: properties.height + 2 * KP.gapM
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar {
                policy: scroll.contentHeight > scroll.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Column {
                id: properties
                x: KP.panelInset
                y: KP.gapS
                width: scroll.width - 2 * KP.panelInset
                spacing: 0

                Rectangle {
                    objectName: "realisticPresetNotice"
                    width: parent.width
                    visible: !panel.realisticEngine && panel.brush.blockedByDigitalEngine
                    height: visible ? realisticNoticeCol.implicitHeight + 2 * KP.gapS : 0
                    radius: KP.radiusS
                    color: KP.rowSelected
                    border.width: 1
                    border.color: KP.ember

                    ColumnLayout {
                        id: realisticNoticeCol
                        anchors.fill: parent
                        anchors.margins: KP.gapS
                        spacing: KP.gapXS
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("\"%1\" is a Realistic-only brush. Select Realistic in Settings → Brush Engine, or pick a Digital preset.")
                                .arg(panel.library.currentName)
                            color: KP.text
                            font.pixelSize: KP.fontXS
                            wrapMode: Text.WordWrap
                        }
                        KpButton {
                            objectName: "switchToRealisticButton"
                            small: true
                            label: qsTr("Brush Engine Settings…")
                            onClicked: Workspace.openBrushEngineSettings()
                        }
                    }
                }

                RowLayout {
                    objectName: "digitalStoredMixtureNotice"
                    width: parent.width
                    visible: !panel.realisticEngine && panel.brush.hasMaterialMixture
                    spacing: KP.gapXS
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Mixer mixture stored (used in Realistic mode)")
                        color: KP.textMuted
                        font.pixelSize: KP.fontMicro
                        elide: Text.ElideRight
                    }
                    KpButton {
                        objectName: "clearDigitalStoredMixtureButton"
                        small: true
                        label: qsTr("Clear")
                        tip: qsTr("Clear stored Paint Mixer mixture")
                        onClicked: panel.brush.clearMaterialMixture()
                    }
                }

                PanelSection {
                    objectName: "paintingMediumSection"
                    width: parent.width
                    title: qsTr("Painting medium")
                    visible: panel.realisticEngine
                    hint: panel.brush.erase
                        ? qsTr("lift material")
                        : (panel.brush.mediumId === "watercolour"
                            ? qsTr("watercolour · transparent fluid")
                            : qsTr("gouache · opaque matte"))
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Item {
                        width: parent.width
                        height: mediumColumn.height
                        Column {
                            id: mediumColumn
                            width: parent.width
                            spacing: KP.gapXS
                            SegmentedChoice {
                                id: mediumChoice
                                objectName: "paintingMediumChoice"
                                width: parent.width
                                visible: !panel.brush.erase
                                options: [qsTr("Gouache"), qsTr("Watercolour")]
                                currentIndex: panel.brush.mediumId === "watercolour" ? 1 : 0
                                onChosen: function(index) {
                                    panel.brush.mediumId = index === 1 ? "watercolour" : "gouache"
                                }
                            }
                            RowLayout {
                                width: parent.width
                                visible: panel.realisticEngine
                                         && panel.brush.hasMaterialMixture
                                spacing: KP.gapXS
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Mixer mixture loaded (auto-reload %1)")
                                        .arg(panel.brush.persistentReservoir ? qsTr("OFF") : qsTr("ON"))
                                    color: KP.textMuted
                                    font.family: KP.fontUi
                                    font.pixelSize: KP.fontMicro
                                    elide: Text.ElideRight
                                }
                                KpButton {
                                    objectName: "clearBrushMixtureButton"
                                    small: true
                                    label: qsTr("Clear")
                                    tip: qsTr("Return brush to active Color Wheel colour")
                                    onClicked: panel.brush.clearMaterialMixture()
                                }
                            }
                        }
                    }
                }

                PanelSection {
                    objectName: "paperSurfaceSection"
                    width: parent.width
                    title: qsTr("Paper & surface")
                    hint: panel.doc ? panel.doc.surfaceName : qsTr("Cold Press")
                    visible: panel.realisticEngine && panel.doc !== null
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: surfaceColumn.height
                        Column {
                            id: surfaceColumn
                            width: parent.width
                            spacing: KP.gapXS

                            SegmentedChoice {
                                id: watercolourPaperChoice
                                objectName: "watercolourPaperChoice"
                                width: parent.width
                                visible: panel.brush.mediumId === "watercolour"
                                options: [qsTr("Hot Press"), qsTr("Cold Press"), qsTr("Rough")]
                                currentIndex: !panel.doc ? 1
                                    : (panel.doc.surfaceId === "hot-press" ? 0
                                        : (panel.doc.surfaceId === "rough-paper" ? 2 : 1))
                                onChosen: function(index) {
                                    if (!panel.doc) return
                                    var ids = ["hot-press", "cold-press", "rough-paper"]
                                    panel.doc.surfaceId = ids[index]
                                }
                            }

                            SegmentedChoice {
                                id: gouachePaperChoice
                                objectName: "gouachePaperChoice"
                                width: parent.width
                                visible: panel.brush.mediumId !== "watercolour"
                                options: [qsTr("Smooth"), qsTr("Textured")]
                                currentIndex: !panel.doc ? 0
                                    : (panel.doc.surfaceId === "textured-illustration" ? 1 : 0)
                                onChosen: function(index) {
                                    if (!panel.doc) return
                                    panel.doc.surfaceId = index === 1 ? "textured-illustration" : "smooth-illustration"
                                }
                            }

                            PropertySlider {
                                objectName: "paperWetnessSlider"
                                width: parent.width
                                label: qsTr("Surface wetness")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceWetness : 0
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceWetness = v }
                            }
                        }
                    }
                }

                PanelSection {
                    width: parent.width
                    title: qsTr("Stroke")
                    hint: qsTr("size · opacity · density")
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: strokeColumn.height
                        Column {
                            id: strokeColumn
                            width: parent.width
                            spacing: KP.gapXS
                            PropertySlider {
                                objectName: "brushSizeSlider"
                                width: parent.width
                                label: qsTr("Size")
                                unit: "px"
                                from: panel.brush.minimumSize
                                to: panel.brush.maximumSize
                                // Keep the small-diameter range useful even though
                                // the right end reaches the numeric boundary.
                                logarithmic: true
                                curve: 2.25
                                fieldWidth: 104
                                value: panel.brush.size
                                onEdited: function(v) { panel.brush.size = v }
                            }
                            PropertySlider {
                                objectName: "brushOpacitySlider"
                                width: parent.width
                                label: qsTr("Opacity")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.opacity
                                onEdited: function(v) { panel.brush.opacity = v }
                            }
                            PropertySlider {
                                objectName: "brushDensitySlider"
                                width: parent.width
                                label: qsTr("Density")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.flow
                                onEdited: function(v) { panel.brush.flow = v }
                            }
                        }
                    }
                }

                PanelSection {
                    width: parent.width
                    title: qsTr("Tip")
                    hint: panel.brush.textured ? qsTr("textured") : qsTr("round")
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: tipColumn.height
                        Column {
                            id: tipColumn
                            width: parent.width
                            spacing: KP.gapXS

                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapM
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: qsTr("Shape")
                                    color: KP.text
                                    font.pixelSize: KP.fontS + 1
                                    elide: Text.ElideRight
                                }
                                SegmentedChoice {
                                    id: shapeChoice
                                    objectName: "brushTipChoice"
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 128
                                    width: 128
                                    options: [qsTr("Round"), qsTr("Textured")]
                                    currentIndex: panel.brush.textured ? 1 : 0
                                    onChosen: function(index) { panel.brush.textured = index === 1 }
                                }
                            }
                            Text { text: qsTr("Hardness"); color: KP.text; font.pixelSize: KP.fontM }
                            SegmentedChoice {
                                objectName: "brushHardnessChoice"
                                width: parent.width
                                options: ["0", "25", "50", "75", "100"]
                                currentIndex: [0, 25, 50, 75, 100].indexOf(panel.brush.hardness)
                                onChosen: function(index) { panel.brush.hardness = [0, 25, 50, 75, 100][index] }
                            }

                        }
                    }
                }

                PanelSection {
                    objectName: "digitalDynamicsSection"
                    width: parent.width
                    title: qsTr("Digital dynamics")
                    hint: panel.brush.blendStrength > 0 ? qsTr("blend · pressure") : panel.brush.tipStyle
                    visible: !panel.realisticEngine && !panel.brush.erase
                    collapsible: true
                    expanded: false
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: digitalDynamicsColumn.height
                        Column {
                            id: digitalDynamicsColumn
                            width: parent.width
                            spacing: KP.gapXS

                            Text { text: qsTr("Tip profile"); color: KP.text; font.pixelSize: KP.fontS }
                            SegmentedChoice {
                                objectName: "digitalTipStyleChoice"
                                width: parent.width
                                options: [qsTr("Round"), qsTr("Textured"), qsTr("Bristle"), qsTr("Graphite"), qsTr("Stipple")]
                                currentIndex: Math.max(0, ["round", "textured", "bristle", "graphite", "stipple"].indexOf(panel.brush.tipStyle))
                                onChosen: function(index) {
                                    const styles = ["round", "textured", "bristle", "graphite", "stipple"]
                                    panel.brush.tipStyle = styles[index]
                                }
                            }
                            PropertySlider {
                                objectName: "digitalAspectRatioSlider"
                                width: parent.width
                                label: qsTr("Tip flatness")
                                unit: "%"
                                from: 100
                                to: 400
                                value: panel.brush.aspectRatio
                                onEdited: function(v) { panel.brush.aspectRatio = v }
                            }
                            PropertySlider {
                                objectName: "digitalTipAngleSlider"
                                width: parent.width
                                label: qsTr("Tip angle")
                                unit: "°"
                                from: -180
                                to: 180
                                value: panel.brush.tipAngle
                                onEdited: function(v) { panel.brush.tipAngle = v }
                            }
                            CheckBox {
                                objectName: "digitalFollowDirectionCheck"
                                text: qsTr("Follow stroke direction")
                                checked: panel.brush.followDirection
                                onToggled: panel.brush.followDirection = checked
                            }
                            PropertySlider {
                                objectName: "digitalPressureSizeSlider"
                                width: parent.width
                                label: qsTr("Pressure → Size")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pressureSize
                                onEdited: function(v) { panel.brush.pressureSize = v }
                            }
                            PropertySlider {
                                objectName: "digitalPressureOpacitySlider"
                                width: parent.width
                                label: qsTr("Pressure → Opacity")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pressureOpacity
                                onEdited: function(v) { panel.brush.pressureOpacity = v }
                            }
                            PropertySlider {
                                objectName: "digitalPressureFlowSlider"
                                width: parent.width
                                label: qsTr("Pressure → Density")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pressureFlow
                                onEdited: function(v) { panel.brush.pressureFlow = v }
                            }
                            PropertySlider {
                                objectName: "digitalTiltSizeSlider"
                                width: parent.width
                                label: qsTr("Tilt → Size")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.tiltSize
                                onEdited: function(v) { panel.brush.tiltSize = v }
                            }
                            CheckBox {
                                objectName: "digitalTiltAngleCheck"
                                text: qsTr("Tilt affects tip angle & flatness")
                                checked: panel.brush.tiltAngle
                                onToggled: panel.brush.tiltAngle = checked
                            }
                            CheckBox {
                                objectName: "digitalStylusRotationCheck"
                                text: qsTr("Follow stylus barrel rotation")
                                checked: panel.brush.stylusRotation
                                onToggled: panel.brush.stylusRotation = checked
                            }
                            PropertySlider {
                                objectName: "digitalPressureGammaSlider"
                                width: parent.width
                                label: qsTr("Pressure curve")
                                unit: "%"
                                from: 40
                                to: 200
                                value: panel.brush.pressureGamma
                                onEdited: function(v) { panel.brush.pressureGamma = v }
                            }
                            PropertySlider {
                                objectName: "digitalBlendStrengthSlider"
                                width: parent.width
                                label: qsTr("Blend / Smudge")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.blendStrength
                                onEdited: function(v) { panel.brush.blendStrength = v }
                            }
                            PropertySlider {
                                objectName: "digitalColorMixSlider"
                                width: parent.width
                                visible: panel.brush.blendStrength > 0
                                label: qsTr("Colour mix")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.colorMix
                                onEdited: function(v) { panel.brush.colorMix = v }
                            }
                        }
                    }
                }

                PanelSection {
                    objectName: "materialPaintSection"
                    width: parent.width
                    title: qsTr("Material paint")
                    hint: qsTr("load · pigment · water · body")
                    visible: panel.realisticEngine && !panel.brush.erase
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: materialColumn.height
                        Column {
                            id: materialColumn
                            width: parent.width
                            spacing: KP.gapXS
                            PropertySlider {
                                objectName: "brushPaintLoadSlider"
                                width: parent.width
                                label: qsTr("Paint load")
                                unit: "%"
                                from: 10
                                to: 100
                                value: panel.brush.paintLoad
                                onEdited: function(v) { panel.brush.paintLoad = v }
                            }
                            PropertySlider {
                                objectName: "brushPigmentSlider"
                                width: parent.width
                                label: qsTr("Concentration")
                                unit: "%"
                                from: 10
                                to: 100
                                value: panel.brush.pigmentConcentration
                                onEdited: function(v) { panel.brush.pigmentConcentration = v }
                            }
                            PropertySlider {
                                objectName: "brushWaterSlider"
                                width: parent.width
                                label: qsTr("Water")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.waterAmount
                                onEdited: function(v) { panel.brush.waterAmount = v }
                            }
                            PropertySlider {
                                objectName: "brushBodySlider"
                                width: parent.width
                                label: qsTr("Paint body")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.paintBody
                                onEdited: function(v) { panel.brush.paintBody = v }
                            }
                            PropertySlider {
                                objectName: "brushPickupSlider"
                                width: parent.width
                                label: qsTr("Pickup")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pickup
                                onEdited: function(v) { panel.brush.pickup = v }
                            }
                        }
                    }
                }

                PanelSection {
                    objectName: "advancedMaterialSection"
                    width: parent.width
                    title: qsTr("Advanced material")
                    hint: panel.brush.persistentReservoir ? qsTr("persistent") : qsTr("auto-reload")
                    visible: panel.realisticEngine
                    collapsible: true
                    expanded: false
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: advancedColumn.height
                        Column {
                            id: advancedColumn
                            width: parent.width
                            spacing: KP.gapXS
                            PropertySlider {
                                objectName: "brushPressureAmountSlider"
                                width: parent.width
                                label: qsTr("Pressure → Amount")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pressureToAmount
                                onEdited: function(v) { panel.brush.pressureToAmount = v }
                            }
                            PropertySlider {
                                objectName: "brushPressureContactSlider"
                                width: parent.width
                                label: qsTr("Pressure → Contact")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.pressureToContact
                                onEdited: function(v) { panel.brush.pressureToContact = v }
                            }
                            PropertySlider {
                                objectName: "brushAspectSlider"
                                width: parent.width
                                label: qsTr("Aspect")
                                unit: "%"
                                from: 100
                                to: 400
                                value: panel.brush.aspectRatio
                                onEdited: function(v) { panel.brush.aspectRatio = v }
                            }
                            PropertySlider {
                                objectName: "brushAngleSlider"
                                width: parent.width
                                label: qsTr("Angle")
                                unit: "°"
                                from: -180
                                to: 180
                                value: panel.brush.tipAngle
                                onEdited: function(v) { panel.brush.tipAngle = v }
                            }
                            PropertySlider {
                                objectName: "brushDepletionSlider"
                                width: parent.width
                                label: qsTr("Depletion")
                                unit: "%"
                                from: 0
                                to: 200
                                value: panel.brush.depletion
                                onEdited: function(v) { panel.brush.depletion = v }
                            }
                            PropertySlider {
                                objectName: "brushToothSlider"
                                width: parent.width
                                label: qsTr("Paper tooth")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.toothResponse
                                onEdited: function(v) { panel.brush.toothResponse = v }
                            }
                            PropertySlider {
                                objectName: "brushEdgeSlider"
                                width: parent.width
                                label: qsTr("Edge")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.brush.edgeAccumulation
                                onEdited: function(v) { panel.brush.edgeAccumulation = v }
                            }
                            PropertySlider {
                                objectName: "brushDryingSpeedSlider"
                                width: parent.width
                                label: qsTr("Drying speed")
                                unit: "%"
                                from: 25
                                to: 200
                                value: panel.brush.dryingSpeed
                                onEdited: function(v) { panel.brush.dryingSpeed = v }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapM
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: qsTr("Follow direction")
                                    color: KP.text
                                    font.pixelSize: KP.fontS + 1
                                    elide: Text.ElideRight
                                }
                                SegmentedChoice {
                                    objectName: "brushFollowDirectionChoice"
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 112
                                    width: 112
                                    options: [qsTr("Off"), qsTr("On")]
                                    currentIndex: panel.brush.followDirection ? 1 : 0
                                    onChosen: function(index) { panel.brush.followDirection = index === 1 }
                                }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapM
                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: qsTr("Reservoir")
                                    color: KP.text
                                    font.pixelSize: KP.fontS + 1
                                    elide: Text.ElideRight
                                }
                                SegmentedChoice {
                                    objectName: "brushReservoirModeChoice"
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 148
                                    width: 148
                                    options: [qsTr("Auto"), qsTr("Persistent")]
                                    currentIndex: panel.brush.persistentReservoir ? 1 : 0
                                    onChosen: function(index) { panel.brush.persistentReservoir = index === 1 }
                                }
                            }
                        }
                    }
                }

                PanelSection {
                    objectName: "advancedPaperSection"
                    width: parent.width
                    title: qsTr("Advanced paper physics")
                    hint: panel.doc ? panel.doc.surfaceId : ""
                    visible: panel.realisticEngine && panel.doc !== null
                    collapsible: true
                    expanded: false
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: paperPhysicsColumn.height
                        Column {
                            id: paperPhysicsColumn
                            width: parent.width
                            spacing: KP.gapXS
                            PropertySlider {
                                objectName: "paperAbsorbencySlider"
                                width: parent.width
                                label: qsTr("Absorbency")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceAbsorbency : 60
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceAbsorbency = v }
                            }
                            PropertySlider {
                                objectName: "paperAbsorptionRateSlider"
                                width: parent.width
                                label: qsTr("Absorption rate")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceAbsorptionRate : 55
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceAbsorptionRate = v }
                            }
                            PropertySlider {
                                objectName: "paperPorositySlider"
                                width: parent.width
                                label: qsTr("Porosity")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfacePorosity : 58
                                onEdited: function(v) { if (panel.doc) panel.doc.surfacePorosity = v }
                            }
                            PropertySlider {
                                objectName: "paperWaterRetentionSlider"
                                width: parent.width
                                label: qsTr("Water retention")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceWaterRetention : 62
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceWaterRetention = v }
                            }
                            PropertySlider {
                                objectName: "paperToothDepthSlider"
                                width: parent.width
                                label: qsTr("Tooth / texture")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceTooth : 52
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceTooth = v }
                            }
                            PropertySlider {
                                objectName: "paperRoughnessSlider"
                                width: parent.width
                                label: qsTr("Roughness")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceRoughness : 48
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceRoughness = v }
                            }
                            PropertySlider {
                                objectName: "paperPigmentRetentionSlider"
                                width: parent.width
                                label: qsTr("Pigment retention")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfacePigmentRetention : 60
                                onEdited: function(v) { if (panel.doc) panel.doc.surfacePigmentRetention = v }
                            }
                            PropertySlider {
                                objectName: "paperGranulationSlider"
                                width: parent.width
                                label: qsTr("Granulation interaction")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceGranulation : 65
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceGranulation = v }
                            }
                            PropertySlider {
                                objectName: "paperRewettingSlider"
                                width: parent.width
                                label: qsTr("Rewetting response")
                                unit: "%"
                                from: 0
                                to: 100
                                value: panel.doc ? panel.doc.surfaceRewetting : 58
                                onEdited: function(v) { if (panel.doc) panel.doc.surfaceRewetting = v }
                            }
                            KpButton {
                                objectName: "resetPaperButton"
                                width: parent.width
                                small: true
                                label: qsTr("Reset paper defaults")
                                onClicked: { if (panel.doc) panel.doc.resetSurfaceSettings() }
                            }
                        }
                    }
                }

                PanelSection {
                    objectName: "boundaryEraserSection"
                    width: parent.width
                    title: qsTr("Boundary-Aware Cleanup")
                    hint: qsTr("protect interior")
                    visible: Workspace.activeTool === "eraser"
                             && panel.ts !== null
                             && panel.ts.executableTool === "boundary_eraser"
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: boundaryCol.height
                        Column {
                            id: boundaryCol
                            width: parent.width
                            spacing: KP.gapXS
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapS
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Reference")
                                    color: KP.text
                                    font.pixelSize: KP.fontS + 1
                                }
                                SegmentedChoice {
                                    Layout.preferredWidth: 150
                                    options: [qsTr("Layer"), qsTr("All"), qsTr("Sel")]
                                    currentIndex: {
                                        if (!panel.ts) return 1
                                        if (panel.ts.referenceMode === "current") return 0
                                        if (panel.ts.referenceMode === "selected_and_below") return 2
                                        return 1
                                    }
                                    onChosen: function(idx) {
                                        if (panel.ts) panel.ts.referenceMode = ["current", "visible", "selected_and_below"][idx]
                                    }
                                }
                            }
                            PropertySlider {
                                width: parent.width
                                label: qsTr("Boundary sensitivity")
                                unit: "%"
                                from: 0; to: 100
                                value: panel.ts ? panel.ts.boundarySensitivity : 65
                                onEdited: function(v) { if (panel.ts) panel.ts.boundarySensitivity = v }
                            }
                            PropertySlider {
                                width: parent.width
                                label: qsTr("Gap tolerance")
                                unit: "px"
                                from: 0; to: 32
                                value: panel.ts ? panel.ts.gapClosing : 8
                                onEdited: function(v) { if (panel.ts) panel.ts.gapClosing = v }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapM
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Conservative rim")
                                    color: KP.text
                                    font.pixelSize: KP.fontS + 1
                                }
                                SegmentedChoice {
                                    Layout.preferredWidth: 112
                                    options: [qsTr("Off"), qsTr("On")]
                                    currentIndex: (!panel.ts || panel.ts.conservativeBoundary) ? 1 : 0
                                    onChosen: function(idx) { if (panel.ts) panel.ts.conservativeBoundary = (idx === 1) }
                                }
                            }
                        }
                    }
                }

                PanelSection {
                    width: parent.width
                    title: qsTr("Correction")
                    hint: qsTr("stabilisation")
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: correctionColumn.height
                        Column {
                            id: correctionColumn
                            width: parent.width
                            spacing: KP.gapXS
                            PropertySlider {
                                objectName: "brushStabilizationSlider"
                                width: parent.width
                                label: qsTr("Stabilisation")
                                from: 0
                                to: 100
                                value: panel.brush.stabilization
                                onEdited: function(v) { panel.brush.stabilization = v }
                            }
                        }
                    }
                }
            }
        }

        // ---- Universal Intelligent / Selection / Shape / Gradient / Text tool properties
        Flickable {
            id: toolPropScroll
            objectName: "nonStrokePropertyScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.hasSubToolProps
            clip: true
            contentWidth: width
            contentHeight: toolPropsCol.height + 2 * KP.gapM
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar {
                policy: toolPropScroll.contentHeight > toolPropScroll.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Column {
                id: toolPropsCol
                x: KP.panelInset
                y: KP.gapS
                width: toolPropScroll.width - 2 * KP.panelInset
                spacing: KP.gapS

                readonly property string t: Workspace.activeTool
                readonly property string exec: panel.ts ? panel.ts.executableTool : t
                readonly property bool isRegionTool: t === "fill" || t === "wand"
                readonly property bool isSmartFill: exec === "smart_fill"
                readonly property bool isSelectionTool: t === "selection" || t === "lasso" || t === "wand"
                readonly property bool isShapeTool: t === "line" || t === "curveline" || t === "rectangle" || t === "circle"

                Rectangle {
                    width: parent.width
                    height: 28
                    radius: KP.radiusS
                    color: KP.paneBgAlt
                    border.width: 1
                    border.color: KP.hairline
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: KP.gapS
                        anchors.rightMargin: KP.gapS
                        Text {
                            Layout.fillWidth: true
                            text: panel.library.currentName || toolPropsCol.t
                            color: KP.text
                            font.pixelSize: KP.fontXS
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        IconButton {
                            glyph: "reset"
                            iconSize: 12
                            cell: 20
                            tooltip: qsTr("Return this preset to its default settings")
                            enabled: panel.library.modified && panel.library.currentBrushBuiltIn
                            onActivated: panel.library.resetCurrent()
                        }
                    }
                }

                // Region Reference & Detection (Fill, Smart Fill, Wand, Magnetic Lasso)
                PanelSection {
                    width: parent.width
                    title: toolPropsCol.isSmartFill ? qsTr("Smart Region Analysis") : qsTr("Detection & Reference")
                    visible: toolPropsCol.isRegionTool || (toolPropsCol.t === "lasso" && panel.ts && panel.ts.lassoMode === "magnetic")
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Item {
                        width: parent.width
                        height: regionCol.height
                        Column {
                            id: regionCol
                            width: parent.width
                            spacing: KP.gapXS
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                spacing: KP.gapS
                                Text { Layout.fillWidth: true; text: qsTr("Reference"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 150
                                    options: [qsTr("Layer"), qsTr("All"), qsTr("Sel")]
                                    currentIndex: {
                                        if (!panel.ts) return 1
                                        if (panel.ts.referenceMode === "current") return 0
                                        if (panel.ts.referenceMode === "selected_and_below") return 2
                                        return 1
                                    }
                                    onChosen: function(idx) {
                                        if (panel.ts) panel.ts.referenceMode = ["current", "visible", "selected_and_below"][idx]
                                    }
                                }
                            }
                            PropertySlider {
                                objectName: "toolToleranceSlider"
                                width: parent.width
                                visible: toolPropsCol.isRegionTool
                                label: qsTr("Color tolerance")
                                from: 0; to: 255
                                value: panel.ts ? panel.ts.tolerance : 28
                                onEdited: function(v) { if (panel.ts) panel.ts.tolerance = v }
                            }
                            PropertySlider {
                                objectName: "toolBoundarySensitivitySlider"
                                width: parent.width
                                visible: toolPropsCol.isSmartFill || toolPropsCol.t === "wand"
                                label: qsTr("Boundary sensitivity")
                                unit: "%"
                                from: 0; to: 100
                                value: panel.ts ? panel.ts.boundarySensitivity : 65
                                onEdited: function(v) { if (panel.ts) panel.ts.boundarySensitivity = v }
                            }
                            PropertySlider {
                                objectName: "toolGapClosingSlider"
                                width: parent.width
                                visible: toolPropsCol.isRegionTool
                                label: toolPropsCol.isSmartFill ? qsTr("Gap tolerance") : qsTr("Close gap")
                                unit: "px"
                                from: 0; to: 32
                                value: panel.ts ? panel.ts.gapClosing : 0
                                onEdited: function(v) { if (panel.ts) panel.ts.gapClosing = v }
                            }
                            PropertySlider {
                                objectName: "toolAreaScalingSlider"
                                width: parent.width
                                visible: toolPropsCol.isRegionTool
                                label: qsTr("Area scaling")
                                unit: "px"
                                from: -16; to: 16
                                value: panel.ts ? panel.ts.areaScaling : 1
                                onEdited: function(v) { if (panel.ts) panel.ts.areaScaling = v }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                visible: toolPropsCol.isRegionTool
                                Text { Layout.fillWidth: true; text: qsTr("Contiguous"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 112
                                    options: [qsTr("All"), qsTr("Region")]
                                    currentIndex: (!panel.ts || panel.ts.contiguous) ? 1 : 0
                                    onChosen: function(idx) { if (panel.ts) panel.ts.contiguous = (idx === 1) }
                                }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                visible: toolPropsCol.isSmartFill
                                Text { Layout.fillWidth: true; text: qsTr("Preview before commit"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 112
                                    options: [qsTr("Instant"), qsTr("Preview")]
                                    currentIndex: (panel.ts && panel.ts.previewBeforeCommit) ? 1 : 0
                                    onChosen: function(idx) { if (panel.ts) panel.ts.previewBeforeCommit = (idx === 1) }
                                }
                            }
                        }
                    }
                }

                // Selection Options (Selection / Lasso / Wand)
                PanelSection {
                    width: parent.width
                    title: qsTr("Selection")
                    visible: toolPropsCol.isSelectionTool
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Item {
                        width: parent.width
                        height: selCol.height
                        Column {
                            id: selCol
                            width: parent.width
                            spacing: KP.gapXS
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                Text { Layout.fillWidth: true; text: qsTr("Mode"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 170
                                    options: [qsTr("New"), qsTr("Add"), qsTr("Sub"), qsTr("Int")]
                                    currentIndex: {
                                        if (!panel.ts) return 0
                                        if (panel.ts.selectionOp === "add") return 1
                                        if (panel.ts.selectionOp === "subtract") return 2
                                        if (panel.ts.selectionOp === "intersect") return 3
                                        return 0
                                    }
                                    onChosen: function(idx) {
                                        if (!panel.ts) return
                                        panel.ts.selectionOp = ["replace", "add", "subtract", "intersect"][idx]
                                    }
                                }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                visible: toolPropsCol.t === "selection"
                                Text { Layout.fillWidth: true; text: qsTr("Shape"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 140
                                    options: [qsTr("Rect"), qsTr("Ellipse")]
                                    currentIndex: (panel.ts && panel.ts.selectionShape === "ellipse") ? 1 : 0
                                    onChosen: function(idx) { if (panel.ts) panel.ts.selectionShape = idx === 1 ? "ellipse" : "rectangle" }
                                }
                            }
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                visible: toolPropsCol.t === "lasso"
                                Text { Layout.fillWidth: true; text: qsTr("Lasso"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 168
                                    options: [qsTr("Free"), qsTr("Poly"), qsTr("Mag")]
                                    currentIndex: {
                                        if (!panel.ts) return 0
                                        if (panel.ts.lassoMode === "polygonal") return 1
                                        if (panel.ts.lassoMode === "magnetic") return 2
                                        return 0
                                    }
                                    onChosen: function(idx) {
                                        if (panel.ts) panel.ts.lassoMode = ["freehand", "polygonal", "magnetic"][idx]
                                    }
                                }
                            }
                            PropertySlider {
                                width: parent.width
                                visible: toolPropsCol.t === "lasso" && panel.ts && panel.ts.lassoMode === "magnetic"
                                label: qsTr("Edge snap strength")
                                unit: "%"
                                from: 0; to: 100
                                value: panel.ts ? panel.ts.magneticStrength : 65
                                onEdited: function(v) { if (panel.ts) panel.ts.magneticStrength = v }
                            }
                            PropertySlider {
                                width: parent.width
                                label: qsTr("Feather")
                                unit: "px"
                                from: 0; to: 64
                                value: panel.ts ? panel.ts.feather : 0
                                onEdited: function(v) { if (panel.ts) panel.ts.feather = v }
                            }
                            RowLayout {
                                width: parent.width
                                spacing: KP.gapXS
                                KpButton { Layout.fillWidth: true; small: true; label: qsTr("All"); onClicked: Workspace.selectAll() }
                                KpButton { Layout.fillWidth: true; small: true; label: qsTr("Invert"); enabled: Workspace.hasSelection; onClicked: Workspace.invertSelection() }
                                KpButton { Layout.fillWidth: true; small: true; label: qsTr("Deselect"); enabled: Workspace.hasSelection; onClicked: Workspace.deselect() }
                            }
                        }
                    }
                }

                // Gradient Options
                PanelSection {
                    width: parent.width
                    title: qsTr("Gradient")
                    visible: toolPropsCol.t === "gradient"
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    RowLayout {
                        width: parent.width
                        height: KP.controlHeight
                        Text { Layout.fillWidth: true; text: qsTr("Type"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                        SegmentedChoice {
                            Layout.preferredWidth: 140
                            options: [qsTr("Linear"), qsTr("Radial")]
                            currentIndex: (panel.ts && panel.ts.gradientType === "radial") ? 1 : 0
                            onChosen: function(idx) { if (panel.ts) panel.ts.gradientType = idx === 1 ? "radial" : "linear" }
                        }
                    }
                }

                // Shape Options
                PanelSection {
                    width: parent.width
                    title: qsTr("Shape")
                    visible: toolPropsCol.isShapeTool
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Item {
                        width: parent.width
                        height: shapeCol.height
                        Column {
                            id: shapeCol
                            width: parent.width
                            spacing: KP.gapXS
                            RowLayout {
                                width: parent.width
                                height: KP.controlHeight
                                visible: toolPropsCol.t === "rectangle" || toolPropsCol.t === "circle"
                                Text { Layout.fillWidth: true; text: qsTr("Style"); color: KP.text; font.pixelSize: KP.fontS + 1 }
                                SegmentedChoice {
                                    Layout.preferredWidth: 165
                                    options: [qsTr("Line"), qsTr("Fill"), qsTr("Both")]
                                    currentIndex: {
                                        if (!panel.ts) return 0
                                        if (panel.ts.shapeMode === "fill") return 1
                                        if (panel.ts.shapeMode === "both") return 2
                                        return 0
                                    }
                                    onChosen: function(idx) {
                                        if (panel.ts) panel.ts.shapeMode = ["stroke", "fill", "both"][idx]
                                    }
                                }
                            }
                            PropertySlider {
                                width: parent.width
                                label: qsTr("Stroke width")
                                unit: "px"
                                from: 1; to: 128
                                value: panel.ts ? panel.ts.shapeStrokeWidth : 4
                                onEdited: function(v) { if (panel.ts) panel.ts.shapeStrokeWidth = v }
                            }
                        }
                    }
                }

                // Text Options
                PanelSection {
                    width: parent.width
                    title: qsTr("Text")
                    visible: toolPropsCol.t === "text"
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    PropertySlider {
                        width: parent.width
                        label: qsTr("Font size")
                        unit: "px"
                        from: 8; to: 200
                        value: panel.ts ? panel.ts.fontSize : 28
                        onEdited: function(v) { if (panel.ts) panel.ts.fontSize = v }
                    }
                }

                // Move / Transform Options
                PanelSection {
                    width: parent.width
                    title: toolPropsCol.t === "transform" ? qsTr("Transform") : qsTr("Move")
                    visible: toolPropsCol.t === "move" || toolPropsCol.t === "transform"
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Text {
                        width: parent.width
                        text: Workspace.hasSelection
                              ? qsTr("Drag on the canvas to move the active selection and its pixels.")
                              : qsTr("Drag on the canvas to reposition the active layer artwork.")
                        color: KP.textMuted
                        font.pixelSize: KP.fontXS
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ---- A view/eyedropper tool without sub-tool properties
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.painting && !panel.hasSubToolProps
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * KP.panelInset, 260)
                spacing: KP.gapM
                KpIcon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "tools/" + Workspace.activeTool
                    size: 26
                    color: KP.iconOff
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Workspace.activeTool.replace(/^./, function(c) { return c.toUpperCase() })
                    color: KP.text
                    font.pixelSize: KP.fontS + 1
                    font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    text: panel.hint(Workspace.activeTool)
                    color: KP.textMuted
                    font.pixelSize: KP.fontS
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
