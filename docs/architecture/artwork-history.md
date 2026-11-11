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

The temporary history safeguard is 64 MiB. Before painting, each touched tile
is conservatively charged two full tile buffers plus 512 bytes. At commit,
when resource admission succeeds, lossless compact records replace snapshots
and are charged by their retained capacity plus the metadata allowance. If
compaction cannot be admitted or allocated, the already-reserved snapshots
remain valid as a fallback. Compact replay allocates and reserves decoding
workspace before restoring tiles; failure preserves the history cursor and
artwork. This is not a complete process-memory bound. The 256 MiB per-layer
pixel safeguard remains, while the shared document budget is unlimited by
default and only partially accounts document resources. If the history is full, drawing
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

The codec integration, limit audit, accounting caveats and remaining gates are described in `compact-history-experiment.md`.
