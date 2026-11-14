# Architecture overview

KomaPaint is structured as a modular C++/Qt Quick application. The
high-level split is:

```
┌─────────────────────────────────────────────┐
│  QML UI (ui/ )                              │
│  - workspaces, panels, components, theme    │
│  - binds to WorkspaceState context object   │
└───────────────────────┬─────────────────────┘
                        │ QObject properties / signals
┌───────────────────────┴─────────────────────┐
│  src/app                                    │
│  - KomaPaintApp  (QML bootstrap)            │
│  - WorkspaceState (doc + tools + colors …)  │
│  - PanelManager (dock panels + layout)     │
└───────────────────────┬─────────────────────┘
                        │
┌───────────────────────┴─────────────────────┐
│  Domain modules (src/<module>)              │
│  core · canvas · drawing · layers ·         │
│  animation · color · assets · files ·       │
│  history                                    │
└─────────────────────────────────────────────┘
```

## Module responsibilities

### `src/core`
Application-scoped services that don't depend on any other KomaPaint module:

* `ApplicationConfig` — name, version, organization, build metadata.
* `Settings` — typed wrapper over `QSettings` for persistent preferences.
* `ModuleInfo` — stable string IDs for the other modules, used by the
  plugin/panel system and for diagnostics.

### `src/app`
The glue between C++ services and the QML front-end:

* `KomaPaintApp` owns the `QQmlApplicationEngine`, configures Qt Quick
  Controls (forced to the `Basic` style), installs fonts, exposes the
  singletons as QML context properties and loads the main window QML.
* `WorkspaceState` aggregates the current document, layer list, timeline,
  palette and history and exposes the active tool and zoom level.
* `DockModel` is the workspace layout: a tree of splits, tab groups and the
  canvas slot, plus floating windows and hidden panels. It owns re-arranging,
  collapse, presets and persistence (`workspace/layout`,
  `workspace/presets`) and knows nothing about what a panel *is* beyond
  `panelTable()`. See [docking.md](docking.md).
* `PanelManager` is the façade the rest of the application uses: it owns the
  `DockModel`, keeps the per-panel visibility flags the menus bind to, and
  provides the `toggle()`, `setVisible()` and `resetLayout()` slots used by
  menus and panel close buttons.

### `src/canvas`
The document model (`CanvasDocument`): dimensions, title, DPI, and whether
the document has a transparent background. The actual canvas viewport
(rendering surface, transformation matrix, rendering flags) will be added
here in Phase 2.

### `src/drawing`
Future home of the brush engine, raster pipeline and tool controllers. In
Phase 1 this directory contains only `ToolId.h` as a stable enumeration
of tools the UI can reference.

### `src/layers`
`Layer` (name / visibility / lock / opacity) and `LayerModel` (a
`QAbstractListModel` that the Layers panel binds to). Real layer
pixel storage, mask layers, folders and blend modes come later.

### `src/animation`
`TimelineState` holds frame count, current frame, FPS and play state.
Phase 1 does not implement a real timeline, keyframes, onion skinning or
playback loop.

### `src/color`
`ColorPalette` holds the current foreground/background colors and a
`swap()` slot. Future additions include color-management profiles,
swatch libraries, and a proper HSB/RGB picker model.

### `src/assets`
Reserved for brush presets, textures, patterns, fonts and user assets.

### `src/files`
Currently `RecentFiles` — a small persistent list of recent documents.
New document creation is underway in Phase 2; open/save/export and native project serialization are planned for milestone 2.8.

### `src/history`
A minimal `UndoStack` with a text label per command and `canUndo`/
`canRedo` state bindings. A full command pattern (with executable
commands, memory-efficient inverses and coalescing for strokes) will land
when editing features require it.

## UI layer (`ui/`)

The QML layer is strictly presentational and contains no business logic.

* `ui/themes/KomaTheme.qml` is a singleton `QtObject` that exports the
  entire palette, font, spacing and sizing token set. **Every other QML
  file must reference tokens through `KP.*`**; adding a new hex color
  outside of `KomaTheme.qml` is considered a regression.
* The same singleton holds both themes: `mode` is `"dark"` (default) or
  `"light"`, and every colour token is a binding on it, so there is no second
  palette to keep in sync. `MainWindow.qml` stores the mode as `ui/theme` and
  the optional icon-tint overrides as `ui/icons/<state>`, restores them before
  the first frame, and binds the window `palette` so Qt Quick Controls follow
  the theme too. The UI tests (`TestBrushUi`) cover the tints, the overrides
  and the restart path.
* `ui/components/` contains small, generic primitives (toolbar buttons,
  separators, panel headers, the canvas surface placeholder, status bar,
  tool palette).
* `ui/panels/` contains the larger dock panels (Layers, Properties, Color,
  Timeline). Each one is only its content — title, close button, collapse,
  dragging and the panel menu come from the docking layer.
* `ui/docking/` is the docking workspace: splits, tab groups, floating
  windows, drag previews and the canvas slot, all driven by `DockModel`
  (see [docking.md](docking.md)).
* `ui/workspaces/MainWindow.qml` assembles the menus and toolbar, maps panel
  ids to components and puts one `DockWorkspace` in the window.

## Threading and performance

In Phase 1 everything runs on the GUI thread — there is no background
processing yet. The architecture anticipates that rendering and stroke
rasterization will move to dedicated render threads in later phases, and
the modules are split so that the document model and rendering pipeline
are independent of QML.

To keep the application responsive even on lower-end hardware, the code
follows a few ground rules from the start:

* Avoid polling; react via signals and property bindings.
* Keep property setters idempotent and bail out early when the value has
  not changed.
* Keep per-frame work out of QML bindings — expensive painting belongs in
  C++ renderers, not in `Canvas.onPaint` handlers.
* Prefer stack allocation and value types (`QSize`, `QRect`, `QColor`)
  over heap allocation for hot-path values.

## Testing strategy

* Pure-C++ services (undo stack, layer model, timeline state, document
  model, recent files, the docking layout) are covered by QtTest unit tests
  in `tests/unit/`.
* The window itself is tested through the real QML scene on the offscreen
  platform: the tests load `MainWindow.qml`, find items through the visual
  tree and drive them with `QTest` mouse events (`TestDockUi`,
  `TestWorkspaceUi`, `TestBrushUi`, `TestCanvasViewport`).
* There is no attempt to mock painting / rasterization in Phase 1 because
  those subsystems do not yet exist.
