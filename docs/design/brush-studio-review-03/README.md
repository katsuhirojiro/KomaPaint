# Brush Studio — third design review

An isolated PySide6/Qt Quick prototype for reviewing the complete Brush Studio
surface **without changing KomaPaint's production brush engine or window**.

This is not the application. It has its own document, renderer, undo stacks,
catalogue and settings model. It never imports `src/`, writes a `.kb`, opens a
project, or touches the production brush library.

The capability audit and phased proposal that go with this prototype are in
[`docs/architecture/brush-capability-audit.md`](../../architecture/brush-capability-audit.md).

## What this review is testing

| Question | What the prototype does |
| --- | --- |
| Can 17 areas and 28 cards fit one inspector? | A 180 px rail, 320 px inspector and fluid pad; every area has a card and an honest one-line purpose. |
| Can creator, personal and device ownership be visible? | Three labelled layers: creator definition, personal overrides + pins, application/device assistance. |
| Can one curve system bind to any setting? | The pad's response-curve dock edits the selected binding; inline curve accordions open under a setting. Reusable presets edit the same binding. |
| Can construction be a tree rather than a flat stack? | Marks, groups, textures and independent secondary brushes are ordinary editable nodes with reuse and deletion. |
| Can a tip be drawn and shared? | The Tip Room fits a hand-drawn alpha mark and stores it by reference; it can then be assigned to a mark. |
| Can the pad and the brush have separate undo? | `Ctrl+Z` / `Ctrl+Shift+Z` affect only test strokes; `Ctrl+Alt+Z` affects only brush edits. |
| Can a control say it is organisation-only? | Yes. The badge is derived from the renderer's `RENDERED` set, not hand-written per panel. |

The review deliberately keeps a **superset** vocabulary. The production registry
currently has **68 parameters in 13 sections**; this review catalogue has
**122 entries in 17 areas** and marks **66 as preview-rendered** and **56 as
organisation-only**. That gap is the point of the audit, not a claim that the
engine already implements the superset.

## Run it from a checkout

```bash
python3 -m venv /tmp/kp-studio-review-03
/tmp/kp-studio-review-03/bin/pip install -r docs/design/brush-studio-review-03/windows/requirements.txt
cd docs/design/brush-studio-review-03
QT_QPA_PLATFORM=offscreen \
LD_LIBRARY_PATH=/path/to/Qt/6.8.3/gcc_64/lib \
/tmp/kp-studio-review-03/bin/python app.py
```

`windows/requirements.txt` pins PySide6-Essentials 6.8.3 and Pillow. The app
loads the bundled Inter variable font; no network access is used.

Capture the review states non-interactively:

```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=... \
  /tmp/kp-studio-review-03/bin/python app.py --captures /tmp/review-03
```

Run the interaction walkthrough used by the packaged verifier:

```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=... \
  /tmp/kp-studio-review-03/bin/python app.py --verify-package /tmp/review-03-check
```

The walkthrough writes `qt-walkthrough.json` and 27 PNGs. The current local
result is **34/34 checks passed, 0 Qt warnings**; it is Linux offscreen evidence
only. It does not validate Windows, a tablet, pressure, tilt, or an artist's
workflow.

### Performance

The first version of this review re-rendered every stamp of every stroke on
every mouse move. That is now split into three paths:

- **Drawing** — the active stroke is rendered incrementally into its own layer;
  a new chunk costs roughly 10–15 ms at 1200×800 instead of growing
  quadratically with the stroke.
- **Adjusting a setting** — while a slider or curve is moving, the pad shows a
  fast tapered path proxy (single-digit milliseconds) and a full-quality stamp
  render is scheduled when the interaction goes idle. The pad labels this
  state as `FAST PATH DRAFT` rather than pretending it is the final grain.
- **Committed strokes** — the normal cached stamp render remains the reference.
  Composite layers are local to the stamp rather than full-canvas images, and
  root grain is applied once per stroke rather than once per stamp.

A normal packaged launch keeps Qt's threaded render loop and hardware RHI.
`KOMAPAINT_STUDIO_SOFTWARE=1` opts into software rasterisation; the packaged
verifier sets it because the hosted Windows runner has no dependable GPU. The
software path is the slow path, not the desktop default.

## Using the prototype

- **Rail** — all 17 areas. Advanced settings are hidden behind one disclosure,
  not a second mode.
- **Layer control** — `Personal` writes sparse overrides; `Creator` edits the
  definition. `Reset mine` clears only personal data.
- **Row** — click the value for exact entry, drag the slider, use the pin for
  Quick Access, reset one row, or open the small curve affordance. A row carries
  an `ORG` tag when the preview does not compose it, and `DEV` for device-only assistance.
- **Pad** — draw with a mouse or pen. The pressure slider changes the test input;
  the test-mode list changes the sample. Retain stores the current input and
  effective settings for comparison.
- **Curve dock** — signal, strength, draggable nodes, double-click to add a
  point, and `Per stamp` / `Per stroke`. `Linear` restores the linear curve.
  Reusable presets act on the selected binding only.
- **Construction** — select a node, then add a mark, group, texture or secondary
  brush; duplicate/remove are undoable. Secondary brushes are independent
  subtrees in this review.
- **Tip Room** — draw several strokes; `Use as tip` fits the alpha silhouette
  and stores a shared mark.
- **Reuse Room** — inserts a construction into the selected group or brush.
- **Compact** — at 1024×720 the rail collapses and the curve dock drops the
  preset column rather than clipping. It remains usable at the minimum window.

Keyboard: `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` are test-pad undo/redo;
`Ctrl+Alt+Z` / `Ctrl+Alt+Shift+Z` are brush-edit undo/redo; `Ctrl+Shift+A`
toggles advanced settings; `Ctrl+S` saves the initial state for the session.

## Honesty boundary

The preview compositor is **not** the production renderer. It is a deterministic
illustration of the proposed controls. Its badges say which controls it actually
reads:

- `AFFECTS PREVIEW` — the compositor changes pixels from this value;
- `ORGANISATION ONLY` — the value is stored/presented but the preview does not
  claim to render it;
- `DEVICE ONLY` — application/input assistance that must never be written into
  a brush.

The audit records the corresponding production reality, including where Digital
`digitalBrushIssue()` refuses a brush and where the Material path differs.
Production keeps the final word.

## Windows x64 build

The workflow
[`.github/workflows/brush-studio-review-03-windows.yml`](../../../.github/workflows/brush-studio-review-03-windows.yml)
freezes this folder with PyInstaller, ZIPs a onedir bundle, then extracts and
tests that exact ZIP on a **fresh Windows runner with no checkout, Python, Qt or
project setup**. It publishes the verified ZIP, SHA-256 and app screenshots as a
prerelease.

The green run is [45019705276](https://github.com/katsuhirojiro/KomaPaint/actions/runs/45019705276).
The verified prerelease is
[`brush-studio-review-03-windows-45019705276-1`](https://github.com/katsuhirojiro/KomaPaint/releases/tag/brush-studio-review-03-windows-45019705276-1).

- Windows x64 ZIP: **68,995,743 bytes**, SHA-256
  `8580ddbd6631ff9ae335533b188a36589b03a61316fef311fdc7dfbc34a0a2a1`
- 34/34 packaged interaction checks passed, zero Qt warnings.
- 27 screenshots captured from the extracted EXE on the native Windows runner.
- Launch without arguments, app-local DLL origins, clean close, and ZIP re-hash
  all passed.

The hosted Windows desktop is 1024×768, so the packaged captures are 1000×700.
The 1440×900 captures used for local design review are not committed; they are
Linux offscreen layout evidence and are not a substitute for the native run.

## Files

| Path | Purpose |
| --- | --- |
| `app.py` | Bootstrap, Qt Quick engine, image provider, capture/verify modes |
| `catalog.py` | 17 areas, 13 mapped production sections, 122 settings, 28 cards, `RENDERED`, `LIMITS` |
| `model.py` | `Studio`: creator/personal/device layers, undo, dynamics, construction, pad |
| `recipes.py` | Node/document builders, starters, stable hashing |
| `assets.py` | Procedural marks, grains, surfaces, image provider |
| `render.py` | Isolated deterministic stroke compositor |
| `artwork.py` | `QQuickPaintedItem` pad and tip surfaces |
| `verify.py` | Capture set and packaged interaction walkthrough |
| `qml/` | Design system, rail, inspector, curve dock, construction, pad, rooms |
| `windows/` | PyInstaller spec, ZIP assembly, fresh-runner verifier, notices |

Licences and font notices travel in the ZIP; Inter 4.1 is used under the SIL Open
Font License.

## Relationship to the earlier reviews

- Review 01 established the isolated Windows package and the first visual
  language.
- Review 02 established the Advanced Setup / components / preview structure and
  the verified ZIP workflow. It is finished and is not modified by this review.
- Review 03 adds the complete 17-area organisation, three ownership layers,
  inline/reusable curve system, construction tree, tip/reuse rooms, responsive
  compact layout, and a capability audit that separates what the engine can
  actually paint from what the UI can organise.
