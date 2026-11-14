# Docking workspace

The interface is built from panels that dock around the canvas: they can be
tabbed together, split, floated into their own windows, closed and reopened,
and the arrangement survives a restart. The same code serves the drawing
workspace and any window added later, because nothing in it knows what a
panel contains.

## The model: `DockModel`

`src/app/DockModel.h` holds the layout as one tree of nodes:

```
split   a row (horizontal) or column (vertical) of children, each with a fraction
tabs    a tab group: one or more panels, one of them active, optionally collapsed
canvas  a slot that marks where the drawing surface goes
```

Every panel id appears at most once in the tree, at most once in a floating
window, or is hidden. The default tree is

```
horizontal {0.185, 0.635, 0.180}
├── vertical {0.18, 0.57, 0.25}   subtool · toolproperty · brushsize
├── canvas
└── vertical {0.45, 0.27, 0.28}   layers · properties · color
```

with `timeline`, `history`, `assets` and `xsheet` hidden.

Panels are described only by `DockModel::panelTable()`: id, title, home side
(`left`, `right`, `bottom`) and whether it is visible by default. Adding a
panel is one table entry plus one QML component; menus, layout persistence,
presets, the missing-panel fallback and every drag target work from that
table, so no other code needs to change.

The model exposes the tree as a `QVariantMap` (`tree`, `nodeById`) and mutates
it through invokable methods that return `false` when a move is not possible:
`moveToGroup`, `moveToTabs`, `dockToNodeSide`, `dockToSide`,
`dockToWorkspace`, `floatPanel`, `dockBack`, `setActive`, `setCollapsed`,
`setFraction`, `hidePanel`/`showPanel`/`togglePanel`, `resetToDefault`, plus
the dock-area calls described below. Query methods (`groupOf`, `floatingIdOf`,
`isVisible`, `isCollapsed`, `isFloating`, `fractionsOf`, `visiblePanels`) let
the QML layer and the tests inspect the result without parsing the tree.

Signals are split by how much they invalidate:

* `layoutChanged()` — the shape changed (splits, groups, order, visibility, a
  floating window appeared or disappeared). The QML layer rebuilds from it.
* `activeTabChanged(panelId)` — only the selected tab changed, so panels are
  not rebuilt.
* `fractionsChanged(splitId)` — only one split's proportions changed. Dragging
  a handle re-sizes two items; it must not recreate the panels inside them.
* `floatingGeometryChanged(id)` — a floating window moved or resized.

## The view: `ui/docking/`

| File | Role |
| --- | --- |
| `DockWorkspace.qml` | Owns the drop targets, the drag state and the canvas surface; hosts the tree and the floating windows. |
| `DockNodeView.qml` | One node: picks split, group or canvas-slot for the node kind. |
| `DockSplit.qml` | Row/column of children sized by the node's fractions, with splitters between them. |
| `DockGroup.qml` | Tab strip, close and collapse buttons, panel menu, panel body. |
| `DockFloatingWindow.qml` | A panel in its own `Window`, docking back when closed. |
| `DockSplitter.qml`, `DockFrame.qml` | Handle and the framed background used by groups and the canvas. |
| `DockAreaStrip.qml` | The thin strip a collapsed dock area leaves behind. |

### The canvas lives beside the tree

`DockWorkspace` loads the canvas component itself (`canvasHost`) and the
tree's canvas node is only a *slot*: `DockNodeView` reports the slot's
position and size and the workspace moves the surface there
(`placeCanvas`). The drawing surface therefore never belongs to the part of
the tree that a layout change rebuilds — closing a panel or dragging a
splitter cannot destroy the canvas, its tiles or its zoom.

### Sizing

Groups publish a minimum: a body width of 118 px and a header plus the
panel's `preferredHeight`. A panel that publishes `preferredHeight` reports
its **content** height only — the frame adds the header — and panels without
one are assumed to fit 64 px of content (their contents scroll). Splits
combine those minimums up the tree (`minimumOf`: side by side they add up,
across the row the widest child sets the floor), honour them while
distributing the space and, when the minimums exceed the space available,
scale them all down proportionally (`fitMinimums`) instead of starving one
child. Fractions are kept in the model, so a splitter drag survives a tab
switch, a close and a restart.

A panel's preferred size is only known once its content is loaded, and it
changes when another tab becomes active or a group collapses. Bindings are
therefore not enough: the group asks the workspace to re-run the whole
layout (`relayoutTree()` → `DockSplit.relayoutDeep()`) whenever its content
item or its collapsed state changes. Without that pass the first layout
runs before any panel exists, and a panel with tall content (the Brush Size
grid) is clipped by the fractions it was given at start-up.

### Dock areas

Panels live in *dock areas*: the bands around the canvas. An area is not a
node — it is the chain of splits that leads from the root to the canvas. Every
split on that chain has two branches, and whichever branch does not contain
the canvas is a side: the first step off a horizontal split gives `left` or
`right`, off a vertical one `top` or `bottom`. `DockModel::collectAreaRefs()`
walks that chain into `AreaRef { side, nodeId, splitId, childIndex }`, and
`areas()` reports one entry per band: `{ side, nodeId, splitId, collapsed,
panels }`. Sides are case-insensitive, and every node on the same side belongs
to the same band — the left column collapses as a whole, not one of its rows.

The canvas only has to be the *first* child of its split, which is what keeps
the walk simple: `childContainingCanvas()` (QML: `containsCanvas` /
`canvasBranchIndex`) picks the branch at each step. Wrapping the canvas in a
split therefore still works. Docking a panel to `bottom` puts the canvas in a
vertical split `[canvas | timeline]` inside the root row; the side areas keep
their sides, and the new band is found as `bottom`. A second panel sent to the
same edge joins the band that is already there as a tab (`dockBesideCanvas`),
so one side never grows two strips side by side.

Two queries keep the view honest:

* `areaSideOf(nodeId)` — the band a node belongs to, walked up from the node.
  Any group inside the left column reports `left`.
* `areaSideOfChild(nodeId)` — the band **only if the node is itself an area**,
  i.e. a direct child of a split on the chain. This is the single source for
  "draw a strip here". It exists because the tempting shortcut — deriving the
  side from geometry, or from `areaSideOf` — gives every node buried inside a
  band a side, so each inner column draws its own strip and the collapsed
  band fills with hollow strips instead of one 14 px line.

Collapsing is a **hide**, never a removal. `setAreaCollapsed(side, true)`
records the side in `m_collapsedAreas` and emits `layoutChanged()`; the tree,
the tab groups, the active tabs and every splitter fraction stay exactly as
they were, and the panels stay loaded. `isAreaCollapsed` answers from the model
and additionally requires the band to still exist, so a side that has no
panels left is never reported — or persisted — as collapsed.

In the view, `DockSplit` asks `model.areaSideOfChild()` for both of its
children and hands each one `areaSide` plus `areaCollapsed`. `DockNodeView`
keeps its content `Loader` alive and only flips `visible`, so expanding an area
restores the same items, scroll positions and canvas tile. The collapsed branch
shrinks to a 14 px strip (`DockAreaStrip`, `dockAreaStrip_<side>`) and the
canvas absorbs the freed pixels: `DockSplit` sizes children from the
fractions, then takes the strip width out of whichever child is the collapsed
area's own branch.

The chevrons come in pairs. `DockSplitter` draws `dockAreaControl_<side>` on
the handle that borders an area (`«`/`»`/`▴`/`▾`), which collapses the band;
the strip draws the reversed glyph, which brings it back. Both are ordinary
`MouseArea`s with hover and pressed states from the theme tokens and an
`Accessible.name`, and the strip is disabled while a drag is in flight
(`workspaceRoot.dragging`) so a tab dragged past a collapsed band cannot
reveal it by accident.

Any change that moves a split's borders has to be followed by a layout pass:
a nested split only re-runs its own layout when *its* size changes, and hiding
a neighbouring band does not change it. `DockWorkspace.qml` therefore
connects `model.layoutChanged` to `relayoutTree()`; without that connection a
collapse shrinks the band to a strip the model knows about but the inner
splits never notice, and the canvas does not grow.

Docking into a collapsed band reopens it (`revealAreaOf`), for the same reason:
the panel has to be placed *and* visible, or the drop would silently succeed
into a strip nobody can see.

### Dragging

A tab press followed by a 6 px move starts a drag of the group's active
panel. While dragging, the workspace asks every registered target
(`dockBody<id>` / canvas slot) what dropping at the pointer would do and draws
`dockDropPreview` over that half of the target; the model is untouched until
the release. Dropping

* on a group's centre or tab strip merges into that group as a tab,
* near an edge splits the target group on that side,
* outside the workspace floats the panel into a new window at the pointer.

A floating window that is closed with its own close button docks back into
the workspace rather than losing the panel.

A collapsed band is not a drop *target* while it is a strip: dragging a tab
near it does not split it, and dropping into the band through the menu or
`dockToWorkspace()` opens it. Splitting and dragging inside a band that is
collapsed is impossible by construction, because its content is not visible —
the tree cannot be corrupted by a drag that never starts.

The object names (`dockGroup<id>`, `dockBody<id>`, `dockTab_<panel>`,
`dockTabClose_<panel>`, `dockCollapse<id>`, `dockHandle<split>_<index>`,
`dockDropPreview`, `dockFloatingWindow<id>`) exist so the UI tests can drive
the real widgets with real mouse events; `TestDockUi` does exactly that, and
it is the reason the close button carries `z: 1` — without it the tab's own
drag area swallows the click.

## Persistence

With `setSettings()` attached, the model loads `workspace/layout` (JSON) on
start-up and writes it back 350 ms after the last change. Named presets live
in `workspace/presets` and are saved, applied and deleted through
`savePreset`/`applyPreset`/`deletePreset`.

The stored JSON carries the tree, the floating windows, the hidden panels,
their remembered placements (`stash`, see below) and `collapsedAreas`:

```json
{ "root": {...}, "floating": [...], "hidden": ["timeline", "history"],
  "collapsedAreas": ["left"],
  "stash": [ { "panel": "properties", "group": 7, "index": 1,
               "anchor": 9, "anchorSide": "top", "fraction": 0.27 } ] }
```

A closed panel remembers where it was. If its group survives with other
panels in it, the panel simply returns to that group and index
(`moveToTabs`). If it was alone in its group, that group is gone, so
`rememberPlacement()` stores the neighbour it sat beside (`anchor`,
`anchorSide`) and the share of the split it occupied; `showPanel()` rebuilds
it with `restoreBesideAnchor()`. A placement whose anchor no longer exists
falls back to the panel's home side, so a stale stash can never make a panel
unreachable.

Restoring is defensive, because a stored layout may come from another build:

* unknown panels are dropped, duplicate ids keep their first occurrence and
  empty groups are pruned;
* a stored side in `collapsedAreas` that this layout does not have (or that
  became empty) is dropped, and stash entries for panels that are not hidden
  are ignored;
* a panel that exists in this build but is missing from the stored layout is
  put back at its home side, so it can never become unreachable;
* a stored panel id that no longer exists simply does not come back;
* node ids are re-issued when they collide, keeping splitter bindings
  unambiguous.

A stored layout that fails to parse leaves the workspace on its default
arrangement.

## `PanelManager`

`PanelManager` is the façade the rest of the application uses: it owns the
`DockModel` (or is handed the application's shared one), keeps the per-panel
"visible" flags the menus bind to, and forwards `toggle`, `setVisible` and
`resetLayout` to the model, together with the dock-area calls the QML menus
use (`areas`, `collapsedAreas`, `hasArea`, `areaSideOf`, `areaSideOfChild`,
`isAreaCollapsed`, `setAreaCollapsed`, `toggleArea`). Menu state is derived
from the model rather than stored twice.

## Tests

* `TestDocking.cpp` — the model on its own: default tree, moves, splits,
  floating, collapse, hiding, presets, persistence and the defensive restore;
  plus the dock areas (which sides exist, that a band under the canvas is found
  without losing the side bands), that collapsing touches nothing but
  `collapsedAreas`, that area state and placements survive a restart, and that
  a hidden panel returns to its own slot.
* `TestDockUi.cpp` — the same behaviour through the real window with real
  mouse events: tab merge, edge split, float and dock back, close and reopen
  from the menu, collapse, reset, a stored layout that mentions a panel this
  build does not have, collapsing the left area (strip appears, the canvas
  grows, the tabs stay loaded and their fractions unchanged, clicking the strip
  restores the exact width), the same for a band under the canvas, and a
  restart that keeps the collapsed band and a closed panel.
* `TestWorkspaceUi.cpp` — the layout (including splitter drags) is still there
  on the next start, next to the session and recovery paths.
* `TestBrushUi.cpp` — closing the brush panels keeps the canvas usable and
  `resetLayout()` brings every panel back.

Because the model is a `QObject` on the app side, the UI tests attach their
own `Settings` to it. That object has to outlive the panel manager: tearing
the dock down writes preferences, so a test fixture must declare its
`Settings` before the manager (see `TestDockUi.cpp`).
