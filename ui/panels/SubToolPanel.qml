import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Brush presets for the active tool.
//
// This panel is a strip of strokes, not a list of names: each preset shows the
// line it actually draws (BrushStrokePreview stamps dabs from the preset's own
// size, hardness, opacity, flow, spacing and texture), so presets are chosen by
// look. The selected preset is marked with an ember edge and a wash; a preset
// that has been edited from its defaults carries a dot, and the header's reset
// command — the only command here — puts it back.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var library: Workspace.subTools
    // Label of the active tool, taken from the tool palette's own list.
    property string toolLabel: Workspace.activeTool
    readonly property bool painting: library.paintTool
    readonly property int rowHeight: 42

    function presetSize(preset) { return Math.round(preset.size) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Active tool
        Item {
            Layout.fillWidth: true
            implicitHeight: 34
            visible: panel.painting

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.panelInset
                anchors.rightMargin: KP.gapM
                spacing: KP.gapM

                KpIcon {
                    name: "tools/" + Workspace.activeTool
                    size: 17
                    color: KP.iconNormal
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: panel.toolLabel
                        color: KP.text
                        font.pixelSize: KP.fontS + 2
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: panel.library.currentName
                        color: KP.textSubtle
                        font.pixelSize: KP.fontXS
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    objectName: "resetSubToolButton"
                    glyph: "reset"
                    iconSize: KP.iconS
                    cell: KP.controlHeightSmall + 2
                    tooltip: qsTr("Return this preset to its default settings")
                    enabled: panel.library.modified
                    onActivated: panel.library.resetCurrent()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }

        // ---- Preset discovery. Search and category filtering operate on the
        // same engine-backed catalog used to apply these configurations.
        RowLayout {
            objectName: "subToolFilters"
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.gapS
            Layout.preferredHeight: 34
            spacing: KP.gapS
            visible: panel.painting

            KpTextField {
                id: searchField
                objectName: "subToolSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: KP.controlHeightSmall + 2
                placeholder: qsTr("Search subtools")
                text: panel.library.searchText
                onEdited: function(value) { panel.library.searchText = value }
            }
            ComboBox {
                id: categoryChoice
                objectName: "subToolCategory"
                Layout.preferredWidth: 112
                Layout.preferredHeight: KP.controlHeightSmall + 2
                model: [qsTr("All categories")].concat(panel.library.categories)
                currentIndex: panel.library.categoryFilter.length === 0
                              ? 0 : Math.max(0, model.indexOf(panel.library.categoryFilter))
                font.pixelSize: KP.fontXS
                Accessible.name: qsTr("Filter subtools by category")
                onActivated: function(index) {
                    panel.library.categoryFilter = index === 0 ? "" : panel.library.categories[index - 1]
                }
                contentItem: Text {
                    leftPadding: KP.gapS
                    rightPadding: 16
                    text: categoryChoice.displayText
                    color: KP.text
                    font.pixelSize: KP.fontXS
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: KP.inputBg
                    border.width: 1
                    border.color: categoryChoice.activeFocus ? KP.focusRing : KP.border
                    radius: KP.radiusS
                }
                indicator: KpGlyph {
                    x: categoryChoice.width - width - 4
                    y: (categoryChoice.height - height) / 2
                    name: "chevron-down"
                    size: 10
                    color: KP.iconNormal
                }
            }
        }

        // ---- Presets
        ListView {
            id: list
            objectName: "subToolList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: panel.painting
            model: panel.library.subTools
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

            delegate: AbstractButton {
                id: row
                required property int index
                required property var modelData
                readonly property int presetIndex: modelData.index
                readonly property bool current: presetIndex === panel.library.currentIndex

                objectName: "subTool" + presetIndex
                width: list.width
                height: panel.rowHeight
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: modelData.name
                onClicked: panel.library.select(presetIndex)

                background: Rectangle {
                    color: row.current ? KP.rowSelected
                         : row.hovered ? KP.hoverOverlay : "transparent"
                    Rectangle {
                        visible: row.current
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 2
                        color: KP.ember
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: KP.hairline
                        opacity: 0.6
                    }
                }

                contentItem: Item {
                    id: contentBox

                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: KP.panelInset
                        anchors.rightMargin: KP.gapM
                        spacing: KP.gapM

                        // The preset's own stroke, drawn at row scale.
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 58
                            Layout.preferredHeight: 26
                            radius: KP.radiusXS
                            color: KP.isDark ? Qt.rgba(0, 0, 0, 0.22) : Qt.rgba(255, 255, 255, 0.65)
                            border.width: 1
                            border.color: KP.hairline

                            BrushStrokePreview {
                                anchors.fill: parent
                                anchors.margins: 2
                                brushSize: row.modelData.size
                                hardness: row.modelData.hardness
                                brushOpacity: row.modelData.opacity
                                flow: row.modelData.flow
                                spacing: row.modelData.spacing
                                textured: row.modelData.textured
                                scaleHint: 0.62
                                ink: KP.text
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: KP.gapS
                                Text {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    color: row.current ? KP.textAccent : KP.text
                                    font.pixelSize: KP.fontS + 1
                                    font.weight: row.current ? Font.Medium : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    visible: row.modelData.modified
                                    Layout.preferredWidth: 5
                                    Layout.preferredHeight: 5
                                    radius: 2.5
                                    color: KP.ember
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: KP.gapS
                                Text {
                                    text: row.modelData.hardness + "%"
                                    color: KP.textSubtle
                                    font.pixelSize: KP.fontMicro
                                    font.family: KP.fontMono
                                }
                                Text {
                                    text: row.modelData.textured ? qsTr("textured") : qsTr("round")
                                    color: KP.textSubtle
                                    font.pixelSize: KP.fontMicro
                                }
                            }
                        }

                        Text {
                            id: rowSize
                            Layout.alignment: Qt.AlignVCenter
                            text: panel.presetSize(row.modelData) + " px"
                            color: row.current ? KP.textMuted : KP.textSubtle
                            font.pixelSize: KP.fontXS
                            font.family: KP.fontMono
                        }
                    }
                }
            }
        }

        Text {
            objectName: "subToolNoResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.painting && list.count === 0
            text: qsTr("No subtools match this search.")
            color: KP.textSubtle
            font.pixelSize: KP.fontS
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        // ---- Not a painting tool
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.painting
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 2 * KP.panelInset
                spacing: KP.gapS
                KpGlyph {
                    Layout.alignment: Qt.AlignHCenter
                    name: "brush"
                    size: 20
                    color: KP.iconOff
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("This tool has no presets.")
                    color: KP.textMuted
                    font.pixelSize: KP.fontS
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pick a painting tool to see its presets.")
                    color: KP.textSubtle
                    font.pixelSize: KP.fontXS
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
