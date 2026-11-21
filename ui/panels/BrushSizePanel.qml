import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Brush size as an instrument, not a menu.
//
// Three ways to set the same value, in the order they are used:
//   1. the readout at the top — a live disc of the actual tip, a stepper and a
//      field, for "40 px, exactly";
//   2. the rail underneath — drag to sweep the whole range the engine allows
//      with a curved logarithmic scale for precision at drawing sizes;
//   3. the ladder — the fixed sizes an artist returns to, one click each, shown
//      as discs at their real relative size.
//
// Every one of them writes Workspace.brush.size, the value the engine, the
// command bar and the Tool Property slider all read. The ladder is required to
// stay scroll-free in the default layout, so it is four rows of seven.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var brush: Workspace.brush
    readonly property bool painting: Workspace.subTools.paintTool
    readonly property int columns: 7
    // A convenience list of the sizes an artist returns to, not a limit: the
    // readout and the rail cover the full numeric range. This is the same
    // ladder the [ and ] shortcuts walk; the host window passes it in.
    property var sizes: [1, 2, 3, 4, 5, 6, 7,
                         8, 10, 12, 15, 17, 20, 25,
                         30, 40, 50, 60, 70, 80, 100,
                         120, 150, 170, 200, 256]
    readonly property int cellHeight: 34
    readonly property int rows: Math.ceil(sizes.length / columns)
    // Height the content needs to show every size without scrolling. The dock
    // frame adds its own header; `preferredHeight` is the panel's content only.
    // A constant, so the dock can size the area before the panel is laid out:
    // the readout, the size rail and every row of the ladder, nothing hidden.
    readonly property int readoutHeight: 40
    readonly property int railHeight: 22
    readonly property int preferredHeight: readoutHeight + railHeight + rows * cellHeight + 2
    readonly property int largest: brush ? brush.maximumSize : 256

    // Circles are shown at their real pixel size up to `exact`; larger sizes keep
    // growing more slowly so the sequence still reads small-to-large.
    function shownDiameter(size) {
        // The labels sit under the discs, so the largest disc stays small enough
        // to leave room for its number in a 34 px cell.
        const exact = 16, largestShow = 19
        if (size <= exact) return size
        return exact + (largestShow - exact) * Math.log(size / exact) / Math.log(panel.largest / exact)
    }
    // Same logarithmic response as Tool Property: the middle is about 90 px.
    readonly property real railCurve: 2.25
    function railPosition(size) {
        return Math.pow(Math.log(Math.max(1, size)) / Math.log(largest), 1 / railCurve)
    }
    function sizeAt(position) {
        const t = Math.pow(Math.max(0, Math.min(1, position)), railCurve)
        return Math.max(1, Math.min(largest, Math.round(Math.pow(largest, t))))
    }

    Text {
        visible: panel.brush.nativeBrush
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: KP.panelInset
        text: qsTr("Size is set in the native brush. Edit it in Brush Studio.")
        wrapMode: Text.Wrap
        color: KP.textMuted
        font.pixelSize: KP.fontS
    }

    ColumnLayout {
        visible: !panel.brush.nativeBrush
        anchors.fill: parent
        spacing: 0

        // ---- 1. Readout: the tip, a stepper and an exact value
        Item {
            id: readout
            Layout.fillWidth: true
            implicitHeight: panel.readoutHeight
            opacity: panel.painting ? 1 : KP.disabledOpacity

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.panelInset
                anchors.rightMargin: KP.panelInset
                spacing: KP.gapM

                // The tip at its true relative size, in the drawing colour.
                Item {
                    implicitWidth: 34
                    implicitHeight: 34
                    Rectangle {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        radius: 16
                        color: KP.inputBg
                        border.width: 1
                        border.color: KP.border

                        Rectangle {
                            anchors.centerIn: parent
                            readonly property real shown: 2 + panel.railPosition(panel.brush.size) * 22
                            width: shown
                            height: shown
                            radius: width / 2
                            color: Workspace.colors.foreground
                            opacity: panel.brush.opacity / 100
                        }
                        // Ember mark: this disc is the live brush size.
                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: KP.ember
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: 2
                            anchors.topMargin: 2
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("BRUSH SIZE")
                        color: KP.textSectionLabel
                        font.pixelSize: KP.fontMicro
                        font.letterSpacing: KP.trackingMicro
                    }
                    Row {
                        spacing: KP.gapXS
                        IconButton {
                            glyph: "minus"
                            iconSize: 12
                            cell: 22
                            tooltip: qsTr("One step smaller ([)")
                            enabled: panel.painting
                            onActivated: panel.brush.size = Math.max(panel.brush.minimumSize, panel.brush.size - 1)
                        }
                        KpNumberField {
                            objectName: "brushSizeField"
                            width: 104
                            height: 22
                            value: panel.brush.size
                            from: panel.brush.minimumSize
                            to: panel.brush.maximumSize
                            enabled: panel.painting
                            unit: "px"
                            onEdited: function(v) { panel.brush.size = v }
                        }
                        IconButton {
                            glyph: "plus"
                            iconSize: 12
                            cell: 22
                            tooltip: qsTr("One step larger (])")
                            enabled: panel.painting
                            onActivated: panel.brush.size = Math.min(panel.brush.maximumSize, panel.brush.size + 1)
                        }
                    }
                }
            }
        }

        // ---- 2. Rail: the whole range in one gesture
        Item {
            id: rail
            Layout.fillWidth: true
            implicitHeight: panel.railHeight
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            opacity: panel.painting ? 1 : KP.disabledOpacity

            readonly property real travel: Math.max(1, width - 8)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 4
                radius: 2
                color: KP.inputBg
                border.width: 1
                border.color: KP.border

                Rectangle {
                    x: 1
                    y: 1
                    height: parent.height - 2
                    width: Math.max(0, rail.travel * panel.railPosition(panel.brush.size))
                    radius: 1
                    color: KP.ember
                    opacity: railMouse.containsMouse || railMouse.pressed ? 1 : 0.8
                }
            }
            Rectangle {
                // Knob
                x: 4 + rail.travel * panel.railPosition(panel.brush.size) - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: railMouse.pressed || railMouse.containsMouse ? 12 : 10
                height: width
                radius: width / 2
                color: KP.ember
                border.width: 1.5
                border.color: KP.isDark ? "#e8e3da" : "#ffffff"
            }
            MouseArea {
                id: railMouse
                anchors.fill: parent
                anchors.topMargin: -6
                anchors.bottomMargin: -6
                hoverEnabled: true
                preventStealing: true
                enabled: panel.painting
                function apply(x) { panel.brush.size = panel.sizeAt((x - 4) / rail.travel) }
                onPressed: function(mouse) { apply(mouse.x) }
                onPositionChanged: function(mouse) { if (pressed) apply(mouse.x) }
                onWheel: function(wheel) {
                    panel.brush.size = Math.max(panel.brush.minimumSize,
                        Math.min(panel.brush.maximumSize, panel.brush.size + (wheel.angleDelta.y > 0 ? 1 : -1)))
                    wheel.accepted = true
                }
            }
        }

        // ---- 3. Ladder: the sizes you actually return to
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
                    AbstractButton {
                        id: cell
                        required property int modelData
                        required property int index
                        readonly property bool current: panel.painting && panel.brush.size === modelData

                        objectName: "brushSize" + modelData
                        width: grid.cellWidth
                        height: panel.cellHeight
                        enabled: panel.painting
                        hoverEnabled: panel.painting
                        focusPolicy: Qt.NoFocus
                        Accessible.name: modelData + " px"
                        onClicked: panel.brush.size = modelData

                        background: Rectangle {
                            radius: KP.radiusS
                            color: cell.current ? KP.rowSelected
                                 : cell.hovered ? KP.hoverOverlay : "transparent"
                            border.width: cell.current ? 1 : 0
                            border.color: KP.borderLight
                        }

                        contentItem: Item {
                            // The disc
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 3
                                readonly property real d: Math.max(1.5, panel.shownDiameter(cell.modelData))
                                width: d
                                height: d
                                radius: d / 2
                                color: cell.current ? KP.text : (cell.hovered ? KP.text : KP.textMuted)
                                opacity: cell.current ? 1 : 0.82
                                antialiasing: true
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 1
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: cell.modelData
                                color: cell.current ? KP.text : KP.textSubtle
                                font.family: KP.fontMono
                                font.pixelSize: KP.fontMicro
                            }
                        }
                    }
                }
            }
        }
    }
}
