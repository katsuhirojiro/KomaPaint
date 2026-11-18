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
which failed only on defect 3).

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

**Validation:** *unit-tested* (g++ + ASan/UBSan) and *CI-tested* (`44299599985`,
green: build, 46 test executables, packaging, extracted-ZIP launch).

**Known limitations:**
- `Sequence` advances by stamp index, not by a cursor the caller owns; a stroke
  that restarts mid-sequence needs the caller to keep the index.
- Colour variation is resolved as parameters but not yet applied to a colour;
  that belongs with the engine wiring.
- No generators emit geometry yet: `Stamp`, `Particle` and the rest select
  components but nothing converts a `DrawRequest` into pixels.

---

## Next

Stage 9 (engine wiring) is the first point where the model touches the
application: `CanvasViewport::beginStroke` must be able to build a
`BrushDefinition` from a `BrushDocument`, with the existing flat path preserved
until equivalence is proved by tile hash. Everything before that is additive and
cannot regress the shipped editor; that is deliberate.
