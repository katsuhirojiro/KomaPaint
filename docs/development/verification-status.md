# Verification status

This document records **what has actually been verified** for this build, and
what remains unverified. It is deliberately conservative: a passing
standalone unit test is never presented as proof that the whole application
works.

Last verified: build `0.1.0`, branch `arena/2bd17f7f-komapaint`, Windows CI
(MSVC 2022, Qt 6.8.3, Release).

---

## 1. Verified by CI, on real Windows

The `Windows Build` workflow runs on `windows-2022` and performs the whole
chain below in one job. Every step listed here has passed:

| Step | What it proves |
| ---- | -------------- |
| Configure + Build | The project compiles with MSVC 2022 in C++20 mode with strict warnings enabled. |
| Verify executable exists | `komapaint.exe` is produced at `build/bin/komapaint.exe`. |
| Run unit tests | All 15 test executables run and pass headless (`QT_QPA_PLATFORM=offscreen`). |
| windeployqt | Qt DLLs, platform plugins and QML modules are deployed next to the exe. |
| Package cleanliness | No `Qt6Test.dll` and no `test_*.exe` leak into the shipped package. |
| Deployed-tree check | `Qt6Core/Gui/Qml/Quick/QuickControls2/Widgets.dll`, `platforms/qwindows.dll` and `platforms/qoffscreen.dll` are present. |
| Launch smoke test | The packaged exe starts, creates a **native main window** whose title begins with `KomaPaint`, and closes cleanly with exit code 0. |
| ZIP packaging | `KomaPaint-Windows-x64.zip` is produced and uploaded as a build artifact. |

Because the smoke test requires a native window with a `KomaPaint …` title,
it also proves that **the QML interface loads successfully** from the
compiled resource bundle. A QML parse/load failure makes `KomaPaintApp`
exit before creating a window.

### Self-containment

The smoke test deliberately **removes Qt's development `bin` directory from
`PATH`** before launching the packaged executable. The package must therefore
resolve every Qt DLL from its own folder. (An earlier revision of the
workflow left the dev Qt `bin` on `PATH`, which could have hidden a package
that was missing a DLL.)

## 2. Verified by unit tests

15 executables, all passing:

`test_undostack`, `test_layers`, `test_timeline`, `test_canvas`,
`test_recent`, `test_document_creation`, `test_document_file`,
`test_command_line`, `test_tile_geometry`, `test_sparse_tile_store`,
`test_dirty_tile_tracker`, `test_tile_allocation_failure`,
`test_cpu_tile_compositor`, `test_canvas_viewport`, `test_brush_engine`.

Coverage highlights:

* Sparse tiled storage, tile geometry, dirty-tile tracking and allocation
  failure handling.
* CPU tile compositing (layer order, opacity, visibility, background).
* Brush rasterisation, spacing/resampling, pressure, layer lock rules.
* Bounded tile-based undo/redo.
* `.koma` format v1 and v2 read/write, validation and error handling
  (`TestDocumentFile.cpp` is the largest suite at ~690 lines).
* Command-line parsing and file-association logic.
* Viewport pan/zoom/fit, and a QML-driven mouse stroke through
  `PointHandler`.

## 3. NOT verified

* **Interactive painting on a real desktop.** Strokes are exercised through
  unit tests and a headless QML stroke test, not by a human dragging a
  mouse in a real session.
* **Save/Open through the GUI file dialogs.** The format and the
  `Workspace.saveToPath` / `openFromPath` paths are unit-tested; the native
  Windows file dialog itself is not covered by automation.
* **Physical tablet / stylus hardware.** No tablet pressure or
  tilt handling exists in this codebase yet, so there is nothing to verify.
  Do not make hardware compatibility claims.
* **Multi-document / tabbed sessions.** One document is open at a time.
* **Crash recovery / autosave.** Not implemented.
* **Long sessions, very large canvases, low-memory behaviour** beyond the
  explicit failure-path unit tests.

## 4. Feature honesty notes

Implemented and wired to the UI:

* Brush tool (round and textured tip), size, opacity, flow, spacing.
* Placement/move of paint onto the active visible, unlocked layer.
* Undo/redo of strokes, layer add/remove/reorder, opacity, visibility, lock.
* Pan/zoom/fit; new document; open/save `.koma`; `.koma` file association.
* Panel visibility toggles.

Placeholders (visible but intentionally non-functional):

* Selection, Move, Pencil, Eraser, Fill, Eyedropper, Text tools.
* The HSV colour gradient area is drawn but not interactive.
* Timeline: state only (frame counter, FPS, play/pause flags) — no frames,
  keyframes, onion skinning or playback engine.
* Assets and History panes; Export/Import; Cut/Copy/Paste; Preferences;
  layer thumbnails; sub-tool / brush-size preset panels.

## 5. Reproducing this verification

```
gh run list --branch arena/2bd17f7f-komapaint --workflow "Windows Build"
```

The packaged ZIP is attached to the newest successful run as the
`KomaPaint-Windows-x64-Qt6.8.3` artifact.
