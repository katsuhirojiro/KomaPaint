import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../components"
import "../themes"

// The docking workspace.
//
// It owns nothing about any particular panel: the host window passes a map of
// panel id → Component and the component for the canvas, and the model
// (DockModel) supplies the tree. Everything below — splits, tab groups,
// floating windows, drag previews — is driven by that tree, so new panels and
// new windows get the same behaviour by adding an entry to the map.
Item {
    id: workspace

    objectName: "dockWorkspace"
    property var model: null
    property var panelComponents: ({})
    property Component canvasComponent: null
    // A panel that the layout knows about but this build does not provide. It is
    // drawn as a deliberate empty state — the panel's own name, its icon and the
    // reason — rather than as a placeholder sentence, so it never looks broken.
    property Component fallbackComponent: Component {
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * KP.panelInset, 240)
                spacing: KP.gapM

                KpGlyph {
                    Layout.alignment: Qt.AlignHCenter
                    name: "window"
                    size: 22
                    color: KP.iconOff
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Not part of this build")
                    color: KP.textMuted
                    font.pixelSize: KP.fontS + 1
                    font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("This panel is defined in the layout but has no view yet. It can be moved, docked or hidden like any other panel.")
                    color: KP.textSubtle
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    readonly property var tree: model ? model.tree : ({})
    readonly property var floatingWindows: model ? model.floating : []

    // Drop targets (dock groups and the canvas), rebuilt from the layout.
    property var targets: []
    // Drag state. Coordinates in `pointer` are workspace-local; the floating
    // preview also keeps the global position so the new window opens where the
    // pointer was released.
    property bool dragging: false
    property string dragPanel: ""
    property var drop: null
    property point pointer: Qt.point(0, 0)
    property point pointerGlobal: Qt.point(0, 0)

    // The canvas instance currently in the tree (the host window's own
    // component), so commands such as "Fit to Window" reach it.
    property Item canvasItem: null

    function canvasLoaded(item) { workspace.canvasItem = item }
    function canvasUnloaded(item) { if (workspace.canvasItem === item) workspace.canvasItem = null }
    function fitCanvas() {
        if (workspace.canvasItem && typeof workspace.canvasItem.fitCanvas === "function")
            workspace.canvasItem.fitCanvas()
    }

    // The canvas lives beside the layout tree, not inside it: a layout change
    // (closing a panel, dragging a splitter) must never destroy the drawing
    // surface, its tiles or its view state. The tree's canvas node is a slot
    // that tells the surface where to sit.
    function placeCanvas(slot) {
        if (!slot || !workspace.canvasItem) return
        const at = slot.mapToItem(workspace, 0, 0)
        canvasHost.x = at.x
        canvasHost.y = at.y
        canvasHost.width = Math.max(0, slot.width)
        canvasHost.height = Math.max(0, slot.height)
        canvasHost.visible = slot.visible
    }

    Connections {
        target: workspace.model

    }

    function componentFor(panelId) {
        if (!workspace.panelComponents) return workspace.fallbackComponent
        const component = workspace.panelComponents[panelId]
        return component !== undefined && component !== null ? component : workspace.fallbackComponent
    }

    function registerTarget(item) {
        const list = workspace.targets.slice(0)
        list.push(item)
        workspace.targets = list
    }

    function unregisterTarget(item) {
        const list = []
        for (let i = 0; i < workspace.targets.length; ++i)
            if (workspace.targets[i] !== item) list.push(workspace.targets[i])
        workspace.targets = list
    }

    // ---- dragging ---------------------------------------------------------
    function beginDrag(panelId) {
        workspace.dragging = true
        workspace.dragPanel = panelId
        workspace.drop = null
    }

    function dragTo(globalX, globalY) {
        if (!workspace.dragging) return
        workspace.pointerGlobal = Qt.point(globalX, globalY)
        const local = workspace.mapFromGlobal(globalX, globalY)
        workspace.pointer = Qt.point(local.x, local.y)
        workspace.drop = workspace.computeDrop(workspace.pointer)
    }

    function cancelDrag() {
        workspace.dragging = false
        workspace.dragPanel = ""
        workspace.drop = null
    }

    function endDrag() {
        if (!workspace.dragging) {
            workspace.cancelDrag()
            return
        }
        const plan = workspace.drop
        const panel = workspace.dragPanel
        const global = workspace.pointerGlobal
        workspace.cancelDrag()
        if (!plan || !panel || !workspace.model) return
        if (plan.kind === "float") {
            workspace.model.floatPanel(panel, Math.round(global.x - 24), Math.round(global.y - 12), 330, 280)
        } else if (plan.kind === "tabs") {
            workspace.model.moveToGroup(panel, plan.nodeId, plan.index)
        } else if (plan.kind === "side") {
            workspace.model.dockToNodeSide(panel, plan.nodeId, plan.side)
        } else if (plan.kind === "workspace") {
            workspace.model.dockToWorkspace(panel, plan.side)
        }
    }

    // The insertion plan for a pointer position (workspace local). This is the
    // single place that decides what a drop would do; the preview and the
    // commit both use it, so what is shown is what happens.
    function computeDrop(point) {
        if (!workspace.model) return null
        const margin = 10
        const outside = point.x < -margin || point.y < -margin
                     || point.x > workspace.width + margin || point.y > workspace.height + margin
        if (outside) {
            return { kind: "float",
                     rect: Qt.rect(point.x + 12, point.y + 8, 300, 220) }
        }

        let target = null
        let rect = Qt.rect(0, 0, 0, 0)
        for (let i = 0; i < workspace.targets.length; ++i) {
            const candidate = workspace.targets[i]
            if (!candidate || !candidate.visible || candidate.width <= 0 || candidate.height <= 0) continue
            const box = candidate.rectInWorkspace()
            if (point.x < box.x || point.y < box.y
                || point.x > box.x + box.width || point.y > box.y + box.height) continue
            // Prefer the smallest matching box (a group inside a bigger one).
            if (!target || box.width * box.height < rect.width * rect.height) {
                target = candidate
                rect = box
            }
        }

        if (!target) return workspace.edgeDrop(point)
        const zoneX = Math.min(60, Math.max(30, rect.width * 0.3))
        const zoneY = Math.min(48, Math.max(26, rect.height * 0.3))
        const left = point.x - rect.x
        const right = rect.x + rect.width - point.x
        const top = point.y - rect.y
        const bottom = rect.y + rect.height - point.y
        const sides = [
            { side: "left", weight: left / zoneX, rect: Qt.rect(rect.x, rect.y, Math.round(rect.width / 2), rect.height) },
            { side: "right", weight: right / zoneX, rect: Qt.rect(rect.x + Math.round(rect.width / 2), rect.y, Math.round(rect.width / 2), rect.height) },
            { side: "top", weight: top / zoneY, rect: Qt.rect(rect.x, rect.y, rect.width, Math.round(rect.height / 2)) },
            { side: "bottom", weight: bottom / zoneY, rect: Qt.rect(rect.x, rect.y + Math.round(rect.height / 2), rect.width, Math.round(rect.height / 2)) }
        ]
        let best = sides[0]
        for (let i = 1; i < sides.length; ++i) if (sides[i].weight < best.weight) best = sides[i]

        const ontoCanvas = target.canvasTarget === true
        if (best.weight < 1) {
            return { kind: "side", nodeId: target.targetId, side: best.side, rect: best.rect }
        }
        if (ontoCanvas) {
            // The canvas has no header to add a tab to: dock beside it.
            const side = left < right ? "left" : "right"
            return { kind: "side", nodeId: target.targetId, side: side,
                     rect: side === "left"
                               ? Qt.rect(rect.x, rect.y, Math.round(rect.width / 2), rect.height)
                               : Qt.rect(rect.x + Math.round(rect.width / 2), rect.y, Math.round(rect.width / 2), rect.height) }
        }
        const body = target.bodyRectInWorkspace()
        const index = target.tabDropIndexFor(point.x - rect.x)
        return { kind: "tabs", nodeId: target.targetId, index: index,
                 rect: Qt.rect(rect.x, rect.y, rect.width, Math.max(2, body.y - rect.y)),
                 marker: Qt.point(rect.x + target.tabInsertionX(index), rect.y) }
    }

    // Dropping on the workspace itself (over the canvas area, or an empty
    // strip): the panel becomes a band on that edge.
    function edgeDrop(point) {
        const distances = [
            { side: "left", value: point.x },
            { side: "right", value: workspace.width - point.x },
            { side: "top", value: point.y },
            { side: "bottom", value: workspace.height - point.y }
        ]
        let best = distances[0]
        for (let i = 1; i < distances.length; ++i) if (distances[i].value < best.value) best = distances[i]
        const band = Math.round(Math.min(280, Math.max(180, (best.side === "left" || best.side === "right") ? workspace.width * 0.24 : workspace.height * 0.3)))
        let rect
        if (best.side === "left") rect = Qt.rect(0, 0, band, workspace.height)
        else if (best.side === "right") rect = Qt.rect(workspace.width - band, 0, band, workspace.height)
        else if (best.side === "top") rect = Qt.rect(0, 0, workspace.width, band)
        else rect = Qt.rect(0, workspace.height - band, workspace.width, band)
        return { kind: "workspace", side: best.side, rect: rect }
    }

    Shortcut {
        // StandardKey.Cancel stands for more than one key binding, so it has to
        // be given as a list; a single `sequence:` would bind only one of them.
        sequences: [StandardKey.Cancel]
        enabled: workspace.dragging
        onActivated: workspace.cancelDrag()
    }

    // Any layout change — a collapsed dock area, a hidden panel, a docking move
    // — re-runs the layout from the top: a nested split only re-lays itself out
    // when its own size changes, and hiding a neighbour does not change it.
    Connections {
        target: workspace.model
        function onLayoutChanged() { workspace.relayoutTree() }
    }

    // Re-run the whole layout: used when a panel reports a new preferred size.
    property bool relayouting: false
    function relayoutTree() {
        if (workspace.relayouting) return
        workspace.relayouting = true
        treeView.relayoutDeep()
        workspace.relayouting = false
    }

    // The single canvas instance, kept for the life of the window.
    Loader {
        id: canvasHost
        objectName: "dockCanvasHost"
        visible: false
        sourceComponent: workspace.canvasComponent
        onLoaded: workspace.canvasLoaded(item)
        Component.onDestruction: workspace.canvasUnloaded(workspace.canvasItem)
    }

    // ---- view -------------------------------------------------------------
    DockNodeView {
        id: treeView
        anchors.fill: parent
        workspaceRoot: workspace
        node: workspace.tree
    }

    // ---- drop preview -----------------------------------------------------
    Rectangle {
        id: preview
        objectName: "dockDropPreview"
        visible: workspace.dragging && workspace.drop !== null
        x: workspace.drop ? workspace.drop.rect.x : 0
        y: workspace.drop ? workspace.drop.rect.y : 0
        width: workspace.drop ? workspace.drop.rect.width : 0
        height: workspace.drop ? workspace.drop.rect.height : 0
        z: 100
        color: Qt.rgba(KP.selection.r, KP.selection.g, KP.selection.b, 0.22)
        border.color: KP.selection
        border.width: 1
        radius: KP.radiusS
    }

    // The tab insertion point, when the drop would join a tab group.
    Rectangle {
        visible: workspace.dragging && workspace.drop !== null && workspace.drop.marker !== undefined
        x: workspace.drop && workspace.drop.marker !== undefined ? workspace.drop.marker.x - 1 : 0
        y: workspace.drop && workspace.drop.marker !== undefined ? workspace.drop.marker.y : 0
        width: 2
        height: KP.panelHeaderH
        color: KP.selection
        z: 101
    }

    // A small chip under the pointer naming the panel being dragged.
    Rectangle {
        visible: workspace.dragging
        x: Math.min(workspace.width - width - 4, workspace.pointer.x + 14)
        y: Math.min(workspace.height - height - 4, workspace.pointer.y + 12)
        width: dragLabel.implicitWidth + KP.gapM * 2
        height: 22
        z: 102
        color: KP.paneHeaderBg
        border.color: KP.selection
        border.width: 1
        radius: KP.radiusS
        opacity: 0.95
        Text {
            id: dragLabel
            anchors.centerIn: parent
            text: workspace.model ? workspace.model.title(workspace.dragPanel) : ""
            color: KP.text
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
        }
    }

    // ---- floating windows -------------------------------------------------
    Instantiator {
        model: workspace.floatingWindows
        delegate: DockFloatingWindow {
            workspaceRoot: workspace
            entry: modelData
        }
    }
}
