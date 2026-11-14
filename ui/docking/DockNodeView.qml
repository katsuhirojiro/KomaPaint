import QtQuick
import "../themes"

// One node of the layout tree. Recursion is explicit: a split creates another
// DockNodeView for each of its children.
Item {
    id: view

    property var workspaceRoot: null
    property var node: ({})
    // Set by the parent split: the dock area this node is in and whether that
    // area is collapsed. A collapsed area hides its content but keeps every
    // item loaded, so expanding it restores the same panels.
    property string areaSide: ""
    property bool areaCollapsed: false

    readonly property string kind: node.kind !== undefined ? node.kind : ""

    Loader {
        id: nodeLoader
        anchors.fill: parent
        visible: !view.areaCollapsed
        sourceComponent: view.kind === "split" ? splitComponent
                       : view.kind === "tabs" ? tabsComponent
                       : view.kind === "canvas" ? canvasComponent
                       : null
    }

    // The strip a collapsed area leaves behind, with the control that brings it
    // back. It sits above the (hidden) content so the panels never unload.
    DockAreaStrip {
        anchors.fill: parent
        visible: view.areaCollapsed
        side: view.areaSide
        workspaceRoot: view.workspaceRoot
        onRevealRequested: {
            if (view.workspaceRoot && view.workspaceRoot.model && view.areaSide.length > 0)
                view.workspaceRoot.model.setAreaCollapsed(view.areaSide, false)
        }
    }

    // Minimum sizes travel up the tree so a split can keep its children usable.
    readonly property real minimumBodyWidth:
        nodeLoader.item && nodeLoader.item.minimumBodyWidth !== undefined ? nodeLoader.item.minimumBodyWidth : 0
    readonly property real minimumBodyHeight:
        nodeLoader.item && nodeLoader.item.minimumBodyHeight !== undefined ? nodeLoader.item.minimumBodyHeight : 0

    // Asked by the parent split; forwards to whichever node is loaded.
    function minimumOf(horizontal) {
        const content = nodeLoader.item
        if (!content) return 0
        if (typeof content.minimumOf === "function") return content.minimumOf(horizontal)
        const value = horizontal ? content.minimumBodyWidth : content.minimumBodyHeight
        return value !== undefined && value > 0 ? value : 0
    }

    function relayoutDeep() {
        const content = nodeLoader.item
        if (content && typeof content.relayoutDeep === "function") content.relayoutDeep()
    }

    Component {
        id: splitComponent
        DockSplit {
            workspaceRoot: view.workspaceRoot
            node: view.node
        }
    }

    Component {
        id: tabsComponent
        DockGroup {
            workspaceRoot: view.workspaceRoot
            node: view.node
        }
    }

    // The canvas is not a panel: it comes from the host window as a component
    // and is drawn like any other dock node, so panels can be dropped on all
    // four of its edges.
    Component {
        id: canvasComponent
        Item {
            id: canvasSlot
            objectName: "dockCanvasSlot"

            readonly property bool canvasTarget: true
            readonly property int targetId: view.node.id !== undefined ? view.node.id : -1

            function rectInWorkspace() {
                if (!view.workspaceRoot) return Qt.rect(0, 0, 0, 0)
                const at = canvasSlot.mapToItem(view.workspaceRoot, 0, 0)
                return Qt.rect(at.x, at.y, canvasSlot.width, canvasSlot.height)
            }
            function bodyRectInWorkspace() { return canvasSlot.rectInWorkspace() }
            function tabDropIndexFor(x) { return 0 }
            function tabInsertionX(index) { return 1 }

            function syncGeometry() {
                if (view.workspaceRoot) view.workspaceRoot.placeCanvas(canvasSlot)
            }
            onXChanged: syncGeometry()
            onYChanged: syncGeometry()
            onWidthChanged: syncGeometry()
            onHeightChanged: syncGeometry()
            onVisibleChanged: syncGeometry()

            Component.onCompleted: {
                if (!view.workspaceRoot) return
                view.workspaceRoot.registerTarget(canvasSlot)
                syncGeometry()
            }
            Component.onDestruction: {
                if (view.workspaceRoot) view.workspaceRoot.unregisterTarget(canvasSlot)
            }
        }
    }
}
