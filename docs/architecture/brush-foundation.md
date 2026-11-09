# Initial CPU brush

The first brush writes to the selected visible, unlocked layer's sparse CPU
storage. The viewport translates input coordinates into document coordinates;
Qt Quick mouse samples use full pressure, and pen samples use the pressure Qt
reports. Brush settings are captured at stroke start. A stroke interpolates
spaced dabs along segments, including fast movements and clipped off-canvas
segments. A separate rasterizer computes round or deterministic document-space
grain coverage, applies a linear pressure-to-opacity response, and deposits
premultiplied source-over pixels. Per-dab notifications are batched by tile;
the existing compositor updates the visible output. UI code does not own pixels.

The definition and separate input, stroke, tip/sensor and deposition functions
are an extension point, not implementations of advanced brushes. Tilt,
rotation, velocity, custom tip images, response curves, bristles, pickup,
smudge, blending and procedural presets are not implemented. The current
texture is simple generated grain, not an imported texture. Pressure changes
opacity only. Opacity and flow are **per dab**, so overlapping dabs build up;
this is not a stroke-wide opacity cap. Changing spacing can change buildup.

## Important safety and performance limitations

Artwork cannot yet be saved or undone. The interface warns before replacing or
closing a modified document and shows the limitation in the status bar. A tile
allocation/tracking failure stops the stroke and reports an error; previously
written pixels, including any partial dab, remain. There is no atomic stroke
rollback or command history. Do not describe this as lossless or production
ready. Tile memory limits are per layer rather than document-wide.

The current CPU algorithm uses per-pixel storage updates and is not optimized
for very large tips or low-end machines. Benchmark and batch raster operations
before expanding the brush engine. Native tablet events and real pressure
hardware need physical Windows testing; automated tests do not establish device
compatibility. Dirty tile notifications are conservatively coalesced over a
single dab's tile bounding box.
