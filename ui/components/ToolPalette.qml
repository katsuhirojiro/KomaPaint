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
    function isAvailable(tool) { return Workspace.subTools.usesSubTools(tool) || viewTools.indexOf(tool) >= 0 }
    function shortcutFor(tool) {
        if (!Workspace.shortcuts || !Workspace.shortcuts.bindings) return ""
        const seq = Workspace.shortcuts.bindings["tool." + tool]
        return seq ? String(seq) : ""
    }
    function labelFor(tool) {
        for (let g = 0; g < groups.length; ++g)
            for (let i = 0; i < groups[g].length; ++i)
                if (groups[g][i].id === tool) return groups[g][i].label
        return tool
    }

    // id = Workspace.activeTool value and icon file name; shortcuts come live from ShortcutManager.
    readonly property var groups: [
        [ { id: "zoom", label: qsTr("Zoom") },
          { id: "hand", label: qsTr("Hand") } ],
        [ { id: "move", label: qsTr("Move") },
          { id: "transform", label: qsTr("Transform") } ],
        [ { id: "selection", label: qsTr("Selection") },
          { id: "lasso", label: qsTr("Lasso") },
          { id: "wand", label: qsTr("Auto select") },
          { id: "eyedropper", label: qsTr("Eyedropper") } ],
        [ { id: "pen", label: qsTr("Pen") },
          { id: "pencil", label: qsTr("Pencil") },
          { id: "brush", label: qsTr("Brush") },
          { id: "airbrush", label: qsTr("Airbrush") },
          { id: "eraser", label: qsTr("Eraser") },
          { id: "smudge", label: qsTr("Blend") } ],
        [ { id: "fill", label: qsTr("Fill") },
          { id: "gradient", label: qsTr("Gradient") } ],
        [ { id: "line", label: qsTr("Line") },
          { id: "curveline", label: qsTr("Curve") },
          { id: "rectangle", label: qsTr("Rectangle") },
          { id: "circle", label: qsTr("Ellipse") } ],
        [ { id: "text", label: qsTr("Text") } ]
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
        objectName: "toolPaletteScroll"
        anchors.top: parent.top
        anchors.bottom: colourPair.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 10
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
            width: KP.toolButtonSize
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
                            color: KP.borderLight
                        }
                    }
                    Grid {
                        columns: 1
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
                                readonly property string keyHint: root.shortcutFor(modelData.id)
                                tooltip: keyHint.length > 0 && available
                                         ? modelData.label + " (" + keyHint + ")" : modelData.label
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
        width: 40
        height: 54

        Rectangle {
            id: backgroundSwatch
            x: 19
            y: 25
            width: 19
            height: 19
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
            y: 3
            width: 20
            height: 20
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
