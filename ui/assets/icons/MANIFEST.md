# Icon asset manifest

Expected files under `ui/assets/icons/`, and the UI function each one serves.
This list is the single reference for uploads and for QML integration.

**Current state: no icon has been uploaded, none is registered in a `.qrc`
file, and no QML file references any of them.**

Totals: 32 required files and 4 optional ones, in five directories.

## File format

| Property | `tools/`, `panels/`, `layers/`, `toolbar/`          | `project-types/`                        |
|----------|-----------------------------------------------------|-----------------------------------------|
| Format   | PNG, 8-bit RGBA, transparent background             | PNG, 8-bit RGBA, transparent background |
| Canvas   | 128 × 128 px                                        | 256 × 256 px                            |
| Colour   | one flat colour; only the shape (alpha) is used     | full colour allowed; used as drawn      |
| States   | one file per icon, no hover/selected/disabled files | one file per icon                       |

- The application tints the single-colour icons itself for the normal, hover,
  selected and disabled states. A baked-in background plate, shadow or second
  colour would be lost or look wrong when tinted.
- Unavailable project types are dimmed by the application; no separate
  greyed-out illustration is expected.
- Keep the same stroke weight, corner style and visual scale within a
  directory, so the icons read as one set.

## Originals and corrected files

- The upload exactly as received is kept at
  `originals/<category>/<name>.png` and is never edited.
- The corrected file is written to `<category>/<name>.png`. Only this file is
  used by the application.
- An upload placed directly in a category directory is first copied,
  byte for byte, to `originals/<category>/` and only then corrected.

## How uploads are corrected

Every correction is made in the PNG. Nothing is compensated in QML through
anchors, margins, padding or positioning.

1. **Measure.** The real artwork bounds are read from the alpha channel.
   Stray, almost transparent pixels that would distort the bounds are reported,
   not silently removed.
2. **Check the format.** Canvas size, transparency and, for the single-colour
   categories, that the artwork really uses one colour. Deviations are
   reported.
3. **Padding.** Excessive or uneven transparent padding is removed. Target
   margin: 12 px on a 128 px canvas (artwork within the central 104 × 104 px)
   and 16 px on a 256 px canvas (within 224 × 224 px).
4. **Scale.** Uniform scaling only; the width-to-height ratio of the artwork is
   never changed. Scale is matched across the icons of one category by visual
   size, not by making every bounding box identical, so that a compact solid
   shape and a thin diagonal one look equally large.
5. **Centre.** Centred by the bounding box first, then checked optically using
   the visual weight of the shape (for example a play triangle or a pencil
   with a heavy end), and nudged where the mathematical centre looks off.
6. **Never** stretch, distort, redraw or crop any part of the artwork.
7. **Report.** For every file: original bounds, final bounds, scale factor and
   offset applied, with a before/after sheet per category for review.

Enlarging small artwork cannot add detail and softens edges. If an upload
would need noticeable enlargement to reach the common scale, that is reported
for a decision instead of being applied silently.

## Status column

- **Works** – the function exists and works in the application today.
- **No function yet** – the control exists in the interface but the engine
  does nothing for it yet. The icon would only ever be shown as unavailable
  until the function is implemented.
- **Optional** – no place in the current interface; only needed if the
  redesign proposal is approved.

## `tools/` – tool palette (`ui/components/ToolPalette.qml`)

The filename is the tool's internal id, so the palette can derive the path
from the id.

| File                   | UI function        | Shortcut | Status          |
|------------------------|--------------------|----------|-----------------|
| `tools/selection.png`  | Selection tool     | V        | No function yet |
| `tools/move.png`       | Move tool          | M        | No function yet |
| `tools/brush.png`      | Brush tool         | B        | Works           |
| `tools/pencil.png`     | Pencil tool        | P        | No function yet |
| `tools/eraser.png`     | Eraser tool        | E        | No function yet |
| `tools/fill.png`       | Fill (bucket) tool | G        | No function yet |
| `tools/eyedropper.png` | Eyedropper tool    | I        | No function yet |
| `tools/text.png`       | Text tool          | T        | No function yet |

## `panels/` – dock panel header (`ui/components/PanelHeader.qml`)

| File                    | UI function                     | Status          |
|-------------------------|---------------------------------|-----------------|
| `panels/close.png`      | Close (hide) the panel          | Works           |
| `panels/menu.png`       | Open the panel's menu           | No function yet |
| `panels/layers.png`     | Symbol for the Layers panel     | Optional        |
| `panels/properties.png` | Symbol for the Properties panel | Optional        |
| `panels/color.png`      | Symbol for the Color panel      | Optional        |
| `panels/timeline.png`   | Symbol for the Timeline panel   | Optional        |

The Assets, History and Navigator panels do not exist yet; no icon is expected
for them.

## `layers/` – Layers panel (`ui/panels/LayersPanel.qml`)

| File                   | UI function                                     | Status |
|------------------------|-------------------------------------------------|--------|
| `layers/add.png`       | Add a new layer                                 | Works  |
| `layers/delete.png`    | Delete the selected layer                       | Works  |
| `layers/move-up.png`   | Move the selected layer up                      | Works  |
| `layers/move-down.png` | Move the selected layer down                    | Works  |
| `layers/visible.png`   | Row toggle, shown while the layer is visible    | Works  |
| `layers/hidden.png`    | Row toggle, shown while the layer is hidden     | Works  |
| `layers/locked.png`    | Row toggle, shown while the layer is locked     | Works  |
| `layers/unlocked.png`  | Row toggle, shown while the layer is not locked | Works  |

Duplicate Layer and Layer Properties are disabled menu entries with no engine
function; no icon is expected for them yet.

## `toolbar/` – main toolbar and project tabs

All from `ui/components/MainToolbar.qml`, except `tab-close.png`, which belongs
to the project tab bar in `ui/workspaces/MainWindow.qml`.

| File                     | UI function                           | Status |
|--------------------------|---------------------------------------|--------|
| `toolbar/new.png`        | New project                           | Works  |
| `toolbar/open.png`       | Open a project file                   | Works  |
| `toolbar/save.png`       | Save the current project              | Works  |
| `toolbar/undo.png`       | Undo                                  | Works  |
| `toolbar/redo.png`       | Redo                                  | Works  |
| `toolbar/zoom-in.png`    | Zoom in                               | Works  |
| `toolbar/zoom-out.png`   | Zoom out                              | Works  |
| `toolbar/zoom-fit.png`   | Fit the canvas to the window          | Works  |
| `toolbar/zoom-reset.png` | Zoom to 100 % (shown as "1:1" today)  | Works  |
| `toolbar/tab-close.png`  | Close a project tab                   | Works  |

The toolbar's "Workspace" button has no function; no icon is expected for it.

## `project-types/` – New Project dialog (`ui/components/NewProjectDialog.qml`)

| File                                 | UI function                   | Status          |
|--------------------------------------|-------------------------------|-----------------|
| `project-types/illustration.png`     | Illustration project type     | Works           |
| `project-types/comic-page.png`       | Comic Page project type       | No function yet |
| `project-types/multi-page-comic.png` | Multi-page Comic project type | No function yet |
| `project-types/animation.png`        | Animation project type        | No function yet |

## Controls without an expected icon

The Timeline panel (`ui/panels/TimelinePanel.qml`) shows first-frame, play,
pause and last-frame symbols. These buttons only change a playing flag and the
frame counter; animation itself is not implemented. No icon is expected for
them until it is.

## Integration plan (not done yet)

- Corrected icons will be listed in the existing `resources/qml.qrc`, next to
  the QML files. That file is already compiled into the application and into
  the four interface tests, so no CMake change is needed.
- Each file gets an alias without the `ui/assets/` prefix, for example:

  ```xml
  <file alias="icons/tools/brush.png">../ui/assets/icons/tools/brush.png</file>
  ```

  QML then refers to it as `qrc:/icons/tools/brush.png`.
- An entry is added only after its corrected PNG exists, because a `.qrc`
  entry for a missing file stops the build.
- Files under `originals/` are never listed, so they are not shipped.
- PNG decoding is built into Qt, so no extra Qt module or packaging change is
  required.
