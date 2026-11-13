# KomaPaint icon assets

Hand-made icon files for the KomaPaint interface. Files are uploaded manually;
nothing in this directory is generated.

| Directory        | Purpose                                                                    |
|------------------|----------------------------------------------------------------------------|
| `tools/`         | One icon per drawing tool shown in the tool palette.                       |
| `panels/`        | Controls in a dock panel's header (close, panel menu) and panel symbols.   |
| `layers/`        | Buttons and row toggles of the Layers panel.                               |
| `toolbar/`       | Main toolbar actions (file, undo/redo, zoom) and the project-tab close.    |
| `project-types/` | The project-type illustrations of the New Project dialog.                  |
| `originals/`     | The uploads exactly as received, one sub-directory per category above.     |

The exact filenames, the UI function each file belongs to, the file format and
the way uploads are corrected are described in [`MANIFEST.md`](MANIFEST.md).
A file whose name is not in the manifest is not used by the application.

## Rules

- Transparent PNG, one icon per file, lowercase names with hyphens.
- `originals/<category>/<name>.png` is the untouched upload and is never
  edited. `<category>/<name>.png` is the corrected file the application uses.
- Centring, padding and scale are corrected in the PNG itself, never through
  QML anchors, margins, padding or positioning.
- Only original artwork, or artwork whose licence allows redistribution. Record
  any third-party source in `docs/development/THIRD_PARTY_NOTICES.md`.
- An icon is not referenced from QML, and not listed in a `.qrc` file, until
  the corrected PNG exists. A `.qrc` entry for a missing file stops the build.
  Files under `originals/` are never listed in a `.qrc` file, so they are not
  shipped with the application.
- The `.gitkeep` files only exist so that Git keeps the empty directories. They
  can be deleted from a directory once it holds at least one icon.
