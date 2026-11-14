import QtQuick
import "../themes"

// One split: a row or a column of dock nodes with handles in between.
//
// Children are positioned by hand (not by a layout) so a handle drag can move
// the preview live without touching the model; the model is only updated once,
// on release. Positions come from the fractions in the layout tree, which are
// shares of the space left over after the handles.
Item {
    id: split

    property var workspaceRoot: null
    property var node: ({})
    readonly property var kids: node.children !== undefined ? node.children : []
    readonly property bool horizontal: node.orientation === "horizontal"
    readonly property int splitId: node.id !== undefined ? node.id : -1
    readonly property int handleWidth: 6
    readonly property int minPixels: 60

    // Width/height of a collapsed dock area: a strip just big enough for the
    // chevron that brings the area back.
    readonly property int areaStripSize: 14
    readonly property var model: workspaceRoot ? workspaceRoot.model : null

    property var liveFractions: []
    property var startFractions: []
    property real startPointer: Number.NaN
    property bool resizing: false
    property int activeHandle: -1

    objectName: "dockSplit" + splitId

    function resetFractions() {
        const list = []
        const n = kids.length
        for (let i = 0; i < n; ++i)
            list.push(kids[i].fraction !== undefined ? kids[i].fraction : 1 / Math.max(1, n))
        liveFractions = list
        relayout()
    }

    // The shares live in the model; `liveFractions` only drifts while a handle
    // is being dragged. Everything else changes the numbers in the model
    // without the view hearing a signal (the node data reads through, so a
    // reset, a restored layout or a moved panel can change `kids[i].fraction`
    // under an unchanged node object), so every layout pass starts by taking
    // the model's shares back unless the user is mid-drag.
    function syncFractions() {
        const n = kids.length
        let stale = liveFractions.length !== n
        for (let i = 0; !stale && i < n; ++i) {
            const wanted = kids[i] && kids[i].fraction !== undefined
                           ? kids[i].fraction : 1 / Math.max(1, n)
            const have = liveFractions[i] === undefined ? 0 : liveFractions[i]
            stale = Math.abs(have - wanted) > 1e-6
        }
        if (!stale) return false
        resetFractions()
        return true
    }

    function usableSize() {
        const n = kids.length
        return Math.max(0, (horizontal ? width : height) - Math.max(0, n - 1) * handleWidth)
    }

    function containsCanvas(node) {
        if (!node) return false
        if (node.kind === "canvas") return true
        const children = node.children !== undefined ? node.children : []
        for (let i = 0; i < children.length; ++i)
            if (split.containsCanvas(children[i].node)) return true
        return false
    }

    // The child whose subtree holds the drawing surface. It can be the canvas
    // node itself or a split that wraps a panel around it (the timeline under
    // the canvas, for instance).
    function canvasBranchIndex() {
        for (let i = 0; i < kids.length; ++i)
            if (split.containsCanvas(kids[i] ? kids[i].node : null)) return i
        return -1
    }

    // Which dock area a child belongs to, as the model defines it: only a child
    // that *is* an area gets a side, so exactly one strip is drawn per area.
    function areaSideOfChild(index) {
        if (!split.model || index < 0 || index >= kids.length) return ""
        const node = kids[index] ? kids[index].node : null
        if (!node || node.id === undefined) return ""
        return split.model.areaSideOfChild(node.id)
    }

    // Of all the columns on one side, the one next to the canvas carries the
    // strip when the area is collapsed; the rest shrink to nothing.
    function stripChildIndex(side) {
        const canvas = split.canvasBranchIndex()
        let best = -1
        let bestDistance = 0
        for (let i = 0; i < kids.length; ++i) {
            if (split.areaSideOfChild(i) !== side) continue
            const distance = Math.abs(i - canvas)
            if (best < 0 || distance < bestDistance) {
                best = i
                bestDistance = distance
            }
        }
        return best
    }

    // -1: not a collapsed child, 0: collapsed and hidden, 1: collapsed strip.
    function areaStripFor(index) {
        const side = split.areaSideOfChild(index)
        if (!side || !split.model) return -1
        if (!split.model.isAreaCollapsed(side)) return -1
        return index === split.stripChildIndex(side) ? 1 : 0
    }

    // The dock area a handle can collapse: the child on the canvas side of it.
    function areaSideOfHandle(index) {
        const canvas = split.canvasBranchIndex()
        if (canvas < 0) return ""
        if (index + 1 === canvas) return split.areaSideOfChild(index)
        if (index === canvas) return split.areaSideOfChild(index + 1)
        return ""
    }

    function toggleArea(side) {
        if (side && split.model) split.model.toggleArea(side)
    }

    // The size a child asks for as a minimum (its content's preferred size,
    // passed up through DockNodeView). Asked of the child every time, so a
    // panel that changes size — a tab switch, a newly loaded panel — is taken
    // into account by the next relayout without caching anything.
    function childMinimum(index) {
        return childMinimumOf(index, split.horizontal)
    }

    function childMinimumOf(index, horizontal) {
        const loader = nodeRepeater.itemAt(index)
        const child = loader ? loader.item : null
        if (!child) return 0
        if (typeof child.minimumOf === "function") return child.minimumOf(horizontal)
        const value = horizontal ? child.minimumBodyWidth : child.minimumBodyHeight
        return value !== undefined && value > 0 ? value : 0
    }

    // What this split needs as a whole, so a parent split can honour it: along
    // the row the children sit side by side, so their minimums add up; across
    // the row the widest child sets the floor.
    function minimumOf(horizontal) {
        let sum = 0
        let widest = 0
        for (let i = 0; i < kids.length; ++i) {
            const value = childMinimumOf(i, horizontal)
            sum += value
            widest = Math.max(widest, value)
        }
        const sameAxis = horizontal === split.horizontal
        return sameAxis ? sum + Math.max(0, kids.length - 1) * handleWidth : widest
    }

    function relayoutFromRoot() {
        if (workspaceRoot && typeof workspaceRoot.relayoutTree === "function") workspaceRoot.relayoutTree()
        else relayout()
    }

    // Re-lay out this split and every split below it. The parent has to go
    // first: its children only get their share of the space after the minimums
    // are known.
    function relayoutDeep() {
        relayout()
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const loader = nodeRepeater.itemAt(i)
            const child = loader ? loader.item : null
            if (child && typeof child.relayoutDeep === "function") child.relayoutDeep()
        }
    }

    // Fractions alone can squeeze a panel below its content. Sizes are raised
    // to the children's minimums and the difference is taken from whoever has
    // slack, so the visible layout never hides a panel's controls while there
    // is room for them. The sizes always add up to the available space.
    function fitMinimums(sizes, available) {
        const n = sizes.length
        const mins = []
        let minimumSum = 0
        for (let i = 0; i < n; ++i) {
            mins.push(childMinimum(i))
            minimumSum += mins[i]
        }
        const scale = minimumSum > available && minimumSum > 0 ? available / minimumSum : 1
        let deficit = 0
        for (let i = 0; i < n; ++i) {
            const floor = mins[i] * scale
            if (sizes[i] < floor) {
                deficit += floor - sizes[i]
                sizes[i] = floor
            }
        }
        for (let pass = 0; pass < 4 && deficit > 0.5; ++pass) {
            let slack = 0
            for (let i = 0; i < n; ++i) slack += Math.max(0, sizes[i] - mins[i] * scale)
            if (slack <= 0) break
            for (let i = 0; i < n; ++i) {
                const room = Math.max(0, sizes[i] - mins[i] * scale)
                if (room > 0) sizes[i] -= Math.min(room, deficit * room / slack)
            }
            deficit = 0
            for (let i = 0; i < n; ++i) deficit += Math.max(0, mins[i] * scale - sizes[i])
        }
    }

    function relayout() {
        const n = kids.length
        if (n === 0) return
        // resetFractions() lays out again, so stop here when it had to resync.
        if (!resizing && syncFractions()) return
        const total = split.horizontal ? width : height
        const available = usableSize()
        const sizes = []
        let sum = 0
        for (let i = 0; i < n; ++i) {
            const fraction = liveFractions[i] !== undefined ? liveFractions[i] : 1 / n
            sizes.push(fraction * available)
            sum += fraction * available
        }
        if (sum > 0) {
            for (let i = 0; i < n; ++i) sizes[i] = sizes[i] * available / sum
        } else {
            for (let i = 0; i < n; ++i) sizes[i] = available / n
        }
        fitMinimums(sizes, available)

        // A collapsed area keeps a strip and hides its panels; they stay loaded,
        // so expanding the area brings back the same arrangement.
        const strips = []
        for (let i = 0; i < n; ++i) {
            const strip = areaStripFor(i)
            strips.push(strip)
            if (strip === 1) sizes[i] = Math.min(areaStripSize, available)
            else if (strip === 0) sizes[i] = 0
        }

        // Round to whole pixels and let the canvas take up the leftover, so the
        // row always ends exactly at the edge even when a strip is in it.
        const handleTotal = Math.max(0, n - 1) * handleWidth
        for (let i = 0; i < n; ++i) sizes[i] = Math.round(sizes[i])
        let used = 0
        for (let i = 0; i < n; ++i) used += sizes[i]
        let absorber = canvasBranchIndex()
        if (absorber < 0 || strips[absorber] >= 0) absorber = n - 1
        sizes[absorber] = Math.max(0, sizes[absorber] + (total - handleTotal - used))

        let position = 0
        for (let i = 0; i < n; ++i) {
            const loader = nodeRepeater.itemAt(i)
            const size = sizes[i]
            if (loader) {
                // The child is a DockNodeView inside this Loader; the strip and
                // the collapsed state are its business, the geometry is ours.
                if (loader.item) {
                    loader.item.areaSide = areaSideOfChild(i)
                    loader.item.areaCollapsed = strips[i] >= 0
                }
                loader.visible = strips[i] !== 0
                if (split.horizontal) {
                    loader.x = position; loader.y = 0; loader.width = size; loader.height = height
                } else {
                    loader.x = 0; loader.y = position; loader.width = width; loader.height = size
                }
            }
            position += size + (i < n - 1 ? handleWidth : 0)
        }
        positionHandles()
    }

    function positionHandles() {
        for (let i = 0; i < handleRepeater.count; ++i) {
            const handle = handleRepeater.itemAt(i)
            const left = nodeRepeater.itemAt(i)
            if (!handle || !left) continue
            handle.collapseSide = split.areaSideOfHandle(i)
            handle.areaCollapsed = handle.collapseSide.length > 0 && split.model
                                   && split.model.isAreaCollapsed(handle.collapseSide)
            if (horizontal) {
                handle.x = left.x + left.width
                handle.y = 0
                handle.width = handleWidth
                handle.height = height
            } else {
                handle.x = 0
                handle.y = left.y + left.height
                handle.width = width
                handle.height = handleWidth
            }
        }
    }

    function beginResize(index) {
        if (index < 0 || index + 1 >= kids.length) return
        startFractions = liveFractions.slice(0, liveFractions.length)
        startPointer = Number.NaN
        resizing = true
        activeHandle = index
    }

    function resizeTo(index, at) {
        if (!resizing || index !== activeHandle) return
        if (index + 1 >= kids.length) return
        const usable = usableSize()
        if (usable <= 0) return
        if (isNaN(startPointer)) startPointer = at
        const pair = (startFractions[index] || 0) + (startFractions[index + 1] || 0)
        const minA = Math.min(0.45, Math.max(minPixels, split.childMinimum(index)) / usable)
        const minB = Math.min(0.45, Math.max(minPixels, split.childMinimum(index + 1)) / usable)
        let fraction = (startFractions[index] || 0) + (at - startPointer) / usable
        fraction = Math.max(minA, Math.min(pair - minB, fraction))
        const list = liveFractions.slice(0, liveFractions.length)
        while (list.length < kids.length) list.push(0)
        list[index] = fraction
        list[index + 1] = pair - fraction
        liveFractions = list
    }

    function endResize(index) {
        if (!resizing) return
        resizing = false
        activeHandle = -1
        if (!workspaceRoot || !workspaceRoot.model || splitId <= 0) return
        const needed = Math.round(Math.max(minPixels, split.childMinimum(index),
                                            split.childMinimum(index + 1)))
        workspaceRoot.model.setFraction(splitId, index, liveFractions[index] || 0,
                                        Math.round(usableSize()), needed)
    }


    onWidthChanged: relayout()
    onHeightChanged: relayout()
    onLiveFractionsChanged: relayout()
    // Every layout change hands down a fresh node: read the fractions and the
    // collapsed areas back from it.
    onNodeChanged: resetFractions()
    Component.onCompleted: resetFractions()

    // Children are loaded from the file so that DockNodeView can reference
    // DockSplit directly without the two files forming a type-resolution
    // cycle; only this direction is resolved at run time.
    Repeater {
        id: nodeRepeater
        model: split.kids
        delegate: Loader {
            id: childLoader
            property var childData: modelData
            source: Qt.resolvedUrl("DockNodeView.qml")
            onLoaded: {
                item.workspaceRoot = split.workspaceRoot
                item.node = (childLoader.childData && childLoader.childData.node !== undefined)
                                ? childLoader.childData.node : ({})
                // The child only exists now: place it and, once it knows its own
                // minimum size, give every child its space.
                split.relayoutFromRoot()
            }
        }
    }

    Repeater {
        id: handleRepeater
        model: Math.max(0, split.kids.length - 1)
        delegate: DockSplitter {
            owner: split
            objectName: "dockHandle" + split.splitId + "_" + index
        }
        onCountChanged: split.positionHandles()
    }
}
