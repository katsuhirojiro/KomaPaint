import QtQuick
import QtQuick.Controls
import "../themes"

// Two-column tool palette. Tools are grouped by what they do; a thin rule
// separates the groups. Every icon comes from ui/assets/icons/tools.
//
// A tool is available when the application really implements it:
//  - painting tools are the ones the sub tool library knows (they drive the
//    brush engine), and
//  - `viewTools` are handled by the canvas (see CanvasWorkspace.qml).
// Every other tool is shown as unavailable: dimmed, not selectable, and its
// tooltip says so.
Item {
    id: root

    // Tools implemented by CanvasWorkspace itself.
    readonly property var viewTools: ["zoom", "hand", "eyedropper"]
    function isAvailable(tool) { return Workspace.subTools.paints(tool) || viewTools.indexOf(tool) >= 0 }
    function labelFor(tool) {
        for (let g = 0; g < groups.length; ++g)
            for (let i = 0; i < groups[g].length; ++i)
                if (groups[g][i].id === tool) return groups[g][i].label
        return tool
    }

    // id = Workspace.activeTool value and icon file name; key = real shortcut (see MainWindow.qml).
    readonly property var groups: [
        [ { id: "zoom", label: qsTr("Zoom"), key: "Z" },
          { id: "hand", label: qsTr("Hand"), key: "H" } ],
        [ { id: "move", label: qsTr("Move"), key: "" },
          { id: "transform", label: qsTr("Transform"), key: "" } ],
        [ { id: "selection", label: qsTr("Selection"), key: "" },
          { id: "lasso", label: qsTr("Lasso"), key: "" },
          { id: "wand", label: qsTr("Auto select"), key: "" },
          { id: "eyedropper", label: qsTr("Eyedropper"), key: "I" } ],
        [ { id: "pen", label: qsTr("Pen"), key: "P" },
          { id: "pencil", label: qsTr("Pencil"), key: "N" },
          { id: "brush", label: qsTr("Brush"), key: "B" },
          { id: "airbrush", label: qsTr("Airbrush"), key: "A" },
          { id: "eraser", label: qsTr("Eraser"), key: "E" },
          { id: "smudge", label: qsTr("Blend"), key: "" } ],
        [ { id: "fill", label: qsTr("Fill"), key: "" },
          { id: "gradient", label: qsTr("Gradient"), key: "" } ],
        [ { id: "line", label: qsTr("Line"), key: "" },
          { id: "curveline", label: qsTr("Curve"), key: "" },
          { id: "rectangle", label: qsTr("Rectangle"), key: "" },
          { id: "circle", label: qsTr("Ellipse"), key: "" } ],
        [ { id: "text", label: qsTr("Text"), key: "" } ]
    ]

    Rectangle {
        anchors.fill: parent
        color: KP.toolRailBg
        // Only the edge facing the workspace is marked; the rail is a tool
        // strip, not a box.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: KP.hairline
        }
    }

    // The rail scrolls only when the window is too short to hold every tool, so
    // no tool becomes unreachable on a small screen.
    Flickable {
        id: toolFlick
        anchors.top: parent.top
        anchors.bottom: colourPair.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: KP.gapS
        anchors.bottomMargin: KP.gapM
        contentHeight: toolColumn.height
        contentWidth: width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: toolColumn
            anchors.horizontalCenter: parent.horizontalCenter
            width: 2 * KP.toolButtonSize + KP.paletteGap
            spacing: 0

            Repeater {
                model: root.groups
                Column {
                    id: group
                    required property int index
                    required property var modelData
                    width: toolColumn.width
                    spacing: 0

                    // A group break: a hairline that stops short of both edges,
                    // so the rail keeps a single continuous rhythm.
                    Item {
                        visible: group.index > 0
                        width: parent.width
                        height: 9
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width - 10
                            height: 1
                            color: KP.hairline
                        }
                    }
                    Grid {
                        columns: 2
                        spacing: KP.paletteGap
                        Repeater {
                            model: group.modelData
                            IconButton {
                                required property var modelData
                                objectName: "tool_" + modelData.id
                                cell: KP.toolButtonSize
                                iconSize: KP.toolIconSize
                                iconName: "tools/" + modelData.id
                                accentBar: true
                                available: root.isAvailable(modelData.id)
                                selected: Workspace.activeTool === modelData.id
                                tooltip: modelData.key.length > 0 && available
                                         ? modelData.label + " (" + modelData.key + ")" : modelData.label
                                onActivated: Workspace.activeTool = modelData.id
                            }
                        }
                    }
                }
            }
        }
    }

    // The colour instrument's home: the pair every painting tool starts from.
    // Clicking either swatch makes it the drawing colour, the arrow swaps them,
    // and the small mark below restores black over white.
    Item {
        id: colourPair
        anchors.bottom: parent.bottom
        anchors.bottomMargin: KP.gapM
        anchors.horizontalCenter: parent.horizontalCenter
        width: 56
        height: 58

        Rectangle {
            id: backgroundSwatch
            x: 22
            y: 24
            width: 30
            height: 30
            radius: KP.radiusXS
            color: Workspace.colors.background
            border.width: 1
            border.color: KP.border
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.text: qsTr("Background colour — click to make it the drawing colour")
                ToolTip.visible: containsMouse
                ToolTip.delay: 500
                onClicked: Workspace.colors.swap()
            }
        }

        Rectangle {
            id: foregroundSwatch
            x: 2
            y: 2
            width: 32
            height: 32
            radius: KP.radiusXS
            color: Workspace.colors.foreground
            border.width: 2
            border.color: KP.isDark ? "#e9ebee" : "#ffffff"
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: KP.radiusXS + 1
                color: "transparent"
                border.width: 1
                border.color: KP.border
                z: -1
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.text: qsTr("Drawing colour — click to open the Colour panel")
                ToolTip.visible: containsMouse
                ToolTip.delay: 500
                onClicked: Dock.showPanel("color")
            }
        }

        IconButton {
            id: swapButton
            anchors.right: parent.right
            anchors.top: parent.top
            cell: 18
            iconSize: 11
            glyph: "swap"
            tooltip: qsTr("Swap drawing and background colour")
            onActivated: Workspace.colors.swap()
        }

        IconButton {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            cell: 18
            iconSize: 11
            glyph: "reset"
            tooltip: qsTr("Restore black and white")
            onActivated: Workspace.colors.reset()
        }
    }
}
