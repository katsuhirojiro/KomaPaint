# KomaPaint

**KomaPaint** is a professional 2D digital painting and animation application,
purpose-built for illustrators, comic and manga artists, and 2D animators.

It aims to combine a serious illustration and brush engine with frame-by-frame
animation workflows inspired by Clip Studio Paint and OpenToonz, but with an
original architecture, interface and identity.

> **Status: early alpha (Phase 2).** Implemented so far: the C++/Qt 6 desktop
> shell, a custom QML interface, the sparse tile storage engine, CPU tile
> compositing, a first pressure-aware brush, undoable brush strokes
> (bounded, tile-based undo/redo), pan/zoom/fit-to-screen, and saving /
> reopening `.koma` illustrations with layers. Animation, 3D assistance,
> comic layouts, export formats, physical tablet support and packaging for
> release are not implemented yet.

---

## 1. At a glance

|                 |                                                    |
| --------------- | --------------------------------------------------- |
| Language        | C++20 (core), QML / Qt Quick (interface)            |
| UI framework    | Qt 6 (Qt Quick / Qt Quick Controls 2)               |
| Build system    | CMake 3.21+, Ninja (recommended)                    |
| Primary target  | Windows 10+ (architecture is portable to Linux/macOS) |
| Version         | 0.1.0 (Alpha)                                       |

## 2. Repository structure

```
KomaPaint/
├── CMakeLists.txt               # Top-level build configuration
├── README.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── apps/
│   └── desktop/                 # Desktop entry point (main.cpp)
├── src/
│   ├── app/                     # Application controller, QML boot, workspace state
│   ├── core/                    # Configuration, settings, module ids
│   ├── canvas/                  # Document & viewport model
│   ├── drawing/                 # Brush/pencil/eraser engine (Phase 2)
│   ├── layers/                  # Layer model
│   ├── animation/               # Timeline state (frame data in Phase 2)
│   ├── color/                   # Palette & future color management
│   ├── assets/                  # Brushes / textures / presets (Phase 2)
│   ├── files/                   # Recent-files list; file I/O (Phase 2)
│   └── history/                 # Undo / redo command stack
├── ui/
│   ├── components/              # Reusable QML primitives (buttons, separators, canvas view)
│   ├── panels/                  # Dock panels (Layers, Properties, Color, Timeline)
│   ├── themes/                  # KomaTheme (color tokens, typography, spacing)
│   └── workspaces/              # MainWindow shell
├── resources/                   # Future icons/fonts/images
├── tests/unit/                  # QtTest unit tests for C++ services
├── docs/                        # Architecture & development documentation
└── tools/run_qml.py             # PySide6 QML launcher for quick UI iteration
```

## 3. Building (Windows — primary target)

These instructions will be tested and refined as CI is introduced. They are
the canonical build procedure for Windows.

### Prerequisites

* Windows 10 or 11 (64-bit).
* **Qt 6.5 or newer** built for MSVC 2022 (64-bit). At minimum install:
  * `qtbase` (Core, Gui, Widgets)
  * `qtdeclarative` (Qml, Quick, Quick Controls 2, Quick Layouts)
  * `qttools` (for `windeployqt` / Linguist)
* **CMake ≥ 3.21** and **Ninja** (or the Visual Studio 2022 CMake integration).
* **Visual Studio 2022** with the *Desktop development with C++* workload
  (MSVC v143 toolset, Windows 10/11 SDK). Qt and MSVC versions must match:
  install the `msvc2022_64` Qt kit (e.g. `6.7.3/msvc2022_64`). Older
  `msvc2019_64` kits are *not* binary-compatible with MSVC 2022 builds.

Qt can be installed via the [Qt Online Installer](https://www.qt.io/download)
or, for scripted installs, with `aqtinstall`:

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.7.3 win64_msvc2022_64 -m qtdeclarative qttools
```

### Configure & build

```powershell
# from the repository root
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2022_64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The executable will be placed at `build/bin/komapaint.exe`. All QML files
are compiled into the executable via a Qt resource (`resources/qml.qrc`),
so there is no separate `ui/` folder that needs to be shipped alongside
the binary.

After building, deploy Qt dependencies next to the executable:

```powershell
C:/Qt/6.7.3/msvc2022_64/bin/windeployqt.exe build/bin/komapaint.exe
```

No `--qmldir` flag is necessary because QML is bundled via qt_add_executable
+ AUTORCC; `windeployqt` discovers the required QML plugins automatically.

### Windows: `.koma` file association

The packaged Windows build (and any locally built `komapaint.exe`) can
register itself to open `.koma` files on double-click. Registration is
per-user (`HKCU\Software\Classes`) and needs no administrator rights:

```powershell
komapaint.exe --associate      # register .koma to open with KomaPaint
komapaint.exe --unassociate    # remove the registration
```

The release ZIP ships `Register_koma_file_association.cmd`, which calls
`komapaint.exe --associate` next to it. The same action is available
inside the app via **File → Associate .koma Files…**. Passing a file path
to `komapaint.exe` opens it directly (`komapaint.exe my-drawing.koma`).

### Running tests

Unit tests are registered with CTest and are executed with:

```powershell
ctest --test-dir build --output-on-failure -C RelWithDebInfo
```

Phase 1 validation note: the C++ build and test run have not yet been
executed in this sandbox because the full Qt 6 MSVC development kit is
not present in the Linux CI container used to author this commit. The
tests are expected to build and pass once the Windows/MSVC prerequisites
are installed locally.

## 4. Building / iterating on Linux (unsupported in Phase 1)

The codebase is written portably and should compile on Linux/macOS once Qt
development packages are available. The Linux toolchain is not the Phase 1
priority, and Linux packages are not yet validated.

For fast UI iteration without compiling C++, a PySide6-based QML launcher is
provided. It reads the QML directly from the source tree (so edits to `.qml`
files are reflected without rebuilding the C++ binary) but uses Python
stand-ins for the C++ backend — it is a designer aid, not a supported
runtime.

System requirements: Python 3.10+, plus graphics libraries required by Qt
(on Linux this typically means `libgl1`, `libegl1` and `libxkbcommon0`;
on Windows/macOS the PySide6 wheels ship these dependencies).

```bash
pip install PySide6
python3 tools/run_qml.py
```

## 5. Architecture overview

See `docs/architecture/` for a more detailed breakdown:

* **`src/core`** — application metadata, user settings, and cross-cutting module
  identifiers. This layer is deliberately UI-free.
* **`src/app`** — owns the QML engine, bootstraps configuration, styles and
  plugins, and exposes the singleton C++ services (`App`, `Settings`,
  `Panels`, `Workspace`) to QML as context properties.
* **`src/canvas`** — the document model (size, DPI, background). Canvas viewport
  and coordinate-space logic will live here.
* **`src/layers`** — layer list model and per-layer state. Compositing and
  per-layer pixel storage will be added alongside the brush engine.
* **`src/animation`** — timeline state (frame count, current frame, FPS,
  play/pause). The actual keyframe model, exposure sheet and onion skinning
  arrive in later phases.
* **`src/color`** — foreground/background palette and future color-management
  (ICC profiles, color-spaces).
* **`src/history`** — Undo/Redo command stack shell. Phase 1 keeps a text
  description per command and supports canUndo/canRedo bindings. Real
  commands will be introduced with the editing features that need them.
* **UI (`ui/`)** — QML only. All visual design tokens live in
  `ui/themes/KomaTheme.qml`; panels and components must never hard-code
  colors or fonts outside of that module.

Architectural principles:

* The UI is strictly QML; painting, rendering and document logic stay in C++.
* Modules expose clean QObject/QAbstractListModel interfaces; QML binds to
  them, never reaching into internal state.
* Side-effect-free services (e.g. undo stack, timeline state) are covered
  by unit tests in `tests/unit/`.
* Dependencies are kept minimal: Qt is the only third-party dependency in
  Phase 1.

## 6. Interface design

The KomaPaint shell is a restrained dark-charcoal workspace. Design tokens
are defined centrally in `ui/themes/KomaTheme.qml` and include:

* Narrow, near-neutral grays (no heavy gradients or "glowing" accents).
* A single cool-blue accent used only for selection, focus and hovered
  primary actions.
* Tight corner radii (2–3 px) to keep the application feeling like a tool,
  not a marketing site.
* Tight spacing rails and consistent typography (system UI sans-serif, with
  monospaced numerics for pixel values and timestamps).

The layout follows a traditional illustration/animation application
workspace:

* Top menu bar (File, Edit, View, Image, Layer, Animation, Window, Help).
* Main toolbar (new/open/save, undo/redo, zoom, workspace selector).
* Left vertical tool palette (Selection, Move, Brush, Pencil, Eraser, Fill,
  Eyedropper, Text) — these are interface placeholders only.
* Central canvas workspace with a checkerboard document placeholder, zoom
  and document info.
* Right dock panel column: Layers, Properties, Color.
* Bottom collapsible timeline area reserved for future animation tools.
* Status bar displaying current tool, document dimensions and zoom.

The View → Panels menu toggles each dock panel; View → Reset Layout
restores defaults.

## 7. Current limitations

The application is an early alpha. The following are **not** implemented:

* Advanced brushes: tilt, rotation, velocity, custom tip images, response
  curves, pickup/smudge/blending and procedural presets. The current brush
  is a basic round/textured tip with size, opacity, flow, spacing, pressure
  (opacity only) and per-dab buildup — it is not the final brush engine.
* Fill, eyedropper, text, selection or move tools (interface placeholders only).
* Layer blend modes, masks, groups or transforms (basic opacity, visibility,
  ordering and locking exist).
* Timeline frames, keyframes, onion skinning or playback (only shell state).
* File I/O beyond open/save of `.koma` illustrations — no export (PNG/PSD/
  PDF), no autosave, no recent-files menu. The unified `.koma` v2 envelope
  is ready to carry comic pages and multi-page projects later; this build
  reads and writes illustrations only.
* Color picker HSB widget (the color panel is a placeholder).
* Preferences dialog.
* Welcome screen / new-document dialog (the New Document dialog is minimal:
  size, background and transparent/white; the full New Project window with
  units, DPI and comic/animation modes is not built yet).
* Cross-platform installer packaging, signed Windows binary, auto-update.
* Physical tablet/stylus verification has not been done on real hardware.

Menus and buttons that would trigger unfinished features are visibly
disabled and are never falsely presented as working.

## 8. Roadmap (next phases)

* **Phase 2 — Document & brush engine (in progress).** Done: real document
  model, sparse tile storage, CPU compositing, first pressure-aware
  raster brush, bounded tile-based undo/redo, pan and zoom, open/save of
  `.koma` illustrations. Not yet: advanced/traditional-media brushes,
  document-wide memory budget.
* **Phase 3 — Layers & compositing.** Full layer list UI, opacity,
  visibility, basic blend modes, transforms, selections.
* **Phase 4 — Animation.** Frame timeline, key exposure sheet, onion
  skinning, playback loop controls.
* **Phase 5 — File I/O & export.** Export of common image formats,
  autosave, recent files in the File menu. (Open/save of `.koma`
  illustrations landed in Phase 2.)
* **Phase 6 — Windows packaging.** Installer, signed binary, auto-update,
  crash reporting.

## 9. Validation

* **Unit tests:** the `tests/unit` directory contains QtTest-based unit
  tests for the undo stack, layer model, timeline state, canvas document,
  tile geometry/storage, dirty tracking, CPU compositor, brush engine,
  viewport, document creation and the `.koma` file format. They are
  registered with CTest via `add_test()` in `tests/unit/CMakeLists.txt`
  and run on Windows (MSVC 2022, Qt 6) in GitHub Actions on every push
  to every branch; the packaged executable is also launch-smoke-tested
  in that workflow. See `docs/development/building.md` for local builds.
* **QML:** QML files have been processed with `qmlformat`; imports are
  structured so they resolve both from the source tree (for the PySide6
  launcher) and from the Qt resource bundle (for the production binary).
* **Production executable:** the C++ binary embeds all QML via a Qt
  resource collection (`resources/qml.qrc`), so `windeployqt` picks up
  the QML modules it needs without `--qmldir` and the resulting
  `komapaint.exe` is self-contained. The final compiled executable has
  not yet been produced in this environment.
* **PySide6 launcher:** `tools/run_qml.py` is a UI prototyping
  convenience only; it is not the supported runtime.

See `docs/development/building.md` for more detail.

## 10. Contributing

KomaPaint is not yet accepting external contributions during its early
foundation phase. Once the codebase reaches its first public beta,
contribution guidelines will be published.
