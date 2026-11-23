# Brush strokes reading as separate circles: diagnosis

Status: investigation complete, no renderer or UI changes made yet.
Scope: the stroke appearance reported in Brush Studio, the renderer that
produces it, and the production path it has to match.

## 1. Which application produced the circles

The binaries delivered so far for Brush Studio are the isolated review
application under `docs/design/brush-studio-review-03/` (workflow
`brush-studio-review-03-windows.yml`). It is a PySide6/QML application with its
own Python painting code. It does **not** link `komapaint_drawing`, does not use
`CanvasViewport`, `BrushStroke`, `BrushRasterizer`, `SparseTileStore` or
`CpuTileCompositor`, and does not open a KomaPaint document.

The product application is `apps/desktop` (`KomaPaint.exe`). Its Brush Studio
(`ui/workspaces/brushstudio/`, `BrushStudioModel`, `BrushStudioPad`) is hosted
inside the normal application and paints through the production renderer.
That application has not been delivered to the user yet.

So the reported artefact comes from the review application's renderer, while
the behaviour that matters for the product is in a different renderer. Both
were examined.

## 2. Immediate cause in the review application

Two independent things in `docs/design/brush-studio-review-03/` make a stroke a
chain of discs rather than a band of ink.

### 2.1 The live stroke has a fixed 8-pixel stamp step

`render.py:642` (`render_stroke_body`) draws the stroke that is visible while
the pointer is down:

```python
step = max(8.0, size * 0.34)          # render.py:660
...
diameter = max(1.0, size * (0.55 + 0.45 * pressure))
painter.setOpacity(bound(0.22 + 0.22 * pressure))   # render.py:682
painter.drawImage(... stamp ...)
```

The step is never smaller than 8 document pixels, regardless of tip size or
pressure. For a tip whose *actual* dab diameter is under 8 px — a small Size
setting, or low pressure with pressure-to-size — consecutive dabs do not touch.
Each is a separate, semi-transparent circular image. The centre body from
`_stroke_body` (46 % strength in the live path, `render.py:646`) is too faint to
bridge them. The result is exactly the reported look: airbrush-like dots.

### 2.2 The finished stroke composites every circular stamp separately

`render.py:697` (`render_stroke`) resamples the path to stamp events and, for
each event, composites the whole brush tree directly into the stroke buffer:

```python
for sample in stream:                              # render.py:726
    ...
    composite_node(paint, doc, doc["root"], local, seed, ...)
```

Every dab is a textured circular alpha image (`assets.py:84`,
`mark_image("graphite")` is a noisy ellipse with a hard cutoff and a 0.42 px
blur). `draw_mark` multiplies per-dab flow, density and pressure
(`render.py:371-380`) and then draws with that alpha. Included components also
use non-Normal blends — Ferrous Ink carries `Soft light`, `Multiply` and
`Screen` secondaries (`recipes.py:95-121`). Because each dab is composited into
ink already left by earlier dabs, those secondaries lighten or darken the
overlap per dab. The boundaries of individual stamps therefore stay visible
instead of merging into one stroke, and the repeated tip texture repeats the
same circle at every step.

This is not a UI or parameter problem, and it is not fixed by changing Spacing
or Flow. It is the composition model of that renderer.

### 2.3 Reproduction

The review renderer was run offscreen with its own Ferrous Ink document, a
straight 60-sample path, `tip_size = 14`, `spacing = 18`, and pressure 0.9:

- live path: a chain of separate discs with visible gaps;
- finished path: a scalloped ribbon of individually visible 12–13 px discs;
- at `tip_size = 54` the finished path reads as a continuous band, but the
  live path still uses an 18 px step.

This was a local diagnostic of the Python renderer only. It is not appearance
evidence for KomaPaint and is not offered as such.

## 3. What the production renderer does

The integrated path is:

```
BrushStudioModel::document()
  -> BrushStudioPad::syncDraft()                       src/app/BrushStudioPad.cpp
  -> drawing::digitalBrushIssue()                      src/drawing/BrushDocumentAdapter.cpp
  -> BrushSettings::setNativeBrush()
  -> CanvasViewport::beginStrokeSample()               src/app/CanvasViewport.cpp
  -> drawing::digitalDefinition(document, runtime)
  -> drawing::DocumentStroke
  -> BrushStroke::begin/extend                         src/drawing/BrushEngine.cpp
  -> BrushRasterizer::dab
  -> SparseTileStore
  -> CpuTileCompositor
```

The Studio pad is not a simplified preview: `BrushStudioPad` subclasses
`CanvasViewport`, owns a private 4096x2048 `CanvasDocument` and an `UndoStack`,
and uses the same `beginStrokeSample`/`DocumentStroke` code as the artwork
canvas. It is the same renderer, with a different document.

Production does **not** use the review application's live overlay or its
per-dab texture compositing. `BrushStroke::extend` resamples the pointer path by
arc length and places dabs at a single interpolated spacing. With the current
Studio default document (one round procedural tip, Normal blend, registry
defaults Size 16 px, Spacing 15 %, Hardness 100) the spacing is
`max(0.25, 16 * 0.15) = 2.4 px` against a 16 px dab, so the stroke is a
continuous band. There is no 8 px floor.

## 4. Production defect of the same class

There is still a real gap-producing bug in the production stroke engine:

- `BrushStroke` computes its step once, from the nominal brush size:
  `m_spacing = max(0.25, brush.size * brush.spacing)`
  (`src/drawing/BrushEngine.cpp:450-468`).
- `BrushRasterizer::dab` then computes the actual dab from pressure and tilt:
  `effectiveSize = max(0.5, b.size * sizeScale * tiltScale)`
  (`src/drawing/BrushEngine.cpp:206-218`), where `sizeScale` can fall to
  `min-size-ratio` (default 15 %).

With Pressure to size enabled, low pressure shrinks the dab but not the step.
At Size 16, Spacing 50 % and pressure 0.2 with a 15 % minimum, the step is 8 px
while the dab is about 5 px across. The dabs no longer meet: the stroke becomes
a row of circles. This is the production equivalent of the reported symptom,
and it is a spacing/dab inconsistency, not a setting that should be nudged.

The same inconsistency exists for per-component scale: `paintStamp` multiplies
`dabBrush.size` by `request.scale` (`BrushDocumentAdapter.cpp:563`) while the
step remains based on the unscaled brush. Component offsets are also computed
from the unscaled radius (`radius = brush.size / 2`, line 544), so a
pressure-shrunk or scaled dab scatters around a larger circle than it occupies.

A separate integration limit: `DocumentStroke::paintStamp` rejects any request
whose blend is not `Normal` or that uses a mask
(`BrushDocumentAdapter.cpp:548`), and `digitalBrushIssue` reports the same
condition before painting. The review application's Ferrous Ink uses Soft
light, Multiply and Screen secondaries; that brush cannot paint in the
production Studio at all. This is a capability gap to report plainly, not to
hide by changing the brush.

## 5. Files and classes that must change

1. `src/drawing/BrushEngine.{h,cpp}`
   - Make stroke advance use the **current** dab diameter, including
     pressure-to-size and tilt, instead of the nominal size fixed at
     construction.
   - Keep spacing a fraction of the effective diameter, so increasing pressure
     enlarges both the dab and the step and the overlap ratio stays constant.
   - Keep a validated lower bound on the step, but derive it from the effective
     diameter, not a fixed 8 px and not a nominal-size constant.
   - Add a unit-level continuity assertion: for a straight stroke, every
     centreline pixel between the endpoints receives coverage, at pressure
     0.2/0.6/1.0, Size 8/32/128, Spacing 5/15/30/50, Pressure to size 0 and 100.

2. `src/drawing/BrushDocumentAdapter.cpp`
   - Use the resolved/effective dab radius for component offsets and scales, so
     components and the step agree.
   - Decide, separately, whether non-Normal components can be composed per
     stamp. That is required before the review application's multi-blend
     brushes can be painted by the product; it is not required to remove the
     circles from a Normal round brush.

3. `src/brush/BrushEvaluator.cpp`
   - `evaluate()` currently computes `size` and discards it (`(void)size`,
     line 359). The request offsets are fractions of tip radius; feed the
     resolved size through so pressure-scaled stamps place their components
     consistently.

4. `docs/design/brush-studio-review-03/render.py` (review app only)
   - Remove the `max(8.0, ...)` live step and the per-dab Screen/Multiply
     compositing, or stop shipping this renderer as a brush preview. It will
     not be used as product evidence either way.

No QML, no new parameters, no new categories, and no default-value changes are
part of this fix.

## 6. Verification plan

All appearance and release evidence will come from Windows x64.

1. **Production unit test (Qt-free).** Paint the same brush and path into a
   `SparseTileStore` at the parameter matrix above. Assert that the centreline
   has no zero-alpha run and that the measured stroke width follows pressure.
   This runs in the existing `windows-build.yml` test step.
2. **Parity check inside the application.** Add a non-interactive
   `--verify-brush-parity` mode next to `--verify-package`. It opens Brush
   Studio in the real application, paints a known path on `BrushStudioPad`,
   paints the identical path on the artwork canvas with the same installed
   sub-tool, reads both `SparseTileStore`s in document coordinates, and asserts
   bit-identical pixels after origin translation. It also saves both PNGs and a
   difference image.
3. **Visual matrix on Windows.** The same mode renders Size 8/32/128 and Spacing
   5/15/30/50, plus three brushes that must stay distinct: round ink, pencil
   and soft airbrush. The run records full-resolution PNGs and per-brush
   coverage/edge measurements. No Linux offscreen image is used as appearance
   evidence.
4. **Packaging.** The existing `windows-build.yml` stages `KomaPaint.exe` with
   `windeployqt`, checks the PE subsystem, runs the extracted ZIP through the
   GUI verification, and prints the file inventory. The ZIP will be published
   only after this run is green, with its real size and SHA-256 recorded from
   the release, and with the standalone review application kept out of it.
5. **Statement of what is being shown.** The deliverable will be the integrated
   KomaPaint Windows x64 application and its normal Brush Studio. The review
   application is not the product and will not be sent again as proof.

## 7. Immediate next step

Fix the production spacing/effective-diameter inconsistency first, add the
continuity test, then build and run the Windows parity mode. The UI stays as it
is.
