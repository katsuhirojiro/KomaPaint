# Documents, recovery snapshots and session restore

This document describes what is implemented in `src/app/WorkspaceState.*`,
`src/app/DocumentRecovery.*`, `src/app/SessionStore.*` and the dialogs in
`ui/workspaces/MainWindow.qml`.

Four mechanisms exist and they are deliberately separate. None of them reads
or writes another's data.

| Mechanism | What it holds | Where | Lifetime |
| --- | --- | --- | --- |
| Project file | The artwork the user saved | Wherever the user saved it (`.koma`) | Until the user deletes it |
| Recovery snapshot | A copy of a project that has work not yet in its file | `AppData/recovery/` | Until that project is saved, closed with "Don't Save", or discarded by the user |
| Session | Which saved files were open, the active one, each project's tool/zoom/position, panel layout | `AppData/session.json` | Rewritten as the user works and at exit |
| Undo/redo history | Steps of the current working session | RAM only | Until the project is closed or the application exits |

Undo history is never written to disk and is never recovered. A recovered or
reopened project always starts with empty undo history. (`src/history/
HistoryJournal` is compiled and unit-tested but is not connected to anything.)

## Documents and tabs

Each tab owns one `CanvasDocument` (canvas, layers, project settings), one
`UndoStack`, and its own:

* file path and "modified" state,
* active tool,
* zoom and the document point shown at the centre of the canvas.

Nothing above is shared between tabs. `WorkspaceState` exposes the active
tab's values (`document`, `layers`, `history`, `currentFile`, `activeTool`,
`zoom`); switching tabs changes which tab those refer to and emits the change
signals. The canvas (`CanvasViewport` + `CanvasWorkspace.qml`) reports every
pan/zoom/resize to the active tab through `storeActiveView()` and, when a
different project is shown, puts that project's stored view back with
`restoreView()`. A project shown for the first time has no stored view and is
fitted to the window. Resizing the window or a dock keeps the same document
point centred.

Each tab gets a UUID when it enters the workspace. Recovery entries and
session entries refer to tabs by that UUID, and a tab keeps its UUID when it
comes back through recovery or session restore.

Rules enforced in `WorkspaceState`:

* Save and Save As act on the active project only.
* One file can belong to only one tab. Opening a file that is already open
  shows its tab; Save As onto a file that another tab has open is refused.
* A modified project is never closed without an explicit decision
  (`closeTab(index, discard)`).
* The active project cannot be switched while a brush stroke is in progress.
* The first project the user creates or opens takes the place of the
  untouched start-up "Untitled" document.

## Recovery snapshots

### Which projects get a snapshot

A snapshot pass (`persistWorkspace()`, run by the interval timer) writes one
snapshot for every open project that has work outside a project file:

* a project that has never been saved (including a new, still blank one — its
  size and settings exist nowhere else);
* a saved project with changes made after its last save.

A saved project with no changes gets no snapshot, and the untouched start-up
document gets none.

### Folder layout

```
AppData/recovery/
  workspace.lock                 QLockFile: one running copy owns the folder
  workspace.json                 index (QSaveFile + fsync, self-checksummed)
  <tab-uuid>-<uuid>.koma         one snapshot per project
```

Snapshots are ordinary `.koma` documents written through the document-type
adapter (`DocumentRecoveryAdapter`), so they contain the canvas, layers and
project settings and nothing else. They are never written to, or next to, the
user's project file.

`workspace.json` (version 2):

| Field | Meaning |
| --- | --- |
| `version` | 2. Version 1 (written by the previous build) is still read |
| `sha256` | SHA-256 of the JSON with this field removed |
| `active` | UUID of the project that was active |
| `projects[]` | One entry per snapshot |

Each entry: `id`, `type`, `snapshot` (bare file name), `sha256` (of the
snapshot), `path` and `originSha256` (the saved file and its content hash at
snapshot time; empty for never-saved work), `modified`, `title`, `savedAt`,
`tool`, `zoom`, `hasView`, `centerX`, `centerY`.

A snapshot name must be a bare file name of the form
`<tab-uuid>-<uuid>.koma`; nothing outside the recovery folder is ever read or
deleted.

### Write order

1. For each project independently: check free disk space against the
   adapter's size estimate, write a **new, uniquely named** snapshot with
   `QSaveFile` (temporary file, flush, atomic rename), reopen it, validate it
   and hash it.
2. A project whose snapshot could not be written (no space, write error,
   validation failure, out of memory) keeps its **previous** entry and
   snapshot. Other projects are unaffected. The failures are reported
   together in one message.
3. The new index is written with `QSaveFile` and flushed, replacing the old
   one atomically. If that fails, the new snapshot files are removed and the
   previous index and snapshots remain in force.
4. Only after the index is committed are superseded snapshots deleted, and
   only those named by the previous **valid** index.

A crash at any point therefore leaves either the previous committed state
(new files are leftovers, removed at the next start when a valid index
exists) or the new committed state (every snapshot in it was validated before
commit). Leftover `QSaveFile` temporaries are removed at start-up.

Entries the user has not decided about yet ("Decide later") are carried
forward untouched by every later pass.

### When a snapshot is removed

* The project is saved successfully (only that project's snapshot).
* The project's tab is closed with "Don't Save" (only that project's).
* The user discards it in the recovery list (only that entry), after a
  confirmation.
* The application exits normally and the user answered every question
  (`finishShutdown`).

A failed save, a cancelled close and a failed discard all keep the snapshot.
A discard commits the new index first and deletes the file afterwards.

## Recovery after a crash

At start-up `initializeRecovery()` takes the folder lock and reads the index.
Nothing is opened and nothing is deleted by reading it. If entries exist the
window shows **Recover unsaved work** with one row per project:

* title, "Never saved" or the saved file's name and location,
* the snapshot time,
* a note when the saved file has changed or is missing,
* **Recover** and **Discard** for that row.

**Recover all** and **Decide later** act on the whole list. **File → Recover
Unsaved Work…** reopens the list for as long as entries remain. Periodic
snapshots keep running at the chosen interval whatever the user decides.

`recoverEntry(key)` validates one entry — a registered adapter for its type,
name shape, file exists, SHA-256 matches, the document loads and is of the
recorded type — and only then adds a tab. Results:

| Situation | Result |
| --- | --- |
| Never-saved work | Opens as an unsaved, modified project |
| Saved file unchanged since the snapshot | Opens bound to that file, marked modified. The file is written only if the user then chooses Save |
| Saved file changed since the snapshot | Opens **detached**: no file, title "… (recovered)", modified. The user keeps it with Save As. The changed file is not touched |
| Saved file missing | Same: detached. The missing file is not recreated |
| Saved file already open in a tab | Same: detached, so two tabs never share one file |
| Snapshot damaged, truncated, wrong type, or its type has no adapter | Not opened. The row is marked damaged with the reason; its data stays on disk until the user discards it. Other rows still work |
| Not enough memory to load it | Reported; the entry is not marked damaged and can be tried again |

Recovery never writes to a project file.

### Damaged or missing index

If `workspace.json` is missing, truncated, fails its checksum or is from a
newer version, it is not trusted: **nothing is deleted**, and every file in
the folder whose name has the snapshot shape is offered as a separate unsaved
project (the original file location is unknown, and the row says so). The
next successful snapshot pass writes a fresh, valid index that still refers
to the entries the user has not decided about.

## Close and shutdown

* Closing a tab asks Save / Don't Save / Cancel if that project is modified
  and affects only that project.
* Closing the application asks about each modified project in turn. Nothing
  is closed, saved on the user's behalf or discarded until every question has
  an answer. Cancel on any question cancels the whole close: every tab, every
  unsaved change and every snapshot is still there, and the project the user
  was looking at is shown again. Answers are not remembered for a later close.
* When all answers are in, `finishShutdown(discardIds)` writes the session
  and removes the recovery snapshots of projects that were saved or that the
  user chose not to save. A storage error at this point is reported but does
  not keep the application open.
* After a normal exit the recovery folder holds no snapshots, so the next
  start asks no recovery question — unless entries the user postponed are
  still waiting.

## Session restore

`session.json` (format `komapaint-session`, version 1, self-checksummed,
written with `QSaveFile`) records:

* the open projects that live in files: path, tab UUID, title, tool, zoom,
  view centre;
* which one was active;
* a small map of layout values (`panel/<id>` visibility, right dock width).

Window size and maximised state are stored in the application settings, as
before.

It contains no artwork and no undo history. Projects that were never saved
are not part of the session: they exist only as recovery snapshots.

At start-up, after the recovery folder is read, `restoreSession()` reopens
each listed file with its own tool, zoom and position and activates the one
that was active. It asks no question. Details:

* A file that is missing or unreadable is reported in one message; the other
  projects still open; the file is not modified.
* A project that has a recovery entry waiting is **not** reopened from its
  older file; it is left to the recovery list.
* A file named on the command line is opened first and stays active.
* A damaged, truncated or too-new session file is ignored (the application
  starts with one empty project) and is replaced at the next save.
* Only the running copy that holds the recovery lock reads or writes the
  session, so a second copy cannot overwrite the first one's tab list.

## Low resources

* **Low disk space or an unwritable recovery folder:** the affected project
  keeps its previous snapshot; the user gets one message naming the projects;
  work continues. Project files are not involved.
* **Recovery folder unavailable at start-up** (cannot be created, or another
  running copy owns it): recovery and session are switched off for that run;
  painting, opening and saving work normally.
* **Memory:** the per-document tile budget and the RAM-only history budget
  from earlier phases are unchanged. A snapshot pass serialises one project
  at a time. An allocation failure during a snapshot or a recovery is caught
  and reported for that project only.
* **Many open projects:** there is no fixed limit; each tab costs its tiles
  and history. This has been exercised with a handful of small projects, not
  with dozens of large ones.

## Interval setting

First launch asks for 5, 10, 15, 30, 60 minutes or Never (default 10) and
stores `recovery/intervalMinutes` and `recovery/configured`. **File →
Recovery settings…** changes it later. "Never" stops periodic snapshots; the
dialog says that unsaved work then has no crash protection.

## Document types

`DocumentRecoveryAdapter` is the one extension point for other document
types: `typeId()`, `displayName()`, `supports()`, `save()`, `open()`,
`estimatedSnapshotBytes()`. `WorkspaceState` stores only the type id and never
branches on concrete types. Only the illustration adapter exists today.

## Tests and what they do not prove

* `tests/unit/TestWorkspaceSession.cpp` — real `WorkspaceState`, real files
  in a temporary folder: document independence, per-project view state, Save
  / Save As isolation, close isolation, which projects get snapshots,
  per-entry recover and discard, damaged and truncated snapshots, interrupted
  writes, damaged/missing/too-new index, missing/changed/already-open
  original, version-1 index, postponed entries, cleanup, low disk space,
  unavailable folder, shutdown decisions, session round trip, session reopen
  failures, session file validation.
* `tests/unit/TestWorkspaceUi.cpp` — the real `MainWindow.qml` driven with
  mouse events: the recovery list, per-project view on the real canvas via
  the tab buttons, close questions including Cancel, tab close, session and
  layout restored at start-up, toolbar Open/Save.
* `tests/unit/TestWorkspaceRecovery.cpp`, `TestRecoveryUi.cpp` — the earlier
  recovery tests, still passing unchanged.

A "crash" in these tests is a workspace destroyed without the shutdown step,
which leaves the same files as a killed process. Not covered by any test
here: the packaged Windows build, a real process kill or power loss in the
middle of a write, a really full disk (free space is simulated through a test
probe), native file dialogs, a real display and pen tablet, and behaviour
with many large projects open.
