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
                    hint: panel.brush.traditionalMedia
                        ? (panel.brush.erase ? qsTr("gouache · lift material") : qsTr("gouache · composition-preserving"))
                        : qsTr("digital · raster")
                    contentTopMargin: KP.gapS
                    contentBottomMargin: KP.gapS
                    Item {
                        width: parent.width
                        height: mediumChoice.height
                        SegmentedChoice {
                            id: mediumChoice
                            objectName: "paintingMediumChoice"
                            width: parent.width
                            options: [qsTr("Digital"), qsTr("Gouache")]
                            currentIndex: panel.brush.traditionalMedia ? 1 : 0
                            onChosen: function(index) { panel.brush.traditionalMedia = index === 1 }
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
