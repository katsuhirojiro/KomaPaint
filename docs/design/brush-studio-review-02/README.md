# KomaPaint Brush Studio — second design review

This is the second isolated **Qt 6 / QML** review of brush authoring for KomaPaint. It is a standalone desktop mockup: it does not import KomaPaint's production QML, engine, artwork, brush library, or file formats. It reads a few production icon images for thumbnails and otherwise lives entirely under `docs/design/brush-studio-review-02/`. No shipping file was changed for this review.

The first review established the overall direction. This review tests a sharper question: **can every brush be the same editable anatomy instead of a collection of special cases?** Graphite Pencil, Cloudbank, and Understory now open in exactly the same workspace; the Cloudbank study is built from nested groups, marks, textures, and a mask; Understory uses the same tree with different images. Selection modes, per-part edits, masks, reuse, tip drawing, image import, and a 24-check interaction walkthrough all run through that one model.

## Where to review it

**Windows x64 review build:** the dedicated workflow [`.github/workflows/brush-studio-review-02-windows.yml`](../../../.github/workflows/brush-studio-review-02-windows.yml) freezes the app, tests the extracted EXE on a fresh `windows-2022` runner with a system-only PATH and no project checkout, then publishes a prerelease. Use the latest **Brush Studio Review 02 · Windows x64** prerelease in [GitHub Releases](https://github.com/katsuhirojiro/KomaPaint/releases). Extract the whole ZIP and double-click **KomaPaint Brush Studio Review 02.exe**; keep `_internal` beside it. The ZIP is unsigned.

The release carries the exact verified ZIP, its checksum, `verification.json`, and 13 Qt screenshots from the packaged EXE. The report records the native window title, app-local DLL origins, clean launch and shutdown, no Qt warnings, the interaction checks, and the screenshot sizes.

**First review (unchanged):** [Windows x64 ZIP](https://github.com/katsuhirojiro/KomaPaint/releases/download/brush-studio-windows-44999457194-1/KomaPaint-Brush-Studio-Windows-x64.zip) · [verified Actions artifact](https://github.com/katsuhirojiro/KomaPaint/actions/runs/44999457194/artifacts/14745401609) · [prerelease](https://github.com/katsuhirojiro/KomaPaint/releases/tag/brush-studio-windows-44999457194-1) · [README](../brush-studio-prototype/README.md). The first build remains the reference for the earlier “one brush, one editor” scope; this review changes the anatomy and the responsive behaviour, not that reference build.

## Research → design decisions

The research brief is `docs/design/brush-studio-competitive-research.md`, kept in the working tree alongside this review. Its central finding is that Procreate makes a change cheap to try and reverse, while Clip Studio Paint makes a brush practical to find, tune, and keep for recurring work. The brief also warns that a generous pad alone does not explain where a control lives, and that a narrower layout must not remove brush features.

| Evidence from the research | Decision in this review | Where to see it |
| --- | --- | --- |
| Procreate shows the source image and the painted result together; grain, shape, spacing, and dynamics are recognisable ingredients. | Every part has a live thumbnail, and selecting a part never changes the pad. Editing that part does change the pad immediately. The brush's own stroke preview sits above the properties. | `01`, `02`, `08` |
| Procreate keeps a substantial freehand pad in the editor; a retained stroke makes a change comparable. | The pad stays visible in every desktop state, including Advanced. Draft / Original replay the same retained test strokes, so a comparison isolates the edit. | `01`–`08` |
| CSP starts from a similar tool, keeps everyday controls in the palette, and puts depth in Advanced Tool Settings. | The first screen is a brush and a pad, not a builder. Advanced Brush Setup uses the same tree and the same selected part; nothing becomes a different editor. | `03`, `04`, `05` |
| CSP combines several tip images with ordered, random, or weighted selection, textures, spray, and blend. | Groups carry the selection mode. `Paint Together`, `In Order`, `Random Selection`, and `Weighted Selection` are properties of the group, not a separate cloud tool. Textures and masks are group-scoped leaves. | `02`, `03`, `04` |
| Procreate's Dual Brush and CSP's second brush show that brushes are useful ingredients. Neither limit is a KomaPaint requirement. | **Add Another Brush** inserts a live, editable copy of any brush. Groups can be saved and reused; a saved group keeps its order, selection, and variation. There is no imposed part count. | `06`, `07`; walkthrough |
| Drawing or importing a tip should lead to a visible mark without a registration ceremony. | **Draw a Tip** opens a surface, shows the fitted stamp before accepting, and adds the mark to the selected parent. **Import Image** uses alpha or lightness and adds the result to the same tree. | `09`, `10`; walkthrough |
| A narrow layout should rearrange the editor, not remove features. | At 1024 × 720 the side docks stay usable, Advanced moves its command to a fixed footer, the Reuse grid becomes one column, and the detail controls reflow. Nothing is hover-only. | `11`, `12`, `13` |
| KomaPaint's existing identity is graphite chrome and restrained type, not a competitor skin. | Dark docks, muted-blue selection, 11–13 px Inter, 26–31 px rails, a white paper pad, and drawn thumbnails. No neon, glass, or decorative gradients. | all |

The scope decision that follows from the brief is deliberate: this is an **authoring** surface, not a brush store. The library is a small set of useful starters and two studies; there is no marketplace, sync, or export. Finding and keeping brushes is left for a later, evidence-led step.

## The model

A brush is a tree. A container is either the brush itself or a group; a leaf is a mark, texture, or mask. Any container can hold any other container, and any brush can be inserted into another brush as a subtree. The tree is editable at arbitrary depth.

- **Mark** — an image imprint with size, position, rotation, variation, tone, opacity, blend, and a pressure-to-response curve. A mark can follow the stroke direction.
- **Texture** — a grain image inside its group, with scale, angle, strength, blend, and an anchor choice (paper or stroke).
- **Mask** — subtraction inside its group, to break an edge or carve a hole.
- **Group** — order, selection mode, weight, blend, opacity, visibility, and its own children. Groups can be saved for reuse.
- **Brush** — a complete editable subtree. Adding one copies it into the current brush; both remain editable.

Selection modes act at the group boundary: **Paint Together** paints every child, **In Order** cycles the group's children down the stroke, **Random Selection** chooses independently for each placement, and **Weighted Selection** uses each child's weight. The walkthrough asserts that the same group yields several different picks under Random Selection and that Weighted Selection exposes the same parts.

## The 13 captured states

The packaged EXE captures these at 1024 × 720. Wide source captures (1400 × 900) are used during development, not as Windows evidence.

| Capture | What it demonstrates |
| --- | --- |
| `01-graphite-workspace` | Opens on Graphite Pencil. The paper is white and the printed graphite study is clearly paper, not brush output. Brush sample, pads, and the compact BUILD list are visible. |
| `02-cloud-study` | Cloudbank on the sky study: translucent bands, masks, and a warm horizon. The whole-brush properties show the real counts. |
| `03-advanced-anatomy` | Advanced Brush Setup → This Part on the whole brush: the tree, group selection, blend, and the Contains strip, with the pad still live. |
| `04-nested-group` | Drift & light selected inside Main masses. It contains two groups and two textures; Wrap in Group, Move Up/Down, Duplicate, Change Order, and Remove are present. |
| `05-mark-response` | Broken edge selected, Response tab. The curve is Pressure → Size; the empty state explains that a response is a curve, not a mode. |
| `06-reuse-groups` | Reuse tab: saved groups on the left, whole brushes ready to combine on the right, and Make a Mark at the point of need. |
| `07-same-system-other-brush` | Understory: Leaves and Twigs & dust groups, a Linen weave texture, the same Advanced surface and Reuse tab as Cloudbank. |
| `08-cloud-brush-on-paper` | Broken edge selected. Size 93, opacity 91, angle −14, Follows stroke direction, and the BUILD list. The pad is pixel-identical to `02`; selecting a part did not repaint it. |
| `09-add-a-mark-picker` | Add a Mark with all 15 labelled mark thumbnails, Replace selected mark, Draw a Tip, and Import Image. The dialog is centred and the workspace is dimmed. |
| `10-draw-a-tip` | Draw a Tip with a multi-stroke mark, a light fitted stamp preview, a name, Replace selected mark, and Add as a Mark. |
| `11-compact-workspace` | 1024 × 720: both docks remain, the pad stays dominant, and the footer Advanced command is present. |
| `12-compact-advanced` | 1024 × 720 Advanced → Reuse. The Reuse grid collapses to one full-width column; the right column's Add buttons are no longer clipped. |
| `13-compact-this-part` | 1024 × 720 Advanced → This Part. The source strip, sliders, and value fields reflow inside the available width. |

## Interaction walkthrough

The packaged EXE runs its own 24-check walkthrough (no external test runner is present on the verifying machine):

```sh
# From this directory, with the development runtime:
python app.py --verify-package /tmp/brush-review-02
```

It captures the screenshots, then checks: the mark picker lists every mark; adding a mark; the texture picker lists every texture; adding a texture; the brush picker lists the library; adding another brush; making a group; saving it for reuse; reusing that group in a different brush; Random and Weighted selection producing per-placement picks; an individual mark edit changing the pad; undo restoring the same pixels; hiding a mark; a group blend change; adding a mask; drawing and adding a three-stroke tip; importing an image mark; saving a brush copy; Draft/Original comparison; and 24 further marks being accepted with no part cap. It also asserts that Qt emitted no warnings and that all 13 screenshots are at least 960 × 690.

On Windows the report must additionally say `frozen: true`, `qtPlatform: "windows"`, and architecture 64. The extracted-package verifier fails the build if any check, screenshot, DLL-origin, or checksum assertion fails.

## Run from source

From this directory, with Python 3.12 and a desktop session:

```sh
python -m venv .venv
# Windows: .venv\Scripts\Activate.ps1
. .venv/bin/activate
python -m pip install -r requirements.txt
python app.py --width 1400 --height 900
```

`app.py` opens at a screen-aware size when no width/height is given: up to 1400 × 900, and smaller on a small desktop. `--advanced` opens with Advanced Brush Setup visible. `--captures` writes the 13 state PNGs and exits; `--verify-package` runs the walkthrough at 1024 × 720.

On a minimal Linux install, PySide needs the platform Qt libraries; the list from the first review still applies (`libegl1`, `libgl1`, `libdbus-1-3`, `libfontconfig1`, `libxkbcommon0`, `libxkbcommon-x11-0`, `libxcb-*`, `libx11-xcb1`). The Linux QA used in this repository is offscreen and is not evidence of native Windows rendering or stylus feel.

## Build and verify on Windows

```powershell
cd docs/design/brush-studio-review-02
python -m pip install --only-binary=:all: -r windows/requirements.txt
python -m PyInstaller --noconfirm --clean --workpath "$env:RUNNER_TEMP\brush-freeze" --distpath "$env:RUNNER_TEMP\brush-dist" windows/BrushStudio.spec
python windows/archive.py "$env:RUNNER_TEMP\brush-dist\KomaPaint Brush Studio Review 02" "$env:RUNNER_TEMP\brush-candidate"
```

`archive.py` fails if the payload is missing `qml/Main.qml`, `python312.dll`, `qwindows.dll`, the Inter fonts, the icon assets, or the Qt Quick Controls Basic / Dialogs / Layouts qmldirs; it rejects WebEngine and VirtualKeyboard. `verify-extracted.ps1` launches the extracted EXE without arguments, checks the window title, every loaded module's origin, and a clean exit, then runs `--verify-package` again. The publish job verifies the ZIP checksum and attaches the ZIP, checksum, report, and screenshots to a prerelease.

## What is simulated, and what is not

- **Working:** the full tree model, selection modes, per-part transforms and curves, visibility, group blend and opacity, nested groups, reusable groups and brushes, tip drawing, image import, save copy, pad history, Draft/Original comparison, and the compact layout.
- **Simulated:** the raster compositor stamps deterministic images. Grain and mask use destination composite modes; long paths are floored to a fixed number of placements; pressure/time are synthesised; texture application is tone modulation. This is not KomaPaint's paint engine and cannot prove parity, performance, or stylus behaviour.
- **Session only:** nothing is written to a production library, artwork document, or `.kbrush`/`.koma` file. Closing the app ends the session. There is no export, sync, or undo persistence across launches.
- **Not verified:** a physical Windows tablet, live pen pressure/tilt, GPU rendering on a user's machine, or unaided-artist usability. Those need a separate study; the Windows run proves packaging, startup, dependency isolation, interaction wiring, and screenshots.

## Files added by this review

| Path | Role |
| --- | --- |
| `app.py` | Launcher, screen-aware sizing, trace hook, and review modes |
| `recipes.py` | Mark/texture/mask sources, starters, presentation, selection modes |
| `studio.py` | In-memory tree, drafts, histories, selection, reuse, tip and import logic |
| `preview.py` | Deterministic PIL/QPainter assets, compositor, pad backgrounds, thumbnails |
| `verify.py` | 13-state capture harness and 24-check packaged walkthrough |
| `qml/` | Theme, controls, DrawingPad, CurveEditor, BrushDock, PropertiesDock, Picker, TipRoom, AdvancedSetup, Main |
| `fonts/` | Inter 4.1 (`InterVariable.ttf`, `Inter-OFL.txt`) |
| `windows/` | PyInstaller spec and hook, build requirements, icon, ZIP builder, notices, and the extracted-package verifier |
| [`../../../.github/workflows/brush-studio-review-02-windows.yml`](../../../.github/workflows/brush-studio-review-02-windows.yml) | Package → fresh-runner verification → prerelease |

The production tree (`src/`, `ui/`, `resources/`, `tests/unit/`, the production Windows workflow, and the brush library) is untouched. The only non-review file is the new dedicated workflow.
