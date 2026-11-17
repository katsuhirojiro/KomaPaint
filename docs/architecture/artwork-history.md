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

There is no fixed history byte or step limit, and no step count by default.
Before painting, the recorder reserves the touched tile's rollback and redo
pixel buffers. At commit it tries compact lossless records; if optional encoding
cannot be admitted or allocated, the already-reserved snapshots remain valid as
a fallback. Major reservations consult current system memory availability and
the existing shared document budget.

Koma Settings can still impose a voluntary step cap on a constrained machine
(`performance/undoHistoryLimit`, 0 = unlimited, which is the default and the
recommended setting). The old 10-200 step cap was an arbitrary limit that
silently discarded an artist's work history; a stored value of 60 - the value
every earlier profile carried, whether or not it was chosen - migrates once to
unlimited.

When the machine cannot admit a new history charge, the oldest committed
commands are released so that the stroke being drawn is still recorded:
painting is never refused because a quota is full. The discards are reported
once per stroke through `UndoStack::historyTrimmed`, shown as a quiet notice
rather than a modal dialog, so nothing is dropped silently. Releasing material
commands unpins pigment snapshots and reclaims compositions, which is only safe
between strokes, so a material stroke that runs out of memory during its own
capture still cancels and rolls back as before. On Windows the check uses available physical
and commit memory and the OS low-memory signal; Linux uses available memory and,
when visible, the cgroup-v2 root limit. This check is advisory, not a guarantee
that an allocation will succeed, and cannot account for all memory. The existing
256 MiB per-layer pixel safeguard remains. If resources are exhausted, drawing
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
