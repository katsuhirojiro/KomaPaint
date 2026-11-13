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
        color: KP.paneHeaderBg
        border.color: KP.border
        border.width: 1
    }

    Column {
        id: toolColumn
        anchors.top: parent.top
        anchors.topMargin: 5
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

                // rule between groups
                Item {
                    visible: group.index > 0
                    width: parent.width
                    height: 7
                    Rectangle {
                        y: 3
                        width: parent.width
                        height: 1
                        color: KP.separator
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

    Item {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: 52
        height: 52
        // Drawing colour over background colour; click swaps them.
        Rectangle {
            x: 20
            y: 20
            width: 30
            height: 30
            color: Workspace.colors.background
            border.color: KP.border
            radius: KP.radiusS
        }
        Rectangle {
            x: 2
            y: 2
            width: 30
            height: 30
            color: Workspace.colors.foreground
            border.color: KP.accent
            border.width: 2
            radius: KP.radiusS
        }
        MouseArea {
            anchors.fill: parent
            onClicked: Workspace.colors.swap()
            ToolTip.text: qsTr("Drawing colour / background colour — click to swap")
            ToolTip.visible: containsMouse
            ToolTip.delay: 500
            hoverEnabled: true
        }
    }
}
