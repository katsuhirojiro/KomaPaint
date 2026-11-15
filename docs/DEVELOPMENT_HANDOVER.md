# Development handover

## Repository and branch

- Repository: `katsuhirojiro/KomaPaint`.
- Active branch: `arena/d7d3b71d-komapaint`; base `d3622c4869fc868ce0d52a05146b35938372b7f0`.
- Last pushed commit: `dfe7fc404e1eac052b570b2b4218aa1c62f250b4` (`Include material snapshot declarations in history`). The work described below is newer working-tree work and has not yet been built with Qt, committed, or pushed.
- Preserve the untracked `workspace-transfer-export/` directory. Do not reset, discard, rebase, switch branches, or overwrite it. No release EXE was produced or requested.

## Request and acceptance scope

Continue the pigment-aware traditional-media implementation while preserving existing Digital/Gouache mode separation and file compatibility. Water must affect pigment concentration, mobility, spreading, diffusion, deposition, edge accumulation, drying, reworking and lifting; media profiles need distinct behavior. Paper profiles must influence the material simulation, not merely its visual overlay. Wet deposition, spreading, settling, gradual drying, dry material, rewetting, reworking, pickup and transfer must retain meaningful state. Prefer lazy material-state work and active sparse tiles. Brush loads must pick up, transfer, mix and deplete material and respond to pressure/movement. Mixer-created compositions must load into a brush without losing named/residual identities or proportions.

Continue to investigate compact cells, shared pigment definitions, mixture deduplication, dirty regions, contiguous memory, SIMD, parallel tiles, lower simulation resolution, lazy evaluation, CPU fallback and optional future GPU acceleration. Measure before making performance claims. Do not claim physical accuracy or full completion without evidence.

## Current implementation in the working tree (not yet full-Qt verified)

- `src/material/MaterialEngine.*` has medium and paper profile behavior, water-aware deposition/optics, wet flow, drying/settling, rewetting and pickup/lift behavior. `MaterialTileStore.*` stores sparse composition-indexed cells, deduplicates compositions, tracks active wet tiles, and exposes material transfer operations. Active-tile tracking avoids repeatedly scanning dry/inactive material tiles.
- `CanvasViewport.*` runs a timer-driven simulation only while material tiles are active, routes Gouache strokes through the material engine, and reads optional named mixture batches from `BrushSettings`. The current viewport uses the cold-pressed paper profile; hot-pressed and rough profiles exist in the engine, but paper selection in the product UI is not established.
- `ColorMixerPanel.qml` offers named pigment choices and a “Load mixture into brush” action. `BrushSettings` stores the QVariantMap and stroke setup converts its component identities and proportions to a `PaintBatch`. This new UI-to-stroke path is not yet verified in the Qt application.
- The native format is now v5: it stores drying and settling in material pixel records. v1–v4 readers remain; v4 cells infer drying from wetness and settling as zero. `TestMaterialDocument.cpp` has v5 lifecycle round-trip assertions, but Qt serialization tests have not yet been run.
- `TestMaterialEngine.cpp` now invokes lifecycle, paper-profile, pickup/rewet and conservation tests. `TestCanvasViewport.cpp` adds mixture-to-stroke composition coverage; `TestBrushUi.cpp` adds a `BrushSettings` preservation check. The latter integration/UI coverage is unverified.
- Undo/history, recovery, rendering, erasing, thumbnails/picking, and Digital-mode preservation were part of earlier changes, but the prior full suite was not green; do not infer integration success from the standalone engine test.

## Verification status

### Local standalone material engine

The sandbox has `g++` but no CMake/Ninja, Qt development SDK/qmake, clang++, or container runtime, so the full Qt build/tests cannot run locally. The latest standalone run, including AddressSanitizer and UndefinedBehaviorSanitizer, passed after active-tile tracking changes:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Isrc tests/unit/TestMaterialEngine.cpp \
  src/material/MaterialEngine.cpp src/material/MaterialTileStore.cpp \
  src/material/BrushPresets.cpp src/canvas/SparseTileStore.cpp \
  src/canvas/TileGeometry.cpp src/canvas/DirtyTileTracker.cpp \
  src/canvas/MemoryAvailability.cpp -o /tmp/test_material_engine_san \
  && ASAN_OPTIONS=detect_leaks=1 /tmp/test_material_engine_san
```

Result: `material engine checks passed`. This covers only the standalone material tests; it does not build/test QML, viewport integration, native file I/O, history, or recovery. `git diff --check` passed before the latest active-list edits and must be rerun.

### Windows CI baseline

Run `43716877704` corresponds to the last pushed commit `dfe7fc4`: **full Windows build succeeded; unit tests failed; packaging/smoke tests were skipped**. Its summary reports six failing test executables, including `test_dock_ui.exe` (5 passed, 9 failed) and a crash in `test_document_creation.exe` during `createsIndependentDocument`. Complete logs were unavailable in that run; material-specific outcomes were not individually confirmed. Earlier compile errors at `8f1fe6d` and `fb99220` were fixed in `fb99220` and `dfe7fc4` respectively. This old CI result does not verify any of the current working-tree changes.

## Immediate next steps

1. Run `git status`, `git diff --check`, and inspect the complete diff; preserve `workspace-transfer-export/`.
2. Re-run the standalone material test after all edits (the sanitizer run above passed after the active-list changes).
3. Get Windows CI on the current branch changes. Fix compile/test regressions and inspect the complete test results, including previous non-material failures. Do not claim full integration or passing tests before verified.
4. Verify mixer-to-brush-to-stroke identity and ratios, plus lifecycle behavior, rollback/conservation on failures, active-tile membership/budget accounting, and v1–v4 read compatibility / v5 serialization.
5. Review simulation behavior and profile distinctions; confirm timer behavior, lazy-state assumptions, and relevant UI choices. Benchmark representative sparse and wet workloads before optimizing or making performance claims.
6. Update this handover with exact CI run/commit and remaining gaps. Commit/push only to `arena/d7d3b71d-komapaint`; never force-push, reset, rebase or switch branches.
