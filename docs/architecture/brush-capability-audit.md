# Brush capability audit and architecture plan

Status: **implemented in `src/drawing/DigitalBrushCapabilities.{h,cpp}`**
Original tree inspected: `18d4a07` (the base of `arena/64f169fc-komapaint`)
Scope: the real brush model, evaluator, rasterisers, input path, region mask,
Studio window, and brush file/library formats — plus the isolated UI prototype
in `docs/design/brush-studio-review-03/`.

This document exists to answer one question before anyone edits `src/`:

> Which parts of the proposed Brush Studio surface can be honest today, which
> parts are only stored, which parts are refused, and which parts do not have a
> model at all?

It deliberately separates three things that are easy to conflate:

1. **the production engine** (`src/brush`, `src/drawing`, `src/material`);
2. **the production Studio** (`src/app/BrushStudio*`, `ui/workspaces/...`);
3. **the isolated review prototype** (`docs/design/...`, its own compositor).

The prototype is evidence about layout, wording and interaction. It is not
evidence that production can paint what it shows.

---

## 1. Executive summary

- The brush **document** is already broad: 68 registry parameters, 13 sections,
  11 component kinds, 6 group-selection modes, 9 signals, 4 distributions,
  nested groups, masks, generators, dynamics and randomisation.
- The **Digital renderer is deliberately narrow**. `digitalBrushIssue()` accepts
  a whitelist of 25 brush values, `Continuous` generators only, `Normal` component
  blends and resource tips; everything else is **refused rather than silently
  ignored**. That is the correct safety behaviour, but it means several
  settings shown in the current Studio are stored-only.
- The **Realistic/Material path is a different renderer**, not a mode of the
  Digital one. It has real pigment, water, reservoir, pickup, tooth and bristle
  behaviour. Digital does not inherit those effects.
- The **evaluator is the weakest shared layer** for authoring. It resolves
  `size`, `rotation-offset`, scatter/cluster, follow flags and component
  transforms, but never reads `size` from that result, never reads
  `tip-angle`, `rotation-jitter`, colour variation, `color-mix`, taper,
  texture or particle settings; `Signal::Random` returns a constant `0.5`.
- Several concepts in the review prototype have **no production model**: a
  personal override/pin layer, independent secondary brushes as first-class
  subtrees, a reusable construction library, anti-overflow/reference-layer
  policy on a whole stroke, vector erase, and screen-relative size.
- The existing masking path is a **selection × protection pixel test**
  (`CanvasToolExecutor::evaluatePixelMask`) installed per stroke only when a
  selection or boundary eraser exists. It is not edge-aware anti-overflow and
  it is not a reference layer.
- `.kb` is the native brush format and is already atomic and validated.
  There is **no `.abr`/`.sut` import/export**; the only image import is a tip
  image in Studio.
- The review prototype's own compositor reads **66 of its 122 catalogue
  settings**; the remaining 56 are marked **organisation only**, not
  "affects preview". Production has 68 registry parameters, not 122. The
  difference is a design vocabulary, not a shipped contract.

### Recommendation

Approve the **ownership/capability contract first**, then Digital rendering
fidelity, then construction/secondary brushes, then masking and input
assistance, then formats and production UI integration. Do not start by
copying the 122-setting prototype into the registry. Do not add UI for a
control until there is a named owner that consumes it.

The immediate approval gate is section 8. No production edit should precede
that decision.

---

## 2. Method and evidence

Read-only inspection of:

| Area | Evidence |
| --- | --- |
| Registry and sections | `src/brush/BrushParameters.cpp` (`kParameters`, `kSections`, `kSectionOf`) |
| Types and meanings | `src/brush/BrushTypes.h` (`Engine`, `ComponentKind`, `ComponentBlend`, `SelectionMode`, `GeneratorKind`, `Signal`, `Distribution`, scopes) |
| Document model | `src/brush/BrushModel.{h,cpp}` |
| Evaluation | `src/brush/BrushEvaluator.cpp` (`evaluate()` and selection rules) |
| Digital gate | `src/drawing/BrushDocumentAdapter.cpp` (`digitalBrushIssue`) |
| Digital renderer | `src/drawing/BrushEngine.h`, `BrushRasterizer`, `BrushDocumentAdapter` stroke sink |
| Material renderer | `src/material/MaterialEngine.h`, `materialFrom()` in `BrushDocumentAdapter.cpp` |
| Input | `src/input/PointerInput.h`, `QtPointerAdapter.*`, `TabletInputSettings.*`, `WinTabBackend.*` |
| Stabilisation | `src/drawing/StrokeStabilizer.*` |
| Masking | `src/region/RegionEngine.h`, `SelectionMask.*`, `CanvasToolExecutor::evaluatePixelMask` |
| Studio UI/model | `src/app/BrushStudioModel.*`, `BrushStudioPad.*`, `BrushStudioHistory.*`, `ui/workspaces/BrushStudioWindow.qml`, `ui/workspaces/brushstudio/*` |
| Files/library | `src/brush/BrushFormat.*`, `BrushFile.*`, `BrushLibraryStore.*`, `SubToolLibrary.*` |
| Review prototype | `docs/design/brush-studio-review-03/*` |

No production file was edited to produce this audit. The prototype was run
offscreen on Linux; that is layout evidence, not Windows, stylus or artist
evidence.

---

## 3. Bucket definitions used below

| Bucket | Meaning | What it implies |
| --- | --- | --- |
| **(a) Live** | Production reads the value and the active renderer paints it. | Can be labelled "affects painting" for that engine. |
| **(b) Stored** | The document validates and saves it, but the active renderer ignores it or has no path for it. | Keep it in the format; do not claim it paints. |
| **(c) Refused** | The Digital gate rejects a brush using it. Realistic may still paint it. | A feature must be implemented or removed from the Digital surface — never silently dropped. |
| **(d) UI only** | The Studio/review can present it, but no document field or engine consumer exists. | UI must say organisation-only or device-only. |
| **(e) Missing model** | No document field, chunk, or import path exists. | Needs a data-model decision before UI. |
| **(f) Input/evidence gap** | The mechanism exists but lacks a device path, hardware validation, or a truthful test. | Must not be called hardware-tested. |

A single feature can occupy more than one bucket by engine. For example
`water-amount` is **(a)** for Realistic and **(c)** for Digital. The matrix
names the engine when it matters.

---

## 4. What the production engine actually paints

### 4.1 Digital acceptance contract

`digitalBrushIssue()` is the authoritative answer to "will Digital paint this
brush at all?". It requires all of the following:

- `Engine::Digital`, valid document, `formatFlags == 0` and no opaque chunks.
- Brush values limited to:
  `size`, `opacity`, `flow`, `spacing`, `hardness`, `aspect`, `tip-angle`,
  `pressure-size`, `pressure-opacity`, `pressure-flow`, `pressure-gamma`,
  `min-size-ratio`, `tilt-size`, `tilt-angle`, `follow-rotation`,
  `follow-direction`, `blend-strength`, `color-mix`, `stabilization`,
  `rotation-offset`, `scatter-x`, `scatter-y`, `scatter-count`, `cluster`,
  `sequence-step`.
  A value outside this list is accepted only when it equals its registry
  default. `taper-in`/`taper-out` are forced to `0`.
- Dynamics only for `Pressure→size`, `Pressure→opacity`, `Pressure→flow` and
  `Tilt→size`; `perStamp` must be true, `amount ≤ 1`, the curve must equal
  `ResponseCurve::linear()`, and the stored explicit value must agree with the
  binding.
- Randomisation only for `rotation-offset`, `scatter-x`, `scatter-y`,
  `scatter-count`, `cluster`, `component-scale`, `component-opacity`.
- At least one visible tip connected to an enabled generator (`Continuous` only).
- Components must be `ProceduralShape` (with only `tip-shape`), `Tip`, `Image`
  or `ColourImage`; images need valid pixels; no mask, no non-`Normal` blend;
  bitmap tips require `hardness == 100`; colour images require
  `blend-strength == 0`.
- No smudge carry on a colour image.

This is a **gate, not a renderer**. Passing it does not mean every whitelisted
value reaches pixels: the evaluator and stroke loop still decide what to use.

### 4.2 What the evaluator actually resolves

`BrushEvaluator::evaluate()` produces `DrawRequest`s for components. It reads:

- `size` into a local, then discards it with `(void)size`;
- `rotation-offset`, `scatter-x`, `scatter-y`, `cluster`, `scatter-count`;
- `follow-direction`, `follow-rotation`;
- `component-scale`, `component-opacity`, `component-weight` and the
  component transform;
- `sequence-step` for `Sequence` components.

It does **not** read `tip-angle`, `rotation-jitter`, `min-size-ratio`,
`pressure-*`, `tilt-*`, colour variation, `color-mix`, taper, texture,
particles, bristles, material values or `Rendering` values. Those may still
reach pixels through the stroke loop or the rasteriser, but not through this
per-component request. `Signal::Random` is a constant `0.5`; the caller must
resolve per-stamp randomness elsewhere.

### 4.3 What the Digital stroke loop and rasteriser add

The `DocumentStroke`/`BrushStroke` path owns spacing, angle smoothing, smearing
carry, clipping, composition and failure reporting. `BrushRasterizer` samples
the selected tip, applies aspect/hardness and the artwork selection mask, and
deposits or erases into normal tiles. This is why some values are live even
though the evaluator ignores them.

The important boundary for UI honesty:

- **live via request**: component selection, transform, per-component opacity,
  scatter/cluster, follow flags, sequence step;
- **live via stroke definition**: size, spacing, opacity, flow, hardness,
  aspect, pressure response, stabilisation, antialias, blend strength;
- **stored/refused**: texture, taper, spacing jitter, stroke delay, particle
  counts, bristle counts, most material keys, colour variation, non-linear
  response curves, masks, non-Normal component blends, most signals.

### 4.4 Realistic/Material is a second renderer

`materialFrom()` maps a document onto `material::BrushDefinition` and its
reservoir/paint batch. It is a real physical model: spectral mixing, load,
depletion, pickup, persistence, water/body/drying, granulation, tooth (seven
papers), edge accumulation, bristles and wet advance. It is **not** available
inside Digital and does not share Digital's component compositor. Any design
that says "one brush, both engines, identical pixels" is currently false.

---

## 5. Capability matrix by review area

The review prototype uses 17 areas. The production registry has 13 sections
(section 6 maps them). Each row below gives the production reality, the bucket,
the owner, and the smallest honest change.

### 5.1 Stroke Path

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Spacing, spacing variation, position jitter | `spacing` is live; `spacing-jitter` is not in the Digital whitelist and not read by the evaluator | (a) spacing, (c) spacing jitter | `BrushParameters.cpp`, `BrushEngine` stroke loop. Implement jitter in the shared spacing scheduler or keep it out of Digital. |
| Repeated stamps / stamp count | `scatter-count` is the only per-step multiplicity; there is no independent "repeated stamps" concept | (d) | Decide whether `scatter-count` is the UI's "repeated stamps" or add a distinct generator parameter. Do not add a second name for the same number. |
| Follow path / direction | `follow-direction` is live; the evaluator adds direction when requested | (a) | No change. |
| Path smoothing / sharp angles | `path_smoothing` and `sharp_angles` are review-only names; production has `stabilization` and stroke input filtering | (d) | Reuse `stabilization`/`StrokeStabilizer`; do not add a second smoothing control. |
| Stamp sequence / ordered modes | `sequence-step` is live for `Sequence` components; ordered group modes exist | (a) model, (b) UI | `BrushEvaluator`, `BrushStudioModel`. Expose only modes the visible generator uses. |

### 5.2 Stabilisation

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Stabiliser strength | `StrokeStabilizer` is live; responsiveness falls from 1 at 0 to 0.04 at 100; `end()` bypasses the filter | (a) | `src/drawing/StrokeStabilizer.*`. Keep it as the one owner. |
| Stroke delay | Stored in the registry, not in the Digital whitelist, no renderer consumer | (c) | `BrushDocumentAdapter`/`BrushEngine`. Implement trailing delay in the input path or remove it from the Digital surface. |
| Motion filter, speed adjustment, post-stroke correction, preserve sharp angles | No production fields except `StrokeStabilizer::strength`; `sharp_angles` is a review-only control | (e) | New input-filter model or a documented subset of `StrokeStabilizer`; do not duplicate stabilisation in QML. |
| Device pressure curve / palm rejection / wheel behaviour | `TabletInputSettings` has pressure threshold, max, gamma, low/mid/high curve, touch mode, palm rejection, barrel actions, eraser end, hover, tilt cursor | (a) application settings, (d) Studio | Bind the Studio's Input Assistance room to `TabletInputSettings`, not to brush parameters. It must remain device-only. |

### 5.3 Taper and Endings

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Taper in/out | Registry fields exist but Digital forces them to 0; Realistic has its own opacity/size curves, not these settings | (b) stored, (c) Digital | `BrushDocumentAdapter`, `BrushEngine`. Implement an envelope in the shared stroke scheduler before exposing it. |
| Pressure taper / touch taper | No production parameter | (e) | Model decision: these are likely derived from `pressure-size`/`pressure-opacity` plus `min-size-ratio`. Do not invent two new controls until the mapping is proved. |
| Start/end behaviours (pointed, flat, lift, hold…) | No production parameter; review-only vocabulary | (d)/(e) | Add a stroke-envelope policy if approved, or keep as design language outside the engine. |
| Taper profile shaping | No production parameter | (e) | Same envelope owner. |

### 5.4 Brush Tip & Marks

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Size, minimum size, aspect, hardness | Live in the Digital stroke definition; `hardness == 100` is required for bitmap tips | (a) | Preserve the bitmap constraint in the UI, or extend the rasteriser. |
| Tip angle | In the Digital whitelist and used by the rasteriser/stroke definition; not read by `BrushEvaluator` | (a) stroke, (b) evaluator | Decide whether angle belongs in `DrawRequest` or the stroke definition; avoid applying it twice. |
| Multiple tips / components | Document supports 12 kinds, multiple visible components, groups and nested groups; Digital gate permits several compatible components | (a) model/compositor | `BrushStudioModel`, `BrushDocumentAdapter`. Keep per-component controls scoped. |
| Component masks | Stored and validated; Digital refuses any component with a mask; rasteriser has no mask composite | (b)/(c) | `BrushRasterizer`/`BrushDocumentAdapter`. Implement a mask buffer or keep gate-refused. |
| Component blends | 6 modes in the model; Digital accepts only `Normal` | (b)/(c) | `BrushRasterizer`. Implement per-component blend modes before exposing in Digital. |
| Procedural tip shapes | Five built-in masks (`tip-shape` 0–4); bitmap/colour tips require resources | (a) | Preserve as the fallback; do not pretend they are editable vector tips. |

### 5.5 Texture & Grain

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Texture strength/scale/rotation/offset/contrast/depth | Registry fields exist; `texture-*` is not in the Digital whitelist and the Digital rasteriser has no grain compositor. Realistic uses textured tip geometry, not a general image texture | (b) stored, (c) Digital | `src/material/MaterialEngine.*` for Realistic; new Digital grain pass only if approved. |
| Texture as a second image / paper | No texture resource kind in `BrushDocument`; no texture editor | (e) | New resource role and compositor. This is not a registry-only addition. |
| Character of paper (seven papers) | Realistic has tooth/paper presets; Digital has none | (a) Realistic, (c) Digital | Do not show paper in Digital without a renderer path. |

### 5.6 Rendering & Ink

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Opacity, flow, accumulation | Opacity/flow live; `accumulate` is not in the Digital whitelist (default only) | (a)/(c) | `BrushEngine` accumulation path. |
| Antialiasing | Registry field; not in the Digital whitelist; rasteriser already antialiases by default | (c)/(b) | Either make it a real rasteriser switch or stop exposing it. |
| Blend strength / smudge | `blend-strength` is live for Digital smudging; colour-image stamps require 0 | (a) with constraint | Preserve the gate sentence in UI. |
| Ink limit, wet edges, burnt edges, alpha threshold, alpha lock | Registry fields / review terms; Material uses edge accumulation and body; Digital has no equivalent | (b)/(d) | Material owner or a new Digital ink model; do not add UI names without consumers. |
| Layer blend / watercolour edges | Review-only or material-only | (d)/(a) Realistic | Keep engine-specific labels. |

### 5.7 Wet Mixing

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Water, load, pigment, body, pickup, depletion, drying, wetness, granulation, tooth, edge accumulation, bristles | Realistic/Material has real fields and simulation | (a) Realistic | `src/material/MaterialEngine.*`. Keep out of Digital. |
| Wet mixing in Digital | Digital gate treats all material keys as unsupported; no material reservoir in the digital path | (c) | A Digital wet model is a research project, not a settings-panel task. |
| Wet edges / watercolour toggle | Review vocabulary; Material uses `edgeAccumulation`, not this name | (d) | Map to the material field or remove. |

### 5.8 Colour Dynamics

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Hue/saturation/brightness variation | Registry fields exist; `BrushEvaluator` does not read them; Digital whitelist excludes them | (b)/(c) | `BrushEvaluator`/`BrushRasterizer`. Resolve per stamp and apply before the tip is cached. |
| `color-mix` | In the Digital whitelist, but not read by the evaluator; material pickup is separate | (b) | `BrushEvaluator`/stroke loop. Define whether it mixes with canvas colour or a second stored colour. |
| Secondary colour | No production field beyond `color-mix`; review-only | (e) | Model decision if wanted. |
| Colour scope (per stroke / per stamp) | No production field | (e) | Add only with the variation implementation. |

### 5.9 Input Dynamics

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Signal→parameter bindings | Model has 9 signals and arbitrary response curves; Digital gate permits only Pressure→size/opacity/flow, Tilt→size, `perStamp`, linear curves, explicit value agreement | (a) narrow, (c) rest | `BrushDocumentAdapter`, `BrushEvaluator`. Widen one binding at a time with pixel tests. |
| Per-stamp / per-stroke | `perStamp` is required for Digital bindings; per-stroke curves are not applied | (c) | Implement or keep out. |
| Random / Time / Progress / Direction / Rotation signals | Signals exist; `Random` is constant 0.5; Time and Progress are not driven; Direction/Rotation were historically zeroed by the stroke loop | (b)/(f) | `BrushEvaluator`, `CanvasViewport`/stroke loop. One owner must provide orientation and timing. |
| Reusable dynamic presets | No production "preset" object for curves | (d) | UI convenience only until a model exists; applying it must edit the binding, not create a second curve system. |
| Artist strength / fallback | No personal dynamics layer | (e) | Personal override model (section 7). |

### 5.10 Spraying & Particles

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Particle count / spread | Registry fields; no particle generator in Digital (`Continuous` only) | (b)/(c) | `BrushEngine`/`BrushEvaluator`. A particle generator changes stamp emission, not just a number. |
| Spray density / deviation / direction / speed | Review-only or partial; no production fields | (d)/(e) | Generator model + deterministic seed tests. |
| Scatter | Live scatter/cluster in the evaluator, distinct from particle emission | (a) | Keep scatter and particles separate in the UI. |

### 5.11 Stroke Modes

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Group selection: All, Random, Weighted, Sequence, Alternating, Combination | Model and evaluator implement six modes; nested groups are supported | (a) model | `BrushEvaluator`. Expose only for the selected group. |
| Repeated stamp / ordered sequence / alternating stroke | `GeneratorKind::Sequence` and sequence components exist; Digital allows only `Continuous` generators; no independent generator schedule | (b)/(c) | New generator execution, deterministic tests, and a gate update. |
| Seed / reproduce | `mode_seed` / randomisation seeds exist; evaluator is deterministic by brush/stroke/group/component/stamp | (a) core | Preserve the seed in pad capture/retain. |
| Ribbon / alternate modes | Review-only | (d) | Do not add names before the generator model. |

### 5.12 Secondary Brushes

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Nested groups / multiple components | Yes; a group can contain groups and components | (a) model | `BrushModel`, `BrushEvaluator`. |
| Independent secondary brush as a subtree with its own settings | No brush-level subtree object; components share the document's parameter bag | (e) | New document concept: a child brush with its own parameters, generator and selection. Do not fake it with a normal group. |
| Unlimited secondary count | No cap exists, but also no secondary-brush kind | (e) | The model needs the kind before a count is meaningful. |
| Reuse a construction into another brush | No reusable fragment object; Studio edits one document | (e) | New resource/fragment reference model, not a clipboard-only feature. |
| Per-secondary tip/opacity/blend | Components have opacity/blend/transform; they do not have their own pressure curves or material | (b)/(e) | Decide whether these are sub-brushes or dressed components. |

### 5.13 Materials & Surface

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Paper, tooth, absorbency, granulation, drying, edge, wetness | Realistic/Material fields and renderer | (a) Realistic | `MaterialEngine`. |
| Surface preview in Studio | Studio reads resources, not a material simulation; pad uses production viewport for some paths | (b)/(f) | Keep the pad's production route; do not draw a fake paper preview in QML. |
| Digital material parity | None | (c) | Explicitly out of scope until a shared compositor exists. |

### 5.14 Properties

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Max/min size | `BrushSettings` and `.kb` support large signed-int diameters; raster bounds are clipped and memory checked | (a) | Preserve, including error handling. |
| Eraser | Legacy erase and component `erases`; Digital sink erases tiles | (a) | Expose as a mode, not a second brush type. |
| Anti-overflow / stay inside selection | `CanvasToolExecutor::evaluatePixelMask` combines selection × protection and is installed only when a selection or boundary eraser exists; no whole-stroke edge-aware policy | (b)/(c) | New stroke-bounds policy; `src/region` already exposes `ReferenceSet`/`buildReferenceSet` as a seam. |
| Reference layer | No document field; `region::ReferenceMode` exists for region operations, not brush reading | (e) | New reference policy; do not call it anti-overflow. |
| Vector erase | No vector layer, no vector eraser | (e) | Out of scope unless a vector layer model is approved. |
| Screen-relative size | No field or zoom-aware sizing | (e) | Needs canvas/viewport zoom input, not a brush-only flag. |
| Preserve layer alpha, persistent reservoir | Persistent reservoir exists for Material; alpha lock has no Digital path | (a) Material, (c) Digital | Engine-specific labels. |

### 5.15 Construction

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Component stack, add/duplicate/remove/reorder, show/hide | `BrushStudioModel` and B2.2 UI exist; undoable | (a) | Keep the document order; no fabricated drag semantics. |
| Nested tree with brushes/groups/textures/masks | Model supports groups/masks/components; no first-class brush subtree | (b)/(e) | Same decision as secondary brushes. |
| Node graph | None | (e) | Not required for the first production pass. |
| Raw tip/texture preview | Studio reads immutable pixels, fit/zoom, no pixel editing | (a) viewer, (b) editor | Tip room can be a separate editor with its own undo. |

### 5.16 Quick Access

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Creator exposure list | `BrushStudioModel` exposure candidates/state, separate page, undoable | (a) | Reuse it. |
| Personal pins / quick overrides | No personal layer in the document or Studio model | (e) | New ownership layer (section 7). |
| Personal "set initial"/reset mine | No per-artist baseline | (e) | Same layer. |
| Personal overrides must not rewrite creator | Not representable today | (e) | Same layer; this is the central architecture change. |

### 5.17 Input Assistance

| Capability | Production reality | Bucket | Owner / required change |
| --- | --- | --- | --- |
| Device pressure curve / threshold / gamma | `TabletInputSettings` | (a) application | Bind to the Studio room; never persist in `.kb`. |
| Stabilisation default | `TabletInputSettings::stabilizationDefault`; `StrokeStabilizer` | (a) application | One owner: input config, not brush registry. |
| Palm rejection, touch mode, barrel actions, eraser end, hover/tilt cursor | `TabletInputSettings` | (a) application | Expose in a device-only page. |
| Per-device profiles | Settings are application-wide, not per-device | (e) | Model decision; do not fake profiles in the review. |
| Windows WinTab / Ink | `WinTabBackend`, pointer adapter, diagnostics | (a) platform, (f) evidence | Needs native Windows + stylus capture; Linux offscreen is not evidence. |

---

## 6. Production 13 sections ↔ review 17 areas

### 6.1 Production → review

| Production section (`kSections`) | Review area(s) | Note |
| --- | --- | --- |
| Brush Tip | Brush Tip & Marks | `aspect`, `tip-angle`, `hardness`, `tilt-angle` |
| Size | Brush Tip & Marks | `size`, `min-size-ratio`; review folds size into Tip |
| Opacity & Flow | Rendering & Ink | `opacity`, `flow`, pressure response, accumulation |
| Stroke | Stroke Path + Taper & Endings | `spacing`, `spacing-jitter`, `taper-in/out`, `follow-direction` |
| Stabilization | Stabilisation | `stabilization`, `stroke-delay` |
| Rotation | Stroke Path + Brush Tip & Marks | `rotation-offset`, `rotation-jitter`, `follow-rotation` |
| Scatter | Stroke Path + Spraying & Particles | `scatter-*`, `cluster` |
| Texture | Texture & Grain | registry texture fields; no image-texture resource |
| Color | Colour Dynamics | variation, `color-mix` |
| Material & Blending | Wet Mixing + Materials & Surface + Rendering & Ink | Realistic fields; the review splits them by intent |
| Dynamics | Input Dynamics | signals and curves |
| Rendering | Rendering & Ink | `antialias`, `blend-strength` |
| Advanced | Spraying & Particles + Properties + Construction | particles, sequence, component scope, tip shape |

### 6.2 Review → production

| Review area | Production home | Status |
| --- | --- | --- |
| Stroke Path | Stroke + Scatter + Rotation | Partial; some review rows are organisation-only |
| Stabilisation | Stabilization + input settings | Partial; delay is gate-refused |
| Taper & Endings | Stroke (`taper-in/out`) | Stored but Digital-refused; most review controls have no field |
| Brush Tip & Marks | Brush Tip + Size + component scope | Strong; bitmap constraints |
| Texture & Grain | Texture | Stored; no Digital grain |
| Rendering & Ink | Opacity & Flow + Rendering + Material | Mixed by engine |
| Wet Mixing | Material & Blending | Realistic only |
| Colour Dynamics | Color | Stored; evaluator doesn't apply |
| Input Dynamics | Dynamics | Narrow Digital whitelist |
| Spraying & Particles | Advanced + Scatter | Stored; no Digital particle generator |
| Stroke Modes | Advanced + group selection | Model exists; generator execution does not |
| Secondary Brushes | none | **No production home** |
| Materials & Surface | Material & Blending | Realistic only |
| Properties | Advanced + region/selection | Partial; anti-overflow/reference/vector/screen size missing |
| Construction | component model + Studio stack | No reusable fragments or brush subtrees |
| Quick Access | exposure list only | **No personal layer** |
| Input Assistance | `TabletInputSettings` | Application-only, not a brush section |

**Deliberate omissions from the review vocabulary:** no second "smoothing"
control alongside `stabilization`; no second "repeated stamps" alongside
`scatter-count`; no "wet" toggle for Digital; no "vector erase" until a vector
layer exists.

---

## 7. The architecture gap that matters most: ownership

The review's three layers are not a presentation detail. They are the part
production cannot express today:

| Layer | Review intent | Production today |
| --- | --- | --- |
| **Creator definition** | Values the brush author publishes; saved in the brush | `BrushDocument` / `.kb`; Studio edits it |
| **Personal overrides + pins** | The artist's changes on top of one brush; never written into the creator's file; resettable; optional Quick Access | **Missing.** `BrushSettings` is a single live record; Studio edits the draft. |
| **Application/device assistance** | Pressure curve, stabiliser default, palm rejection; this device only, never serialised into `.kb` | `TabletInputSettings` exists, but the Studio has no ownership boundary around it |

### Proposed contract (needs approval before code)

1. **Document stays authoritative.** A `.kb` file contains only the creator
   definition. Opening a brush never mutates it because an artist moved a
   slider.
2. **Personal overrides are a sparse map plus a pin set**, keyed by registry
   parameter name, stored with the brush *reference* in the library (not in the
   `.kb`). An override is resolved at stroke start into the same
   `ResolvedValues` path as today; no parallel renderer.
3. **Pins do not change values.** They only choose which rows appear in Quick
   Access.
4. **Device assistance is an input configuration**, resolved before the brush
   sample. It cannot be saved into `.kb`; it must not be silently applied to a
   different device.
5. **Every row declares its capability**: `rendered(engine)`, `stored`,
   `organisation`, or `device`. The UI badge is data, not a hand-maintained
   string per panel.
6. **Undo is split**: pad/test strokes own one history; creator/personal/
   assistance edits own another. Retaining test input must store the effective
   settings and seed, not a pointer to mutable state.

This contract is the cheapest way to make the review's honesty labels real. It
also protects existing presets: with no overrides, the resolved values are
bit-identical to today.

---

## 8. Phased plan and approval gate

No phase below is approved by this document. The user approves the plan (or a
subset) first. Each phase must land with its own tests and must preserve the
legacy no-override path byte-for-byte.

### Phase 0 — approval and scope lock (no code)

**Deliverable:** this audit, the review-03 prototype, and a written decision on
the four open questions:

1. Is the first production target **Digital fidelity** or **personal ownership**?
2. Which review-only concepts are accepted as vocabulary, and which are cut?
3. Is a **secondary brush** a document subtree with its own parameters, or a
   dressed group? (The answer changes the file format.)
4. Is **anti-overflow/reference layer** a selection policy, a stroke-bounds
   policy, or a region-engine feature?

**Exit:** explicit approval. Until then: no `src/`, `ui/`, `tests/` or
`resources/` edits.

### Phase 1 — capability contract and ownership (low pixel risk)

**Scope:** registry metadata for engine support; resolve personal overrides
into `ResolvedValues`; pins; device-assistance boundary; capability badges in
Studio; split undo identifiers.

**Owners:** `src/brush/BrushParameters.{h,cpp}`, `src/app/BrushStudioModel.*`,
`BrushStudioHistory.*`, `ui/workspaces/...`, `BrushLibraryStore.*`.

**Exit criteria:**

- A parameter cannot be labelled "renders" unless a named engine consumer
  exists; the label is derived, not copied.
- With no overrides, every existing brush renders byte-identically.
- Personal overrides never enter `.kb`; clearing them restores exact creator
  values.
- Pins only affect list membership.
- Device settings cannot be serialised into a brush.
- Unit tests for sparse map, precedence, reset, save/reload, and split undo.

**Explicitly not in this phase:** new pixels, new generators, new formats.

### Phase 2 — Digital fidelity for existing whitelisted values

**Scope:** make the evaluator consume what it already claims: `size`,
`tip-angle`, `rotation-jitter`, `color-mix` (with a defined meaning), colour
variation; one owner for orientation and timing; per-stamp random signal;
taper/spacing jitter implemented or removed from the Digital gate.

**Owners:** `src/brush/BrushEvaluator.cpp`, `src/drawing/BrushDocumentAdapter.cpp`,
`src/drawing/BrushEngine.*`, `CanvasViewport.cpp`.

**Exit criteria:**

- Every changed binding has a pixel test against the legacy path where the
  legacy path is supposed to be equivalent.
- The gate has no value it accepts and then ignores; accepted-but-ignored is a
  test failure.
- Existing eight legacy brushes still compare byte-for-byte.
- Seeded repeatability across runs.

**Risk:** pixel changes. Keep the legacy path as the reference until
equivalence is proven by tile hash.

### Phase 3 — texture, taper and ink as real compositor passes

**Scope:** a shared Digital grain/texture compositor; an envelope applied once
in the stroke scheduler; antialias/accumulation switches; error messages for
unsupported combinations. Realistic keeps its own material path.

**Owners:** `BrushRasterizer`, `BrushEngine`, `BrushDocumentAdapter`,
`src/material/MaterialEngine.*` for parity decisions.

**Exit criteria:**

- Texture changes pixels in Digital and is not silently dropped.
- Taper is applied once; no double application through evaluator and stroke.
- Unsupported mask/blend/non-linear curve combinations remain refused with a
  sentence, not a blank stroke.
- Performance budget recorded for a textured stroke at the maximum supported
  size.

### Phase 4 — generators, dynamics and spray

**Scope:** execute `Particle`, `Sequence`, repeated-stamp and ordered schedules;
widen the Digital gate only as each generator is proven; deterministic seeds;
retain/replay input with the effective settings.

**Owners:** `BrushEvaluator`, `BrushEngine`, `BrushDocumentAdapter`,
`BrushStudioPad`/`BrushStudioHistory` for retain.

**Exit criteria:**

- Each generator has a seeded pixel test and a gate test.
- No generator is accepted by the gate before its execution path exists.
- The pad's retained strokes reproduce the same pixels after edits (or the
  comparison explicitly says which settings changed).
- Time/Progress/Random signals have a defined driver or stay unavailable.

### Phase 5 — construction: secondary brushes and reusable fragments

**Scope:** decide and implement the subtree model; independent parameters,
generator and selection per secondary; a reusable fragment reference; the
construction tree; tip room and reuse room as real editors.

**Owners:** `BrushModel`, `BrushFormat`/`BrushFile` (new chunk), `BrushStudioModel`,
`BrushStudioHistory`, `BrushStudioAssetView`, QML construction/tip/reuse rooms.

**Exit criteria:**

- A secondary brush is a first-class object, not a group pretending to be one.
- Editing a secondary cannot mutate the parent or a sibling (copy-on-write).
- Reuse inserts an independent copy or an explicit shared reference; the UI
  says which.
- `.kb` round-trip preserves the new structure and old files still load.
- Undo/redo covers structural edits and resource replacement.

**Risk:** the largest file-format change. Do not combine it with Phase 2.

### Phase 6 — selection bounds, reference layer and vector erase (research gate)

**Scope:** whole-stroke anti-overflow, edge-aware boundaries, reference-layer
reads, vector erase, screen-relative size. Reuse `region::ReferenceSet` and
`CanvasToolExecutor::evaluatePixelMask`; do not add a second mask system.

**Owners:** `src/region/*`, `CanvasToolExecutor.cpp`, `CanvasViewport.cpp`,
`BrushDocumentAdapter` (if the policy becomes brush state).

**Exit criteria:**

- A written semantic difference between selection clipping, boundary
  protection and edge-aware anti-overflow.
- A stroke-level test that proves paint cannot cross the chosen boundary.
- Reference layer reads are explicit about which layers and which revisions.
- Vector erase is either implemented on a vector layer or visibly unavailable;
  no fake toggle.
- Screen-relative size consumes real viewport zoom, with a test at two zooms.

### Phase 7 — formats and interop

**Scope:** `.abr`/`.sut` import (read-only first), compatibility report,
resource extraction, `.kb` export. Never silently approximate an unsupported
feature.

**Owners:** `BrushFormat`, `BrushFile`, `BrushLibraryStore`, `SubToolLibrary`.

**Exit criteria:**

- Import never overwrites the user's brush on a partial parse.
- Unknown/unsupported features are listed by name.
- Round-trip of native `.kb` remains byte-stable.
- A malformed file leaves the library untouched.

### Phase 8 — production UI integration

**Scope:** adapt the review-03 layout to real production types: 180 rail,
320 inspector, fluid pad; inline curve accordions; construction tree; tip/reuse
rooms; split undo; quick access; honest badges. Replace the isolated `Studio`
model with the production `BrushStudioModel`; no parallel data path.

**Owners:** `ui/workspaces/BrushStudioWindow.qml`, `ui/workspaces/brushstudio/*`,
`src/app/BrushStudioModel.*`, `BrushStudioPad.*`.

**Exit criteria:**

- The window opens at 1024×720 without clipped controls, and at 1440×900
  without unused dead space.
- Every visible value comes from the production model; no duplicated catalogue.
- Keyboard, IME, high-DPI and screen-reader labels are checked.
- Zero QML warnings in the packaged self-check.
- A control labelled "affects preview" changes pixels; a control labelled
  "organisation only" does not claim to.

### Phase 9 — evidence

**Scope:** Windows x64 packaged build, native stylus capture, artist task study,
performance measurements.

**Exit criteria:**

- A fresh Windows runner launches the extracted EXE with a system-only PATH.
- A physical pen reports pressure, tilt and barrel rotation through
  `WinTabBackend`; the test states which device and driver.
- An artist completes the agreed tasks; failures are recorded, not smoothed
  over.
- Linux offscreen captures are labelled as layout evidence only.

---

## 9. Open decisions and risks

| Decision | Why it blocks work | Recommended default |
| --- | --- | --- |
| Personal layer first or Digital fidelity first? | Both touch the same resolve path; doing both at once makes pixel regressions hard to attribute | Personal layer first (Phase 1), because it is reversible and protects creator files |
| Secondary brush = subtree or group? | Determines `.kb` chunk and evaluator semantics | Subtree with own parameter bag; do not overload groups |
| Anti-overflow semantics | Selection clipping, boundary protection and edge-aware fill are different products | Start with explicit selection × protection; research edge-aware separately |
| Texture in Digital | Requires a compositor and performance work | Implement only after Phase 1/2; keep gate-refused until then |
| `.abr`/`.sut` | Large compatibility surface; easy to overpromise | Read-only import with a compatibility report, after the model is stable |
| 122-setting vocabulary | Production has 68 registry parameters; copying the prototype creates dead controls | Treat the review as a superset proposal; promote only with an owner |
| Hardware evidence | No tablet in this sandbox; Windows CI is mouse automation | Do not call any result hardware-tested or artist-tested |

### Risks to watch

1. **Double application.** Size, angle and taper can be applied in the
   evaluator, the stroke definition and the rasteriser. One owner per value.
2. **Gate drift.** Adding a key to the whitelist without a consumer recreates
   the silent-ignore problem the gate was designed to prevent.
3. **Format creep.** Secondary brushes and reusable fragments change `.kb`.
   Version it; skip unknown chunks; never write a partial brush.
4. **History confusion.** Pad strokes, creator edits, personal overrides and
   device settings are four histories. A single `Ctrl+Z` stack will destroy
   trust.
5. **Preview honesty.** The isolated compositor is not the production renderer.
   Its labels must remain data-driven, and its screenshots must never be
   presented as production output.

---

## 10. What review 03 proves — and what it does not

**Proves (local Linux offscreen and the packaged Windows x64 build):**

- 17 areas, 28 cards, 122 catalogue entries, 66 preview-rendered and 56
  organisation-only entries can coexist without a second naming system.
- 180 rail + 320 inspector + fluid pad holds at 1440×900 and 1024×720.
- Inline curve accordions, construction tree, tip room, reuse room, split pad/
  brush undo, pins and quick access are reviewable interactions.
- A packaged interaction walkthrough can assert 34 behaviours and zero Qt
  warnings in the isolated app.
- The exact Windows x64 ZIP (`68,991,615` bytes, SHA-256
  `f6dbd899…5aa87ba0`) launched from a fresh runner with a system-only PATH and
  passed 35/35 checks with 28 screenshots at 1000×700. That is evidence about
  the review app, not about the production window.

**Does not prove:**

- that production paints texture, taper, particles, colour variation or
  secondary brushes;
- that `.abr`/`.sut` import exists;
- that anti-overflow, reference layers, vector erase or screen-relative size
  exist;
- that the production `ui/workspaces/BrushStudioWindow.qml` adopts this layout;
- that a stylus, a physical tablet or an artist can use it.

The 34 checks in `verify.py` are evidence about the review app, not a
production acceptance suite. The production suites remain
`tests/unit/TestBrush*` and the packaged `KOMAPAINT_VERIFY_PACKAGED` path.

---

## 11. Concrete file map for the first three approved steps

| Step | Files | Change | Test |
| --- | --- | --- | --- |
| Capability metadata | `src/brush/BrushParameters.h/.cpp` | Add engine-support metadata (rendered by Digital/Realistic, stored-only, requires gate) next to each parameter; keep the registry order and `kSectionOf` contract | `TestBrushParameters` rejects missing/duplicate metadata; existing 707 checks still pass |
| Personal resolve | `src/app/BrushStudioModel.*`, `BrushLibraryStore.*`, `src/brush/BrushTypes.h` | Sparse overrides + pins keyed by parameter name; resolve into the existing `ResolvedValues` path; no `.kb` write | New unit test: no overrides = byte-identical; override precedence; clear/reset; save/reload; personal data absent from `.kb` |
| Capability badges | `ui/workspaces/...` + `BrushStudioModel` | Show the derived badge; disable or annotate controls the selected engine cannot paint | UI test: a stored-only row says organisation-only; a rendered row says affects preview; no duplicated label table |

This table is the smallest honest start. It does not change pixels, does not
alter `.kb`, and gives every later phase a truthful UI contract to build on.

---

## 12. Approval block

- [ ] I approve **Phase 0 only** (keep auditing/prototyping; no production edits).
- [ ] I approve **Phase 1** (capability contract + personal ownership).
- [ ] I approve **Phases 1–2** (contract, then Digital fidelity).
- [ ] I approve the plan through **Phase 5** (construction/secondary brushes).
- [ ] I want a different order or a narrower scope: ______________________.

Nothing in `src/`, `ui/`, `tests/` or `resources/` changes until one of these is
chosen. The review-03 prototype and this document are the complete current
deliverable.
