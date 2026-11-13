import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Brush sizes as circles: pick a size by how big it looks. Clicking a cell
// sets Workspace.brush.size, the same value the Tool Property slider edits.
Rectangle {
    id: panel
    color: KP.paneBg
    border.color: KP.border
    border.width: 1

    readonly property var brush: Workspace.brush
    readonly property bool painting: Workspace.subTools.paintTool
    readonly property int columns: 7
    // All inside the engine's range (1–256 px).
    readonly property var sizes: [1, 2, 3, 4, 5, 6, 7,
                                  8, 10, 12, 15, 17, 20, 25,
                                  30, 40, 50, 60, 70, 80, 100,
                                  120, 150, 170, 200, 256]
    readonly property int cellHeight: 44
    readonly property int rows: Math.ceil(sizes.length / columns)
    // Height the panel needs to show every size without scrolling.
    readonly property int preferredHeight: KP.panelHeaderH + rows * cellHeight + 2

    // Circles are shown at their real pixel size up to `exact`; larger sizes
    // keep growing more slowly so the sequence still reads small-to-large.
    function shownDiameter(size) {
        const exact = 18, largest = 26
        if (size <= exact) return size
        return exact + (largest - exact) * Math.log(size / exact) / Math.log(brush.maximumSize / exact)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Brush Size")
            onCloseClicked: Panels.setVisible("brushsize", false)
        }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: grid.height
            boundsBehavior: Flickable.StopAtBounds
            opacity: panel.painting ? 1 : 0.4

            Grid {
                id: grid
                readonly property int cellWidth: Math.floor((scroll.width - 2) / panel.columns)
                x: Math.round((scroll.width - cellWidth * panel.columns) / 2)
                width: cellWidth * panel.columns
                columns: panel.columns
                spacing: 0
                Repeater {
                    model: panel.sizes
                    Rectangle {
                        id: cell
                        required property int modelData
                        required property int index
                        readonly property bool current: panel.painting && panel.brush.size === modelData
                        objectName: "brushSize" + modelData
                        width: grid.cellWidth
                        height: panel.cellHeight
                        color: current ? KP.toolActive : (cellMouse.containsMouse && panel.painting ? KP.buttonBgHover : "transparent")
                        border.width: 1
                        border.color: current ? KP.accent : KP.separator

                        Item {
                            // circle area
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 2
                            height: 28
                            Rectangle {
                                anchors.centerIn: parent
                                readonly property real d: Math.max(1, panel.shownDiameter(cell.modelData))
                                width: d
                                height: d
                                radius: d / 2
                                color: cell.current ? KP.textOnActive : KP.text
                                antialiasing: true
                            }
                        }
                        Label {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 1
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: cell.modelData
                            color: cell.current ? KP.textOnActive : KP.textMuted
                            font.family: KP.fontMono
                            font.pixelSize: KP.fontXS
                        }
                        MouseArea {
                            id: cellMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: panel.painting
                            onClicked: panel.brush.size = cell.modelData
                        }
                    }
                }
            }
        }
    }
}
