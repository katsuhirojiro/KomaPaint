# KomaPaint illustration format (`.koma`), version 1

This document specifies the on-disk format used by *Save* / *Open*. The
implementation lives in `src/files/DocumentFile.{h,cpp}` and is covered by
`tests/unit/TestDocumentFile.cpp`.

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
   rejected before any document is constructed.
4. **Versioned.** The format version is an explicit field; unknown versions
   are rejected, never guessed at.

## Byte layout

All integers are little-endian. All floating-point values are IEEE-754
doubles. Strings are UTF-8 with a `quint32` byte-length prefix.

```
offset  size    field
0       9       magic: "KOMAPAINT"
9       2       format version (quint16) = 1
11      4       width  (quint32), 1..32768
15      4       height (quint32), 1..32768
19      8       dpi (double), finite, > 0, <= 1 000 000
27      1       transparent background flag (quint8): 0 or 1
28      4       background colour (quint32 ARGB); alpha must be 0xFF
32      4+n     title: quint32 byte length (<= 4096) + UTF-8 bytes
...     4       active layer index (quint32, < layerCount)
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
...     4       CRC-32 (IEEE) of every byte before this field
```

A tile rectangle is one allocated (non-transparent) internal tile clipped to
the document bounds. Rectangles are never all-zero, never out of bounds, and
their pixels satisfy the premultiplied invariant (`r <= a`, `g <= a`,
`b <= a`).

## Save safety

`DocumentFile::save` writes to a temporary file in the destination
directory (`QSaveFile`, direct-write fallback disabled), flushes and closes
it, and only then atomically replaces the target. If any step fails, the
target file is never opened for writing, so the previous save — or the
original file — survives a disk-full or interrupted save.

## Open safety

`DocumentFile::open` reads the whole file, checks the CRC-32 **before**
parsing, fully validates every field and every pixel, and only then builds a
detached `CanvasDocument` (re-tiling rectangles onto the current tile
geometry). The new document is swapped in by the workspace only on complete
success. Any failure deletes the detached document and reports a
user-readable error; the live document, its tiles and its history are
untouched.

Validated limits: file <= 2 GiB, 1..512 layers, <= 262144 rectangles per
layer, rectangles inside the document, per-layer memory budget enforced by
the store (256 MiB, same as editing), opacity in [0,1], DPI finite and
positive, UTF-8 strings well-formed and length-capped, background alpha
opaque, no trailing bytes.

## What is intentionally absent

* Undo history (never serialized).
* Layer selection state is kept (`active layer index`); layer blend modes,
  masks and groups do not exist yet and therefore cannot appear.
* No thumbnails, previews, autosave or recent-files metadata yet.
* No crash-recovery journal. A crash mid-session loses unsaved work, as in
  most editors; a crash mid-*save* is safe (see above).
