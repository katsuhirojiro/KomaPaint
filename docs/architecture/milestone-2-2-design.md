# Milestone 2.2: sparse tile storage and rendering design

Status: geometry, sparse storage, dirty tracking and a standalone CPU tile
compositor are implemented; viewport integration remains planned. Validate each component
before moving on; painting is not part of this milestone. The current QML blank-canvas preview is temporary and is not
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

## Implemented storage constraints

The initial store uses full-sized CPU tiles with inaccessible edge padding,
only allocates for a nontransparent write, and reclaims a tile as soon as its
last nontransparent pixel is cleared. Its default 256 MiB budget is **per
layer** and counts pixel buffers only, not map/vector overhead, snapshots,
composite caches or other layers; a document-wide budget remains future work.
Individual pixel writes report failure without modifying the store. Multi-tile
atomic edits, operation history and renderer notifications are not yet provided,
so the low-level write API must not be exposed as a finished painting tool.
Document resizing is rejected when any layer contains pixels or pending dirty
regions until a safe resize/crop operation is designed.

### Storage review and dirty tracking

The per-layer 256 MiB pixel-buffer cap is not a global cap: many layers can
exceed a safe document or system budget. It excludes allocator/map overhead,
tracker memory, undo snapshots and future caches. Introduce shared accounting
and an eviction/failure policy before real editing. A single pixel write catches
allocation errors from both tile and dirty-region allocation, and rolls back a
new tile if dirty tracking fails; deterministic allocation-failure tests cover
these paths. There is no atomicity across multiple writes and no user-facing
out-of-memory handling yet. Default-constructed layer/document allocation and
other Qt/UI allocations are not handled by this low-level guarantee.

The store validates premultiplied pixels (zero alpha must have zero RGB), keeps
a count of nonzero pixels, and reclaims only when the count reaches zero.
Tests cover recolouring, clearing one of several pixels, and clearing the last.
The storage/geometry/tracker library uses only the C++ standard library and
has no renderer or QML dependency.

Successful pixel changes mark a tile-local half-open rectangle; no-op, rejected
or failed writes do not. The tracker can also mark clipped multi-tile rectangles
and unions repeated regions conservatively. Tracking survives tile reclamation.
It is bounded to 65,536 pending tiles per layer; when that limit or allocation
fails, a pixel write is rejected before changing artwork. Multi-tile marking
uses a temporary copy for all-or-nothing failure behavior, so large batches
can temporarily use more memory. Pending regions are single-consumer, single-threaded state. Each store has a
monotonic dirty version; acknowledgments with an old version do not clear
pending changes. Version exhaustion rejects writes rather than wrapping. There
are no per-tile revisions, stroke bounds or multi-consumer notifications yet.

### CPU compositing and invalidation protocol

The UI-independent compositor accepts a top-first layer list (matching the
layer panel), transparent or premultiplied background and a tile coordinate.
It composites back-to-front, source-over, using premultiplied ARGB and opacity
quantized to 8 bits. The result is a separate edge-clipped CPU buffer; absent
tiles are read as transparent and do not allocate artwork. Sources and output
are not shared for mutation. Tile views are valid only during a synchronous
call with no source mutation.

`CompositeInvalidation::prepare` captures ordered layer identity, visibility,
opacity, background, each store's dirty version and the union of dirty tile
coordinates. A changed configuration requires the consumer to discard its whole
display cache before calling `accept`; this is lazy cache invalidation, not
full-canvas recomposition. The consumer composites requested dirty tiles and
publishes the result, then calls `acknowledge` per successfully processed tile.
It must discard output if acknowledgment rejects stale versions/configuration.
Unprocessed and off-screen tiles remain pending. A newer pixel change prevents
acknowledgment of the older snapshot; no failed rendering may be acknowledged.
Allocation errors preparing a plan propagate without clearing dirty state. The
consumer must supply current layer properties every time, reset the tracker on
document replacement, and keep the stores alive for the plan; this is a
single-threaded single-consumer protocol, not an observer or a viewport.
Layer property changes are detected on the next prepare, not pushed as signals.
Multiple independent caches or render threads require a new revision/subscriber
scheme before integration. No display cache or viewport is implemented here.

### Resource budget and benchmarks: scheduled before editing ships

Add document-wide accounting shared by every layer, history and composite/display
caches before brush editing is exposed, with configurable limits, allocation
failure handling and safe eviction. Per-layer caps alone are insufficient.
Benchmark sparse and populated documents across canvas sizes and layer counts,
measuring memory, dirty-tile compositing time, stroke latency and CPU usage;
add navigation/zoom and pan measurements with the viewport. Establish repeatable
workloads and record hardware/configuration before optimizing. Test the CPU
fallback on genuinely low-end Windows hardware, including a machine without a
dedicated GPU. Future display caches must be bounded; optional costly features
and any future 3D viewport need adjustable quality and explicit resource limits.
No performance or low-end hardware claims are made until measured.
