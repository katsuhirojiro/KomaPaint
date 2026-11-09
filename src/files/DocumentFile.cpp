#include "DocumentFile.h"

#include "../canvas/CanvasDocument.h"
#include "../canvas/TileGeometry.h"
#include "../canvas/SparseTileStore.h"
#include "../canvas/DirtyTileTracker.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"

#include <QColor>
#include <QFile>
#include <QSaveFile>
#include <QSize>
#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace komapaint {
namespace {

constexpr char Magic[] = "KOMAPAINT";
constexpr qsizetype MagicSize = 9;
constexpr qsizetype MinFileBytes = 48;

// ---- little-endian writers ---------------------------------------------------

void putU8(QByteArray& out, quint8 v) { out.append(static_cast<char>(v)); }
void putU16(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
}
void putU32(QByteArray& out, quint32 v)
{
    for (int i = 0; i < 4; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
void putU64(QByteArray& out, quint64 v)
{
    for (int i = 0; i < 8; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xff));
}
void putDouble(QByteArray& out, double v)
{
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    putU64(out, bits);
}
void putUtf8(QByteArray& out, const QString& text, qsizetype maxBytes)
{
    QByteArray bytes = text.toUtf8();
    if (bytes.size() > maxBytes) {
        QString clamped = text;
        // Chop whole code units so we never split a multi-byte sequence.
        // Titles/names are tiny, so the repeated re-encode is fine.
        while (!clamped.isEmpty() && clamped.toUtf8().size() > maxBytes) clamped.chop(1);
        bytes = clamped.toUtf8();
    }
    putU32(out, static_cast<quint32>(bytes.size()));
    out.append(bytes);
}

// ---- CRC-32 (IEEE, zlib polynomial) -----------------------------------------

const std::array<quint32, 256>& crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

struct Crc32 {
    quint32 state = 0xFFFFFFFFU;
    void add(const char* data, qsizetype size)
    {
        const auto& table = crcTable();
        for (qsizetype i = 0; i < size; ++i)
            state = table[(state ^ static_cast<quint8>(data[i])) & 0xff] ^ (state >> 8);
    }
    void add(const QByteArray& bytes) { add(bytes.constData(), bytes.size()); }
    quint32 value() const { return state ^ 0xFFFFFFFFU; }
};

// ---- strict bounds-checked reader -------------------------------------------

class Reader {
public:
    Reader(const QByteArray& bytes, qsizetype end) : m_bytes(bytes), m_end(end) {}

    qsizetype remaining() const { return m_end - m_pos; }

    bool readU8(quint8& v)
    {
        if (remaining() < 1) return false;
        v = static_cast<quint8>(m_bytes[m_pos]);
        m_pos += 1;
        return true;
    }
    bool readU16(quint16& v)
    {
        if (remaining() < 2) return false;
        v = static_cast<quint16>(static_cast<quint8>(m_bytes[m_pos]))
            | static_cast<quint16>(static_cast<quint8>(m_bytes[m_pos + 1]) << 8);
        m_pos += 2;
        return true;
    }
    bool readU32(quint32& v)
    {
        if (remaining() < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<quint32>(static_cast<quint8>(m_bytes[m_pos + i])) << (8 * i);
        m_pos += 4;
        return true;
    }
    bool readU64(quint64& v)
    {
        if (remaining() < 8) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<quint64>(static_cast<quint8>(m_bytes[m_pos + i])) << (8 * i);
        m_pos += 8;
        return true;
    }
    bool readDouble(double& v)
    {
        quint64 bits = 0;
        if (!readU64(bits)) return false;
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }
    bool readUtf8(qsizetype maxBytes, QString& out)
    {
        quint32 length = 0;
        if (!readU32(length)) return false;
        if (length > static_cast<quint32>(maxBytes) || length > static_cast<quint32>(remaining())) return false;
        const char* data = m_bytes.constData() + m_pos;
        QStringDecoder decoder(QStringConverter::Utf8);
        out = decoder(QByteArrayView(data, static_cast<qsizetype>(length)));
        if (decoder.hasError()) return false;
        m_pos += length;
        return true;
    }
    bool readBytes(qsizetype size, const char*& out)
    {
        if (size < 0 || size > remaining()) return false;
        out = m_bytes.constData() + m_pos;
        m_pos += size;
        return true;
    }

private:
    const QByteArray& m_bytes;
    qsizetype m_pos = 0;
    qsizetype m_end;
};

bool premultiplied(quint32 p)
{
    const auto a = p >> 24;
    return ((p >> 16) & 255U) <= a && ((p >> 8) & 255U) <= a && (p & 255U) <= a;
}

struct RectRecord {
    quint64 left = 0, top = 0;
    quint32 width = 0, height = 0;
    const char* pixels = nullptr; // w*h*4 bytes, little-endian premultiplied ARGB
};
struct LayerRecord {
    QString name;
    bool visible = true, locked = false;
    double opacity = 1.0;
    std::vector<RectRecord> rects;
};

} // namespace

quint32 DocumentFile::crc32(const QByteArray& bytes)
{
    Crc32 crc;
    crc.add(bytes);
    return crc.value();
}

bool DocumentFile::save(const CanvasDocument& document, const QString& path, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (path.isEmpty()) return fail(QStringLiteral("No filename was given."));
    const QSize size = document.size();
    if (!CanvasDocument::validSize(size)) return fail(QStringLiteral("The document has invalid dimensions."));

    QSaveFile file(path);
    file.setDirectWriteFallback(false); // Never overwrite the original unless the temp file is complete.
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Could not write to \"%1\". The file was not changed.").arg(path));

    Crc32 crc;
    const auto writeChunk = [&file, &crc](const QByteArray& chunk) {
        crc.add(chunk);
        return file.write(chunk) == chunk.size();
    };
    QByteArray buffer;
    buffer.reserve(4096);
    const auto flush = [&buffer, &writeChunk]() {
        if (buffer.isEmpty()) return true;
        const bool ok = writeChunk(buffer);
        buffer.clear();
        return ok;
    };
    const auto failAndCancel = [&](const QString& message) {
        file.cancelWriting();
        if (error) *error = message;
        return false;
    };

    buffer.append(Magic, MagicSize);
    putU16(buffer, FormatVersion);
    putU32(buffer, static_cast<quint32>(size.width()));
    putU32(buffer, static_cast<quint32>(size.height()));
    putDouble(buffer, document.dpi());
    putU8(buffer, document.hasTransparentBackground() ? 1 : 0);
    const QColor background = document.backgroundColor();
    putU32(buffer, (0xFF000000U | (static_cast<quint32>(background.red()) << 16)
                    | (static_cast<quint32>(background.green()) << 8) | static_cast<quint32>(background.blue())));
    putUtf8(buffer, document.title(), MaxTitleBytes);
    const auto* layers = document.layers();
    putU32(buffer, static_cast<quint32>(layers->activeLayerIndex() < 0 ? 0 : layers->activeLayerIndex()));
    putU32(buffer, static_cast<quint32>(layers->rowCount()));

    for (int row = 0; row < layers->rowCount(); ++row) {
        const Layer* layer = layers->layerAt(row);
        const SparseTileStore& store = layer->tiles();
        const TileGeometry geometry = store.geometry();
        auto points = store.allocatedTilePoints();
        std::sort(points.begin(), points.end(), [](const TilePoint& a, const TilePoint& b) {
            return a.y < b.y || (a.y == b.y && a.x < b.x);
        });
        putUtf8(buffer, layer->name(), MaxNameBytes);
        putU8(buffer, layer->isVisible() ? 1 : 0);
        putU8(buffer, layer->isLocked() ? 1 : 0);
        putDouble(buffer, layer->opacity());
        putU32(buffer, static_cast<quint32>(points.size()));
        if (!flush()) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
        const std::size_t tilePixels = static_cast<std::size_t>(geometry.tileSize()) * static_cast<std::size_t>(geometry.tileSize());
        for (const TilePoint point : points) {
            const auto bounds = geometry.tileBounds(point);
            const auto pixels = store.tilePixels(point);
            if (!bounds || pixels.size() != tilePixels)
                return failAndCancel(QStringLiteral("Internal error: inconsistent tile storage."));
            const std::uint64_t width = static_cast<std::uint64_t>(bounds->right - bounds->left);
            const std::uint64_t height = static_cast<std::uint64_t>(bounds->bottom - bounds->top);
            QByteArray rect;
            rect.reserve(24 + static_cast<qsizetype>(width * height) * 4);
            putU64(rect, static_cast<quint64>(bounds->left));
            putU64(rect, static_cast<quint64>(bounds->top));
            putU32(rect, static_cast<quint32>(width));
            putU32(rect, static_cast<quint32>(height));
            const std::size_t stride = static_cast<std::size_t>(geometry.tileSize());
            bool anyNonZero = false;
            for (std::uint64_t y = 0; y < height; ++y) {
                for (std::uint64_t x = 0; x < width; ++x) {
                    const std::uint32_t p = pixels[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)];
                    if (p != 0) anyNonZero = true;
                    putU32(rect, p);
                }
            }
            // Allocated tiles are never all-zero (an all-zero tile is erased by
            // the store), so this would indicate internal corruption. Failing
            // the save is safer than writing a count that does not match the
            // number of rectangles that follow.
            if (!anyNonZero)
                return failAndCancel(QStringLiteral("Internal error: an empty tile was reported as present."));
            if (!writeChunk(rect)) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
        }
    }
    if (!flush()) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    QByteArray trailer;
    putU32(trailer, crc.value());
    // The trailer bytes are written directly, not through writeChunk, so the
    // checksum never covers itself.
    if (file.write(trailer) != trailer.size()) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    if (!file.commit()) return failAndCancel(QStringLiteral("Could not replace \"%1\". The previous file was left unchanged.").arg(path));
    return true;
}

CanvasDocument* DocumentFile::open(const QString& path, QString* error)
{
    const auto fail = [error](const QString& message) -> CanvasDocument* {
        if (error) *error = message;
        return nullptr;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Could not read \"%1\".").arg(path));
    const qint64 fileSize = file.size();
    if (fileSize < static_cast<qint64>(MinFileBytes))
        return fail(QStringLiteral("\"%1\" is not a KomaPaint illustration.").arg(path));
    if (static_cast<quint64>(fileSize) > MaxFileBytes)
        return fail(QStringLiteral("\"%1\" is larger than the %2 GiB limit.").arg(path).arg(MaxFileBytes / (1024 * 1024 * 1024)));
    QByteArray data = file.readAll();
    if (data.size() != fileSize) return fail(QStringLiteral("\"%1\" could not be read completely.").arg(path));

    // Verify the checksum BEFORE parsing anything else. The last four bytes
    // of the file hold the little-endian CRC-32 of everything before them.
    const char* end = data.constData() + data.size();
    const quint32 storedCrc = (static_cast<quint32>(static_cast<quint8>(end[-4]))
                               | (static_cast<quint32>(static_cast<quint8>(end[-3])) << 8)
                               | (static_cast<quint32>(static_cast<quint8>(end[-2])) << 16)
                               | (static_cast<quint32>(static_cast<quint8>(end[-1])) << 24));
    Crc32 crc;
    crc.add(data.constData(), data.size() - 4);
    if (crc.value() != storedCrc)
        return fail(QStringLiteral("\"%1\" is corrupted (checksum mismatch).").arg(path));

    // ---- fully validate the structure before constructing anything ----
    Reader reader(data, data.size() - 4);
    const char* magic = nullptr;
    if (!reader.readBytes(MagicSize, magic) || std::memcmp(magic, Magic, MagicSize) != 0)
        return fail(QStringLiteral("\"%1\" is not a KomaPaint illustration.").arg(path));
    quint16 version = 0;
    if (!reader.readU16(version)) return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
    if (version != FormatVersion)
        return fail(QStringLiteral("\"%1\" uses format version %2, which this version of KomaPaint cannot read.").arg(path).arg(version));
    quint32 width = 0, height = 0;
    double dpi = 0.0;
    quint8 transparent = 0;
    quint32 backgroundRgba = 0;
    QString title;
    if (!reader.readU32(width) || !reader.readU32(height))
        return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
    if (!CanvasDocument::validSize(QSize(static_cast<int>(width), static_cast<int>(height))))
        return fail(QStringLiteral("\"%1\" declares invalid dimensions %2 × %3.").arg(path).arg(width).arg(height));
    if (!reader.readDouble(dpi) || !std::isfinite(dpi) || dpi <= 0.0 || dpi > 1000000.0)
        return fail(QStringLiteral("\"%1\" declares an invalid DPI value.").arg(path));
    if (!reader.readU8(transparent) || transparent > 1)
        return fail(QStringLiteral("\"%1\" declares an invalid background mode.").arg(path));
    if (!reader.readU32(backgroundRgba) || (backgroundRgba >> 24) != 0xFF)
        return fail(QStringLiteral("\"%1\" declares an invalid background colour.").arg(path));
    if (!reader.readUtf8(MaxTitleBytes, title))
        return fail(QStringLiteral("\"%1\" has an invalid title.").arg(path));
    quint32 activeLayer = 0, layerCount = 0;
    if (!reader.readU32(activeLayer) || !reader.readU32(layerCount))
        return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
    if (layerCount < 1 || layerCount > MaxLayers)
        return fail(QStringLiteral("\"%1\" declares an invalid layer count.").arg(path));
    if (activeLayer >= layerCount)
        return fail(QStringLiteral("\"%1\" declares an invalid active layer.").arg(path));

    std::vector<LayerRecord> records;
    records.reserve(layerCount);
    const auto docWidth = static_cast<quint64>(width);
    const auto docHeight = static_cast<quint64>(height);
    for (quint32 li = 0; li < layerCount; ++li) {
        LayerRecord record;
        quint8 visible = 0, locked = 0;
        quint32 rectCount = 0;
        if (!reader.readUtf8(MaxNameBytes, record.name))
            return fail(QStringLiteral("\"%1\" has an invalid layer name.").arg(path));
        if (!reader.readU8(visible) || visible > 1 || !reader.readU8(locked) || locked > 1)
            return fail(QStringLiteral("\"%1\" has an invalid layer state.").arg(path));
        record.visible = visible != 0;
        record.locked = locked != 0;
        if (!reader.readDouble(record.opacity) || !std::isfinite(record.opacity) || record.opacity < 0.0 || record.opacity > 1.0)
            return fail(QStringLiteral("\"%1\" has an invalid layer opacity.").arg(path));
        if (!reader.readU32(rectCount) || rectCount > MaxTileRectsPerLayer)
            return fail(QStringLiteral("\"%1\" has an invalid tile count.").arg(path));
        record.rects.reserve(rectCount);
        for (quint32 ri = 0; ri < rectCount; ++ri) {
            RectRecord rect;
            if (!reader.readU64(rect.left) || !reader.readU64(rect.top)
                || !reader.readU32(rect.width) || !reader.readU32(rect.height))
                return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
            if (rect.width < 1 || rect.height < 1 || rect.width > 4096 || rect.height > 4096)
                return fail(QStringLiteral("\"%1\" has an invalid tile rectangle.").arg(path));
            if (rect.left >= docWidth || rect.top >= docHeight)
                return fail(QStringLiteral("\"%1\" has an out-of-bounds tile rectangle.").arg(path));
            if (rect.left + rect.width > docWidth || rect.top + rect.height > docHeight)
                return fail(QStringLiteral("\"%1\" has an out-of-bounds tile rectangle.").arg(path));
            const quint64 pixelCount = static_cast<quint64>(rect.width) * rect.height;
            const quint64 byteCount = pixelCount * 4;
            if (byteCount > static_cast<quint64>(reader.remaining()))
                return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
            const char* payload = nullptr;
            if (!reader.readBytes(static_cast<qsizetype>(byteCount), payload))
                return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
            bool anyNonZero = false;
            for (quint64 p = 0; p < pixelCount; ++p) {
                const auto offset = static_cast<qsizetype>(p * 4);
                const quint32 pixel = static_cast<quint32>(static_cast<quint8>(payload[offset]))
                    | (static_cast<quint32>(static_cast<quint8>(payload[offset + 1])) << 8)
                    | (static_cast<quint32>(static_cast<quint8>(payload[offset + 2])) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(payload[offset + 3])) << 24);
                if (!premultiplied(pixel))
                    return fail(QStringLiteral("\"%1\" contains invalid pixels.").arg(path));
                if (pixel != 0) anyNonZero = true;
            }
            if (!anyNonZero)
                return fail(QStringLiteral("\"%1\" contains an empty tile rectangle.").arg(path));
            rect.pixels = payload;
            record.rects.push_back(rect);
        }
        records.push_back(std::move(record));
    }
    if (reader.remaining() != 0)
        return fail(QStringLiteral("\"%1\" has trailing data.").arg(path));

    // ---- structure is valid; build the document off to the side ----
    auto document = std::make_unique<CanvasDocument>();
    document->setSize(QSize(static_cast<int>(width), static_cast<int>(height)));
    document->setDpi(dpi);
    document->setTransparentBackground(transparent != 0);
    if (!transparent) {
        document->setBackgroundColor(QColor(static_cast<int>((backgroundRgba >> 16) & 0xff),
                                            static_cast<int>((backgroundRgba >> 8) & 0xff),
                                            static_cast<int>(backgroundRgba & 0xff)));
    }
    for (quint32 li = 0; li < layerCount; ++li) {
        if (li > 0) document->layers()->addLayer();
        Layer* layer = document->layers()->layerAt(static_cast<int>(li));
        const LayerRecord& record = records[li];
        layer->setName(record.name);
        layer->setVisible(record.visible);
        layer->setLocked(record.locked);
        layer->setOpacity(record.opacity);
        if (record.rects.empty()) continue;

        // Re-tile the stored document-space rectangles onto this build's tile
        // geometry. The file format never stores tile size, so a file written
        // by any KomaPaint build loads identically everywhere.
        auto& store = layer->tiles();
        const TileGeometry geometry = store.geometry();
        const int tileSize = geometry.tileSize();
        const std::size_t tilePixelCount = static_cast<std::size_t>(tileSize) * static_cast<std::size_t>(tileSize);
        std::map<TilePoint, std::vector<std::uint32_t>, TilePointLess> staged;
        for (const RectRecord& rect : record.rects) {
            const TileRect documentRect{static_cast<std::int64_t>(rect.left), static_cast<std::int64_t>(rect.top),
                                        static_cast<std::int64_t>(rect.left + rect.width),
                                        static_cast<std::int64_t>(rect.top + rect.height)};
            const auto range = geometry.tilesForRect(documentRect);
            if (!range) return fail(QStringLiteral("\"%1\" has an out-of-bounds tile rectangle.").arg(path));
            const std::size_t stride = static_cast<std::size_t>(tileSize);
            for (std::int64_t ty = range->first.y; ty <= range->last.y; ++ty) {
                for (std::int64_t tx = range->first.x; tx <= range->last.x; ++tx) {
                    const TilePoint tile{tx, ty};
                    auto bounds = geometry.tileBounds(tile);
                    if (!bounds) return fail(QStringLiteral("\"%1\" has an out-of-bounds tile rectangle.").arg(path));
                    auto& buffer = staged[tile];
                    if (buffer.empty()) buffer.assign(tilePixelCount, 0);
                    const std::int64_t ix0 = std::max<std::int64_t>(bounds->left, documentRect.left);
                    const std::int64_t iy0 = std::max<std::int64_t>(bounds->top, documentRect.top);
                    const std::int64_t ix1 = std::min<std::int64_t>(bounds->right, documentRect.right);
                    const std::int64_t iy1 = std::min<std::int64_t>(bounds->bottom, documentRect.bottom);
                    for (std::int64_t y = iy0; y < iy1; ++y) {
                        std::uint32_t* dst = buffer.data() + static_cast<std::size_t>(y - bounds->top) * stride + static_cast<std::size_t>(ix0 - bounds->left);
                        const char* src = rect.pixels + (static_cast<std::size_t>(y - documentRect.top) * rect.width + static_cast<std::size_t>(ix0 - documentRect.left)) * 4;
                        for (std::int64_t x = ix0; x < ix1; ++x, ++dst, src += 4) {
                            *dst = static_cast<quint32>(static_cast<quint8>(src[0]))
                                | (static_cast<quint32>(static_cast<quint8>(src[1])) << 8)
                                | (static_cast<quint32>(static_cast<quint8>(src[2])) << 16)
                                | (static_cast<quint32>(static_cast<quint8>(src[3])) << 24);
                        }
                    }
                }
            }
        }
        std::vector<SparseTileStore::TileSnapshot> snapshots;
        snapshots.reserve(staged.size());
        for (auto& [tile, pixels] : staged) {
            const bool anyNonZero = std::any_of(pixels.begin(), pixels.end(), [](std::uint32_t p) { return p != 0; });
            if (!anyNonZero) continue; // all-zero staged tiles stay absent, matching save-side omission
            snapshots.push_back(SparseTileStore::TileSnapshot{tile, std::move(pixels)});
        }
        if (!snapshots.empty() && store.restoreTiles(snapshots) != SparseTileStore::WriteResult::Changed)
            return fail(QStringLiteral("The artwork in \"%1\" exceeds the memory limit or cannot be restored.").arg(path));
    }
    document->layers()->setActiveLayerIndex(static_cast<int>(activeLayer));
    document->setTitle(title);
    document->setModified(false);
    return document.release();
}

} // namespace komapaint
