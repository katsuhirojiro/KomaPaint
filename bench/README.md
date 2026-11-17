# Workload benchmarks

Qt-free harnesses that stress the same core libraries the editor links, so the
numbers describe the real code path. They are enabled with
`-DKOMAPAINT_BUILD_BENCHMARKS=ON` and land in `build/bench/bin`. CI builds and
runs them as a smoke check; the numbers themselves are noisy on shared runners
and are not asserted.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DKOMAPAINT_BUILD_BENCHMARKS=ON
cmake --build build
./build/bench/bin/bench_compositing
./build/bench/bin/bench_brush
./build/bench/bin/bench_material
```

Without a Qt install, the same harnesses build with a plain compiler, which is
how they were first measured:

```
g++ -std=c++20 -O2 -I src bench/compositing_bench.cpp \
    src/canvas/{TileGeometry,SparseTileStore,DirtyTileTracker,MemoryAvailability,CpuTileCompositor}.cpp \
    -o bench_compositing
```

## What each harness measures

**`bench_compositing`** — `CpuTileCompositor::composeInto` for one 256 px tile,
the unit the viewport re-composites when a stroke dirties a tile. Cases sweep
layer count, layer opacity, coverage and blend mode. Reported per-tile
percentiles and ns/px.

**`bench_brush`** — one continuous 700-sample stroke on a 2048x2048 canvas for
sizes from 12 px to 400 px, plus soft, textured and smudging variants. This is
the digital brush path including dab spacing, tile writes and per-stroke
allocation count.

**`bench_material`** — the Traditional Material Engine twice over: a watercolour
wash stroke through the reservoir/deposition path (1600x1200, 22 active tiles),
and the render pass the viewport runs over the wet tiles the stroke dirtied.
The two are reported separately because they bottleneck differently.

## Reference numbers

Linux x86-64, g++ 13, `-O2`, single run — a laptop-class machine, useful for
relative comparisons, not a product claim. Windows Release numbers still have to
be taken on the target hardware.

| Harness | Workload | p50 | p95 | p99 |
| --- | --- | --- | --- | --- |
| compositing | 1 opaque layer, disc 12% of tile | 0.052 ms | 0.067 ms | 0.090 ms |
| compositing | 4 opaque layers, disc 12% | 0.144 ms | 0.173 ms | 0.213 ms |
| compositing | 4 half-alpha layers, disc 40% | 0.240 ms | — | — |
| compositing | 4 Multiply layers, disc 40% | 0.876 ms | — | — |
| brush | 12 px round, 700 samples | 6.74 ms | 8.28 ms | — |
| brush | 60 px round, 700 samples | 20.68 ms | 23.34 ms | — |
| brush | 200 px round, 700 samples | 51.8 ms | — | — |
| material | watercolour wash stroke, 1600x1200 | 122.3 ms | 125.3 ms | 126.6 ms |
| material | render pass over 22 wet tiles | 8.87 ms | 9.16 ms | 9.30 ms |

Percentiles are interpolated over the sample set; a dash means the case has not
been re-measured since the harness moved into the repository.

## Interpreting them

- Compositing is bandwidth-bound at roughly 2 ns per pixel except for Multiply,
  which costs more than three times as much per pixel.
- The material render pass is arithmetic-bound on 12-band Kubelka-Munk per
  distinct cell: `opticalResultPrepared` is about a third of the time,
  `linearToSrgbByte` about an eighth. Enlarging the optical cache was measured
  and did not help.
- The material stroke is limited by per-cell optical evaluation plus reservoir
  bookkeeping (`PaintBatch` state refreshes and cell validation show up in a
  `-pg` profile).

Never change a hot loop to make a number smaller at the cost of rendered
pixels. Brush changes were verified pixel-identical; anything that alters output
needs its own validation.
