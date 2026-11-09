#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace komapaint {

class CanvasDocument;

// Reads and writes the KomaPaint illustration format (".koma"), format
// version 1. The layout is deliberately independent of the internal tile
// size: artwork is stored as document-space rectangles of premultiplied
// ARGB pixels, and loading re-tiles them into whatever tile geometry the
// running build uses. See docs/architecture/file-format.md for the exact
// binary layout, limits and safety model.
class DocumentFile {
public:
    static constexpr quint16 FormatVersion = 1;
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
    // existing artwork.
    static CanvasDocument* open(const QString& path, QString* error = nullptr);

    // IEEE CRC-32 (zlib polynomial), for the format's end-of-file checksum.
    // Exposed for tests.
    static quint32 crc32(const QByteArray& bytes);
};

} // namespace komapaint
