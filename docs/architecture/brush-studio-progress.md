# Brush Studio — progress and validation record

One entry per milestone, with the level of validation actually reached.
Levels, strongest last: **implemented** → **unit-tested** → **integration-tested**
→ **CI-tested** → **packaged** → **launch-tested** → **hardware-tested** →
**artist-tested**.

Nothing in this file is aspirational. A claim that is not backed by a command
that was run does not appear here.

---

## Milestone 1 — architecture audit (stage 1) — complete

**What:** `docs/architecture/brush-studio-audit.md`. An inventory of what the
application actually has (a flat 45-property `BrushSettings`, two unrelated
engine definitions, a library-shaped JSON store with no per-brush file), the
gaps against the specification, the compatibility risks, the proposed data
model and the migration strategy.

**Changed architecturally:** nothing. It is a document.

**Validation:** *implemented*. No code.

---

## Milestone 2 — universal brush model and `.kb` (stage 2) — complete

**Commit:** `116c18e` (+ `9651728` fix).

**What:**
- `src/brush/` — a Qt-free library: `BrushTypes`, `BrushParameters` (65-entry
  registry), `BrushModel` (components, groups, generators, dynamics,
  randomization, exposed properties, shared resources, validation,
  legacy migration) and `BrushFormat` (the `.kb` container).
- `BrushDocument::validate()` rejects what would corrupt a brush: duplicate
  ids, dangling references, mask and group cycles, membership disagreement,
  out-of-range values, duplicate rules, weighted groups with no weight,
  resource byte mismatches. Empty brushes are a warning, not an error, so a
  draft can be saved.
- `.kb` v1: chunked, little-endian, CRC-32, deterministic bytes, unknown chunks
  skipped and reported, known-but-bad chunks a hard error, failed decode
  leaves the caller's document untouched.

**Changed architecturally:** the brush becomes data rather than 45 fields in a
`QObject`. Nothing in the running application reads it yet, so no behaviour
changed.

**Tests:**
- `test_brush_model` — 125 checks.
- `test_brush_format` — 467 checks, including 400 randomised corruptions, a
  500-component brush and a file round-trip.
- Both build and pass with `g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow`,
  and under `-fsanitize=address,undefined`.

**Defects found by the tests and fixed:**
1. `hardness` had no registry entry, so migrating a legacy brush silently
   dropped it. Found by the migration round-trip assertion; the migration path
   now also asserts in debug builds that every legacy field has an entry.
2. The chunk writer emitted a length-prefixed tag where the documented layout
   has a raw four-byte tag. Found by the unknown-chunk test.
3. `/tmp` was hard-coded in the file round-trip test — found by CI on Windows,
   now uses `std::filesystem::current_path()`.

**Validation:** *unit-tested* (here, g++ + sanitizers) and *CI-tested*
(Windows MSVC 2022 / Qt 6.8.3 run `44299599985`, green; both test executables
are run by the CI test step and gate the build — proved by run `44298945946`,
which failed only on defect 3). Confirmed again, with the counts printed in the
run summary, on run `44300815450`: `test_brush_format=467 checks/0 failures`,
`test_brush_model=125 checks/0 failures`.

**Known limitations:**
- No Qt binding, so nothing is visible in the application yet.
- `thumbnail` is stored as raw bytes; no encoder or size policy yet.
- Nested groups are stored and evaluated, but no editor exists to build them.

---

## Milestone 3 — deterministic evaluation (stages 6–8, foundation) — complete

**Commit:** `e8d9ede`.

**What:** `BrushEvaluator` — the decision layer between the document and a
renderer. Given a brush, an input sample and a stamp index it decides which
components paint, where, at what rotation, scale, opacity and blend. It draws
nothing and allocates no tiles, which is what lets both engines share it.

- Six selection modes including weighted (without replacement) and nested
  groups, so a parent set holding leaf/branch/grass sets is expressible.
- Dynamics: signal → curve → parameter. Multiplicative for sizes and opacity,
  additive for angles, with `amount` as a strength rather than a switch.
- Randomization: per parameter, per stamp or per stroke, own seed, four
  distributions, clamped to the parameter's range.
- Scatter with a per-stamp cluster centre, so clustering makes clumps.
- Determinism via splitmix64 keyed by brush seed, group seed, component id and
  stamp index.

**Tests:** `test_brush_evaluator` — 5728 checks, including statistical
agreement with declared weights over 4000 stamps, sequence wrap-around, nested
group contribution, scatter bounds, and a 500-component brush.

**Defects found by the tests and fixed:**
1. A parent group with no members of its own returned before recursing into its
   children, so nested sets contributed nothing.
2. The stroke seed did not influence selection order as intended.

**Validation:** *unit-tested* (g++ + ASan/UBSan) and *CI-tested* (`44299599985`
and `44300815450`, green: build, 46 test executables, `test_brush_evaluator=5728
checks/0 failures`, packaging 33.4 MB / 1354 files, extracted-ZIP launch from a
clean PATH, exit code 0).

The packaged application is *launch-tested*, not *hardware-tested* and not
*artist-tested*. No Brush Studio UI exists yet, so there is nothing for an
artist to test at this stage.

**Known limitations:**
- `Sequence` advances by stamp index, not by a cursor the caller owns; a stroke
  that restarts mid-sequence needs the caller to keep the index.
- Colour variation is resolved as parameters but not yet applied to a colour;
  that belongs with the engine wiring.
- No generators emit geometry yet: `Stamp`, `Particle` and the rest select
  components but nothing converts a `DrawRequest` into pixels.

---

## Stage 9, part 1: the document paints through the production engines

The bridge is `src/drawing/BrushDocumentAdapter.{h,cpp}`. Two jobs, deliberately
in one file:

- **Translation.** `digitalDefinition()` and `materialDefinition()` turn either a
  `LegacyBrush` or a `BrushDocument` into exactly the record the shipped engine
  already consumes. Both inputs go through one resolver, so a migrated brush is
  not a second, approximately-equal brush: it is the same numbers.
- **Rendering.** `DocumentStroke` drives the existing `BrushStroke` loop through
  a new *stamp sink* seam (`BrushStroke::StampSink`). Spacing, angle smoothing,
  smearing carry, clipping and failure reporting stay in one place. A stroke with
  no sink is byte-identical to before -- the default path is unchanged.

**What is proved, by pixels.** `test_brush_document_engine` (892 checks) paints
eight legacy brushes -- pen, pencil, graphite, chisel with direction following,
tilted, smudging, erasing, dry/textured, wide wash -- twice: once through the
flat record, once through `BrushDocument::fromLegacy` and `DocumentStroke`. The
tile stores are compared **byte for byte**, not sampled. They are identical. The
test also proves the material projection is field-for-field identical (all 26
numbers plus tip geometry and the initial paint batch), that three components
paint three dabs per stamp, that hidden components paint nothing, that a
per-component eraser removes paint, that a seed reproduces a stroke exactly, and
that an impossible size fails loudly instead of painting nothing.

**Two model corrections the wiring forced:**

1. `DrawRequest::opacity` is now *relative to the brush*. The absolute value
   belongs to the stroke definition the renderer holds; folding it into the
   request as well applied it twice. The test that asserted the old absolute
   figure was wrong and was changed with it.
2. A randomization rule with no pinned seed now varies **per stroke**; pinning
   `seed`, or setting `sharedSeed`, makes it repeat. Before, every stroke of a
   randomly textured brush was identical, which is the opposite of the point.
   Covered by a new evaluator test.

Two gaps the flat record had, closed rather than papered over:
`persistent-reservoir` (a real Sub-Tool setting with no registry entry) and
`LegacyBrush::persistentReservoir` (a real setting the migration record could not
carry). Both would have silently dropped a value on migration.

**Validation:** *unit-tested* (g++ -std=c++20 with `-Wall -Wextra -Wpedantic
-Wshadow`, four suites: 127 / 467 / 5733 / 892 checks, 0 failures). Not yet
CI-tested, not yet wired into `CanvasViewport`, so not integration-tested and not
packaged.

**Known limitations at this point:**
- The stroke loop still owns direction, barrel rotation and tilt orientation, so
  the evaluator is fed a zero orientation vector. `Signal::Direction` and
  `Signal::Rotation` dynamics therefore resolve to the neutral value until the
  loop hands orientation to the evaluator (one owner, not two).
- `Signal::Time` and `Signal::Progress` are not driven yet: `InputSample` carries
  no timestamp and a stroke's total length is unknown until it ends.
- Component blends other than `Normal` are evaluated and reported but the dab
  composites `Normal` only.
- Brush-scope `size` randomization resolves but does not reach the pixels, since
  spacing is fixed when the stroke begins.

---

## Stage 9, part 2: it is the shipped path

`CanvasViewport::beginStroke` now builds one `brush::BrushDocument` at the top
of a stroke and uses it for both engines. The digital branch paints through
`DocumentStroke`; the Realistic branch resolves its `material::BrushDefinition`
and its pigment batch through the adapter's document overloads. The inline copy
of the thirty digital formulae and the inline copy of the twenty material ones
are gone, so there is one place where a brush becomes engine numbers.

`BrushSettings::toLegacyBrush()` is the single conversion from the Sub-Tool
settings to that record. It also closed a real gap: `LegacyBrush` could not
carry `persistentReservoir`, and the registry had no entry for it, so a brush
that keeps its reservoir across strokes lost that setting on migration.

**Validation:** *CI-tested* on Windows (runs `44311151563`, `44311942139`,
green). Full suite including the viewport, tablet, docking and UI tests, the
package, and the extracted-ZIP launch. Not *hardware-tested* (no tablet in CI)
and not *artist-tested*.

## Stage 10, part 1: settings have homes, and a model the window can use

- Every registry parameter now declares which of the thirteen groups it belongs
  to, and `test_brush_parameters` enforces the contract: one group per setting,
  no empty group, the table and the registry name the same set, ranges agree
  with units, every default inside its own range. 707 checks.
- `BrushStudioModel` is the Qt face of a document: components, groups, every
  registry setting with group and range, the Sub-Tool exposure list, undo/redo
  over whole-document snapshots bounded by memory, and `.kb` open/save that
  leaves the open brush untouched on failure. Ten test cases run headless.

**Two defects that the first CI run of the model found, both fixed:**

1. The exposure list sorted by the raw section string, so a blank section beat
   every named one and the tip's settings would have been buried alphabetically.
   It now sorts by the thirteen groups in their declared order, then the
   artist's order inside the group; an invented group sorts last.
2. Adding an image component produced a brush that could be edited but never
   saved -- the component pointed at no pixel data and the decoder refuses that,
   correctly. New pixel kinds now arrive with a soft round mask, and saving
   validates first and refuses with a sentence rather than writing a file that
   cannot be read back.

**Known limitations at this point:**
- There is no Brush Studio window yet. The model is not registered for QML and
  no menu action opens it; that is the next milestone, and until it exists the
  Studio is *unit-tested* and *CI-tested* but has no artist-facing surface.
- The model edits one document. Collections, `.sut`/`.abr` import, and storing a
  document inside a library preset are not written yet.
- The stroke loop still owns orientation, and `Signal::Time`/`Signal::Progress`
  are still not driven (see part 1).

---

## Next

The window: register `BrushStudioModel` for QML, build the approved layout
(toolbar, component rail, tabbed centre workspace, expandable settings, live
preview), open it from `Edit -> Brush Studio`, and paint the preview with the
production engines through `DocumentStroke`. Then the tip/texture editors, the
guided behaviour blocks, and import/export.
