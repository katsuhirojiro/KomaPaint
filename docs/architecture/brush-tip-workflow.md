# Brush tips from Illustration artwork

This is the design and implementation audit for **Create Brush Tip from
Selection**. It records what was studied, what already existed, what was added,
and what is deliberately not claimed.

## Research: useful interaction principles

Sources studied:

- Clip Studio Paint, [Creating your own materials](https://help.clip-studio.com/en-us/manual_en/630_material/Creating_your_own_materials.htm)
  and [Customizing brush tools](https://help.clip-studio.com/en-us/manual_en/240_brushes/Customizing_brush_tools.htm).
- Clip Studio Paint, [Brush tip and brush size settings](https://help.clip-studio.com/en-us/manual_en/810_subtools/B.htm).
- Procreate, [Brush Studio](https://help.procreate.com/procreate/handbook/brushes/brush-studio)
  and [Brush Studio settings](https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings).

The principles worth borrowing:

1. **Artwork first.** A tip is registered from a layer, or from a selection on
   that layer. The artist does not redraw the leaf inside a brush editor.
2. **A selection is a crop, not a copy of the canvas.** Clip Studio registers
   only the selected part; without a selection the whole layer is the source.
   Transparency and colour are part of the registered image.
3. **A brush can use more than one tip.** Clip Studio's brush-tip settings and
   Procreate's Shape Source both separate the stored tip image from the brush's
   stroke, spacing, rotation and dynamics. The image is a reusable resource, not
   the brush itself.
4. **The drawing pad is for testing the production brush.** Procreate's live
   Drawing Pad exists so the artist can change a setting and see the real result
   without leaving the studio. It is not a separate illustration app.
5. **Progressive disclosure.** Everyday users see a small set of controls;
   deeper controls are available but not required. Explanations are attached to
   the control, not buried in a manual.
6. **Keep the source.** A tip editor should be non-destructive and should let
   the artist return to the registered image.

KomaPaint deliberately does not copy the interfaces, jargon or branding of
either application.

## Existing systems reused

| Need | Existing system | Reuse |
| --- | --- | --- |
| Current/selected/visible artwork | `CanvasDocument`, `LayerModel`, `Layer::tiles()` | Read only; no second document |
| Layer opacity and blend | `CpuTileCompositor` | Composites selected/visible layers exactly as the canvas does |
| Realistic artwork | `MaterialTileStore` + `MaterialRenderer` | Rendered per tile so material paint is not invisible |
| Selection shape and feathering | `region::SelectionMask` | The same mask used by painting and fill |
| Transparent margins | `SparseTileStore` / compositor alpha | Tight content bounds when there is no selection |
| Tip storage | `BrushResource`, `BrushComponent`, `.kb` | A tip is a normal resource referenced by a normal component |
| Multiple tips | component stack, groups, `SelectionMode` | Existing add/remove/reorder/duplicate/group controls |
| Stroke evaluation | `BrushEvaluator` + Digital and material engines | The Studio pad and the canvas both use this path |
| Undo/redo | `BrushStudioModel` edit transactions and shared pixels | Every tip edit and import is one undo step |
| Save/install | `saveDraftToLibrary`, `installNativeBrush` | Existing truthful outcomes |
| Explanations | `KpButton.tip`, `ToolTip`, `Accessible.description` | Hover, keyboard focus and press/touch |

## Implementation

### Menu command

`Edit → Create Brush Tip from Selection...` calls
`Workspace.brushTipSourceInfo()` and opens one small dialog. It is disabled
when no illustration is open.

### Extraction

`src/app/BrushTipExtraction.cpp`:

- **Current layer**: active layer only.
- **Selected layers**: rows named by `LayerModel::selectedLayerRows()`.
- **Merged visible**: every visible layer, in canvas order, with layer opacity
  and blend mode.
- If a selection exists and is used, its tight bounds define the result and its
  coverage multiplies alpha.
- Without a selection, tight non-transparent bounds are used. This is the
  explicit "current layer's whole contents" path, and it avoids a tip that is
  99% empty canvas.
- Coverage choices: full colour, transparency outline, or darkness.
- The source document is never modified. No flattening, no layer changes and no
  selection changes.

The result is encoded as a lossless PNG and handed to Brush Studio. The QML
layer never invents pixels.

### Studio import

`BrushStudioModel::importTipPng` and `newDraftFromTipPng`:

- **Add** creates a new bitmap component (`Image` for colour, `Tip` for
  coverage) and leaves every existing component alone.
- **Replace** writes a new resource into one chosen tip and keeps its order,
  transform, group membership, dynamics and all other components.
- **New brush** starts from the same default draft as the New command, then
  replaces its first tip. A modified previous draft is kept in memory and can be
  restored from the Studio banner.
- PNG bytes are decoded and stored as a normal resource; save, reopen and install
  use the existing `.kb` path.

### Non-destructive tip editing

The existing pixel editor gained: crop, rotate left/right, flip horizontally and
vertically, invert, contrast, fade/transparency, grayscale, outline/alpha
extraction, resize and **Reset to artwork**. Every operation creates a new
resource and one undo step. The imported resource is retained for reset in the
session; it is not serialised, so after reopening a saved brush the reset source
is whatever the file contains, not the old session's pixels.

### Multiple tips

The existing component stack already supports add, select, preview, reorder,
duplicate, remove and group selection modes. The selection workflow uses those
same controls; it does not introduce a second mark system. A brush with more
than one tip is expressed by more than one bitmap component (or a colour `Image`
plus coverage `Tip`) and the existing generator/group selection decides which
one a stamp uses.

## Truthful limits

- Realistic painting accepts procedural physical tips, not bitmap tips. A
  bitmap tip is retained in `.kb` but is not painted by Realistic. The dialog
  refuses to continue a Realistic draft with a bitmap tip and says why.
- A multi-layer selection is only offered when more than one layer is selected.
  The extraction API accepts explicit rows; the Layers panel's ordinary click
  still selects one layer.
- Crop is to the visible alpha bounds; there is no freehand crop rectangle.
- Rotate is in quarter turns; the stored tip is resampled only for Resize.
- The "reset to artwork" source survives the session, not a save/reopen. Undo
  history remains the recovery mechanism after reopening.
- Generated tips are limited by the same memory and pixel-representation checks
  as any other `.kb` resource. There is no arbitrary artistic size cap.
