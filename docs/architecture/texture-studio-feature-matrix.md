# Advanced Texture Studio — audit and feature matrix

Status: stages 1–4 and the first persistence/undo pass are implemented on
`arena/64f169fc-komapaint`. Stage 5 (tablet/touch adaptation) and stage 6
(regression verification) are tracked below; Android hardware remains
unverified because this repository has no Android CI or device farm.

## What artists get

The menu path is deliberately short:

1. **Brush Studio → Texture** in the component rail (or the Texture header in
   the preview pad).
2. Tap **Add texture** and choose a starter grain, or **Import…** an image.
3. Drag **Strength** and **Scale**; draw in the **Live Preview**.
4. **Save**.

Starting a new texture layer never changes the tip or removes paint by itself:
it adds an independent grain layer. The layer can be switched off or removed
without deleting its image resource. Rotation, contrast, inversion, position,
variation, pressure response and grain blending are behind **More texture
controls**.

## Audit at the start of the work

| Area | Digital engine | Realistic engine | `.kb` storage before | Studio before |
|---|---|---|---|---|
| Bitmap texture/mask on a mark | Yes, but tip-local only | No; use the physical paper and tooth controls | Component `resourceId` + mask bytes | Tip picker existed; no strength/scale surface |
| Strength | Yes (`mask-strength`) | No bitmap path | Component parameters | No dedicated control |
| Scale | Yes (`mask-scale`) | No bitmap path | Component parameters | No dedicated control |
| Rotation / offset | Yes | No bitmap path | Component parameters | Only in the generic parameter list |
| Contrast / depth | Yes | No bitmap path | Component parameters | Generic parameter list |
| Inversion | Code existed but the adapter never populated `maskInvert` | No | Stored, not rendered | Looked available |
| Variation | Missing | Missing | Missing | Missing |
| Placement (tip / stroke / canvas) | Tip only | n/a | Missing | Missing |
| Multiple independent texture layers | Missing | Missing | Missing | Missing |
| Procedural tooth grain | n/a | Yes (`textureStrength` and paper profile) | Material parameters | Realistic controls |
| Texture images/resources | Yes | Stored | `RSRC` chunk, deduplicated by shared pixel block | Asset/tip rail |
| Undo/redo of texture edits | n/a | n/a | n/a | Generic parameter edits only |

The audit found one important trap: known `.kb` chunks reject trailing bytes.
New fields therefore could not be appended to `COMP` or the existing brush
meta. They are written in a new `TXLY` chunk, which old builds preserve as an
unknown chunk — so an older KomaPaint opens and re-saves a textured brush
without losing the stack.

## What now works

| Feature | Digital | Realistic | Notes |
|---|---|---|---|
| Choose an existing image | Yes | Data only; it does not paint | The layer stays in the file and the Studio says Realistic follows its paper instead. |
| Import PNG/JPG/BMP/TIFF | Yes | Data only | The full-size source is kept (RGBA for colour/transparent images, 8-bit for grayscale); grain is perceived brightness multiplied by alpha. No resize or colour loss. |
| Starter grains (paper, canvas, speckle, mottle) | Yes | Data only | Generated as ordinary resources, 256², deterministic. |
| Strength | Yes | Data only | 0–100%, mixed per pixel. |
| Scale | Yes | Data only | 5–2000% of the natural image size. |
| Rotation | Yes | Data only | −180°…180°. |
| Offset X/Y | Yes | Data only | Fraction of the brush radius. |
| Contrast | Yes | Data only | 0–200%. |
| Invert | Yes | Data only | Swaps light and dark grain. |
| Variation | Yes | Data only | Per-stamp jitter, seeded by the stroke seed. |
| Tip placement | Yes | n/a | Grain repeats in each stamp's local space and follows tip rotation. |
| Stroke placement | Yes | n/a | Grain follows stroke movement and the stroke direction. |
| Canvas placement | Yes | n/a | Grain is fixed in document space and does not move with the brush. |
| Grain blending | Yes | n/a | Normal (soft), Multiply (dark breaks paint), Screen (light breaks paint), Overlay (crisp). |
| Disable / remove a layer | Yes | Yes, data kept | Removing a layer does not delete its resource. |
| Reorder / many layers | Yes | Yes, data kept | No fixed count or size constant; admission is by available memory. |
| Pressure deepens grain | Yes, opt-in | No | Only consumed when the device reports pressure (`hasPressure`); a mouse or touch sample cannot activate it, and the setting defaults off. |
| Save / reopen | Yes | Yes | `TXLY` round-trips every field; reorder is preserved. |
| Undo / redo | Yes | Yes | Each texture edit is a normal brush-history step; a slider drag is one step. |

`Realistic` deliberately does **not** pretend to render an image texture. Its
physical paper/tooth model is separate. The Texture panel still edits and saves
the stack, marks it as not rendering, and the Realistic parameter list keeps its
own grain controls.

## Rendering path (no second renderer)

The Live Preview pad and the canvas both build `drawing::DocumentStroke` from
the same `BrushDocument`. The adapter builds one immutable vector of
`TextureGate`s per stroke:

* disabled layers, zero-strength layers and missing images are skipped;
* image pointers are borrowed from the document for the stroke's lifetime;
* the stroke samples textures with wrap (repeat), while legacy bitmap tips and
  masks keep their original non-repeating sample;
* gates are applied per painted pixel, after the legacy mask and before the
  existing pixel mask;
* `Tip` samples local to the stamp, `Stroke` samples stroke-relative and
  rotates with the stroke angle, `Canvas` samples absolute document space;
* variation uses a deterministic hash of layer index and stamp seed, so a
  replay is identical;
* colour blending is component blending, not alpha replacement.

The preview therefore uses the same evaluator and pixel sink as production;
there is no mock renderer and no separate preview shader.

## Banking, memory and Android adaptation

* The texture image lives once in the brush's resource store. A stroke holds
  borrowed pointers; it does not copy or decode an image per stamp.
* The pixel loop writes through the existing `SparseTileStore` and
  `DirtyTileTracker`. No full-canvas recalculation is introduced.
* Layers are limited by actual admission: `BrushStudioModel` asks its
  `MemoryCheck` before importing and before history metadata grows. There is no
  fixed layer-count, dimension or scale constant beyond storage limits.
* Import decodes at source size. The preview uses the normal small preview
  surface, so the on-screen copy is smaller while the saved resource remains
  full size.
* Resource exhaustion fails with a clear message and leaves the document
  unchanged (`Nothing was changed`), rather than silently dropping a layer or
  lowering saved resolution.
* The render path is shared C++ and has no desktop-only APIs. Android still
  needs a real device pass for stylus pressure, touch targets and long-session
  memory; that has not been run.

## Known limitations / honest status

* Realistic painting does not sample image textures. No control is shown as
  rendering there; the data is preserved.
* Android build and hardware tests were not run. Windows CI compiles and tests
  the Qt application; that is not device testing.
* Stylus pressure was exercised through unit-level genuine-pressure samples,
  not on physical tablet hardware. Touch/stylus feel remains a human test.
* The Studio does not yet expose a texture library outside the brush; images
  are imported into the brush or added as starters. Reuse from the brush's
  existing resources is available through the texture chooser.
* Legacy brush-wide texture component parameters still exist for old brushes.
  The Studio hides that legacy Texture section in Texture mode when a stack is
  present, to avoid two conflicting grain surfaces; the stored values remain
  untouched and editable from other modes.
* Search/filter, per-layer duplicating and texture painting directly onto the
  canvas are out of scope for this pass.

## Verification

Qt-free suites (run locally with `g++ -std=c++20`):

* `tests/unit/TestBrushModel.cpp` — ids, resource usage, ordering, validation,
  removal that keeps images, missing resources, blend/range diagnostics.
* `tests/unit/TestBrushFormat.cpp` — `TXLY` round-trip, ordering, all fields,
  deterministic bytes, duplicate/corrupt chunk handling.
* `tests/unit/TestBrushDocumentEngine.cpp` — production pixel tests for grain
  removal, disable, two-layer combination, tip/stroke/canvas differences,
  variation, opt-in genuine pressure, and no pressure fabrication.

Qt suite (CI):

* `tests/unit/TestBrushStudioModel.cpp` — add starter/import, candidates,
  selection, one-step slider undo/redo, placement and advanced fields,
  reorder/remove that preserves resources, save/reopen, Realistic honesty.
* Existing QML UI tests cover the Studio shell; a dedicated touch-target and
  long-press pass is still required on hardware.
