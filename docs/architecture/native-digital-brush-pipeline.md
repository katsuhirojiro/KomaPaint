# Native brushes: from a Studio draft to saved artwork

## Try the workflow

1. Open **Edit → Brush Studio**. Use a **Digital** draft. A new draft has a
   round procedural tip. To test a bitmap instead, add a **Tip mask**, **Image**,
   or **Colour Image** component, select it, and choose **Load image…** below
   the source view. Hide or remove the procedural tip if you only want the
   bitmap. Use **Normal** blending and set brush **Hardness** to 100 for a
   bitmap; its pixels determine the edge. In a Tip mask, the source alpha (or
   luminance for an opaque image) controls coverage of the foreground colour.
   Image and Colour Image stamps retain their source RGB and alpha.
2. Expand **Live Preview** and draw in the private pad before saving. **Undo
   stroke** affects only the pad; Studio's edit undo remains separate.
   **Clear** clears both the pad and its stroke history. Pad strokes stay on
   the pad when the draft changes. They do not become a brush asset or get
   serialized. The pad uses the current foreground and tablet preferences but
   does not modify the artwork, the active brush, or the artwork's undo stack.
3. **Save and install brush** writes the portable file and, when the selected
   renderer supports it, installs a native brush under **Brush → Digital** or
   **Brush → Realistic** in the active tab (or the collection's default tab).
   It switches to that tab and selects the new brush. Saving an installed
   original again updates that entry in its existing tab. Unsupported stored
   data is saved but not installed, with the reason shown. **Export** alone
   writes a copy without installing it. To install a received file, use
   **Install .kb…** in either collection's tab menu.
4. Select the `.kb` row in the ordinary Sub-Tool list and draw. Edit the brush
   in Studio rather than in the legacy quick settings. Save and reopen the
   artwork as `.koma`; it stores the resulting artwork pixels and does not
   need the brush file to display them.

If a definition is valid to store but unsupported for painting, Studio can
still save it as a file. The pad gives the reason and the save result says it
was not installed. A missing or damaged *installed* file blocks painting with
that row instead of substituting another tip. Other presets remain usable.

## Ownership and rendering

- `src/brush/BrushFormat.*` and `BrushFile.cpp`: native chunk codec and atomic
  writer. A v1 round trip retains unknown chunk payloads and header flags;
  extensions are not executed. Unknown major versions, invalid resources,
  and unexpected fields in fixed known chunks are refused rather than
  silently rewritten.
- `src/app/BrushLibraryStore.*` and `SubToolLibrary.*`: the versioned, atomic
  `brush-library.json` manifest references exact `.kb` snapshots in the sibling
  `brush-library.json.assets/` directory by SHA-256. Manifest and assets must
  be backed up together. Install writes the asset before committing the
  manifest. Original files can move or disappear. Library duplicates pin their
  selected snapshot; updating the original does not change those copies.
  Per-brush Sub-Tool adjustments are stored as `nativeOverrides` on the entry
  and applied to a copy of the immutable `.kb`; the original file and the
  library snapshot are never rewritten. Old and unreferenced snapshots are not
  automatically deleted in this slice. Existing legacy library versions still
  load.
- `src/app/BrushStudioModel.*`, `BrushStudioPad.*`, `CanvasViewport.cpp`, and
  `ui/workspaces/BrushStudioWindow.qml`: the pad owns a sparse private canvas,
  private history, and private draft snapshot. Both viewports start their
  strokes through the same `CanvasViewport::beginStrokeSample()`; only the
  legacy path migrates settings. Each stroke holds its own immutable native
  definition and shares its embedded resource buffers.
- `src/drawing/BrushDocumentAdapter.*` and `BrushEngine.*`: `DocumentStroke`
  uses the existing evaluator and spacing loop. `BrushRasterizer` samples
  authored tip coverage or RGBA, then applies the independent artwork
  selection mask and deposits/erases into normal tiles. Bitmap transforms use
  the source aspect and bilinear sampling (without RGB fringes from fully
  transparent texels). Equivalent stroke comparisons require the same input
  samples, seed, colour, starting surface, and input settings.

## Limits

The exact per-setting contract lives in
[the feature matrix](brush-studio-feature-matrix.md). In summary, both engines
paint supported native documents, including procedural and bitmap Digital
tips, texture and mask sources, Normal/Multiply/Screen/Overlay components,
component selection and variation, and the supported stroke controls.
Realistic paints `ProceduralShape` components through the material engine and
refuses image or mask components rather than ignoring them.

Still not executed: arbitrary response-curve dynamics, generator kinds other
than `Continuous`, Height/Mask component blends, accumulation-off, hue or
luminance variation, and the bristle/particle parameter families. These values
round-trip in the file and are reported as unavailable in Studio. There is no
animated image sequence, node graph editor, SVG/ABR/SUT conversion, pad replay
or A/B comparison, per-artist native quick override, or native pad recovery.
Image decoding uses Qt's supported local image codecs; `.kb` embeds pixel data,
not a link to the imported image.

## Verification entry points

- `test_brush_format` and `test_brush_document_engine`: byte/asset handling,
  seeded and legacy pixel equality, bitmap colour/coverage/transforms, and
  artwork-mask interaction.
- `test_native_brush_library`, `test_native_brush_controls`,
  `test_digital_capabilities` and `test_brush_studio_model`: persistent exact
  resolution, immutable aliases, per-install overrides, exact capability
  vocabulary, corrupt/missing file handling, and image import with undo/reload.
- `test_brush_ui nativeBrushStudioPadToArtwork`: real Studio input and ordinary
  Sub-Tool selection, pad isolation, rendered tile comparison, and artwork
  save/reopen. The packaged `KOMAPAINT_VERIFY_PACKAGED=1` path also exercises
  the Studio pad, library reload, bitmap painting, and project round-trip.
- Mouse automation and packaged verification do not validate artist workflow,
  a physical tablet/eraser, Android, or macOS.
