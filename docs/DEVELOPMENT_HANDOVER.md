# Development handover

## Repository and branch

- Repository: `katsuhirojiro/KomaPaint`.
- Active branch: `arena/6c086365-komapaint`; branched from `6a0fb770eb4658104509c07eb9f89c3760df6042` (`arena/d7d3b71d-komapaint`).
- Upstream CI timing fixes (`42bba0d` and `0a4d476`) were cherry-picked onto `6a0fb77` (`55b7b33`) and pushed to `origin/arena/6c086365-komapaint`.
- Preserve any local workspace artifacts only per repo conventions; never force-push, reset, rebase, or switch branches.

## Request and acceptance scope

Stabilize the traditional material engine starting with Stage 2 (Pigment System & Optics), Stage 3 (Wet Physical Simulation & Composition Reclamation), and Stage 4 (Brush Reservoir with automatic per-stroke clean reload and size-independent depletion), while preserving Digital/Gouache mode separation, normal folder animation/X-sheet behavior, and v1–v5 `.koma` file-format compatibility.

## Stage 2 / 3 / 4 changes implemented

1. **Stage 2 — 12-band CIE 1931 D65 Spectral & Kubelka-Munk Optics (`src/material/MaterialEngine.h`, `src/material/MaterialEngine.cpp`, `src/material/MaterialTileStore.cpp`)**:
   - Replaced the wide overlapping Gaussian RGB curves with a row-normalized 12-band (`405..680 nm`, `25 nm` step) CIE 1931 D65 `SpectralToLinearRgb` matrix and a 6-corner complementary basis (`R, G, B, C, M, Y`) for `rgbToReflectance()`.
   - Fixed `pigmentFromReflectance()` so `tintStrength` scales absorption properly instead of dividing by `tintStrength`, and added `PR122` Quinacridone Magenta's blue-violet reflectance lobe (`PR122`: `(196,117,54)` -> `(255,102,130)`; `PB15`: `(0,73,232)`; `PR122+PB15`: `(70,75,73)` -> `(98,60,161)`).
   - Replaced `filmMass` / `0.25` thickness floor in `reflectance()`, `opticalResult()`, and `composite()` with `depositShare`, computing `opticalDepth` from pigment + filler + medium/substrate retention so diluted tints stay translucent even after water evaporates (`0.05` `PR122` + `5.0` water: wet `alpha=0.0082`, dried `alpha=0.0145`, preserving magenta hue).
   - Aligned `MaterialDab::apply()` (`SparseTileStore` and `MaterialTileStore`) and `MaterialRenderer::renderTile()` on the `4096.0` tile-pixel reference unit (`ReferenceBatchPixels = 4096.0`) so optical depth is consistent between direct rendering and material-tile rendering.

2. **Stage 3 — Composition Refcounting, Undo Pinning, Evaporation Fast-Path, Dense Save (`src/material/MaterialTileStore.*`, `src/material/MaterialEngine.cpp`, `src/history/UndoStack.*`, `src/files/DocumentFile.cpp`)**:
   - Added `m_compositionLiveRefs`, `m_compositionPinRefs`, `m_freeCompositionIds`, `reclaimUnreferenced()`, `pinSnapshots()`, `unpinSnapshots()`, `activeCompositionCount()`, and `updateCellState()` to `MaterialTileStore`.
   - Added `1e-12` quantization in `MaterialTileStore::intern()` (for proportions `>= 1e-10`) to prevent IEEE ULP drift while keeping `1e-8` pigment distinctions separate.
   - Updated `advanceWetMaterial()` and `MaterialDab` rewetting to mutate cell water/binder/filler/drying/settling in place via `updateCellState()` without re-interning unchanged pigment ratios, and call `reclaimUnreferenced()` after wet ticks. Measured 30-step drying probe dropped from `141,731` compositions (`27.34 MB`) to `1` active composition (`0.28 MB`) constant.
   - Wired `pinSnapshots()` / `unpinSnapshots()` + `reclaimUnreferenced()` into `UndoStack` (`captureMaterialTile`, `finishMaterialCapture`, `~StrokeCommand`) and dense live-composition remapping into `DocumentFile::save()` so `.koma` v5 saves remain dense and valid even after reclamation or pinned undo entries.

3. **Stage 4 — Automatic Per-Stroke Clean Reservoir Reload & Size-Independent Depletion (`src/material/MaterialEngine.*`, `src/app/BrushSettings.h`, `src/app/CanvasViewport.cpp`)**:
   - Added `BrushDefinition::autoReloadOnStroke` (default `true`) and `BrushReservoir::reload()`, calling `reload()` automatically in `MaterialStroke::begin()` when `autoReloadOnStroke` is enabled so every new stroke starts with a clean full configured mixture without carrying picked-up paint from prior strokes.
   - Added `BrushSettings::persistentReservoir` (default `false`) and wired it in `CanvasViewport::beginStroke()` / `endStroke()` / `cancelStroke()` / `setDocument()`, plus `reclaimUnreferenced()` on stroke commit.
   - Normalized `MaterialDab::apply()` area transfer by `(contactWeight / ellipseArea) * 1.8 * sqrt(brush.size / 18.0)` so large brushes (e.g. `watercolour-mop`, size `72`) sustain long strokes smoothly without collapsing in a few dabs, and light pressure (`0.08`) remains visible.

4. **Stage 5 — Complete Brush Property Architecture & Gouache Round (`src/material/*`, `src/app/{BrushSettings.h,SubToolLibrary.*,CanvasViewport.cpp}`, `resources/brush-presets.json`, `ui/panels/ToolPropertyPanel.qml`)**:
   - Extended `BrushDefinition` with `nominalSize`, `followDirection`, `textureStrength`, `contactPressureScale`, `tiltFlattening`, `paintBody`, `dryingRate`, `contactPressureCurve`, and `effectiveReservoirCapacity()` so capacity scales quadratically with `size / nominalSize` and stroke depletion stays consistent across brush sizes.
   - Extracted shared `dabContactAt()` in `src/material/MaterialEngine.cpp` (fixing outer-rim `hardness` falloff without shrinking dab radius, deterministic `textureStrength` grain, `contactFirmness`, and replacing duplicated pickup/erase contact loops), wired `paintBody`/`viscosity` spread damping, independent `contactPressureCurve`/`contactPressureScale`, `tiltFlattening`, `dryingRate`, `followDirection` tangent rotation in `MaterialStroke::extend()`, and calibrated `normalizedArea = fillFraction * 0.52 * (size / 18.0)^2` with `ReferenceBatchPixels = 16384.0`.
   - Tuned `gouache-round` (`size=18, cap=8.5, rel=0.055, body=0.78, hardness=0.72, dryingRate=1.15`), `watercolour-round` (`cap=7.5, rel=0.11`), and `watercolour-mop` (`cap=32.0, rel=0.10`) in `src/material/BrushPresets.cpp`, and set `nominalSize` on all 10 presets.
   - Wired Stage 5 traditional properties across `BrushSettings`, `SubToolLibrary` (preset-stored and resettable), `resources/brush-presets.json` (adding `"Gouache Round"` under `"category": "Traditional"`), `CanvasViewport::beginStroke()` (preserving real gouache filler, binder, pigment concentration, water, and size-scaled capacity), and `ui/panels/ToolPropertyPanel.qml` (beginner `"Material paint"` section + collapsible `"Advanced material"` section).

## Verification status

### Local pure-C++ test suites (verified)

The sandbox has `g++` (no CMake/Ninja/Qt SDK locally), so Qt/QML targets require Windows CI, while all 9 pure-C++ unit test suites compiled with `-std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined` and passed under `ASAN_OPTIONS=detect_leaks=1`:
- `tests/unit/TestTileGeometry.cpp` -> `Tile geometry tests passed`
- `tests/unit/TestSparseTileStore.cpp` -> `Sparse tile storage tests passed`
- `tests/unit/TestDirtyTileTracker.cpp` -> `Dirty tile tracking tests passed`
- `tests/unit/TestTileAllocationFailure.cpp` -> `Allocation failure tests passed`
- `tests/unit/TestCpuTileCompositor.cpp` -> `CPU compositing tests passed`
- `tests/unit/TestBrushEngine.cpp` -> `Brush engine tests passed`
- `tests/unit/TestStrokeStabilizer.cpp` -> `Stroke stabilizer tests passed`
- `tests/unit/TestDocumentMemoryBudget.cpp` -> `Document pixel budget tests passed`
- `tests/unit/TestMaterialEngine.cpp` -> `material engine checks passed` (17 test suites including Stage 2–4 checks plus `gouacheRoundCoversOpaquelyDepletesSizeIndependentlyAndReworks` and `brushPropertiesIndependentlyAffectMaterialSimulation`)

### Qt / QML / Windows CI (verified at `8cd2330`, Stage 5 pending CI run)

- Prior commit `8cd2330` passed Windows CI run `43748765704` (`33/33` unit tests + GUI smoke launch).
- Stage 5 updates in `tests/unit/TestCanvasViewport.cpp` and `tests/unit/TestBrushUi.cpp` are ready for Windows CI verification upon push.

## Immediate next steps

1. Verify the Stage 5 Windows CI run on `origin/arena/6c086365-komapaint` passes build + all 33/33 unit tests + smoke/packaging checks.
2. Continue through Stage 6+ (medium profiles & surface selection UI, physical Paint Mixer panel integration, benchmarks).
