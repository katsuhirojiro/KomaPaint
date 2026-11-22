# KomaPaint Brush Studio — interactive design prototype

This is a standalone **Qt 6 / QML application**. It is not linked to KomaPaint’s shipping interface, brush engine, artwork, library, or file formats. The design follows the Procreate and Clip Studio Paint research for this review.

## Windows x64 review build

Open the **Windows x64 design-review prerelease** in [GitHub Releases](https://github.com/katsuhirojiro/KomaPaint/releases). Download `KomaPaint-Brush-Studio-Windows-x64.zip`, extract the **whole ZIP**, and double-click **KomaPaint Brush Studio.exe**. Keep `_internal` beside the executable. No installer, Python, Qt SDK, runtime installation, or terminal is required. The ZIP is unsigned and meant for design review only.

The isolated [Windows packaging workflow](../../../.github/workflows/brush-studio-prototype-windows.yml) builds a candidate, tests the extracted EXE on a separate Windows job, and only then uploads the verified artifact and release. The release also includes the five Qt screenshots, the ZIP checksum, and the verification report. The report records the native window launch with no arguments, the loaded modules, and the in-app interaction checks. The app is not tested on a physical Windows tablet.

## Run from source (development only)

From the repository root, use Python **3.9–3.13** with pip and a desktop session:

```sh
cd docs/design/brush-studio-prototype
python -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python main.py
```

On Windows, use `.venv\Scripts\Activate.ps1` instead of the activation line. On minimal Linux installs, PySide may need the platform’s Qt runtime libraries. For example, on Debian 12 with X11:

```sh
sudo apt install libegl1 libgl1 libdbus-1-3 libfontconfig1 libxkbcommon0 libxkbcommon-x11-0 \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
  libxcb-xinerama0 libxcb-xkb1 libx11-xcb1
```

The source tree also includes an optional development mirror of the same Qt window; it is not needed or bundled for Windows review.

## Five review states

| State | What to try |
| --- | --- |
| **First opening** | Compare the Pencil, Pen, Paintbrush, and Airbrush swatches. Scratch is already paintable. Duplicate opens a visual brush picker; Draw opens a tip surface directly; Import goes straight from the file chooser to an editable imprint. All eight routes lead to the same editor. |
| **Basic pencil** | Open **Pencil**. Lower **Opacity** without changing **Brush size**. Select **Warm paper**, then **Choose texture** to swap the grain. Draw on the pad and undo the test; the brush edits remain. **Draft edits** is the separate brush history. |
| **Brush with multiple marks** | Open **Cloud study**, or add marks to any draft. **In Order**, **Random**, and **Weighted** show the first picks from the same example. Pick **Wind wisp** to size or rotate it independently. Use the arrows to reorder, the eye to hide, and **Edit entry** to duplicate or remove. The arrow beside **Add a Mark** also opens textures, other brushes, and groups. |
| **Advanced** | Open **Dynamics**, then **Advanced**. Pick an input and an output, move a curve point, and switch **Modified / Original**. The retained stroke’s path and inputs don’t change. Advanced also reveals placement, variation, paint, and mask controls. Return to Basic with the same draft, selection, comparison and test history. |
| **Draw a tip** | Draw on the tip surface, undo or revise it, then click **Use as a mark**. Its fitted imprint is visible before accepting. The mark can be tested or reused from **My marks**. |

**Three different previews:** **Brush sample** shows the complete draft in a short stroke; **Single imprint** shows just the selected mark; **Drawing Pad** lets you draw with the complete brush. The pad can show a live pressure study, a sky study, or blank paper. **Compare** replays the same tests with the original brush. Test color and size apply to new pad strokes only; Clear removes user test strokes, not the chosen study backdrop. **My Controls** pins frequently used adjustments; changing their working values affects the pad, not the brush or a kept copy.

The window has a three-column desk at desktop widths, a tabbed tool dock beside the pad at narrower widths, and a stacked, scrollable dock in portrait/small windows. **Pad / Brush Marks / Settings** jump between regions in the smallest layout. None of the brush operations require hovering, right-clicking, or changing to a different editor.

## Working interactions versus simulation

- **Working:** live raster swatches and freehand drawing, independent histories, numeric edit and sliders, stable comparison, visual tip/texture selection and imports, mark order and frequency, per-mark transforms, nested groups, reusable groups, multiple independent brush copies with contribution/blend, custom curves, personal pinning, and responsive layout. Select a group before adding to it; use **Edit entry → Move into group…** to organize existing entries. Select the **Brush sample** to add at the top level again.
- **Simulated:** paint is deterministic raster stamping, not KomaPaint’s paint physics. The provided examples replay synthetic pressure and tilt. Mouse, touch and pen-as-pointer can draw; live hardware pressure and tilt are not read. Curves, dilution, soft edges, texture anchoring, pattern masks and blending produce visible approximations, not production brush behavior. Imports use image alpha or lightness as monochrome coverage, not full-color paint. Grain, built-in marks and the field-study artwork are generated in this prototype.
- **Session only:** keeping a brush, importing or drawing assets, storing groups, and pinning do not write a production library or export `.kb`/`.koma` files. Closing the native application ends the session. There is no artwork document mounted or modified.

## Run the Qt walkthrough

```sh
python verify.py
# Optional screenshots of the actual Qt window:
python verify.py --captures /tmp/brush-studio-review
```

The walkthrough drives Qt controls and checks the resulting draft state and pad pixels. It covers pencil opacity, brush and pad history isolation, texture swap and application, selection modes, per-mark editing, Advanced comparison, personal values, nesting and reuse, minimum-size touch navigation, tip drawing, and image import. It is an interaction check, not a study of unaided artists or physical stylus feel.

## New files

Prototype files are under `docs/design/brush-studio-prototype/`. The only addition outside it is the dedicated Windows workflow. No shipping files were changed for the prototype.

| Path | Role |
| --- | --- |
| `main.py`, `requirements.txt` | Native launcher and isolated Python dependencies |
| `model.py`, `art.py` | In-memory drafts, histories, sample art, and simulated Qt rendering |
| `qml/{Main,Home,Editor,TipRoom,MarkRail,DrawingPad,Inspector,ResponsePanel,CurveGraph,Picker,Personal}.qml` | Window, states, and interactive workspaces |
| `qml/{Desk,DeskButton,DeskScrollBar,Disclosure,Field,Flyout,Glyph,Knob,MarkOrder,ParamList,Section,Segments,Selector,Sheet,StarterCard,Tag,Toggle,Type,Well}.qml`, `qml/qmldir` | Local styles and controls; no production QML imports |
| `remote.py`, `review.html`, `requirements-review.txt` | Optional transport for the live Qt review |
| `verify.py` | Interaction walkthrough and captures, including when frozen in the Windows EXE |
| `windows/` | Packaging spec, Windows-only build dependencies, icon, notices, ZIP builder, and extracted-package verification |
| `fonts/InterVariable.ttf`, `fonts/Inter-OFL.txt` | Inter 4.1 from [upstream](https://github.com/rsms/inter/tree/v4.1), redistributed under the SIL Open Font License |
| `README.md` | This handoff |
