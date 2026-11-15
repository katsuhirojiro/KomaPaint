# Development handover

## Repository and branch

- Repository: `katsuhirojiro/KomaPaint`.
- Current Arena session branch: `arena/6fca8f12-komapaint`.
- Current checkout HEAD: `d3622c4869fc868ce0d52a05146b35938372b7f0`.
- On inspection, `origin/development` also pointed to that exact commit. This is a shallow checkout; the complete history is not available locally.
- **All work described below is uncommitted in this workspace**, not part of the HEAD commit and not present on `origin/development`. Nothing is staged or stashed. Preserve all modified and untracked files when transferring the workspace.
- The user now requires future development directly on `development`, with regular commits and pushes there, and no work on `main`. This Arena session is restricted to its existing branch and cannot perform that switch or push. A session/workspace provisioned on `development`, with these changes safely carried over, is needed. Do not reset, rebase, force-push, delete branches, or merge without explicit permission.

## Architecture and current work

The application uses a Qt Quick desktop UI with a C++ document model. `CanvasDocument` owns a flat `LayerModel` of raster `Layer` objects backed by sparse premultiplied-ARGB tiles. `CanvasViewport` composes visible tiles; brush history is handled separately by `UndoStack`. `DocumentFile` reads/writes native `.koma` files, and the docking workspace stores its own layout.

The uncommitted changes include:

- Neutral theme and partial shell, toolbar, tool rail, Layers, Color/Palette/Mixer/Assets panel and docking UI changes. The approved visual reference is `docs/investigations/design-proposal/refined-dark-layers-workspace.png`; no extra in-app title bar should be introduced. Panel options belong in header menus. Preserve square and triangular color selectors and the Assets manager.
- Layer thumbnails, inline rename, thumbnail wheel/presets, selection bookkeeping, color labels and Normal/Multiply/Screen/Overlay compositing changes. These are not yet verified through the running application. Layer property edits do not yet have complete undo support.
- Native format version 3 metadata for blend mode and color label; version 1/2 reading remains in the code and default-metadata documents are written as version 2. Save preflight rejects unrepresentable metadata rather than silently truncating it. Hidden background color is retained when transparency is enabled. Re-tiling checks/charges staging memory. See `docs/architecture/file-format.md`.
- Corresponding source and tests across `src/layers`, `src/canvas`, `src/files`, `src/app`, `tests/unit`, `ui`, `resources/qml.qrc`, and `THIRD_PARTY_NOTICES.md`; design research and mockups are untracked under `docs/investigations`.

The changes above are implementation in progress, not a declaration of a completed milestone. Inspect the full working tree and diffs before committing. `docs/WORKSPACE_CHANGE_INVENTORY.md` lists every modified and untracked source/design file individually; the transfer archive contains their actual bytes, not just this description.

### Additional implementation details

- `LayerModel` tracks the selected object across moves/removals, notifies when a selected row is removed even if its numeric index stays the same, and returns no children for list-model indexes. Thumbnails are rendered from each layer's real pixels and coalesced in QML. Thumbnail sizes are 24, 32, and 48 logical pixels.
- `CpuTileCompositor` implements Multiply, Screen and Overlay alongside the original Normal integer path; `CanvasViewport` supplies the layer mode. The color-label and blend controls, model roles, cache invalidation and v3 persistence are connected in code, but have not been exercised in the full running app.
- `DocumentFile` still has pre-existing format bounds, including 512 layers, 32768-pixel dimensions and per-store pixel budget. These conflict with the requested absence of arbitrary limits and need a carefully designed, compatible replacement. Save now fails rather than producing an unreadable file or silently truncating names when those bounds are exceeded.
- `DockModel`, `DockGroup.qml` and `MainWindow.qml` contain partial organization and panel-menu work. The underlying docking behavior should be retained, not replaced by a fixed mock layout. Existing saved layouts may retain new panels as hidden.
- `ColorMixerPanel.qml` uses an MIT-licensed Spectral.js adaptation (`ui/components/SpectralMix.js`; attribution in `THIRD_PARTY_NOTICES.md`). The Palette and Mixer additions and selector changes have not had end-to-end app verification. Assets is only a placeholder.


## Verified tests and build status

- Standalone C++ CPU compositor unit executable: **passed**.
- Qt 6.8.3 was built locally far enough to provide Qt Core, Gui and Test. A manually compiled `TestLayers` executable using the actual current source reported **12 passed, 0 failed** (including QtTest initialization/cleanup).
- A manually compiled copy of `TestDocumentFile` excluding only `workspaceSaveOpenFlow` (which requires the full application dependency graph) reported **16 passed, 0 failed**, including version 3 metadata, malformed metadata, save safety, hidden background color, and sparse re-tiling amplification. This is not a run of the complete application test suite.
- Changed QML files previously passed `qmllint` with warnings; `git diff --check` passed.
- **The full application has not compiled or been runtime verified with these changes.** An earlier CMake configure failed because the Qt6 C++ SDK/`Qt6Config.cmake` was unavailable. The PySide6 runtime also lacked system `libGL.so.1`, `libEGL.so.1`, `libxkbcommon.so.0` and `libdbus-1.so.3`. A local Qt base/declarative source build is still in progress in the current sandbox; Qt Core, Gui and Test were built for the targeted tests, but the full SDK is not yet ready. `/tmp` build tools and results are transient and must not be assumed to survive a new session.
- GitHub Windows run `43623798114` succeeded for baseline commit `d3622c4869fc868ce0d52a05146b35938372b7f0` only. Its artifact does **not** contain current uncommitted changes and must not be presented as a current build.

## Remaining requirements and technical debt

- Finish and test the document/layer architecture, including nested folders, masks, clipping, alpha lock, multi-selection, hierarchy-safe moves, and atomic undo/redo. The present model is flat; property operations and layer deletion do not yet provide the requested complete history behavior.
- Address existing native format and pixel-store limits, memory safety, and compatibility without silently discarding artwork. Document metadata changes and restore behavior need full integration and recovery testing.
- Complete the approved Layers workspace, functional Canvas Viewer, multiple independently navigable References collections, and functional Assets manager. `AssetsPanel.qml` is currently a placeholder.
- Verify full docking: resize, rearrange, combine/separate tabs, float/redock, hide/restore, and workspace persistence at multiple window sizes. Verify both themes and the complete UI in the running application.
- Build and test the current source on Windows x64, package Qt dependencies, execute the packaged smoke test, and provide the actual artifact. Existing `.github/workflows/windows-build.yml` runs on pushes/PRs and packages a Windows build; a push of the actual reviewed changes to an allowed branch is required to invoke it.

## Build and immediate next tasks

With Qt 6.8.3 C++ development packages and CMake/Ninja installed, configure with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<qt-prefix> -DKOMAPAINT_BUILD_TESTS=ON`, build with `cmake --build build`, and run `ctest --test-dir build --output-on-failure` (headless environments may need `QT_QPA_PLATFORM=offscreen` and software Qt Quick rendering). The Windows workflow is the source of truth for Windows packaging.

1. Arrange an authorized Arena session actually provisioned on `development`. Carry across the transfer archive's actual files, not a recreation from this handover. Its `MANIFEST.json` records the base commit and SHA-256 hashes, `tracked-changes.patch` records the tracked diff, and `files/` contains the complete working bytes of every changed and untracked source/design file. The archive does not contain `.git`, ignored build outputs or transient `/tmp` tools. In the destination, first verify the archive checksum and each manifest entry; inspect branch, HEAD and status. If HEAD is the recorded base and the destination is clean, check `git apply --check tracked-changes.patch`, check that every untracked destination is absent, then apply the patch and copy the untracked files without overwriting existing files. Recheck all hashes and `git status`. If the destination has diverged or any path already exists, stop and reconcile deliberately; never overwrite or reset it blindly. This handover and the inventory are currently untracked and are included in the archive.
2. Review the diff and preserve partial work; run the full build and tests, fix failures, and verify the UI and document flows at runtime before claiming a milestone complete.
3. Commit meaningful reviewed milestones and push only to the branch permitted by the active session and user instructions. Record the resulting exact commit hash and Windows Actions result in the handover. A commit cannot contain its own hash; record the implementation commit hash in a subsequent handover update.
4. Continue foundational layer architecture first, then its dependent UI/Viewer/References features. Do not introduce decorative or permanently disabled requested controls, arbitrary new limits, or unsupported completion claims.
