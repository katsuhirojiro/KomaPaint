# Performance baseline and measurement notes

This file records what has actually been measured, on which machine, with which
build, so future optimisation work does not start from guesses.

## How measurements are produced

Two independent paths exist:

1. **Windows Release application** (the primary target). Only Windows CI can
   build the Qt/QML application, and only a human running the portable ZIP can
   measure startup, panel interaction, zooming and long sessions. Those numbers
   are not produced automatically and are not recorded here yet.
2. **Pure C++ workload suites** that need no Qt. These can be built and run on
   any machine with a C++20 compiler:

   ```
   # one suite, example: brush rasterizer
   g++ -std=c++20 -O2 -I src tests/unit/TestBrushEngine.cpp \
       src/drawing/BrushEngine.cpp src/canvas/SparseTileStore.cpp \
       src/canvas/TileGeometry.cpp src/canvas/DirtyTileTracker.cpp \
       src/canvas/MemoryAvailability.cpp -o /tmp/test_brush_engine
   /tmp/test_brush_engine
   ```

   `test_performancebenchmark` prints p50/p95/p99 rows for digital dabs and
   strokes, tile compositing, the material engine, the Paint Mixer, memory and
   undo. `test_inputperformancebenchmark` covers pointer normalisation and
   allocation-free steady state.

## Reference run: 2026-11-17, GCC 12.2, `-O2`, Linux x86-64

Not the shipping target. MSVC and Windows will differ, especially in memory
behaviour; treat these as *relative* numbers for the same work, not as
predictions of Windows frame times.

### Digital brush strokes, `tests/unit/TestPerformanceBenchmark` (median)

| workload | before | after | change |
| --- | --- | --- | --- |
| `stroke_round_128px_400px` | 6.955 ms | 6.100 ms | -12% |
| `stroke_smudge_36px_400px` | 2.023 ms | 1.821 ms | -10% |
| `stroke_stipple_40px_600px` | 2.517 ms | 2.269 ms | -10% |
| `dab_smudge_48px` | 0.075 ms | 0.068 ms | -9% |
| `dab_bristle_48px` | 0.164 ms | 0.155 ms | -5% |
| `stroke_round_48px_600px` | 3.582 ms | 3.517 ms | -2% |
| `stroke_bristle_36px_600px` | 4.757 ms | 4.670 ms | -2% |

The change was: skip the rows a round tip cannot reach, compute each row's
horizontal extent once, and take the square root only on the antialiased rim of
a hard tip. Output pixels are unchanged - a hash of the canvas after 16 varied
strokes matched the previous implementation exactly.

A separate harness with 700 input samples per stroke, 2048x2048 canvas:

| brush | before | after |
| --- | --- | --- |
| 200 px round | 58.9 ms | 51.8 ms |
| 200 px smudge | 53.8 ms | 46.5 ms |
| 200 px textured | 95.3 ms | 89.5 ms |
| 200 px soft | 92.2 ms | 92.3 ms |
| 400 px round | failed (over the old 256 px cap) | 99.3 ms |

Soft tips keep their square root, so they are unchanged; that is the next
candidate for the brush path.

### Tile compositing

`composeInto` on a 256x256 tile, measured directly:

| workload | median |
| --- | --- |
| 1 opaque layer, blob over ~12% of the tile | 0.048 ms |
| 4 opaque layers, blob over ~12% | 0.133 ms |
| 4 opaque layers, full coverage | 0.146 ms |
| 4 half-alpha layers, ~40% | 0.240 ms |
| 4 layers at 50% opacity, ~40% | 0.352 ms |
| 4 Multiply layers, ~40% | 0.876 ms |
| 8 opaque layers, ~30% | 0.257 ms |

Full-coverage compositing costs the same as 12% coverage because the pass reads
and writes whole tiles: it is already memory-bandwidth bound at roughly
2 ns/pixel, and there is nothing left to win in the normal path. Two attempts
that did *not* help are recorded so they are not repeated:

* a 64 KiB lookup table replacing the integer `(v * f + 127) / 255` scaling made
  every row 5-60% slower (cache pressure, not arithmetic, is the cost);
* enlarging the material optical cache from 512 to 1024/2048/4096 slots changed
  the render pass by under 1%, so its misses are distinct values, not collisions.

Multiply/Screen/Overlay run double-precision blending per pixel (0.876 ms per
4-layer tile). That is the one clearly arithmetic-bound compositing path, and it
is deliberately untouched: changing it to integer maths would change rendered
pixels, which needs its own validation.

### Material engine (realistic brushes)

A profile of `MaterialRenderer::renderTile` over 22 active 64 px tiles (one
watercolour wash) attributes the time as:

| area | share |
| --- | --- |
| `MaterialEngine::opticalResultPrepared` (per-cell optics) | 35% |
| `renderTile` itself (clears, copies, cell loop) | 19% |
| `linearToSrgbByte` (linear to sRGB conversion) | 12% |
| `blendPremultiplied` | 11% |
| surface tooth / dab contact / batch validation | 8% |

74 ns per material cell, 8.7 ms for the full 22-tile pass. The sRGB conversion
already uses a 4096-entry hint table plus a one-step threshold search, so it is
close to its floor; the remaining cost is the 12-band Kubelka-Munk evaluation
per distinct cell, and a wash produces nearly a distinct value per pixel. Making
that cheaper means changing the optics or accepting coarser caching, both of
which change what an artist sees, so it is left as the next deliberate project
rather than a silent tweak.

## What is still missing

* No Windows Release measurements yet: startup, pan/zoom, rotation, dock
  resizing, panel switching, sub-tool browsing, settings navigation, autosave.
* No long-session memory or allocation growth measurements.
* QML-side costs (bindings, delegates, layout) are not measured at all; only the
  C++ side is covered by the suites above.
* The material renderer's per-cell optics is unchanged.
