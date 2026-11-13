# Icon asset manifest

The files under `ui/assets/icons/`, the upload each one was made from, and the
place in the interface where it is used. This list is the single reference for
uploads and for QML integration.

**Current state: 37 icons uploaded, corrected, registered in
`resources/qml.qrc` and used by the interface** (21 tools, 12 layers, 4 project
types). No icon has been supplied for `toolbar/` or `panels/`; those controls
still show text symbols (see "Not supplied").

## File format

| Property | `tools/`, `panels/`, `layers/`, `toolbar/`          | `project-types/`                        |
|----------|-----------------------------------------------------|-----------------------------------------|
| Format   | PNG, 8-bit RGBA, transparent background             | PNG, 8-bit RGBA, transparent background |
| Canvas   | 128 × 128 px                                        | 256 × 256 px                            |
| Colour   | one flat colour; only the shape (alpha) is used     | full colour allowed                     |
| States   | one file per icon, no hover/selected/disabled files | one file per icon                       |

- The application gives the single-colour icons their colour, so one file serves
  the normal, hover, selected and disabled states (`ui/components/KpIcon.qml`,
  `ui/components/IconButton.qml`). The tints belong to the theme
  (`ui/themes/KomaTheme.qml`): `iconNormal`, `iconHover`, `iconSelected`,
  `iconDisabled` and `iconOff`, each with its own dark-theme and light-theme
  default, and the first four can be overridden per user in Edit → Preferences
  (stored as `ui/icons/<state>`; an empty value means "use the theme default").
  Tinting only multiplies the existing alpha channel, so transparency and the
  antialiased edges are preserved; the full-resolution source is loaded at the
  display size, so nothing is resampled twice.
- The four project-type illustrations are supplied as dark single-colour line
  drawings, but they are treated as finished artwork, not as glyphs: they are
  drawn **untinted** (`KpIcon.tinted: false`) and shown on a paper-coloured
  preview card in `NewProjectDialog.qml`, which keeps their own colours legible
  in both themes. Full-colour artwork dropped into `project-types/` later needs
  no change — that directory is never recoloured.

## Originals and corrected files

- The upload exactly as received is kept at `originals/<category>/<upload name>`
  and is never edited.
- The corrected file is written to `<category>/<name>.png`. Only this file is
  registered in `resources/qml.qrc` and used by the application.
- Corrected files are produced by `tools/icons/normalize_icons.py` (needs
  Pillow and numpy). Run from the repository root:

  ```
  python3 tools/icons/normalize_icons.py            # rewrite the corrected files and corrections.json
  python3 tools/icons/normalize_icons.py --check    # verify they match the originals
  ```

- `corrections.json` holds the measurements for every file: original bounds
  and padding, scale factor, optical shift, final bounds and padding, and notes.

## How uploads are corrected

Every correction is made in the PNG. Nothing is compensated in QML through
anchors, margins, padding or positioning.

1. **One colour.** The artwork is written in one flat dark colour. Lighter
   shading inside a drawing (`gradient`, `smudge`) is kept as transparency, so
   it survives tinting.
2. **Stray pixels.** Almost transparent haze that is not attached to the
   drawing is removed (counted per file in `corrections.json`). Pieces of a
   neighbouring drawing that touch the canvas edge are removed and reported.
3. **Measure.** The artwork bounds are read from the alpha channel (alpha > 16).
4. **Scale.** Uniform scaling only; the width-to-height ratio is never changed.
   - `tools/`: longest side at most 104 px, and the geometric mean of width and
     height at most 90 px, so a compact solid shape and a long thin one look
     equally large.
   - `layers/`: the layer-stack base that these icons share is 92 px wide in
     every icon; height at most 96 px.
   - `project-types/`: longest side at most 200 px, mean side at most 192 px.
5. **Centre.** Half-way between the centre of the bounds and the centre of
   mass of the shape (optical centring), limited to 3 % of the canvas and kept
   inside the live area.
6. **Never** stretched, distorted, redrawn or cropped.

All 37 uploads already had the right canvas size. They sat 2–8 px up and to the
left of centre with uneven padding, and differed in scale by up to a third.

## `tools/` – tool palette (`ui/components/ToolPalette.qml`)

The filename is the tool's internal id (`Workspace.activeTool`), so the palette
derives the path from the id. The same file is shown in the Sub Tool panel
header. Order as in the palette.

| File                   | Upload           | UI function              | Key | Status          |
|------------------------|------------------|--------------------------|-----|-----------------|
| `tools/zoom.png`       | `zoom.png`       | Zoom tool                | Z   | Works           |
| `tools/hand.png`       | `hand.png`       | Hand (move the view)     | H   | Works           |
| `tools/move.png`       | `move.png`       | Move layer contents      | –   | No function yet |
| `tools/transform.png`  | `transform.png`  | Transform                | –   | No function yet |
| `tools/selection.png`  | `selection.png`  | Rectangular selection    | –   | No function yet |
| `tools/lasso.png`      | `lasso.png`      | Lasso selection          | –   | No function yet |
| `tools/wand.png`       | `wand.png`       | Auto select (magic wand) | –   | No function yet |
| `tools/eyedropper.png` | `eyedropper.png` | Eyedropper               | I   | Works           |
| `tools/pen.png`        | `pen.png`        | Pen                      | P   | Works           |
| `tools/pencil.png`     | `pencil.png`     | Pencil                   | N   | Works           |
| `tools/brush.png`      | `paint.png`      | Brush                    | B   | Works           |
| `tools/airbrush.png`   | `airbrush.png`   | Airbrush                 | A   | Works           |
| `tools/eraser.png`     | `eraser.png`     | Eraser                   | E   | Works           |
| `tools/smudge.png`     | `smudge.png`     | Blend (smudge)           | –   | No function yet |
| `tools/fill.png`       | `fill.png`       | Fill                     | –   | No function yet |
| `tools/gradient.png`   | `gradient.png`   | Gradient                 | –   | No function yet |
| `tools/line.png`       | `line.png`       | Straight line            | –   | No function yet |
| `tools/curveline.png`  | `curveline.png`  | Curve                    | –   | No function yet |
| `tools/rectangle.png`  | `rectangle.png`  | Rectangle                | –   | No function yet |
| `tools/circle.png`     | `circle.png`     | Ellipse                  | –   | No function yet |
| `tools/text.png`       | `text.png`       | Text                     | –   | No function yet |

- **Works**: zoom, hand and eyedropper are handled by the canvas
  (`ui/components/CanvasWorkspace.qml`); pen, pencil, brush, airbrush and eraser
  paint with the brush engine and have sub tools (`src/app/SubToolLibrary.cpp`).
- **No function yet**: the tool is shown dimmed, cannot be selected, and its
  tooltip says it is not available yet.
- `paint.png` is the paintbrush drawing; it is stored as `brush.png` because the
  brush tool's id is `brush`.

## `layers/` – Layers panel (`ui/panels/LayersPanel.qml`)

These twelve files were uploaded to `panels/`. They are layer commands, so the
originals are kept in `originals/layers/` and the corrected files are here.

| File                           | Upload                 | UI function                                                                          | Status          |
|--------------------------------|------------------------|--------------------------------------------------------------------------------------|-----------------|
| `layers/lock.png`              | `Alphalock.png`        | Lock / unlock the layer: attribute bar button and row toggle (dimmed while unlocked) | Works           |
| `layers/lock-transparency.png` | `Transparencylock.png` | Lock transparent pixels (attribute bar)                                              | No function yet |
| `layers/reference.png`         | `Referencelayer.png`   | Set as reference layer (attribute bar)                                               | No function yet |
| `layers/mask.png`              | `Mask.png`             | Layer mask (attribute bar)                                                           | No function yet |
| `layers/merge-down.png`        | `Merge.png`            | Merge with the layer below (attribute bar)                                           | No function yet |
| `layers/visible.png`           | `Layervis.png`         | Row toggle: show / hide the layer (dimmed while hidden)                              | Works           |
| `layers/new-raster.png`        | `Raster.png`           | New raster layer (command bar)                                                       | Works           |
| `layers/new-vector.png`        | `Vector.png`           | New vector layer (command bar)                                                       | No function yet |
| `layers/folder.png`            | `Folder.png`           | New layer folder (command bar)                                                       | No function yet |
| `layers/adjustment-layer.png`  | `Adjlayer.png`         | New adjustment layer (command bar)                                                   | No function yet |
| `layers/move.png`              | `Layermove.png`        | Label of the "move layer up / down" pair (command bar)                               | Works           |
| `layers/delete.png`            | `Layerdelete.png`      | Delete the selected layer (command bar)                                              | Works           |

## `project-types/` – New Project dialog (`ui/components/NewProjectDialog.qml`)

| File                                 | Upload             | UI function                   | Status          |
|--------------------------------------|--------------------|-------------------------------|-----------------|
| `project-types/illustration.png`     | `Illustration.png` | Illustration project type     | Works           |
| `project-types/comic-page.png`       | `Comic.png`        | Comic Page project type       | No function yet |
| `project-types/multi-page-comic.png` | `Multicomic.png`   | Multi-page Comic project type | No function yet |
| `project-types/animation.png`        | `Animation.png`    | Animation project type        | No function yet |

Removed from two uploads, because they are pieces of a neighbouring drawing and
not part of the icon (the originals still contain them):

- `Comic.png`: a 184 px sliver at the right canvas edge (x 252–255, y 132–206).
- `Multicomic.png`: a 188 px sliver at the left canvas edge (x 0–2, y 23–114).

## Not supplied

No file exists for the following, so nothing is registered and the interface
keeps its text symbols there.

| Expected file                                                   | Where                                   | Shown today      |
|-----------------------------------------------------------------|-----------------------------------------|------------------|
| `toolbar/new.png`, `open.png`, `save.png`                        | `ui/components/MainToolbar.qml`         | N, O, S          |
| `toolbar/undo.png`, `redo.png`                                   | `ui/components/MainToolbar.qml`         | ↶, ↷             |
| `toolbar/zoom-in.png`, `zoom-out.png`, `zoom-fit.png`, `zoom-reset.png` | `ui/components/MainToolbar.qml`  | +, −, ⤧, 1:1     |
| `toolbar/tab-close.png`                                          | project tabs, `ui/workspaces/MainWindow.qml` | ×           |
| `panels/close.png`, `panels/menu.png`                            | `ui/components/PanelHeader.qml`         | ×, ≡             |
| `layers/move-up.png`, `layers/move-down.png`                     | `ui/panels/LayersPanel.qml`             | ▲, ▼             |

A separate `layers/hidden.png` and `layers/unlocked.png` are not needed: the
"off" state is the same icon in the dimmed tint.

## Measurements

Padding is left / top / right / bottom in pixels, before and after correction.

### `tools/`

| Upload           | File             | Padding before    | Scale  | Padding after     |
|------------------|------------------|-------------------|--------|-------------------|
| `airbrush.png`   | `airbrush.png`   | 8 / 14 / 14 / 30  | ×0.954 | 12 / 27 / 15 / 21 |
| `circle.png`     | `circle.png`     | 20 / 28 / 24 / 34 | ×1.209 | 14 / 24 / 13 / 24 |
| `curveline.png`  | `curveline.png`  | 11 / 31 / 17 / 22 | ×1.039 | 12 / 30 / 12 / 22 |
| `eraser.png`     | `eraser.png`     | 19 / 21 / 25 / 22 | ×1.065 | 16 / 22 / 22 / 15 |
| `eyedropper.png` | `eyedropper.png` | 27 / 18 / 27 / 17 | ×1.085 | 22 / 16 / 25 / 12 |
| `fill.png`       | `fill.png`       | 10 / 26 / 19 / 21 | ×1.005 | 16 / 22 / 12 / 25 |
| `gradient.png`   | `gradient.png`   | 20 / 35 / 28 / 32 | ×1.288 | 13 / 26 / 12 / 22 |
| `hand.png`       | `hand.png`       | 23 / 23 / 32 / 24 | ×1.170 | 18 / 16 / 24 / 18 |
| `lasso.png`      | `lasso.png`      | 27 / 18 / 23 / 20 | ×1.074 | 24 / 18 / 20 / 14 |
| `line.png`       | `line.png`       | 24 / 21 / 23 / 26 | ×1.111 | 19 / 19 / 18 / 18 |
| `move.png`       | `move.png`       | 13 / 16 / 22 / 19 | ×0.968 | 19 / 19 / 19 / 19 |
| `paint.png`      | `brush.png`      | 20 / 19 / 25 / 23 | ×1.065 | 21 / 16 / 20 / 20 |
| `pen.png`        | `pen.png`        | 25 / 18 / 17 / 21 | ×1.029 | 21 / 18 / 19 / 18 |
| `pencil.png`     | `pencil.png`     | 20 / 17 / 17 / 18 | ×0.978 | 19 / 19 / 20 / 18 |
| `rectangle.png`  | `rectangle.png`  | 20 / 32 / 23 / 32 | ×1.220 | 13 / 25 / 12 / 25 |
| `selection.png`  | `selection.png`  | 20 / 24 / 25 / 25 | ×1.111 | 18 / 20 / 18 / 21 |
| `smudge.png`     | `smudge.png`     | 24 / 16 / 22 / 15 | ×1.009 | 22 / 16 / 23 / 14 |
| `text.png`       | `text.png`       | 22 / 27 / 24 / 24 | ×1.133 | 18 / 24 / 18 / 16 |
| `transform.png`  | `transform.png`  | 13 / 18 / 19 / 16 | ×0.947 | 18 / 20 / 19 / 19 |
| `wand.png`       | `wand.png`       | 28 / 16 / 23 / 27 | ×1.112 | 24 / 17 / 20 / 16 |
| `zoom.png`       | `zoom.png`       | 19 / 18 / 26 / 26 | ×1.078 | 19 / 19 / 19 / 18 |

### `layers/`

| Upload                 | File                    | Padding before    | Scale  | Padding after     |
|------------------------|-------------------------|-------------------|--------|-------------------|
| `Raster.png`           | `new-raster.png`        | 16 / 27 / 21 / 27 | ×1.011 | 18 / 28 / 18 / 25 |
| `Vector.png`           | `new-vector.png`        | 15 / 21 / 20 / 29 | ×0.989 | 18 / 26 / 18 / 25 |
| `Folder.png`           | `folder.png`            | 15 / 23 / 22 / 38 | ×1.011 | 19 / 28 / 17 / 32 |
| `Adjlayer.png`         | `adjustment-layer.png`  | 23 / 24 / 31 / 34 | ×1.243 | 17 / 19 / 19 / 21 |
| `Mask.png`             | `mask.png`              | 18 / 26 / 22 / 27 | ×1.045 | 18 / 24 / 18 / 26 |
| `Referencelayer.png`   | `reference.png`         | 26 / 32 / 33 / 34 | ×1.333 | 19 / 23 / 19 / 22 |
| `Alphalock.png`        | `lock.png`              | 19 / 18 / 23 / 30 | ×1.070 | 18 / 21 / 18 / 22 |
| `Transparencylock.png` | `lock-transparency.png` | 16 / 22 / 20 / 26 | ×1.000 | 18 / 24 / 18 / 24 |
| `Layermove.png`        | `move.png`              | 23 / 20 / 33 / 30 | ×1.231 | 20 / 14 / 20 / 19 |
| `Merge.png`            | `merge-down.png`        | 12 / 13 / 23 / 27 | ×0.989 | 18 / 18 / 18 / 23 |
| `Layerdelete.png`      | `delete.png`            | 19 / 24 / 29 / 25 | ×1.150 | 18 / 19 / 18 / 18 |
| `Layervis.png`         | `visible.png`           | 21 / 38 / 23 / 42 | ×1.095 | 19 / 38 / 18 / 38 |

### `project-types/`

| Upload             | File                   | Padding before    | Scale  | Padding after     |
|--------------------|------------------------|-------------------|--------|-------------------|
| `Illustration.png` | `illustration.png`     | 50 / 32 / 29 / 31 | ×1.036 | 38 / 26 / 34 / 31 |
| `Comic.png`        | `comic-page.png`       | 53 / 32 / 57 / 41 | ×1.093 | 48 / 27 / 49 / 29 |
| `Multicomic.png`   | `multi-page-comic.png` | 59 / 14 / 18 / 54 | ×1.047 | 35 / 31 / 35 / 28 |
| `Animation.png`    | `animation.png`        | 27 / 13 / 20 / 58 | ×0.957 | 28 / 37 / 28 / 41 |

## Resource registration

- Corrected icons are listed in `resources/qml.qrc`, next to the QML files.
  That file is compiled into the application and into the interface tests, so
  no CMake change is needed.
- Each file has an alias without the `ui/assets/` prefix, for example

  ```xml
  <file alias="icons/tools/brush.png">../ui/assets/icons/tools/brush.png</file>
  ```

  and QML refers to it through `KpIcon { name: "tools/brush" }`, which resolves
  to `qrc:/icons/tools/brush.png`.
- An entry is added only when its corrected PNG exists; a `.qrc` entry for a
  missing file stops the build.
- Files under `originals/` are never listed, so they are not shipped.
- `tests/unit/TestBrushUi.cpp` checks that all 37 files are in the resources,
  have the right canvas size, use one colour and are centred.
