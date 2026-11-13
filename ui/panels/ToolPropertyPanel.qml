import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Settings of the active tool. For painting tools these are the properties
// of Workspace.brush, which CanvasViewport hands to the brush engine at the
// start of every stroke; nothing shown here is decorative.
Rectangle {
    id: panel
    color: KP.paneBg
    border.color: KP.border
    border.width: 1

    readonly property var brush: Workspace.brush
    readonly property var library: Workspace.subTools
    readonly property bool painting: library.paintTool

    function hint(tool) {
        if (tool === "zoom") return qsTr("Click to zoom in. Alt-click or right-click to zoom out.")
        if (tool === "hand") return qsTr("Drag to move the view.")
        if (tool === "eyedropper") return qsTr("Click or drag on the artwork to pick the drawing colour.")
        return qsTr("This tool is not available yet.")
    }

    component GroupTitle: RowLayout {
        property alias text: title.text
        Layout.fillWidth: true
        Layout.topMargin: 2
        spacing: KP.gapM
        Label {
            id: title
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: KP.borderLight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Tool Property")
            onCloseClicked: Panels.setVisible("toolproperty", false)
        }

        // Sub tool name + reset
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            visible: panel.painting
            color: KP.paneBgAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapS
                spacing: KP.gapM
                Label {
                    Layout.fillWidth: true
                    text: panel.library.currentName
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontL
                    font.bold: true
                    elide: Text.ElideRight
                }
                AbstractButton {
                    id: reset
                    objectName: "resetSubToolButton"
                    enabled: panel.library.modified
                    implicitHeight: KP.controlHeight - 2
                    implicitWidth: resetText.implicitWidth + 16
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: panel.library.resetCurrent()
                    ToolTip.text: qsTr("Return this sub tool to its initial settings")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    background: Rectangle {
                        radius: KP.radiusS
                        color: reset.down ? KP.buttonBgPress : (reset.hovered ? KP.buttonBgHover : KP.buttonBg)
                        border.color: KP.border
                        opacity: reset.enabled ? 1 : 0.4
                    }
                    contentItem: Label {
                        id: resetText
                        text: qsTr("Reset")
                        color: reset.enabled ? KP.text : KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.separator
            }
        }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.painting
            clip: true
            contentWidth: width
            contentHeight: properties.implicitHeight + 2 * KP.gapM
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: scroll.contentHeight > scroll.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff; width: 8 }

            ColumnLayout {
                id: properties
                x: KP.gapL
                y: KP.gapM
                width: scroll.width - 2 * KP.gapL - (scroll.contentHeight > scroll.height ? 6 : 0)
                spacing: KP.gapM

                GroupTitle { text: qsTr("Stroke") }
                PropertySlider {
                    objectName: "brushSizeSlider"
                    Layout.fillWidth: true
                    label: qsTr("Brush size")
                    unit: "px"
                    from: panel.brush.minimumSize
                    to: panel.brush.maximumSize
                    curve: 2
                    value: panel.brush.size
                    onEdited: function(v) { panel.brush.size = v }
                }
                PropertySlider {
                    objectName: "brushOpacitySlider"
                    Layout.fillWidth: true
                    label: qsTr("Opacity")
                    unit: "%"
                    from: 0
                    to: 100
                    value: panel.brush.opacity
                    onEdited: function(v) { panel.brush.opacity = v }
                }
                PropertySlider {
                    objectName: "brushDensitySlider"
                    Layout.fillWidth: true
                    label: qsTr("Brush density")
                    unit: "%"
                    from: 0
                    to: 100
                    value: panel.brush.flow
                    onEdited: function(v) { panel.brush.flow = v }
                }

                GroupTitle { text: qsTr("Brush tip") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: KP.gapM
                    Label {
                        text: qsTr("Shape")
                        color: KP.text
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontM
                        Layout.fillWidth: true
                    }
                    SegmentedChoice {
                        objectName: "brushTipChoice"
                        Layout.preferredWidth: 132
                        width: 132
                        options: [qsTr("Round"), qsTr("Textured")]
                        currentIndex: panel.brush.textured ? 1 : 0
                        onChosen: function(index) { panel.brush.textured = index === 1 }
                    }
                }
                PropertySlider {
                    objectName: "brushHardnessSlider"
                    Layout.fillWidth: true
                    label: qsTr("Hardness")
                    unit: "%"
                    from: 0
                    to: 100
                    value: panel.brush.hardness
                    onEdited: function(v) { panel.brush.hardness = v }
                }
                PropertySlider {
                    objectName: "brushSpacingSlider"
                    Layout.fillWidth: true
                    label: qsTr("Gap between dabs")
                    unit: "%"
                    from: panel.brush.minimumSpacing
                    to: panel.brush.maximumSpacing
                    value: panel.brush.spacing
                    onEdited: function(v) { panel.brush.spacing = v }
                }

                GroupTitle { text: qsTr("Correction") }
                PropertySlider {
                    objectName: "brushStabilizationSlider"
                    Layout.fillWidth: true
                    label: qsTr("Stabilization")
                    from: 0
                    to: 100
                    value: panel.brush.stabilization
                    onEdited: function(v) { panel.brush.stabilization = v }
                }
            }
        }

        Label {
            visible: !panel.painting
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: KP.gapL
            text: panel.hint(Workspace.activeTool)
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignTop
        }
    }
}
