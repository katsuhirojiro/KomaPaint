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
    readonly property var library: Workspace.subTools
    readonly property var doc: Workspace.document
    readonly property bool painting: library.paintTool
    function hint(tool) {
        if (tool === "zoom") return qsTr("Click to zoom in. Alt-click or right-click to zoom out.")
        if (tool === "hand") return qsTr("Drag to move the view.")
        if (tool === "eyedropper") return qsTr("Click or drag on the artwork to pick the drawing colour.")
        return qsTr("This tool is not available yet.")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- The stroke this tool makes
        Item {
            Layout.fillWidth: true
            implicitHeight: 52
            visible: panel.painting

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
                        enabled: panel.library.modified
                        onActivated: panel.library.resetCurrent()
                    }
                }
            }
        }

        Flickable {
            id: scroll
            objectName: "toolPropertyScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.painting
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

                PanelSection {
                    objectName: "paintingMediumSection"
                    width: parent.width
                    title: qsTr("Painting medium")
                    hint: !panel.brush.traditionalMedia
                        ? qsTr("digital · raster")
                        : (panel.brush.erase
                            ? qsTr("lift material")
                            : (panel.brush.mediumId === "watercolour"
                                ? qsTr("watercolour · transparent fluid")
                                : qsTr("gouache · opaque matte")))
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
                                options: [qsTr("Digital"), qsTr("Gouache"), qsTr("Watercolour")]
                                currentIndex: !panel.brush.traditionalMedia ? 0
                                    : (panel.brush.mediumId === "watercolour" ? 2 : 1)
                                onChosen: function(index) {
                                    if (index === 0) {
                                        panel.brush.traditionalMedia = false
                                    } else if (index === 1) {
                                        panel.brush.mediumId = "gouache"
                                        panel.brush.traditionalMedia = true
                                    } else {
                                        panel.brush.mediumId = "watercolour"
                                        panel.brush.traditionalMedia = true
                                    }
                                }
                            }
                            RowLayout {
                                width: parent.width
                                visible: panel.brush.traditionalMedia
                                         && Object.keys(panel.brush.materialMixture).length > 0
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
                    visible: panel.brush.traditionalMedia && panel.doc !== null
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
                                curve: 2
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
                    width: parent.width
                    title: qsTr("Material paint")
                    hint: qsTr("load · pigment · water · body")
                    visible: panel.brush.traditionalMedia && !panel.brush.erase
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
                    width: parent.width
                    title: qsTr("Advanced material")
                    hint: panel.brush.persistentReservoir ? qsTr("persistent") : qsTr("auto-reload")
                    visible: panel.brush.traditionalMedia
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
                    visible: panel.brush.traditionalMedia && panel.doc !== null
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

        // ---- A tool without brush properties
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.painting
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
