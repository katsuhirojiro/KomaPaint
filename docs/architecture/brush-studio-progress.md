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
  leaves the open brush untouched on failure. The model tests run headless.

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
- This stage provided the model only. Phase B1 below adds the window and menu
  entry; it does not yet expose editing controls.
- The model edits one document. Collections, `.sut`/`.abr` import, and storing a
  document inside a library preset are not written yet.
- The stroke loop still owns orientation, and `Signal::Time`/`Signal::Progress`
  are still not driven (see part 1).

---

## Phase B1 — separate Brush Studio window

**What:** `Edit → Brush Studio` opens a native, non-modal window on demand.
`KomaPaintApp` registers `BrushStudioModel` in the existing `KomaPaint` QML
module. The window owns a single Studio model independently of the painting
workspace. Closing Studio keeps its draft in memory; reopening reuses
the same window and model. An accepted main-window close releases both, while
canceling the artwork save prompt leaves Studio open.

The window is deliberately a read-only shell. No editing, save, or preview
controls are exposed yet. The approved Tip and Texture mockups are unchanged.

**Regression coverage:** `TestBrushUi::brushStudioWindowIsIndependentAndReusable`
uses the application's QML type registration and bundled QML. It opens Studio
from Edit and checks one-window reuse, draft lifetime, project/tool/undo
isolation, and both canceled and accepted application close.

**Validation:** *integration-tested* on Debian 12 with GCC 12 and Qt 6.8.3
(offscreen, software rendering). The desktop target built and the lifecycle
case passed on its own and in the full brush UI suite. All 19 selected CTest
suites passed: brush format, model, evaluation, engines and UI; canvas and
pointer/tablet input; docking, project workflow, session, recovery, and QML
responsiveness. The built desktop executable also passed all eight checks in
its launch verification mode, including the bundled Studio QML resource and
`.koma` save/reopen. That local run was not from a Windows ZIP. Physical tablet
and artist usability testing are still outstanding.

**Build fix found while testing:** `QtPointerAdapter.cpp` used an unsigned
literal with a different type from `std::uint64_t` on Linux. The timestamp
gap now uses the timestamp's type, with the same value and behavior. The
pointer and synthetic tablet tests passed.

**Windows CI:** [Run 44562767088](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44562767088)
passed all 49 test executables, ZIP creation, and extracted application launch
and GUI checks for B1 commit `45843da`.

## Phase B1.5.1 — consistent group membership

Studio now moves a component through the document's membership operation:
its old group entry is removed and its new group lists it once. Ungrouping,
adding or duplicating a grouped component, and removal keep both sides in
agreement. A group's explicit stamp sequence is independent of the global
component row order. Regrouping preserves parent/child links. Group deletion
refuses to leave a generator pointing at a missing group, and document
validation rejects duplicate membership.

**Validation:** the new Qt regression reproduced the stale old membership
before the fix. It now passes regrouping, undo/redo, reordering, nested stamp
selection, removal, and a native `.kb` round trip. Eight full brush CTest
suites passed on Debian 12 / Qt 6.8.3, including the core membership tests
(171 checks) and the Studio model and UI suites. Windows [44565724726](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44565724726)
passed build, tests, and the extracted ZIP launch for `ad3455f`.

## Phase B1.5.2 — truthful component settings

Registry edits now route to the selected component when the setting is
component-scoped. Transform and opacity values are converted from registry
units into the actual component fields, and read back from those same fields.
Invalid IDs, values, modes, and tip-shape choices produce an error before
changing the brush or its history. Reapplying an existing valid value is a
no-op. Validation also rejects non-finite transforms and opacity, unusable
scale/aspect, and invalid blend and tip-shape values in raw brush data.

**Validation:** the new tests reproduced a successful-looking opacity edit
that changed nothing and an invalid opacity edit that gave no error. After
the fix, the registry tests pass for all eight component settings, including
checking the evaluator's draw requests. Invalid edits leave document,
selection, dirty status, and history unchanged. All eight brush CTest suites
passed on Debian 12 / Qt 6.8.3 (179 core checks; 46 QtTest passes in the Studio
model suite, including setup and cleanup). This does not wire unfinished
compositing or texture effects into either engine. Windows [44566697081](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44566697081)
passed build, tests, and the extracted ZIP launch for `6dfb60d`.

## Phase B1.5.3 — atomic native brush saves

The Qt-free `.kb` writer stages output in an exclusively created sibling file
on the destination filesystem. It checks writes, flush/sync and close, verifies
that the staged bytes match and decode to the intended brush, then replaces
with a same-directory rename. No unlink-first or direct-write fallback is
allowed. Studio uses native filesystem paths for Unicode filenames and only
marks the draft clean after the commit succeeds. The replacement is atomic on
filesystems that support rename-over-file; this is not a power-loss guarantee
for the enclosing directory.

**Validation:** 506 format checks, including injected short write, corrupt
readback and replace failure over a valid Unicode-named brush. Each leaves
its original bytes loadable and removes the temp file. Also covered actual
rename failure to a directory, missing parent, invalid brush, and successful
replacement. Studio tests verify unchanged dirty draft/file on failed save.
All eight brush CTest suites passed on Debian 12 / Qt 6.8.3. Windows
[44567746345](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44567746345)
passed build, tests, and the extracted ZIP launch for `75112fe`.

## Phase B1.5.4 — brush diameters

The live control, preset parser, and `.kb` registry now share the signed-int
representation boundary (2,147,483,647 px on supported builds), not the old
8,192 px ceiling. Realistic no longer refuses sizes above 4,096 px. Precision
entry accepts the full range; curved logarithmic rails preserve precision at
small sizes, and shortcuts continue above the quick-size ladder.

Raster bounds are clipped before integer conversion. Realistic scratch is
sized to the clipped dab, checked for arithmetic overflow, and charged to
available memory while held. An unaffordable stroke reports the failure and
the canvas history rolls back its changes. Existing canvas/file tile-storage
constraints are separate and unchanged (including the `.koma` loader's
sparse-tile amplification guard).

**Validation:** Digital and Realistic rendered past the old radius on
12,000 px strips and at the representation endpoint on small canvases. Also
covered native and preset round trips, exact keyboard entry, off-canvas
coordinates, overflow rejection, memory-pressure rollback, and redo after a
failed stroke. Seventeen relevant Linux CTest suites passed (56.65 s); both
engine suites also passed UBSan with float-cast-overflow checks. Windows
[44569823210](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44569823210)
passed build, 49 test executables, benchmark smoke, and the extracted ZIP
launch checks for `2a5b404`.

## Phase B1.5.5 — resource-aware Studio undo

Studio no longer prunes history at 96 MiB. Undo and redo retain metadata
checkpoints but share immutable pixel buffers across states. Replacing a
resource creates new pixels without changing an older state; the `.kb`
representation is unchanged. Capacity diagnostics count both directions and
count shared pixel buffers once. They report retained payload, not process RSS
or allocator overhead. This is session history, not a persistent disk journal.
The existing canvas journal was considered, but it does not yet store Studio
revisions and would require serializing brush data for each checkpoint.

Before editing, Studio checks available memory for the next metadata copy and
creates the checkpoint before mutating. A denied or failed edit reports an
error and leaves the draft, selection, dirty status, and both branches intact.
Only a successful divergent edit clears redo. Undo and redo move existing
nodes and swap state, so replay does not need another allocation for history.
Grouped edits make one step, and replay restores the last successful save/load
state as clean. Exposure metadata changes and reordering now use this same
transaction, including rejecting invalid control, range, and order inputs.

**Validation:** four focused regressions cover one shared 100 MiB pixel buffer
through 512 undo/redo steps (under 2 MiB of metadata), replacement and release
of a resource, pressure rejection without losing redo, allocation-free replay,
grouped steps, dirty state, and exposure transactions. All 49 Linux CTest
suites passed (136.44 s; 51 Studio model QtTest passes including setup and
cleanup) on Debian 12 / Qt 6.8.3. The packaged-resource self-check passed all
8 launch/GUI checks with zero QML warnings. The isolated large-history test
peaked at 127,836 KiB process RSS on this runner (including Qt and the image).

Windows [44571089385](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44571089385)
passed build, all 49 test executables (51 Studio model QtTest passes), benchmark
smoke, deployment, and extracted-package GUI checks for `9130f97`. The
[Windows human-test ZIP](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44571089385/artifacts/14554279238)
is ready for tablet and artist testing.

## Phase B2.1 — native layout and file commands

The separate Studio window now has the menu and command bands, component and
settings rails, center workspace, collapsible Live Preview shelf, and status
band from the approved Tip/Texture design. At narrow widths the three central
regions share a panel switcher; file actions move to a menu. Both themes use
KomaPaint controls and palette tokens. The comparison and stamp/size wells
are explicitly unavailable, not sample strokes.

The brush name and Digital/Realistic choice edit the independent draft. New
and Duplicate are undoable and create independent brush identities. Import
and Save use local `.kb` files and the validated native codec; Export writes
a copy without moving the draft's save point. Save is **not** Save to Library.
The Studio has its own Undo/Redo shortcuts and uses file dialogs owned by
its own native window. Closing Studio still hides it and retains its draft
for the session.

**Validation (Debian 12 / Qt 6.8.3):** full desktop build; eight brush CTest
suites passed (56.68 s). `localFileCommandsPreserveHistoryAndIdentity` covers
Unicode URLs, dirty state, failure/redo preservation, and New/Duplicate/Open
replay. `brushStudioCommandsAndCompactLayout` exercised mouse/keyboard edits,
file dialog accept handlers, compact panel switching, preview collapse and
close. The existing lifecycle/isolation regression also passed. The packaged
self-check now opens the lazy window and round-trips a `.kb`; all nine checks
passed with zero QML warnings. The offscreen plugin itself does not support
raising native windows; this is not a QML warning.

## Phase B2.2 — component stack and read-only editor

The left rail filters the real draft components without rearranging them.
Add, duplicate, show/hide, remove, and move up/down call the document model
and are undoable. The stack retains document order (not a fabricated drag or
compositing operation). The Library rail explicitly says it is unavailable.
Component tabs share selection with the browser, and choosing Tip or Texture
selects a matching component when available. Document-wide and component
settings are still independent of editor navigation.

The center reads existing immutable pixel resources, not a rendered stroke.
It shows a fitted resource in Tip mode or repeats the stored tile in Texture
mode, with Fit and sample zoom controls. The view retains one shared pixel
buffer and downsamples only the display cache (640 px maximum on its long
edge). It does not alter the resource or claim pixel-accurate editing,
seam verification, or production brush behavior. Procedural tips without
stored pixels, pixel tools, and Nodes have explicit availability messages.

**Validation (Debian 12 / Qt 6.8.3):** full desktop build. All seven other
brush suites passed on the brush run (including 57.95 s for `test_brush_ui`);
the Studio model suite passed on rerun after the undo test was updated to
assert the actual original component name rather than the old placeholder.
The new GUI regression exercised both modes, component changes, history,
filtering and zoom, and sampled the actual RGBA tile and its neighbor from
the rendered window. The nine packaged-resource checks passed. Wide Tip and
Texture captures were inspected; they showed no fake strokes or node data.

## Next

B2.3 (registry-driven settings) is pending. B3 (tip editor), B4 (brush library
integration), and B5 (isolated production-engine preview) are not started.
Hardware tablet and artist workflow testing are still outstanding.
