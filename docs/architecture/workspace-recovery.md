# Persistent workspace and automatic project recovery

This document describes the recovery system implemented in
`src/app/WorkspaceState.*` and `src/app/DocumentRecovery.*`. It is **session
recovery**, not disk-backed undo: undo/redo remains in RAM and is never
restored after a restart.

## Goals and non-goals

* Reopen the previously open project tabs, including projects that were never
  saved, after a crash or a normal shutdown.
* Never overwrite the user's original `.koma` project from a recovery
  snapshot.
* Keep recovery data for every native document type behind one shared
  interface, so future animation/comic types do not need a redesign.
* Not a storage-management framework, not backup versioning, and not a
  journal of undo history.

## Data model

Each open tab owns one `CanvasDocument` and one `UndoStack`. `WorkspaceState`
assigns the tab a UUID immediately when the document enters the workspace
(New, Open, or restore). That UUID, the document type id, the optional source
path and the type adapter are the tab's recovery identity. Undo stacks are not
part of that identity.

### Shared document-type interface

`DocumentRecoveryAdapter` (in `src/app/DocumentRecovery.h`) is the single
extension point:

| Member | Responsibility |
| --- | --- |
| `typeId()` | Stable, non-localised id written to the workspace index |
| `displayName()` | Diagnostics/UI label |
| `supports(document)` | Type association check (no guessing) |
| `save(document, path, error)` | Type-specific atomic snapshot writer |
| `open(path, error)` | Type-specific validated snapshot reader |

`DocumentRecoveryRegistry` owns the adapters. The illustration adapter is
built in and delegates to `DocumentFile`, so illustration snapshots use the
same version-2 `.koma` payload as normal saves and the existing file format is
unchanged. `CanvasDocument::documentType()` defaults to `"illustration"` and
is **not** written into `.koma`; a future document type sets its own id in
memory and its adapter owns its payload.

Adding a future document type means implementing one adapter and registering
it before the workspace restores. `WorkspaceState` stores only the type id and
never branches on concrete document types. An unknown or mismatched type
fails the restore instead of attaching a snapshot to the wrong document.

## Recovery directory layout

The directory is `QStandardPaths::AppDataLocation/recovery`.

```
recovery/
  workspace.lock                 QLockFile: one recovering instance per user
  workspace.json                 committed index (QSaveFile + fsync)
  <tab-uuid>-<uuid>.koma         one recovery snapshot per tab
```

`workspace.json` contains:

| Field | Meaning |
| --- | --- |
| `version` | Index schema version (currently 1) |
| `sha256` | SHA-256 of the canonical JSON with this field removed |
| `active` | UUID of the tab to activate after restore |
| `activeTool`, `zoom` | Relevant workspace state |
| `projects[]` | One entry per tab |

Each project entry stores `id`, `type`, `snapshot`, `sha256` (of the
snapshot), `path` (optional original file), `originSha256` and `modified`.
A snapshot filename must be a bare filename that starts with the tab UUID and
ends in `.koma`; paths are never followed out of the recovery directory.

## Write ordering and failure behavior

1. Every tab is serialized by its type adapter to a **new uniquely named**
   snapshot (`QSaveFile`: temp file, atomic replace) and flushed to disk.
2. Each snapshot is re-opened and validated; its SHA-256 is computed. If any
   tab fails, all snapshots created by this attempt are removed and the
   previously committed index and snapshots stay untouched.
3. The new index is written with `QSaveFile`, `fsync`/`_commit`, then
   committed. The old index is replaced atomically.
4. Only after the new index is committed are snapshots referenced by the
   previous valid index removed. A corrupt previous index is never used as a
   deletion list.
5. On startup, an unreferenced `.koma` in the recovery directory is removed
   **only when a checksum-valid index exists**; without a valid index nothing
   is deleted.

Crash points therefore resolve to either the previous committed index (the
new snapshots are orphans, cleaned at next startup) or the new index (all of
its snapshots were validated before commit).

## Restore validation

Restore is all-or-nothing and mutates nothing until every entry has passed:

* index checksum, schema version and project array;
* UUID uniqueness, snapshot name shape and UUID prefix;
* snapshot SHA-256, `.koma` CRC and document payload validity;
* a registered adapter for the recorded type and an exact type match on the
  loaded document;
* active tab id refers to an existing entry.

If a saved project's original file is missing or its hash differs from
`originSha256`, the recovered artwork opens **without a filename** and is
marked modified, so saving cannot silently overwrite a file that changed
outside KomaPaint. Invalid recovery data is reported and retained; the user
must explicitly discard it.

## Shutdown, discard and cleanup

* Application close prompts Save / Don't Save / Cancel for every modified
  tab. Cancel keeps all tabs and the application open.
* "Don't Save" confirms once per modified project. Saved projects are
  snapshotted from disk (without the discarded edits); unsaved projects are
  omitted from the new index.
* Normal shutdown writes a fresh recovery index so the tab list and active
  tab survive restart.
* Explicit "Discard recovery" removes the index first, then the snapshots it
  referenced. If index removal fails, everything is retained.
* If a restore decision is still pending, new snapshots are refused and the
  old recovery state is retained; the user can restore, discard, or choose
  *Not now* (no new snapshots that session).

## Interval settings

The first launch asks for 5, 10, 15, 30, 60 minutes or Never (default 10) and
persists `recovery/intervalMinutes` plus `recovery/configured`. The dialog is
shown once; **File → Recovery settings…** changes it later. Never (`0`)
stops the periodic timer; the dialog states that unsaved work then has no
periodic crash protection.

## Testing status

Automated tests cover restart isolation (saved and never-saved projects),
active tab, layer order/visibility/opacity, tool/zoom state, empty undo
history after restore, original-file isolation, injected snapshot and index
write failures, index/snapshot corruption, unknown document types, type
adapter registration, and orphan cleanup. They do **not** prove the packaged
Windows application's interactive behavior: save/discard/cancel prompts,
actual process termination, timer-driven snapshots against real files, or
power loss. Those require the manual packaged-application test listed in the
task and must not be inferred from serialization tests.
