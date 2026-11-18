# Brush Studio — architecture audit (Stage 1)

Status: **complete for the current tree** (`519673b`).
Scope: what KomaPaint has today, what a universal brush-authoring system
requires, and the smallest honest path between the two.
Nothing in this document describes code that exists; the implementation it
plans begins in Stage 2 (`src/brush/`).

## 1. What exists today

### 1.1 The brush model is flat

`BrushSettings` (`src/app/BrushSettings.h`, 286 lines) is a `QObject` exposing
**45 `Q_PROPERTY`s** — size, opacity, flow, spacing, hardness, stabilization,
pressure ×3 + gamma, min-size ratio, tilt ×2, stylus rotation, blend strength,
colour mix, erase, engine, and sixteen material properties (load, pigment,
water, body, pickup, aspect, angle, depletion, edge accumulation, tooth,
granulation, drying, pressure-to-amount, pressure-to-contact).

There is no component, no group, no generator, no per-property input mapping,
and no concept of "more than one tip". The whole tip vocabulary is one string:

```
tipStyle ∈ { round, textured, bristle, graphite, stipple }
```

`BrushPresetValues` (`src/app/BrushLibraryStore.h`) mirrors those 45 fields
plus `executableTool` and `toolParams`. A brush *is* that record. Every preset
in a collection carries three copies of it — `defaults`, `current`, and a
bundled default — so a "brush" is 45 numbers and no structure.

### 1.2 Two engines, two unrelated definitions

| | Digital | Realistic |
| --- | --- | --- |
| definition | `BrushDefinition` in `src/drawing/BrushEngine.h` (30 fields) | `BrushDefinition` in `src/material/MaterialEngine.h` (~45 fields + 4 `SensorCurve`s + `PaintBatch`) |
| renderer | `BrushRasterizer::dab` / `BrushStroke` | `dabContactAt` + `MaterialRenderer` + reservoir |
| tip | enum `BrushTip` (5 shapes), procedural mask | `TipGeometry` enum + bristle count/spread |
| dynamics | pressure/tilt/rotation scalars on the definition | `SensorCurve` per signal, paint reservoir, wetness |

The same name in two namespaces, built by the same 45 values in
`CanvasViewport::beginStroke` (`src/app/CanvasViewport.cpp:1084`), which
hand-translates `BrushSettings` into the digital struct. The realistic path
takes a different branch seeded from `material::defaultBrushDefinitions()`
(`CanvasViewport.cpp:896`, `:947`). **Two mappings of one flat record, both
lossy, neither aware of components.**

### 1.3 Storage

`BrushLibraryStore` writes one JSON document, `format: komapaint-brush-library`,
`version: 2`, with a `sha256` self-digest, structured as
`digital`/`realistic`/`toolCollections` → tabs → brushes → `defaults`/`current`.
It is a **library** file, not a brush file: there is no per-brush document, no
`.kb`, and no import path for anyone else's format.

### 1.4 What is genuinely reusable

| system | where | why it matters here |
| --- | --- | --- |
| tile storage | `canvas/SparseTileStore` | tips and textures can be stored as sparse tiles, not one flat buffer |
| material model | `material/MaterialEngine.{h,cpp}` | pigment, water, pickup, depletion are real physical state — Stage 9 must feed them, not fake them |
| shot-level history | `history/UndoStack` | stroke and material-stroke commands already exist; Brush Studio needs its own document history, reusing the same tile-diff codec |
| stabiliser | `drawing/StrokeStabilizer` | already an input filter; Stage 8 binds to it rather than duplicating it |
| Qt-free precedent | `canvas/TileGeometry`, tests built without Qt | lets the brush model be compiled and tested outside a Qt build |
| theme + mockup | `ui/themes/KomaTheme.qml`, `docs/design/brush-studio/` | the visual contract for Stage 3+ |

## 2. Gaps against the specification

| requirement | today | blocking? |
| --- | --- | --- |
| N components per brush | 1 implicit tip | **yes** — core data model |
| component groups, weighted/sequence selection | none | **yes** |
| generators (stamp, particle, spray, sequence…) | one continuous stroker per engine | **yes** |
| per-parameter input mapping (pressure→size, etc.) | fixed scalar fields, no curves per property | **yes** |
| independent randomization per property | none | **yes** |
| choose which settings Sub-Tool Properties shows | hard-coded QML panel | **yes** |
| node graph | none | later |
| `.kb` native per-brush format | none | **yes** |
| `.abr` / `.sut` import-export | none | later |
| tip/texture editor | none | later |
| preview using the production engine | none | later |

## 3. Compatibility risks

1. **Presets must not break.** `BrushLibraryStore::load` already tolerates
   missing keys and migrates v1 → v2 (`BrushLibraryStore.cpp:501`). A v3 that
   only *adds* an optional `graph` object per brush keeps every existing file
   readable, and a v2 file without `graph` must keep loading with identical
   behaviour — the flat fields stay authoritative until a brush has a graph.
2. **Two engines, one definition.** The model must be shared, but the *values*
   it can express must stay engine-aware: a Digital brush must not display
   water pickup, a Realistic brush must not pretend to be a stamp overlay.
3. **`executableTool` decoupling.** A preset may live in any compatible
   collection; the brush document must not assume it owns its placement.
4. **Bit-exact rendering.** Any refactor of `beginStroke` that changes the
   existing flat-field path risks changing pixels. The rule for Stage 9: the
   legacy path stays until a document is proven equivalent, and equivalence is
   measured by tile hash, not by eye.

## 4. Proposed data model (implemented in Stage 2)

Structural parts are typed; *values* are a string-keyed bag so a new generator
or parameter never forces a schema change.

```
BrushDocument
├── identity      id, name, description, engine (Digital|Realistic), timestamps
├── components[]  kind, resourceId, groupId, transform, opacity, blend, mask, weight
├── groups[]      selection mode (all|random|weighted|sequence|alternating|combination)
├── generators[]  kind (continuous|stamp|particle|spray|bristle|procedural|pattern|sequence|multi)
│                 + ordered parameter bag
├── dynamics[]    param ← signal, response curve (arbitrary points), amount
├── randomization[] param, range, distribution, perStamp/perStroke, seed
├── exposed[]     which params Sub-Tool Properties shows, label, section, order, control
├── resources[]   shared pixel data (grayscale/alpha/RGBA), referenced, never duplicated
└── engineBag     engine-specific parameters (reservoir, pickup, wetness… per engine)
```

Decisions and why:

- **Ids are strings, allocated by the document.** Stable across save/load, and
  a component can be referenced by a mask, a group, or a generator without
  index churn when something is reordered.
- **Parameters are registry-checked strings, not an enum.** `BrushParameters`
  declares name, label, range, unit and scope; unknown names fail validation.
  Adding a generator is a registry entry, not a rebuild of the model
  (Part 3's "future generator types without a rewrite").
- **Resources are shared by id.** A 50-leaf brush stores 12 leaf images and 50
  references — Part 2.2's "reused across brushes without duplicating data"
  starts at the document level.
- **Randomization and dynamics are separate lists.** Part 5 vs Part 6 are
  different mechanisms (a mapping vs a distribution) and merging them would
  make both harder to reason about.
- **`exposed[]` is data, not UI state.** Part 10 requires that hiding a setting
  is a property of the brush, not of the window — so it lives in the document
  and travels with the `.kb`.

## 5. Native `.kb` format (Stage 2)

Chunked binary, little-endian, explicitly versioned:

```
"KOMABRSH"  u16 version=1  u16 flags  u32 payloadCrc32
[ tag u32 length payload ]*      tags: META RSRC GRP  COMP GENR DYNA RAND EXPO ENGN
```

- Unknown tags are **skipped**, so a newer file opens in an older build minus
  the features it does not know (and validation reports what was ignored).
- A known tag with a bad length or a failing referential check is a **hard
  error** — never a silent partial load.
- Version > current is refused with a readable message rather than guessed.
- Binary, not JSON, because tips and textures are pixel data; JSON would spend
  the file on base64.

## 6. Migration strategy

1. **v1 (now):** flat 45 values remain the persisted truth for every existing
   brush. `.kb` is additive; nothing reads it yet.
2. **v2:** `fromLegacy()` converts a flat record into a one-tip document, proven
   by round-trip test. A brush saved as `.kb` gains a `graph` object in the
   library JSON; brushes without one keep loading through the old path.
3. **v3:** when the Studio can author components, the library stores `.kb`
   payloads and the flat fields become a derived *projection* for the legacy
   engines until Stage 9 rewires them.

No step deletes data. A truncated or unreadable `.kb` leaves the library entry
untouched and reports the reason.

## 7. Stage plan and exit criteria

| stage | exit criteria |
| --- | --- |
| 1 audit | this document |
| 2 model + `.kb` | Qt-free library, validation, round-trip and malformed-input tests |
| 3 tip editor | draw/edit a tip, undo/redo, save into a component |
| 4 texture editor | layered texture, tiling, seamless check |
| 5 components | multiple components, groups, ordering, reuse |
| 6 generators | stamp + particle + sequence on the shared model |
| 7 randomization | weighted selection, seeds, per-stamp/per-stroke |
| 8 dynamics | curves per parameter, pressure/tilt/velocity/distance |
| 9 engines | both engines consume the model; legacy path preserved |
| 10 preview | production-engine preview with seeds and A/B |
| 11 exposed props | Sub-Tool panel driven by `exposed[]` |
| 12 behaviour builder | guided blocks producing model edits |
| 13 node graph | optional graph over the same model |
| 14 formats | `.kb`, `.abr`, `.sut` with compatibility reports |
| 15 library | save/duplicate/collections/search in the real library |
| 16 optimisation | profiling, memory, artist testing |

## 8. Build and test reality in this environment

The sandbox has `g++` but **no Qt and no CMake**, so:

- anything in the Qt-free core can be compiled and run here, and is;
- anything touching Qt can only be built by the Windows CI job.

Stage 2 is deliberately placed in the Qt-free core for that reason: its tests
run locally *and* in CI, and the Qt binding is a thin adapter added in a later
stage rather than a prerequisite.
