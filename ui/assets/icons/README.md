# KomaPaint icon assets

Hand-made icon files for the KomaPaint interface. The artwork is uploaded
manually; nothing is drawn or generated here. The files the application uses
are the uploads after centring and scaling (see `MANIFEST.md`).

| Directory        | Purpose                                                                    |
|------------------|----------------------------------------------------------------------------|
| `tools/`         | One icon per drawing tool shown in the tool palette.                       |
| `panels/`        | Controls in a dock panel's header (close, panel menu). No files yet.       |
| `layers/`        | Buttons and row toggles of the Layers panel.                               |
| `toolbar/`       | Main toolbar actions (file, undo/redo, zoom), project-tab close. No files yet. |
| `project-types/` | The project-type illustrations of the New Project dialog.                  |
| `originals/`     | The uploads exactly as received, one sub-directory per category above.     |
| `corrections.json` | Measurements of every correction (bounds, padding, scale, shift, notes). |

The exact filenames, the UI function each file belongs to, the file format and
the way uploads are corrected are described in [`MANIFEST.md`](MANIFEST.md).
A file whose name is not in the manifest is not used by the application.

## Rules

- Transparent PNG, one icon per file, lowercase names with hyphens.
- `originals/<category>/<upload name>` is the untouched upload and is never
  edited. `<category>/<name>.png` is the corrected file the application uses;
  it is written by `tools/icons/normalize_icons.py`, not edited by hand.
- Centring, padding and scale are corrected in the PNG itself, never through
  QML anchors, margins, padding or positioning.
- Only original artwork, or artwork whose licence allows redistribution. Record
  any third-party source in `THIRD_PARTY_NOTICES.md` at the repository root.
- Colour comes from the interface, not from the file: the single-colour icons are
  tinted per theme state (`KpIcon` + the `icon*` tokens in
  `ui/themes/KomaTheme.qml`, overridable in Edit → Preferences). Artwork that is
  itself full colour — the `project-types/` illustrations — is never tinted.
- An icon is not referenced from QML, and not listed in a `.qrc` file, until
  the corrected PNG exists. A `.qrc` entry for a missing file stops the build.
  Files under `originals/` are never listed in a `.qrc` file, so they are not
  shipped with the application.
- The `.gitkeep` files only exist so that Git keeps the empty directories. They
  can be deleted from a directory once it holds at least one icon.
