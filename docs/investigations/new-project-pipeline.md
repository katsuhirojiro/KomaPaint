# New Project pipeline investigation

Baseline: PR #3 head `28d1537c7846b1fad8e467752e45c05d8e42ef1e`, applied to this branch without merging the PR. This is a source analysis, not a claim that a Windows user session has been reproduced.

## First breaks and contributing defects

1. `NewProjectDialog.qml` originally evaluates `NewProject.isValid()` in the Create button's `enabled` binding. `NewProjectSettings::isValid` is an ordinary C++ method, not a `Q_INVOKABLE` or slot, so QML cannot call it. This occurs during dialog instantiation, before a click. Even if exposed, the call alone would not subscribe to the individual setting notifications. The opening diagnostics also call the unavailable method.
2. Other handlers call `setName`, `setUnits`, `setDpi` and `setTransparentBackground` as methods, but those are property writers, not invokable methods. Editing those controls therefore does not reliably update the model.
3. In the Create click handler, `nameInput.text.trimmed()` is not a JavaScript string method. It throws before `Workspace.newProject` is reached, even if the button can be activated. `Qt.color(...)` is also not a supported color conversion function in these bindings.
4. The settings, summary and status rectangles contain layouts anchored to them but originally have no implicit height. A layout cannot infer a rectangle's height from anchored children; the controls can overlap or collapse. The fixed 680-pixel dialog height also exceeds the application's 640-pixel minimum window height.

The corrections use observable validation, property assignment, JavaScript `trim`, direct QML color conversion, and explicit content-derived section heights. They do not replace the dialog design.

## Downstream path

* The toolbar's `onClicked` emits `newDocumentRequested`; `MainWindow.qml` calls `requestNewDocument`, including a discard confirmation for modified artwork, then opens `NewProjectDialog`.
* Create calls `WorkspaceState::newProject`, which invokes `ProjectCreationService::createDocument`; `validate` checks name, pixel dimensions, opaque valid color, and positive finite DPI. `buildDocument` sets title, size, DPI and background on a fresh `CanvasDocument` with a default layer.
* `WorkspaceState::replaceDocument` clears artwork history and current file, replaces the pointer and emits `documentChanged`; QML's `CanvasWorkspace` binding assigns the new document to `CanvasViewport::setDocument`. That disconnects the old document, cancels a stroke, resets caches and origin, connects invalidation signals and emits its own `documentChanged`. The QML connection calls `fit` for the new document.
* `CanvasViewport::refreshNow` composites visible tiles and background into the frame; `paint` displays that frame. `CanvasWorkspace` mouse/stylus PointHandlers call `beginStroke`, `continueStroke`, and `endStroke`, using the workspace brush, color and history. New project creation now rejects an active stroke, like the existing open/new paths, before replacement.
* The service originally retained QObject parentage of the created document despite claiming ownership transfer. The workspace now takes parent ownership explicitly.

## Verification and limitations

`test_project_workflow` loads the real resource-bundled main window and context objects, clicks the New toolbar control, edits the project name, checks validation, clicks Create, checks replacement and viewport binding, then sends mouse input and verifies artwork and undo. Existing C++ validation and viewport tests remain complementary. A headless integration test cannot establish Windows layout, real stylus behavior or correctness of a distributed ZIP; the packaged application still requires a manual Windows acceptance pass. Fractional physical-unit input and all size/DPI/color combinations are not covered by this test and remain areas for further verification.
