# KomaPaint unified native format (`.koma`)

This document specifies the on-disk format used by *Save* / *Open*. The
implementation lives in `src/files/DocumentFile.{h,cpp}` and is covered by
`tests/unit/TestDocumentFile.cpp`.

`.koma` is KomaPaint's **single unified native project format**. Today it
carries individual illustrations; the container is deliberately designed so
future KomaPaint versions can additionally carry individual comic pages,
multi-page comic projects and shared assets/resources — without breaking
existing files and without inventing a competing extension.

Design goals, in order:

1. **Never lose artwork.** Saves are atomic; a failed save leaves the
   previous file byte-for-byte identical. A failed or corrupted open never
   touches the live document.
2. **No lock-in to implementation details.** The format does not store the
   tile size, the tile-allocation strategy, or any in-memory layout detail.
   Artwork is stored as document-space rectangles of premultiplied pixels;
   the loader re-tiles them into whatever tile geometry the running build
   uses.
3. **Strict, predictable validation.** A file that fails any check is
   rejected before any document is constructed. Unknown or reserved data is
   never skipped or discarded — so a newer file is never silently
   "understood" by an older build and never corrupted by a re-save.
4. **Versioned and extensible.** The container version is an explicit
   field; unknown versions are rejected, never guessed at. The container is
   a project envelope: a header, one or more typed document payloads, an
   (currently empty) shared asset section and an end-of-file checksum.

## Versions at a glance

| Version | Status | Description |
| ------- | ------ | ----------- |
| 1 | legacy, read-only | Flat illustration layout: magic + version + illustration body + CRC. Written by KomaPaint ≤ the format milestone. |
| 2 | current | Unified project envelope: magic + version + one typed document + asset section + CRC. Written by this build. |

**Extension rule.** Only `.koma` is ever used. The extension is purely a
convention for the operating system and the file dialogs — serialization
never inspects or depends on the filename extension. A KomaPaint file is
identified by its magic bytes and container version, not by its name.

## Byte layout (version 2)

All integers are little-endian. All floating-point values are IEEE-754
doubles. Strings are UTF-8 with a `quint32` byte-length prefix.

```
offset  size    field
0       9       magic: "KOMAPAINT"
9       2       container version (quint16) = 2
11      4       document count (quint32), currently must be 1 (max 8)
...     ...     document record(s), in order:
                quint8  document kind (1 = illustration; 2 = comic page,
                        reserved; 3 = comic project, reserved)
                quint64 payload length in bytes (quint64)
                payload bytes          (exactly payload length)
...     4       shared asset count (quint32) — must be 0 in version 2
...     4       CRC-32 (IEEE) of every byte before this field
```

An **illustration document payload** is byte-identical to the version 1
document body:

Offsets within the payload:

```
offset  size    field
0       4       width  (quint32), 1..32768
4       4       height (quint32), 1..32768
8       8       dpi (double), finite, > 0, <= 1 000 000
16      1       transparent background flag (quint8): 0 or 1
17      4       background colour (quint32 ARGB); alpha must be 0xFF
21      4+n     title: quint32 byte length (<= 4096) + UTF-8 bytes
...     4       active layer index (quint32, < layer count)
...     4       layer count (quint32), 1..512
...     ...     per layer, in panel order:
                quint32 name length (<= 1024) + UTF-8 name bytes
                quint8  visible (0/1)
                quint8  locked (0/1)
                double  opacity, finite, 0.0..1.0
                quint32 tile-rectangle count (<= 262144)
                per tile rectangle:
                    quint64 left, quint64 top   (document pixels)
                    quint32 width, quint32 height (1..4096)
                    width*height*4 bytes          premultiplied ARGB,
                                                  little-endian per pixel
```

A tile rectangle is one allocated (non-transparent) internal tile clipped to
the document bounds. Rectangles are never all-zero, never out of bounds, and
their pixels satisfy the premultiplied invariant (`r <= a`, `g <= a`,
`b <= a`).

### Version 1 (legacy) layout

```
offset  size    field
0       9       magic: "KOMAPAINT"
9       2       format version (quint16) = 1
11      ...     illustration document payload (as above) starts immediately
...     4       CRC-32 (IEEE) of every byte before this field
```

## Compatibility and migration

* **Opening.** This build reads version 1 and version 2 files
  transparently. Version 1 files were produced by earlier KomaPaint builds
  and use the same extension, the same magic and the same illustration
  payload; opening them is lossless and changes nothing on disk.
* **Saving.** This build always writes version 2. The first save of a
  previously-opened version 1 file migrates it to version 2 **in place** —
  the file is never renamed or left in a half-written state (see *Save
  safety* below), and the artwork round-trips exactly.
* **No competing extensions.** There is no legacy/temporary extension to
  support: `.koma` has been the only native extension since the first
  persisted build. Older files are the version 1 files described here.
* **Unknown versions.** A file with a version other than 1 or 2 is
  rejected with a clear error before any construction happens. A file that
  uses reserved document kinds (comic page / comic project) or declares a
  non-empty shared asset section is rejected as *"uses features this
  version of KomaPaint does not support yet"* — never silently partially
  loaded, so an illustration-only build cannot destroy a newer project file
  on re-save.
* **Multiple documents.** Version 2 files declaring more than one document
  (i.e. multi-page comic projects) are rejected with *"multi-document
  projects are not supported yet"*. The envelope accommodates them; this
  milestone does not implement them.

Serialization never depends on the filename extension, so renaming a
`.koma` file does not corrupt it, and opening is driven by content, not by
the name in the file dialog.

## Save safety

`DocumentFile::save` first serializes the whole project in memory; any
failure there means the destination is never opened. It then writes to a
temporary file in the destination directory (`QSaveFile`, direct-write
fallback disabled), flushes and closes it, and only then atomically
replaces the target. If any step fails, the target file is never opened
for writing, so the previous save — or the original file — survives a
disk-full or interrupted save.

## Open safety

`DocumentFile::open` reads the whole file, checks the CRC-32 **before**
parsing, fully validates the envelope (document count, kinds, payload
lengths, asset section) and then every field and every pixel of the
illustration payload(s), and only then builds a detached `CanvasDocument`
(re-tiling rectangles onto the current tile geometry). The new document is
swapped in by the workspace only on complete success. Any failure deletes
the detached document and reports a user-readable error; the live
document, its tiles and its history are untouched.

Validated limits: file <= 2 GiB, 1..8 documents declared (exactly 1
accepted), 1..512 layers per illustration, <= 262144 rectangles per layer,
rectangles inside the document, per-layer memory budget enforced by the
store (256 MiB, same as editing), opacity in [0,1], DPI finite and
positive, UTF-8 strings well-formed and length-capped, background alpha
opaque, payload lengths exact, no trailing bytes, checksum first.

## Future direction (not implemented)

The version 2 envelope reserves:

* **Document kinds 2 and 3** for comic pages and comic projects.
* **Multiple documents per file** for multi-page projects.
* **A shared asset section** (count > 0) for resources shared across pages
  or illustrations.

None of these exist yet. When implemented, they will appear under a new
container version (or an explicitly extended version 2 rule), and this
build will keep opening version 1 and version 2 illustration files
unchanged. Comic layouts, timelines, animation and publishing workflows
are out of scope for the current milestone.

## What is intentionally absent

* Comic pages, multi-page projects and shared assets (reserved only).
* Undo history (never serialized).
* Layer selection state is kept (`active layer index`); layer blend modes,
  masks and groups do not exist yet and therefore cannot appear.
* No thumbnails, previews, autosave or recent-files metadata yet.
* No crash-recovery journal. A crash mid-session loses unsaved work, as in
  most editors; a crash mid-*save* is safe (see above).
