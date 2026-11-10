# Artwork history

Each completed brush stroke is one command. The first changed pixel in each
tile causes the stroke recorder to reserve space for that tile's exact before
and after premultiplied pixel data; missing tiles are represented as absent.
No full-document image is saved. Empty strokes do not create commands, and a
new completed stroke discards redo commands. Cancelling normally restores the
before states and does not add history.

Undo and redo stage all replacement tiles and dirty invalidations before
committing any pixel changes. If staging, tracking or a layer pixel budget fails,
artwork and the history cursor remain unchanged and the user is notified. Tile
notifications invalidate the viewport after commit. A stroke may hit the
history or tile limit after applying earlier pixels: the recorded stroke is
rolled back and the user is notified. The tested rollback restores captured
pixels, tile existence and dirty state without allocating; it is not a proof
of safety for every callback or platform allocation failure. See
`stage-2-resource-foundation.md` for current boundaries.

## Memory and lifecycle

The initial history budget is 64 MiB. It conservatively charges two full tile
buffers plus 512 bytes per touched tile, even if a tile was initially absent.
This bounds recorded data but not all allocator overhead or temporary buffers
needed while staging undo/redo. The storage limit remains 256 MiB **per layer**,
not a document-wide limit. Composite caches, the viewport, undo staging, other
layers and Qt allocations are not accounted together. A shared document-wide
budget remains required before production use. If the history is full, drawing
stops rather than silently dropping undo; the Edit menu offers an explicit,
confirmed clear-history action which preserves artwork. Redo entries continue
to count until a new stroke commits or history is cleared.

History belongs to the workspace's active document and is cleared when the
document is replaced. Since layer deletion is not yet undoable, deleting a
layer clears artwork history with a warning instead of leaving dangling layer
references. Layer reordering, visibility and opacity do not alter stored pixels.
The document modified flag is conservative and stays set after undo.

Only brush pixel changes made through the recorded stroke path are commands.
Future editing operations must participate in the same history protocol;
untracked mutation of artwork can make older tile snapshots inappropriate.
Artwork can be saved and reopened as a `.koma` illustration (see
file-format.md); history is never serialized and always starts empty after
opening. There is no crash-recovery journal.

An independent compact-record prototype and the complete limit audit are described in `compact-history-experiment.md`; it is not yet used by the application.
