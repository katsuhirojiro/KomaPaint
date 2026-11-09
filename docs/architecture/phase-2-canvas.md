# Phase 2: canvas direction

Status: design direction, not an implementation of tile storage or painting.

A document owns its layer model and background metadata; the workspace exposes
its current document and changes the model together when creating a document.
Saving and reopening illustrations now exists as `.koma` files (see
file-format.md); replacing or closing a modified document prompts the user.

## Storage and drawing

Use sparse, document-bounded tile maps on individual raster layers. Missing
entries mean transparent pixels, not allocated blank images. Start evaluation
with 256 × 256 pixels in premultiplied 8-bit RGBA (256 KiB per allocated
full tile); edge tiles are clipped to the document. 128-pixel tiles reduce
small-edit waste and upload size but multiply lookup/compositing overhead;
512-pixel tiles reduce bookkeeping but make tiny edits and undo expensive.
Measure real workloads before fixing this as a permanent choice. Check bounds
and arithmetic before allocation, report allocation failure, and impose a
memory budget; sparse allocation does not make a fully painted huge document
cheap. Keep the background as metadata, not a full-size bitmap.

Artwork data is authoritative on the CPU, independent of QML, with explicit
ownership and no untracked mutable access. Drawing operations compute affected
tile coordinates and clipped dirty rectangles, including strokes crossing tile
edges. Each changed tile gets a revision/invalidation; compositing combines
visible layers in order with opacity, using premultiplied alpha. Visibility,
opacity and ordering changes invalidate the affected composites as well.

The view should request visible tiles at the current transform, maintain a
bounded, evictable composite/texture cache keyed by tile and revision, and
update only invalidated visible tiles. Cache eviction must never discard the
authoritative layer pixels. A Qt Quick scene-graph renderer could upload tile
textures for GPU compositing; a CPU compositor and software rendering path
must remain possible. Do not use a single QML Canvas image as the artwork or
an enormous texture. Define synchronization and immutable snapshots before
moving rasterization or texture uploads across threads; initially keep document
mutation on the GUI thread.

Undo commands should capture before/after data only for touched tiles, with a
byte budget, and remove newly created tiles on undo. Define transaction and
failure behavior before edits are exposed. Document serialization must record
layer order, properties and sparse tile data with versioning and defensive
size limits. Zoom and pan use one invertible document-to-view transform for
both rendering and tablet/mouse coordinates.

## Current limitations

The blank preview is still a bounded QML placeholder, not the tile renderer.
Its size is capped to avoid a huge QML surface, so displayed zoom is not yet
accurate. Existing layer UI manipulates metadata only; history has labels, not
reversible artwork commands. There is no paint, eraser, tablet input, save or
export. Milestone 2.2 must replace the preview and establish tested tile
storage and rendering before brush work starts.
