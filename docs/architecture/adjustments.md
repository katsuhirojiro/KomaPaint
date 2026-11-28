# Colour adjustments

## Why this is a separate module

Colour adjustments and geometric transforms are different operations with
different data, different previews and different failure modes:

| | Colour adjustment | Geometric transform |
| --- | --- | --- |
| Changes | channel values of pixels that stay where they are | where pixels land |
| Needs | histogram, per-channel LUT, colour space | resampling, matrices, warp mesh |
| Selection | coverage mask blends the result per pixel | selection clips the resampled result |
| Examples | Levels, Curves, Hue/Saturation | Perspective Warp, Distortion |

They therefore live in different families. `src/adjustments/` contains only
colour adjustments and never imports a transform, warp or resampling header.
A future `src/transform/` (or the existing canvas geometry code) owns geometric
operations. Sharing the undo transaction, selection mask and preview lifecycle
is expected; sharing the operation itself is not.

## Layers of the implementation

```
QML dialog  (ui/components/LevelsDialog.qml)
    |  user-visible controls, explanations, histogram, before/after
    v
LevelsController  (src/app/LevelsController.*)
    |  owns the target, captures originals, previews, history transaction
    |  target = document layer/selection, or an in-memory image
    v
adjustments::Levels  (src/adjustments/Levels.*)
    |  pure CPU: parameters -> 256-entry LUT, LUT application, histogram
    |  no Qt, no tiles, no document
    +--> SparseTileStore / SelectionMask   (document target)
    +--> QImage RGBA8888                   (in-memory image target)
```

The pure core is deliberately Qt-free so the same maths is used by documents,
brush-tip creation, and any future import or export path. There is no generic
node graph: an adjustment is a concrete parameter struct, a LUT builder and one
application function. Adding Curves or Hue/Saturation later means adding a new
typed module beside `Levels`, not extending a universal graph.

## Levels model

`adjustments::LevelsParameters` holds exactly the values a Levels dialog edits:

* `inputBlack`, `inputWhite` — the input range, 0…255. Values at or below black
  map to 0, values at or above white map to 1.
* `gamma` — the midtone curve, 0.1…10.0. 1.0 is neutral; above 1 lightens
  midtones, below 1 darkens them.
* `outputBlack`, `outputWhite` — the output range, 0…255. Reversing them
  inverts the tonal result.

The "midpoint" shown by the simple controls is a view of `gamma`:
`midpoint = 0.5 ^ gamma` (as a percentage), and
`gamma = log(0.5) / log(midpoint / 100)`. The two controls cannot disagree.

`LevelsLut::fromParameters` produces a 256-entry lookup table. Application has
two forms:

* premultiplied ARGB (document tiles): unpremultiply, map RGB, premultiply
  again. Alpha is never changed, so transparency is preserved exactly.
* straight RGBA8888 (QImage and brush-tip extraction): map RGB bytes, keep the
  alpha byte.

The histogram is accumulated from the *source* values with alpha as the weight,
so transparent pixels do not produce a false spike at black. Selected pixels
are weighted by selection coverage, which makes the histogram describe what the
operation will actually touch.

## Document target and undo

For a document, `LevelsController`:

1. captures a copy of every affected source tile once;
2. builds the histogram from those copies (selection coverage applied);
3. writes a preview directly to the layer, blending the adjusted pixel with the
   original by selection coverage (feathered selections therefore feather the
   adjustment);
4. on Apply, restores the originals, starts an `UndoStack` pixel transaction,
   writes the same adjustment again and commits it. Undo/redo therefore uses the
   existing tile-snapshot path, not a second history system;
5. on Cancel, restores the originals and restores the document's previous
   `modified` flag.

Nothing outside the active layer or the selection is written. A selection that
covers only part of a tile is honoured per pixel, never promoted to the whole
tile or the whole document.

## Image target and source resources

The same controller can open an in-memory PNG. This is how Create Brush Tip
uses Levels: the extracted tip image is adjusted before it is handed to Brush
Studio. The document, the selection, the brush library and the stored tip
resource are untouched; Cancel simply discards the in-memory result.

Applying an adjustment to a document layer is an undoable pixel edit, not an
adjustment layer. The source file of an imported image is never rewritten, and
the pre-adjustment pixels remain recoverable for the whole session through undo
and Cancel. A persistent adjustment-layer system is future work and must not be
faked by leaving hidden state in the document.

## Menu and discovery

`Edit → Adjustments → Levels…` is the only entry while Levels is the only
implemented adjustment. Entries are added only when their operation exists and
has tests. The menu is nested (`KpMenu` inside `KpMenu`) so future entries do not
require restructuring the Edit menu.

## Testing

* `test_adjustments` — Qt-free LUT, gamma/midpoint round trip, histogram,
  premultiplied and straight-RGBA application, alpha preservation.
* `test_levels_controller` — document apply, selection clipping, undo/redo,
  cancel, and in-memory image apply through the real controller.
