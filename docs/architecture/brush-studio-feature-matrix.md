# Brush Studio feature matrix

Status of the native brush document (`.kb`) in the production renderers. The
authority is code, not this table: `drawing::inspectNativeBrush()` decides
whether a document can be installed and painted, and
`drawing::digitalSupportsParameter()` / `drawing::realisticSupportsParameter()`
answer each setting. If this page and those functions disagree, the functions
are right and this page is a bug.

Legend: **yes** = the renderer reads the stored value for its default path;
**no** = the value is stored and preserved, but the renderer ignores it and
Studio explains why. A `no` is never silently discarded on save.

## Engines

| Engine | Preview / paint | Installation | Renderer |
| --- | --- | --- | --- |
| Digital | yes | Brush → Digital → active target tab | procedural or bitmap dabs, `BrushRasterizer` |
| Realistic | yes | Brush → Realistic → active target tab | material dabs, `MaterialEngine` + reservoir |

Both engines load the same `BrushDocument`. `inspectNativeBrush()` dispatches on
`document.engine`; the pad and the artwork viewport route by engine as well, so
a supported Realistic draft paints wet material instead of being refused.

Realistic paint accepts a document only when every visible mark component is a
`ProceduralShape`. Its tip is chosen from the component's `tip-shape`
(Round/Textured/Bristle/Graphite/Stipple) and then interpreted by the material
tip geometry. Image, Texture, mask-source, Particle, Pattern and Sequence
components are preserved but not painted by Realistic; Studio reports the
first one. Digital accepts the kinds marked below.

## Parameters

`yes` means the renderer consumes it on the normal painting path. Several rows
are deliberately engine-specific: physical paint settings belong to Realistic,
whereas taper, delay, masking and bitmap grain belong to Digital.

| Key | Label | Digital | Realistic |
| --- | --- | --- | --- |
| `size` | Size | yes | yes |
| `min-size-ratio` | Minimum | yes | no |
| `pressure-size` | Pressure to size | yes | no |
| `tilt-size` | Tilt to size | yes | no |
| `aspect` | Tip Shape | yes | yes |
| `tip-angle` | Tip angle | yes | yes |
| `hardness` | Hardness | yes | yes |
| `tilt-angle` | Tilt to angle | yes | no |
| `opacity` | Opacity | yes | yes |
| `flow` | Flow | yes | yes |
| `pressure-opacity` | Pressure to opacity | yes | no |
| `pressure-flow` | Pressure to flow | yes | no |
| `pressure-gamma` | Pressure curve | yes | no |
| `speed-size` | Size response to speed | yes | yes |
| `speed-opacity` | Opacity response to speed | yes | yes |
| `speed-flow` | Flow response to speed | yes | yes |
| `speed-spacing` | Spacing response to speed | yes | yes |
| `accumulate` | Accumulation | no | no |
| `spacing` | Spacing | yes | yes |
| `spacing-jitter` | Spacing variation | yes | no |
| `taper-in` | Taper in | yes | no |
| `taper-out` | Taper out | yes | no |
| `follow-direction` | Follow direction | yes | yes |
| `stabilization` | Stabilization | yes | yes |
| `stroke-delay` | Stroke delay | yes | no |
| `rotation-offset` | Rotation | yes | no |
| `rotation-jitter` | Rotation variation | no | no |
| `follow-rotation` | Follow pen rotation | yes | no |
| `scatter-x` | Scatter X | yes | no |
| `scatter-y` | Scatter Y | yes | no |
| `scatter-count` | Stamps per dab | yes | no |
| `cluster` | Clustering | yes | no |
| `texture-strength` | Texture | yes | no |
| `texture-scale` | Texture scale | yes | no |
| `texture-rotation` | Texture rotation | yes | no |
| `texture-offset-x` | Texture offset X | yes | no |
| `texture-offset-y` | Texture offset Y | yes | no |
| `texture-contrast` | Texture contrast | yes | no |
| `texture-depth` | Texture depth | yes | no |
| `hue-variation` | Hue variation | yes | no |
| `saturation-variation` | Saturation variation | yes | no |
| `brightness-variation` | Brightness variation | yes | no |
| `color-mix` | Colour mix | yes | no |
| `paint-load` | Paint load | no | yes |
| `pigment-concentration` | Pigment concentration | no | yes |
| `water-amount` | Water | no | yes |
| `paint-body` | Paint body | no | yes |
| `pickup` | Pickup | no | yes |
| `depletion` | Depletion | no | yes |
| `wetness` | Wetness | no | yes |
| `drying-speed` | Drying | no | yes |
| `tooth-response` | Tooth | no | yes |
| `granulation` | Granulation | no | yes |
| `edge-accumulation` | Edge accumulation | no | yes |
| `pressure-amount` | Pressure to load | no | yes |
| `pressure-contact` | Pressure to contact | no | yes |
| `persistent-reservoir` | Keep the reservoir | no | yes |
| `bristle-count` | Bristle count | yes | yes |
| `bristle-spread` | Bristle spread | yes | yes |
| `particle-count` | Particles | no | no |
| `particle-spread` | Particle spread | no | no |
| `sequence-step` | Sequence step | yes | no |
| `antialias` | Antialiasing | yes | no |
| `blend-strength` | Blending | yes | no |
| `component-opacity` | Opacity | yes | no |
| `component-scale` | Scale | yes | no |
| `component-rotation` | Rotation | yes | no |
| `component-aspect` | Tip Shape | yes | no |
| `component-offset-x` | Offset X | yes | no |
| `component-offset-y` | Offset Y | yes | no |
| `component-weight` | Weight | yes | no |
| `tip-shape` | Built-in tip | yes | no |

`tip-shape` is a component parameter, not a brush-wide one: Digital selects
the raster tip from it and Realistic selects the physical tip geometry.
`component-opacity`, `component-weight`, `component-scale`,
`component-rotation`, `component-aspect` and component offsets are written by
the evaluator and are Digital-only.

## Application Speed Dynamics

Speed Dynamics is an application-level input capability, not a brush document
feature. The artist's personal master control and property choices live in
`TabletInputSettings` (`input/speedDynamics`, `input/speedAffects*`) and are
persisted separately from every `.kb` file. A brush document only supplies the
creator's response ceilings `speed-size`, `speed-opacity`, `speed-flow` and
`speed-spacing`; the adapter multiplies those ceilings by the artist's choices
at stroke time.

The master is signed and continuous: `-100` makes slow strokes thicker/stronger,
`0` is neutral, and `+100` makes fast strokes thicker/stronger. Size, opacity,
flow and spacing can each be enabled independently. Size changes the footprint
and therefore the spacing distance, opacity scales the mark, flow scales the
paint released per dab, and spacing changes how closely dabs are laid. Realistic
maps these onto the real material inputs it already owns: size, contact
transfer, release rate and spacing ratio. No other property is claimed.

The factors multiply real stylus pressure rather than replacing it. A pressure
sensitive pen keeps its pressure response and the speed response is applied on
top; a mouse or finger starts from a constant pressure, which is exactly why
Speed Dynamics is useful there. The normalized speed itself comes from
`input::SpeedTracker`, which uses elapsed time rather than a fixed per-sample
weight, ignores impossible gaps, settles to zero when the pointer is stationary
and is independent of the device sample rate.

The same settings object reaches the artwork viewport and the Brush Studio
preview pad (`inputSettings`), so what the artist sees in the Studio is what the
canvas paints. Authored `Signal::Speed` bindings are also executed by
`BrushEvaluator`; the capability inspector accepts them only for size, opacity,
flow and spacing and reports every other target.

## Components and compositing

| Feature | Digital | Realistic |
| --- | --- | --- |
| Procedural tip (Round/Textured/Bristle/Graphite/Stipple) | yes | yes |
| Image / Colour Image / Tip bitmap | yes | no |
| Texture / AlphaMask / GrayscaleMask as a **mask source** | yes, via `maskComponentId` | no |
| Texture / mask as a painted mark | no, source only | no, source only |
| Height / Mask blends | no (stored) | no (stored) |
| Normal / Multiply / Screen / Overlay | yes | Normal only |

Digital samples the tip and its mask in independent local spaces
(`texture-scale/rotation/offset-x/offset-y/contrast/depth/strength`), applies
the mask before the artwork selection mask, and can gate with a selection or
the boundary-eraser protection region. `texture-depth` drives how far dark grain removes paint and
`texture-strength`/`texture-contrast` scale it; the document path does not
expose mask inversion.

## Generators, groups and variation

| Feature | Status |
| --- | --- |
| Continuous generator | yes, both engines |
| Stamp / Particle / Spray / Bristle / Procedural / Pattern / Sequence / Multi generators | stored by the model; not executed by either renderer |
| Generator parameter overrides | stored; not executed |
| Groups: All / Random / Weighted / Sequence / Alternating / Combination | evaluated for component requests |
| Group nesting and `membersPerStamp` | evaluated |
| Randomization: scatter X/Y, cluster, stamp count, component scale/opacity, rotation offset | per-stamp/per-stroke rules evaluated by `BrushEvaluator` |
| Other randomization targets | stored, reported unsupported |
| Dynamics: linear legacy pressure→size/opacity/flow and tilt→size | yes |
| Authored response curve for pressure→size/opacity/flow | yes, any monotonic-in-x point set |
| Authored `Signal::Speed` binding to size/opacity/flow/spacing | yes |
| Authored `Signal::Speed` binding to any other parameter | stored, reported unsupported |
| Response curves for other parameters or non-pressure signals | stored; not executed by the production stroke loop |

The base stroke (spacing, size, angle, taper, delay) is still resolved once per
stroke. Dynamics on the tip itself are represented by the legacy `pressure-*`,
`tilt-*` and `pressure-gamma` scalar settings; per-component dynamics and
randomization run per stamp through `BrushEvaluator`.

## Stroke features

| Feature | Digital | Notes |
| --- | --- | --- |
| Spacing, spacing jitter, seeded variation | yes | deterministic from the stroke seed |
| Taper in / taper out | yes | distance ramps relative to size |
| Stroke delay | yes | line trails the pointer by a share of size; tail is flushed on release |
| Antialiasing on/off | yes | off quantises coverage to whole pixels |
| Pressure size/opacity/flow, minimum size, pressure curve | yes | shared with the rasterizer and spacing loop |
| Tilt size / tilt angle / stylus rotation | yes when the device reports them | |
| Follow direction | yes | tip and component rotation |
| Stabilisation | yes | native document value feeds `StrokeStabilizer`; quick setting is the fallback |
| Accumulation off | no | repeated dabs always build up |
| Hue / saturation / brightness variation | yes | deterministic per-stamp HSV shift; seeded with the stroke |
| Bristle count/spread | yes | shapes the procedural Bristle tip |
| Particle count/spread | no | stored only |

## Studio and library behaviour

| Feature | Status |
| --- | --- |
| Settings inspector from the production registry | all sections, exact entry, bounded sliders, reset |
| Disabled controls with plain-language reason | yes, including unsupported stored values |
| Component stack: add, select, reorder, rename, hide, remove, blend, masks, image import | yes |
| Tip / Texture source view and pixel tip editing | yes, shared immutable resources |
| Create Brush Tip from Selection (current layer, selected layers, merged visible) | yes, from the real layers and selection mask |
| Multiple layers for a tip | yes; Control/Command-click toggles rows in the Layers panel (Shift range is not implemented) |
| Tip crop, rotate, flip, invert, contrast, fade, grayscale, outline, resize, reset | yes, each one undoable and non-destructive |
| Add / replace a tip from selected Illustration artwork | yes, in the previous draft or a new brush |
| Previous unsaved draft kept when starting a new brush | yes, restorable from the Studio banner |
| Creator exposure page | add, rename, remove, reorder, restore; does not change values |
| Private production preview pad | Digital and Realistic, Clear, Undo stroke |
| Save only / Save and install / Export | yes, with five explicit outcomes |
| Native sub-tool overrides and reset | yes; original `.kb` and library snapshot stay immutable |
| Compact layout at the 540×400 minimum | yes |
| GUI automation coverage | `test_brush_ui`, `test_brush_studio_model`, `test_native_brush_controls`, `test_brush_tip_extraction` |

## Create Brush Tip from Selection — truthfulness

| Source | Offered when | What it reads |
| --- | --- | --- |
| Current layer | the active layer has pixels | that layer only |
| Selected layers | more than one layer is selected and has pixels | those rows |
| Merged visible | any visible layer has pixels | the same compositor the canvas uses |

Full colour is stored as an `Image` component. Transparency outline and
darkness are stored as a `Tip` component. The source illustration, layer
visibility, blend modes and selection are never changed.

Realistic brushes use physical/procedural tips; a bitmap tip is retained and not
painted by Realistic. The dialog therefore does not continue a Realistic draft
with a bitmap tip.

## Deliberately unsupported

These are refused or reported, never guessed at:

- non-`ProceduralShape` visible components in Realistic;
- native extension chunks / format flags, or a document that fails validation;
- dynamics and randomization the stroke loop cannot reproduce; pressure
  curves with a strength that disagrees with the stored `pressure-*` value;
- generators other than `Continuous` and their overrides;
- Height and Mask component blends;
- accumulation off, particle count/spread, and particle generators.

The values are retained on save so a later build can implement them without a
format migration, and the Studio inspector marks each one unavailable rather
than accepting an edit it cannot honour.

## Verification

- `tests/unit/TestDigitalCapabilities.cpp` — vocabulary, combinations, the
  exact reason strings, every parameter's answer.
- `tests/unit/TestBrushDocumentEngine.cpp` — pixel-level Digital features
  (masking, blends, bitmap tips, taper, delay, antialiasing) and Realistic
  painting through the material engine.
- `tests/unit/TestBrushStudioModel.cpp` — inspector readback, unsupported-data
  round trips, save/install outcomes.
- `tests/unit/TestNativeBrushLibrary.cpp`, `TestNativeBrushControls.cpp` —
  install, tab selection, overrides, corrupt snapshots.
- `tests/unit/TestBrushUi.cpp` — packaged Studio UI in both themes, compact
  layout, native lifecycle, Realistic preview/install.
- `tests/unit/TestSpeedDynamics.cpp` — neutral centre, both directions, every
  property independently, amplitude, inversion, invalid input, sample-rate
  independence, stationary/short/interrupted strokes, sharp turns, arbiter
  publication and reset.
- `tests/unit/TestSpeedRendering.cpp` — Digital and Realistic pixels: neutral
  identity, both master directions, size/opacity/flow/spacing independently,
  pressure combined with speed, and authored document weights.
