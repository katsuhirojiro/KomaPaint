#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace komapaint {

class CanvasDocument;

// Reads and writes KomaPaint's unified native format (".koma"), format
// version 2. The format is a small versioned project envelope around one
// or more typed document payloads. Format version 2 defines a single
// document kind: "illustration". The envelope is deliberately extensible —
// future KomaPaint versions may add a comic-page document kind, comic
// projects with multiple page documents, and a shared asset section —
// without changing this version's layout or breaking existing files.
//
// The layout is deliberately independent of the internal tile size:
// artwork is stored as document-space rectangles of premultiplied ARGB
// pixels, and loading re-tiles them into whatever tile geometry the
// running build uses. See docs/architecture/file-format.md for the exact
// binary layout, limits and safety model.
//
// Compatibility: files written by previous KomaPaint builds use format
// version 1, which is a strict subset of the v2 illustration payload.
// This build opens version 1 files transparently (same extension, same
// magic) and always writes version 2. Files with unknown versions, or
// documents/asset sections this version does not understand, are rejected
// with a clear error; unknown data is never skipped or discarded, so an
// illustration-only build can never silently destroy a newer file on
// re-save.
class DocumentFile {
public:
    // Version written by this build.
    static constexpr quint16 FormatVersion = 2;
    // Legacy version written by KomaPaint before the project envelope
    // existed. Read-only support; never written.
    static constexpr quint16 LegacyFormatVersion = 1;
    // Document kind ids (the first payload byte of each document).
    static constexpr quint8 DocumentKindIllustration = 1;
    // Hard upper bound on documents per file; generous for future
    // multi-page projects, prevents absurd allocations on hostile input.
    static constexpr quint32 MaxDocuments = 8;
    static constexpr const char* FileExtension = "koma";
    // A corrupt or hostile file must not exhaust memory before validation.
    static constexpr quint64 MaxFileBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    static constexpr quint32 MaxLayers = 512;
    static constexpr quint32 MaxTileRectsPerLayer = 262144;
    static constexpr qsizetype MaxTitleBytes = 4096;   // UTF-8 bytes
    static constexpr qsizetype MaxNameBytes = 1024;    // UTF-8 bytes

    // Atomically saves the document. The new file is fully written and synced
    // to a temporary file in the destination directory first; the previous
    // file (if any) is only replaced afterwards, and only on success. On any
    // failure the previous file is left untouched and *error (if given) is
    // set to a human-readable message.
    static bool save(const CanvasDocument& document, const QString& path, QString* error = nullptr);

    // Reads and fully validates the file before constructing anything.
    // Returns a detached, unmodified document on success (caller takes
    // ownership), or nullptr on any failure. Never modifies the file and
    // never touches any other document, so a failed open cannot harm
    // existing artwork. A version 1 file opens transparently; a version 2
    // file must describe exactly one illustration document.
    static CanvasDocument* open(const QString& path, QString* error = nullptr);

    // IEEE CRC-32 (zlib polynomial), for the format's end-of-file checksum.
    // Exposed for tests.
    static quint32 crc32(const QByteArray& bytes);
};

} // namespace komapaint
