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

   Workload harnesses now live in `bench/` and are built with
   `-DKOMAPAINT_BUILD_BENCHMARKS=ON`. They link the same core libraries the
   editor links, so they measure the shipping code path rather than a copy:
   `bench_compositing` (one 256 px tile through `composeInto` across layer,
   opacity, coverage and blend-mode cases), `bench_brush` (a 700-sample stroke
   at 12-400 px, including soft, textured and smudging tips, with a per-stroke
   allocation count) and `bench_material` (a wash stroke through the reservoir
   path, then the render pass over the wet tiles it dirtied). Windows CI builds
   and runs all three as a smoke check and prints the numbers into the run log;
   the numbers are not asserted because shared runners are too noisy for
   timing thresholds. See `bench/README.md` for the reference table.

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

74 ns per material cell, 8.7 ms for the full 22-tile pass (p50 8.87 ms,
p95 9.16 ms, p99 9.30 ms over 40 passes in `bench_material`).

The same stroke through the deposition path - the part the artist feels while
dragging - measures p50 122.3 ms, p95 125.3 ms, p99 126.6 ms on a 1600x1200
canvas with 22 active tiles. Its profile differs from the render pass:
`MaterialDab::apply` itself is 37% (per-pixel optics and compositing are inlined
there), `dabContactAt` 10% over 26 M calls, `MaterialTileStore::validateCell` 10%
over 15.7 M calls, `linearToSrgbByte` 6%, and `PaintBatch` state refreshes
(`totalMass` 55 M calls, `refreshBatchState` 16 M calls) another 8%. Those
bookkeeping calls are the clearest next target: the batch is fixed for a whole
dab, yet its derived wetness/consistency is recomputed many times per pixel.
They were not touched in this pass because they sit inside the material write
path that must stay bit-identical without its own validation round.
 The sRGB conversion
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
* The material renderer's per-cell optics is unchanged, and so is the
  `PaintBatch` bookkeeping inside the stroke path.
* The canvas viewport keeps one frame image and repaints only the dirty
  rectangle into it, but `QQuickPaintedItem` still uploads the whole texture per
  frame. At 4K that is ~33 MB of upload per update, which is a plausible
  bottleneck that no C++ harness can see; replacing it with per-tile scene-graph
  textures is a rendering change that needs its own verification pass.
