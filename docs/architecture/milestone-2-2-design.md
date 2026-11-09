# Milestone 2.2: sparse tile storage and rendering design

Status: proposed implementation plan. Neither tile storage nor the renderer
exists yet. Validate each component before moving on; painting is not part of
this milestone. The current QML blank-canvas preview is temporary and is not
an artwork store or production renderer.

## Boundaries and ownership

`WorkspaceState` owns the current `CanvasDocument`, which owns the layer
model and layers. Each raster layer will own its own sparse tile store; tile
pixels are authoritative artwork, not QML or renderer state. A tile store has
no viewport or UI dependency. Document dimensions and background remain document
metadata. A compositor reads layer snapshots and creates separate disposable
composite results. The view owns its bounded display/texture cache and can
throw that cache away at any time. All mutation initially occurs on the GUI
thread; no shared writable pixel access across threads. Define immutable or
copy-on-write snapshots and synchronization before introducing worker threads.

The current `LayerModel` is a Qt list model and also owns `Layer` objects; it
is not yet a UI-independent domain collection. Keep this limitation explicit:
attach storage to layers without duplicating artwork in QML, and evaluate a
separate domain collection if needed rather than introducing two sources of
truth. The current history stack holds descriptions only; it is not undo.

## Tile allocation and lifetime

Use a sparse map keyed by integer tile coordinates per raster layer. Missing
tiles mean entirely transparent; reading a missing tile must not allocate it.
A write creates a transparent tile only when needed. Full tiles initially use
premultiplied 8-bit RGBA: 256 × 256 × 4 bytes = 256 KiB, excluding overhead.
The internal tile dimension starts at 256 and is configurable in the storage
implementation, with tests using smaller dimensions. Do not treat it as a
permanent native-file-format assumption. Compare 128 and 512 using measured
workloads later.

Tiles on document edges are logically clipped to the document; either allocate
full tiles with inaccessible padding or edge-sized buffers, chosen explicitly
at implementation and tested. Tiles outside bounds never become artwork. Empty
tiles can be released after an operation verifies they are fully transparent,
including after erase or undo. Do not scan every tile on each input sample;
reclaim at transaction boundaries or when already known empty. A layer's
destruction releases its tiles. Allocation failure must not silently produce a
partial operation: plan atomic operation/rollback and a user-visible error.
Enforce limits with checked arithmetic and a memory budget before exposing
painting; sparse storage does not make a fully painted large canvas cheap.

## Coordinates and dirty tracking

Document pixel space has origin at the top left, x increasing right and y
down. Pixel rectangles are half-open: [left, right) × [top, bottom). For a
nonnegative document pixel (x, y) and tile size T, tile coordinate is
(x / T, y / T), local pixel coordinate is (x % T, y % T), using integer
division. Clip geometry to [0, width) × [0, height) before conversion; do not
use C++ truncating division on negative input as if it were floor division.
For a nonempty clipped rectangle, the final tile index uses (right - 1) / T
and (bottom - 1) / T. This avoids spuriously invalidating an adjacent tile
when an edge lies exactly on a boundary.

For each operation, compute the conservative pixel bounds of each stroke
segment including radius and antialiasing, intersect with the document, split
that area into all intersected tiles, and union clipped local dirty rectangles
per tile. Segments—not just input points—must be considered so fast movement
and a brush overlapping a boundary cannot leave gaps or miss invalidation.
Only actual mutations advance a tile revision. Add tests for exact boundaries,
corners, negative/out-of-document bounds, edge tiles, multi-tile segments and
large coordinates. Input interpolation and brush rendering belong to later
milestones, but dirty-region geometry can be tested independently now.

## Compositing and presentation

For a requested document tile, composite the background and visible raster
layers in a documented back-to-front order, using source-over and each layer's
opacity with premultiplied alpha. The transparent background is alpha zero;
the checkerboard is view decoration, never artwork or exported pixels.
Compositing is read-only with respect to source tiles. Missing tiles contribute
transparent pixels. Layer visibility, opacity, ordering, deletion and background
changes invalidate the relevant composite even if pixel revisions are unchanged.
Test overlapping colours, partial transparency, order, missing tiles and source
immutability. Do not assume the current UI row order until it is explicitly
defined and checked against the layer panel.

Initially use a testable CPU compositor; a Qt Quick view can then upload
composited tiles for visible regions, with a software-capable path. Cache
entries must account for all contributing layer/document revisions and be
bounded and evictable. Only invalidated visible tiles need refreshing, but
invalidations must remain effective for tiles not currently visible. Avoid
creating a document-sized QML Canvas or one full-canvas GPU texture. Zoom,
pan and coordinate transforms will be implemented and tested separately in
2.3; do not infer correct navigation from the current capped preview.

## Undo and redo

History must represent actual document operations, not interface selections or
text labels. An editing transaction records each touched tile's pre-operation
state once, including whether the tile was absent, and its resulting state (or
an equivalent reversible delta). Undo/redo restores those states, including
removing tiles that were previously absent, and issues the same invalidation
as an edit. Clear redo on a new edit. Set a byte-based history budget; define
pruning, allocation failure and atomic rollback so the document and history
cannot diverge. Document replacement resets history; before editing is exposed,
add dirty-document protection for replace/close and eventually save.

## Independently testable increments

1. **Geometry:** configurable tile-grid coordinate conversion and clipping,
   including boundary and overflow tests. No rendering or brush.
2. **Storage:** sparse per-layer allocation, reads/writes, bounds checks,
   transparent reclamation, ownership and allocation-failure behavior.
3. **Invalidation:** per-tile dirty-region accumulation and revision changes;
   test crossing multiple tiles without relying on a brush implementation.
4. **CPU compositor:** layer ordering, alpha, visibility and background;
   prove it does not alter source data and invalidates on property changes.
5. **Viewport adapter:** visible tile requests, bounded cache and partial
   refresh, with a software path; replace the placeholder only after tests.

Build and run the existing suite and new component tests for each increment.
Do not start the brush or claim undo functionality as part of this milestone.
