#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace komapaint {

class CanvasDocument;

// Strict native .koma reader/writer. Versions 1 and 2 load with default
// layer metadata. Version 3 adds blend and label bytes; unknown metadata and
// versions are rejected rather than silently discarded. Files with only
// default metadata are written as v2 for backwards compatibility. Artwork is
// document-space premultiplied ARGB rectangles, independent of tile size.
// See docs/architecture/file-format.md for the binary layout and safety model.
class DocumentFile {
public:
    // Latest supported version; files with default metadata are written as v2.
    static constexpr quint16 FormatVersion = 3;
    static constexpr quint16 FlatEnvelopeVersion = 2;
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
    // existing artwork. Versions 2 and 3 must describe exactly one
    // illustration document.
    static CanvasDocument* open(const QString& path, QString* error = nullptr);

    // IEEE CRC-32 (zlib polynomial), for the format's end-of-file checksum.
    // Exposed for tests.
    static quint32 crc32(const QByteArray& bytes);
};

} // namespace komapaint
