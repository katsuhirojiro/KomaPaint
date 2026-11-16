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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace komapaint {
namespace {

constexpr char Magic[] = "KOMAPAINT";
constexpr qsizetype MagicSize = 9;
constexpr qsizetype MinFileBytes = 48;

// Versions 1 and 2 share a flat illustration payload. Version 3 extends
// the v2 layer records with metadata; v4 adds material cells and v5 lifecycle state.

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
void putFloat(QByteArray& out, float v)
{
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    putU32(out, bits);
}
bool putUtf8String(QByteArray& out, const std::string& value)
{
    if (value.size() > std::numeric_limits<quint32>::max()
        || value.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) return false;
    const auto valueSize = static_cast<qsizetype>(value.size());
    const QString text = QString::fromUtf8(value.data(), valueSize);
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() != valueSize || std::memcmp(bytes.constData(), value.data(), value.size()) != 0) return false;
    putU32(out, static_cast<quint32>(bytes.size()));
    out.append(bytes);
    return true;
}
bool putUtf8(QByteArray& out, const QString& text, qsizetype maxBytes)
{
    const QByteArray bytes = text.toUtf8();
    // QString can contain unpaired UTF-16 surrogates, which toUtf8 replaces.
    // Never silently save a changed name or title.
    if (bytes.size() > maxBytes || QString::fromUtf8(bytes) != text) return false;
    putU32(out, static_cast<quint32>(bytes.size()));
    out.append(bytes);
    return true;
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
    // Sub-range reader, used for the version 2 document payloads.
    Reader(const QByteArray& bytes, qsizetype begin, qsizetype end) : m_bytes(bytes), m_pos(begin), m_end(end) {}

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
    bool readFloat(float& v)
    {
        quint32 bits = 0;
        if (!readU32(bits)) return false;
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }
    bool readUtf8(qsizetype maxBytes, QString& out)
    {
        quint32 length = 0;
        if (!readU32(length)) return false;
        if (length > static_cast<quint32>(maxBytes) || length > static_cast<quint32>(remaining())) return false;
        const char* data = m_bytes.constData() + m_pos;
        // Strict RFC 3629 validation of OUR OWN: the file format spec rejects
        // invalid UTF-8 outright, and we do not want codec-dependent results.
        qsizetype i = 0;
        const qsizetype size = static_cast<qsizetype>(length);
        while (i < size) {
            const auto b0 = static_cast<quint8>(data[i]);
            if (b0 < 0x80) { ++i; continue; }
            int continuation = 0;
            quint32 codepoint = 0;
            if (b0 >= 0xC2 && b0 <= 0xDF) { codepoint = b0 & 0x1FU; continuation = 1; }
            else if (b0 >= 0xE0 && b0 <= 0xEF) { codepoint = b0 & 0x0FU; continuation = 2; }
            else if (b0 >= 0xF0 && b0 <= 0xF4) { codepoint = b0 & 0x07U; continuation = 3; }
            else return false;
            for (int j = 0; j < continuation; ++j) {
                if (i + 1 + j >= size) return false;
                const auto b = static_cast<quint8>(data[i + 1 + j]);
                if ((b & 0xC0U) != 0x80U) return false;
                codepoint = (codepoint << 6) | (b & 0x3FU);
            }
            if (continuation == 2 && codepoint < 0x800U) return false;   // overlong
            if (continuation == 3 && codepoint < 0x10000U) return false; // overlong
            if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) return false; // surrogate
            if (codepoint > 0x10FFFFU) return false;
            i += 1 + continuation;
        }
        out = QString::fromUtf8(data, size);
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
    // Offset of the next unread byte (for the v2-v5 document-length record).
    qsizetype position() const { return m_pos; }

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
struct MaterialPixelRecord {
    quint64 x = 0, y = 0;
    material::MaterialCell cell{};
};
struct LayerRecord {
    QString name;
    bool visible = true, locked = false;
    double opacity = 1.0;
    quint8 blendMode = 0, colorLabel = 0;
    std::vector<RectRecord> rects;
    std::vector<material::MaterialComposition> materialCompositions;
    std::vector<MaterialPixelRecord> materialPixels;
};
// The illustration document payload: the exact byte body that existed as
// "the format" in version 1 and that is embedded inside the version 2
// project envelope. Shared by the v1 (legacy) reader and the v2-v5 envelope
// reader. Note: this build never stores nor reads the tile size; artwork
// is always document-space rectangles of premultiplied ARGB pixels.
struct IllustrationPayload {
    quint32 width = 0, height = 0;
    double dpi = 0.0;
    quint8 transparent = 0;
    quint32 backgroundRgba = 0;
    QString title;
    quint32 activeLayer = 0;
    std::vector<LayerRecord> records;
    bool hasSurface = false;
    material::SurfaceProfile surface = material::coldPressedPaper();
};

// Fully validates an illustration document payload stored in data[begin,end).
// On success, *payload is filled (records borrow pointers into data, so data
// must outlive the result). On any failure, *error carries a readable message
// and false is returned. Nothing is constructed until every check passes.
bool parseIllustrationPayload(const QByteArray& data, qsizetype begin, qsizetype end,
                              quint16 version, const QString& path, IllustrationPayload* payload, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    Reader reader(data, begin, end);
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
    if (!reader.readUtf8(DocumentFile::MaxTitleBytes, title))
        return fail(QStringLiteral("\"%1\" has an invalid title.").arg(path));
    quint32 activeLayer = 0, layerCount = 0;
    if (!reader.readU32(activeLayer) || !reader.readU32(layerCount))
        return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
    if (layerCount < 1 || layerCount > DocumentFile::MaxLayers)
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
        if (!reader.readUtf8(DocumentFile::MaxNameBytes, record.name))
            return fail(QStringLiteral("\"%1\" has an invalid layer name.").arg(path));
        if (!reader.readU8(visible) || visible > 1 || !reader.readU8(locked) || locked > 1)
            return fail(QStringLiteral("\"%1\" has an invalid layer state.").arg(path));
        record.visible = visible != 0;
        record.locked = locked != 0;
        if (!reader.readDouble(record.opacity) || !std::isfinite(record.opacity) || record.opacity < 0.0 || record.opacity > 1.0)
            return fail(QStringLiteral("\"%1\" has an invalid layer opacity.").arg(path));
        if (version >= DocumentFile::LayerMetadataVersion &&
            (!reader.readU8(record.blendMode) || record.blendMode > 3 ||
             !reader.readU8(record.colorLabel) || record.colorLabel > 7))
            return fail(QStringLiteral("\"%1\" has invalid layer metadata.").arg(path));
        if (!reader.readU32(rectCount) || rectCount > DocumentFile::MaxTileRectsPerLayer)
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
            const char* payloadBytes = nullptr;
            if (!reader.readBytes(static_cast<qsizetype>(byteCount), payloadBytes))
                return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
            bool anyNonZero = false;
            for (quint64 p = 0; p < pixelCount; ++p) {
                const auto offset = static_cast<qsizetype>(p * 4);
                const quint32 pixel = static_cast<quint32>(static_cast<quint8>(payloadBytes[offset]))
                    | (static_cast<quint32>(static_cast<quint8>(payloadBytes[offset + 1])) << 8)
                    | (static_cast<quint32>(static_cast<quint8>(payloadBytes[offset + 2])) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(payloadBytes[offset + 3])) << 24);
                if (!premultiplied(pixel))
                    return fail(QStringLiteral("\"%1\" contains invalid pixels.").arg(path));
                if (pixel != 0) anyNonZero = true;
            }
            if (!anyNonZero)
                return fail(QStringLiteral("\"%1\" contains an empty tile rectangle.").arg(path));
            rect.pixels = payloadBytes;
            record.rects.push_back(rect);
        }
        if (version >= DocumentFile::MaterialVersion) {
            quint64 compositionCount = 0;
            if (!reader.readU64(compositionCount) || compositionCount == 0
                || compositionCount > static_cast<quint64>(reader.remaining() / 12))
                return fail(QStringLiteral("\"%1\" has an invalid material composition table.").arg(path));
            record.materialCompositions.reserve(static_cast<std::size_t>(compositionCount));
            for (quint64 ci = 0; ci < compositionCount; ++ci) {
                QString medium;
                quint64 componentCount = 0;
                if (!reader.readUtf8(std::numeric_limits<qsizetype>::max(), medium)
                    || !reader.readU64(componentCount)
                    || componentCount > static_cast<quint64>(reader.remaining() / 16))
                    return fail(QStringLiteral("\"%1\" has a truncated material composition.").arg(path));
                material::MaterialComposition composition;
                const QByteArray mediumBytes = medium.toUtf8();
                composition.mediumId.assign(mediumBytes.constData(), static_cast<std::size_t>(mediumBytes.size()));
                composition.pigments.reserve(static_cast<std::size_t>(componentCount));
                for (quint64 pi = 0; pi < componentCount; ++pi) {
                    QString pigmentName;
                    quint32 residual = 0;
                    double proportion = 0.0;
                    if (!reader.readUtf8(std::numeric_limits<qsizetype>::max(), pigmentName)
                        || !reader.readU32(residual) || !reader.readDouble(proportion)
                        || !std::isfinite(proportion) || proportion <= 0.0)
                        return fail(QStringLiteral("\"%1\" has an invalid material pigment component.").arg(path));
                    const QByteArray pigmentBytes = pigmentName.toUtf8();
                    std::string pigmentId(pigmentBytes.constData(), static_cast<std::size_t>(pigmentBytes.size()));
                    if ((pigmentId.empty() && (residual >> 24U) != 0xffU)
                        || (!pigmentId.empty() && residual != 0))
                        return fail(QStringLiteral("\"%1\" has an invalid residual pigment identity.").arg(path));
                    composition.pigments.push_back({std::move(pigmentId), residual, proportion});
                }
                if (ci == 0) {
                    if (!composition.mediumId.empty() || !composition.pigments.empty())
                        return fail(QStringLiteral("\"%1\" has an invalid material composition sentinel.").arg(path));
                } else {
                    double total = 0.0;
                    for (const auto& component : composition.pigments) total += component.mass;
                    if (composition.mediumId.empty()
                        || (!composition.pigments.empty() && (!std::isfinite(total) || std::abs(total - 1.0) > 1e-9)))
                        return fail(QStringLiteral("\"%1\" has a non-normalized material composition.").arg(path));
                }
                record.materialCompositions.push_back(std::move(composition));
            }
            quint64 materialPixelCount = 0;
            const qsizetype materialRecordSize = version >= DocumentFile::MaterialLifecycleVersion ? 56 : 48;
            if (!reader.readU64(materialPixelCount)
                || materialPixelCount > static_cast<quint64>(reader.remaining() / materialRecordSize))
                return fail(QStringLiteral("\"%1\" has an invalid material pixel count.").arg(path));
            record.materialPixels.reserve(static_cast<std::size_t>(materialPixelCount));
            std::pair<quint64, quint64> previous{};
            bool firstMaterialPixel = true;
            for (quint64 mi = 0; mi < materialPixelCount; ++mi) {
                MaterialPixelRecord pixel;
                quint64 compositionId = 0;
                if (!reader.readU64(pixel.x) || !reader.readU64(pixel.y) || !reader.readU64(compositionId)
                    || compositionId == 0 || compositionId >= compositionCount
                    || !reader.readFloat(pixel.cell.pigmentMass) || !reader.readFloat(pixel.cell.water)
                    || !reader.readFloat(pixel.cell.binder) || !reader.readFloat(pixel.cell.filler)
                    || !reader.readFloat(pixel.cell.wetness) || !reader.readFloat(pixel.cell.consistency))
                    return fail(QStringLiteral("\"%1\" has a truncated material pixel.").arg(path));
                if (version >= DocumentFile::MaterialLifecycleVersion) {
                    if (!reader.readFloat(pixel.cell.drying) || !reader.readFloat(pixel.cell.settling))
                        return fail(QStringLiteral("\"%1\" has truncated material lifecycle state.").arg(path));
                } else {
                    pixel.cell.drying = 1.0F - pixel.cell.wetness;
                    pixel.cell.settling = 0.0F;
                }
                if (pixel.x >= docWidth || pixel.y >= docHeight
                    || !std::isfinite(pixel.cell.pigmentMass) || pixel.cell.pigmentMass < 0.0F
                    || !std::isfinite(pixel.cell.water) || pixel.cell.water < 0.0F
                    || !std::isfinite(pixel.cell.binder) || pixel.cell.binder < 0.0F
                    || !std::isfinite(pixel.cell.filler) || pixel.cell.filler < 0.0F
                    || !std::isfinite(pixel.cell.wetness) || pixel.cell.wetness < 0.0F || pixel.cell.wetness > 1.0F
                    || !std::isfinite(pixel.cell.consistency) || pixel.cell.consistency < 0.0F || pixel.cell.consistency > 1.0F
                    || !std::isfinite(pixel.cell.drying) || pixel.cell.drying < 0.0F || pixel.cell.drying > 1.0F
                    || !std::isfinite(pixel.cell.settling) || pixel.cell.settling < 0.0F || pixel.cell.settling > 1.0F
                    || (pixel.cell.pigmentMass == 0.0F && pixel.cell.water == 0.0F
                        && pixel.cell.binder == 0.0F && pixel.cell.filler == 0.0F)
                    || (record.materialCompositions[static_cast<std::size_t>(compositionId)].pigments.empty()
                        && pixel.cell.pigmentMass != 0.0F))
                    return fail(QStringLiteral("\"%1\" has an invalid material pixel.").arg(path));
                pixel.cell.compositionId = static_cast<std::size_t>(compositionId);
                const std::pair<quint64, quint64> position{pixel.y, pixel.x};
                if (!firstMaterialPixel && position <= previous)
                    return fail(QStringLiteral("\"%1\" has duplicate or unsorted material pixels.").arg(path));
                previous = position;
                firstMaterialPixel = false;
                record.materialPixels.push_back(pixel);
            }
        }
        records.push_back(std::move(record));
    }
    if (version >= DocumentFile::FormatVersion) {
        QString surfaceId, surfaceName;
        if (!reader.readUtf8(DocumentFile::MaxNameBytes, surfaceId)
            || surfaceId.trimmed().isEmpty()
            || !reader.readUtf8(DocumentFile::MaxNameBytes, surfaceName))
            return fail(QStringLiteral("\"%1\" has an invalid substrate identifier.").arg(path));
        material::SurfaceProfile surface;
        const QByteArray idBytes = surfaceId.toUtf8();
        const QByteArray nameBytes = surfaceName.toUtf8();
        surface.id.assign(idBytes.constData(), static_cast<std::size_t>(idBytes.size()));
        surface.name.assign(nameBytes.constData(), static_cast<std::size_t>(nameBytes.size()));
        auto readUnitDouble = [&](double* dst) {
            return reader.readDouble(*dst) && std::isfinite(*dst) && *dst >= 0.0 && *dst <= 1.0;
        };
        if (!readUnitDouble(&surface.absorbency) || !readUnitDouble(&surface.absorptionRate)
            || !readUnitDouble(&surface.porosity) || !readUnitDouble(&surface.waterRetention)
            || !readUnitDouble(&surface.tooth) || !readUnitDouble(&surface.roughness)
            || !readUnitDouble(&surface.pigmentRetention) || !readUnitDouble(&surface.binderRetention)
            || !readUnitDouble(&surface.flowResistance)
            || !reader.readDouble(surface.dryingAcceleration) || !std::isfinite(surface.dryingAcceleration)
            || surface.dryingAcceleration < 0.0 || surface.dryingAcceleration > 5.0
            || !readUnitDouble(&surface.edgeAccumulation) || !readUnitDouble(&surface.rewetting)
            || !readUnitDouble(&surface.surfaceWetness) || !readUnitDouble(&surface.granulationInteraction))
            return fail(QStringLiteral("\"%1\" has invalid substrate parameters.").arg(path));
        payload->hasSurface = true;
        payload->surface = std::move(surface);
    }
    if (reader.remaining() != 0)
        return fail(QStringLiteral("\"%1\" has trailing data.").arg(path));

    payload->width = width;
    payload->height = height;
    payload->dpi = dpi;
    payload->transparent = transparent;
    payload->backgroundRgba = backgroundRgba;
    payload->title = title;
    payload->activeLayer = activeLayer;
    payload->records = std::move(records);
    return true;
}

// Serializes the illustration document body exactly as a v1 file stores it
// (and as the digital portion of a v2-v5 illustration payload stores it). Returns false only on
// internal inconsistency (which the caller must treat as a save failure).
bool buildIllustrationPayload(const CanvasDocument& document, quint16 version, QByteArray* out, QString* error)
{
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QByteArray& buffer = *out;
    buffer.clear();
    const QSize size = document.size();
    if (!std::isfinite(document.dpi()) || document.dpi() <= 0.0 || document.dpi() > 1000000.0)
        return fail(QStringLiteral("The document DPI cannot be stored in a KomaPaint file."));
    const auto* layers = document.layers();
    if (layers->rowCount() < 1 || static_cast<quint64>(layers->rowCount()) > DocumentFile::MaxLayers ||
        layers->activeLayerIndex() < 0 || layers->activeLayerIndex() >= layers->rowCount())
        return fail(QStringLiteral("The document has an unsupported layer count or active layer. The file was not changed."));
    putU32(buffer, static_cast<quint32>(size.width()));
    putU32(buffer, static_cast<quint32>(size.height()));
    putDouble(buffer, document.dpi());
    putU8(buffer, document.hasTransparentBackground() ? 1 : 0);
    const QColor background = document.backgroundColor();
    putU32(buffer, (0xFF000000U | (static_cast<quint32>(background.red()) << 16)
                    | (static_cast<quint32>(background.green()) << 8) | static_cast<quint32>(background.blue())));
    if (!putUtf8(buffer, document.title(), DocumentFile::MaxTitleBytes))
        return fail(QStringLiteral("The document title is too long or contains invalid Unicode. The file was not changed."));
    putU32(buffer, static_cast<quint32>(layers->activeLayerIndex() < 0 ? 0 : layers->activeLayerIndex()));
    putU32(buffer, static_cast<quint32>(layers->rowCount()));

    for (int row = 0; row < layers->rowCount(); ++row) {
        const Layer* layer = layers->layerAt(row);
        const SparseTileStore& store = layer->tiles();
        const TileGeometry geometry = store.geometry();
        if (!store.isValid() || geometry.width() != size.width() || geometry.height() != size.height() ||
            geometry.tileSize() > 4096 || !std::isfinite(layer->opacity()) ||
            layer->opacity() < 0.0 || layer->opacity() > 1.0)
            return fail(QStringLiteral("A layer has invalid geometry or opacity. The file was not changed."));
        auto points = store.allocatedTilePoints();
        if (points.size() > DocumentFile::MaxTileRectsPerLayer)
            return fail(QStringLiteral("A layer has too many tile rectangles for the file format. The file was not changed."));
        std::sort(points.begin(), points.end(), [](const TilePoint& a, const TilePoint& b) {
            return a.y < b.y || (a.y == b.y && a.x < b.x);
        });
        if (!putUtf8(buffer, layer->name(), DocumentFile::MaxNameBytes))
            return fail(QStringLiteral("A layer name is too long or contains invalid Unicode. The file was not changed."));
        putU8(buffer, layer->isVisible() ? 1 : 0);
        putU8(buffer, layer->isLocked() ? 1 : 0);
        putDouble(buffer, layer->opacity());
        if (version >= DocumentFile::LayerMetadataVersion) {
            putU8(buffer, static_cast<quint8>(layer->blendMode()));
            putU8(buffer, static_cast<quint8>(layer->colorLabel()));
        }
        putU32(buffer, static_cast<quint32>(points.size()));
        const std::size_t tilePixels = static_cast<std::size_t>(geometry.tileSize()) * static_cast<std::size_t>(geometry.tileSize());
        for (const TilePoint point : points) {
            const auto bounds = geometry.tileBounds(point);
            const auto pixels = store.tilePixels(point);
            if (!bounds || pixels.size() != tilePixels)
                return fail(QStringLiteral("Internal error: inconsistent tile storage."));
            const std::uint64_t width = static_cast<std::uint64_t>(bounds->right - bounds->left);
            const std::uint64_t height = static_cast<std::uint64_t>(bounds->bottom - bounds->top);
            // Account for the envelope, asset count and CRC before allocating
            // rectangle pixels. A successful save must be readable by open().
            const quint64 rectangleBytes = 24 + width * height * 4;
            constexpr quint64 envelopeBytes = 32;
            if (static_cast<quint64>(buffer.size()) > DocumentFile::MaxFileBytes - envelopeBytes ||
                rectangleBytes > DocumentFile::MaxFileBytes - envelopeBytes - static_cast<quint64>(buffer.size()))
                return fail(QStringLiteral("The document is too large for the native file format. The file was not changed."));
            putU64(buffer, static_cast<quint64>(bounds->left));
            putU64(buffer, static_cast<quint64>(bounds->top));
            putU32(buffer, static_cast<quint32>(width));
            putU32(buffer, static_cast<quint32>(height));
            const std::size_t stride = static_cast<std::size_t>(geometry.tileSize());
            bool anyNonZero = false;
            for (std::uint64_t y = 0; y < height; ++y) {
                for (std::uint64_t x = 0; x < width; ++x) {
                    const std::uint32_t p = pixels[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x)];
                    if (!premultiplied(p))
                        return fail(QStringLiteral("A layer contains invalid pixels. The file was not changed."));
                    if (p != 0) anyNonZero = true;
                    putU32(buffer, p);
                }
            }
            // Allocated tiles are never all-zero (an all-zero tile is erased by
            // the store), so this would indicate internal corruption. Failing
            // the save is safer than writing a count that does not match the
            // number of rectangles that follow.
            if (!anyNonZero)
                return fail(QStringLiteral("Internal error: an empty tile was reported as present."));
        }
        if (version >= DocumentFile::MaterialLifecycleVersion) {
            const auto& materialStore = layer->materialTiles();
            const auto& compositions = materialStore.compositions();
            std::vector<TilePoint> materialPoints = materialStore.allocatedTilePoints();
            std::sort(materialPoints.begin(), materialPoints.end(), [](TilePoint a, TilePoint b) {
                return a.y < b.y || (a.y == b.y && a.x < b.x);
            });
            quint64 materialPixelCount = 0;
            std::vector<std::size_t> denseIds(compositions.size(), 0);
            std::vector<std::size_t> exportIds;
            exportIds.push_back(0);
            for (TilePoint tile : materialPoints) {
                const auto entries = materialStore.tileEntries(tile);
                if (entries.size() > std::numeric_limits<quint64>::max() - materialPixelCount)
                    return fail(QStringLiteral("The material content is too large to save."));
                materialPixelCount += static_cast<quint64>(entries.size());
                for (const auto& entry : entries) {
                    const std::size_t cid = entry.cell.compositionId;
                    if (cid == 0 || cid >= compositions.size() || compositions[cid].mediumId.empty())
                        return fail(QStringLiteral("A layer references an invalid material composition. The file was not changed."));
                    if (denseIds[cid] == 0) {
                        denseIds[cid] = exportIds.size();
                        exportIds.push_back(cid);
                    }
                }
            }
            putU64(buffer, static_cast<quint64>(exportIds.size()));
            for (std::size_t outId = 0; outId < exportIds.size(); ++outId) {
                const std::size_t srcId = exportIds[outId];
                const auto& composition = compositions[srcId];
                if ((outId == 0 && (!composition.mediumId.empty() || !composition.pigments.empty()))
                    || (outId != 0 && composition.mediumId.empty()))
                    return fail(QStringLiteral("A layer has an invalid material composition table. The file was not changed."));
                if (!putUtf8String(buffer, composition.mediumId))
                    return fail(QStringLiteral("A material medium identifier is not valid UTF-8. The file was not changed."));
                putU64(buffer, static_cast<quint64>(composition.pigments.size()));
                double proportionSum = 0.0;
                for (const auto& pigment : composition.pigments) {
                    if (!std::isfinite(pigment.mass) || pigment.mass <= 0.0
                        || (pigment.pigmentId.empty() && (pigment.residualArgb >> 24U) != 0xffU)
                        || (!pigment.pigmentId.empty() && pigment.residualArgb != 0))
                        return fail(QStringLiteral("A layer contains invalid pigment composition data. The file was not changed."));
                    proportionSum += pigment.mass;
                    if (!putUtf8String(buffer, pigment.pigmentId))
                        return fail(QStringLiteral("A pigment identifier is not valid UTF-8. The file was not changed."));
                    putU32(buffer, pigment.residualArgb);
                    putDouble(buffer, pigment.mass);
                }
                if (!composition.pigments.empty()
                    && (!std::isfinite(proportionSum) || std::abs(proportionSum - 1.0) > 1e-9))
                    return fail(QStringLiteral("A layer contains a non-normalized pigment composition. The file was not changed."));
            }

            const auto& materialGeometry = materialStore.geometry();
            if (materialGeometry.width() != geometry.width() || materialGeometry.height() != geometry.height())
                return fail(QStringLiteral("A material layer has invalid geometry. The file was not changed."));
            struct MaterialCursor {
                quint64 x = 0, y = 0;
                std::size_t tileIndex = 0, entryIndex = 0;
            };
            struct LaterMaterialPixel {
                bool operator()(const MaterialCursor& a, const MaterialCursor& b) const noexcept
                {
                    return a.y != b.y ? a.y > b.y : a.x > b.x;
                }
            };
            std::priority_queue<MaterialCursor, std::vector<MaterialCursor>, LaterMaterialPixel> ordered;
            const auto cursorFor = [&](std::size_t tileIndex, std::size_t entryIndex) {
                const TilePoint tile = materialPoints[tileIndex];
                const auto entries = materialStore.tileEntries(tile);
                const auto local = entries[entryIndex].localIndex;
                const auto tileSize = static_cast<std::size_t>(materialGeometry.tileSize());
                const auto x = tile.x * materialGeometry.tileSize() + static_cast<std::int64_t>(local % tileSize);
                const auto y = tile.y * materialGeometry.tileSize() + static_cast<std::int64_t>(local / tileSize);
                return MaterialCursor{static_cast<quint64>(x), static_cast<quint64>(y), tileIndex, entryIndex};
            };
            // Merge each tile's locally sorted cells into document y/x order.
            // This avoids a second allocation proportional to every material
            // pixel when saving large sparse artwork.
            for (std::size_t tileIndex = 0; tileIndex < materialPoints.size(); ++tileIndex) {
                if (!materialStore.tileEntries(materialPoints[tileIndex]).empty())
                    ordered.push(cursorFor(tileIndex, 0));
            }
            putU64(buffer, materialPixelCount);
            quint64 written = 0;
            quint64 previousX = 0, previousY = 0;
            bool first = true;
            while (!ordered.empty()) {
                const MaterialCursor item = ordered.top();
                ordered.pop();
                const TilePoint tile = materialPoints[item.tileIndex];
                const auto entries = materialStore.tileEntries(tile);
                const auto& cell = entries[item.entryIndex].cell;
                if (item.x >= static_cast<quint64>(materialGeometry.width())
                    || item.y >= static_cast<quint64>(materialGeometry.height())
                    || cell.compositionId == 0 || cell.compositionId >= compositions.size()
                    || denseIds[cell.compositionId] == 0
                    || !std::isfinite(cell.pigmentMass) || cell.pigmentMass < 0.0F
                    || !std::isfinite(cell.water) || cell.water < 0.0F
                    || !std::isfinite(cell.binder) || cell.binder < 0.0F
                    || !std::isfinite(cell.filler) || cell.filler < 0.0F
                    || !std::isfinite(cell.wetness) || cell.wetness < 0.0F || cell.wetness > 1.0F
                    || !std::isfinite(cell.consistency) || cell.consistency < 0.0F || cell.consistency > 1.0F
                    || !std::isfinite(cell.drying) || cell.drying < 0.0F || cell.drying > 1.0F
                    || !std::isfinite(cell.settling) || cell.settling < 0.0F || cell.settling > 1.0F
                    || (!first && (item.y < previousY || (item.y == previousY && item.x <= previousX))))
                    return fail(QStringLiteral("A layer contains invalid or unordered material pixels. The file was not changed."));
                constexpr quint64 pixelRecordBytes = 56;
                if (static_cast<quint64>(buffer.size()) > DocumentFile::MaxFileBytes - 32 - pixelRecordBytes)
                    return fail(QStringLiteral("The material content is too large for the native file format. The file was not changed."));
                putU64(buffer, item.x);
                putU64(buffer, item.y);
                putU64(buffer, static_cast<quint64>(denseIds[cell.compositionId]));
                putFloat(buffer, cell.pigmentMass);
                putFloat(buffer, cell.water);
                putFloat(buffer, cell.binder);
                putFloat(buffer, cell.filler);
                putFloat(buffer, cell.wetness);
                putFloat(buffer, cell.consistency);
                putFloat(buffer, cell.drying);
                putFloat(buffer, cell.settling);
                ++written;
                previousX = item.x;
                previousY = item.y;
                first = false;
                const std::size_t next = item.entryIndex + 1;
                if (next < entries.size()) ordered.push(cursorFor(item.tileIndex, next));
            }
            if (written != materialPixelCount)
                return fail(QStringLiteral("The material content changed while saving. The file was not changed."));
        }
    }
    if (version >= DocumentFile::FormatVersion) {
        const auto& surface = document.surfaceProfile();
        if (surface.id.empty() || !putUtf8String(buffer, surface.id) || !putUtf8String(buffer, surface.name))
            return fail(QStringLiteral("The document substrate settings could not be serialized. The file was not changed."));
        putDouble(buffer, surface.absorbency);
        putDouble(buffer, surface.absorptionRate);
        putDouble(buffer, surface.porosity);
        putDouble(buffer, surface.waterRetention);
        putDouble(buffer, surface.tooth);
        putDouble(buffer, surface.roughness);
        putDouble(buffer, surface.pigmentRetention);
        putDouble(buffer, surface.binderRetention);
        putDouble(buffer, surface.flowResistance);
        putDouble(buffer, surface.dryingAcceleration);
        putDouble(buffer, surface.edgeAccumulation);
        putDouble(buffer, surface.rewetting);
        putDouble(buffer, surface.surfaceWetness);
        putDouble(buffer, surface.granulationInteraction);
    }
    if (static_cast<quint64>(buffer.size()) > DocumentFile::MaxFileBytes - 32)
        return fail(QStringLiteral("The document is too large for the native file format. The file was not changed."));
    return true;
}

// Builds the detached document from a validated payload. Shared by the v1
// and v2-v5 readers. Returns nullptr only if the restored artwork exceeds the
// memory limit or cannot be restored.
CanvasDocument* buildDocument(const IllustrationPayload& payload, const QString& path, QString* error)
{
    const auto fail = [&](const QString& message) -> CanvasDocument* {
        if (error) *error = message;
        return nullptr;
    };
    auto document = std::make_unique<CanvasDocument>();
    document->setSize(QSize(static_cast<int>(payload.width), static_cast<int>(payload.height)));
    document->setDpi(payload.dpi);
    document->setTransparentBackground(payload.transparent != 0);
    // The opaque background colour remains editable while transparency is
    // enabled; restore it even when it is not currently shown on the canvas.
    document->setBackgroundColor(QColor(static_cast<int>((payload.backgroundRgba >> 16) & 0xff),
                                        static_cast<int>((payload.backgroundRgba >> 8) & 0xff),
                                        static_cast<int>(payload.backgroundRgba & 0xff)));
    const auto layerCount = static_cast<quint32>(payload.records.size());
    for (quint32 li = 0; li < layerCount; ++li) {
        if (li > 0) document->layers()->addLayer();
        Layer* layer = document->layers()->layerAt(static_cast<int>(li));
        const LayerRecord& record = payload.records[li];
        layer->setName(record.name);
        layer->setVisible(record.visible);
        layer->setLocked(record.locked);
        layer->setOpacity(record.opacity);
        layer->setBlendMode(record.blendMode);
        layer->setColorLabel(record.colorLabel);
        if (!record.rects.empty()) {
        // Re-tile the stored document-space rectangles onto this build's tile
        // geometry. The file format never stores tile size, so a file written
        // by any KomaPaint build loads identically everywhere.
        auto& store = layer->tiles();
        const TileGeometry geometry = store.geometry();
        const int tileSize = geometry.tileSize();
        const std::size_t tilePixelCount = static_cast<std::size_t>(tileSize) * static_cast<std::size_t>(tileSize);
        // Count distinct target tiles first: a small file with many isolated
        // pixels may require far more memory after re-tiling. Reject it before
        // allocating the full buffers when it cannot fit the existing store.
        std::set<TilePoint, TilePointLess> neededTiles;
        const std::size_t maxTiles = store.pixelBudget() / store.tileBytes();
        for (const RectRecord& rect : record.rects) {
            const TileRect documentRect{static_cast<std::int64_t>(rect.left), static_cast<std::int64_t>(rect.top),
                                        static_cast<std::int64_t>(rect.left + rect.width),
                                        static_cast<std::int64_t>(rect.top + rect.height)};
            const auto range = geometry.tilesForRect(documentRect);
            if (!range) return fail(QStringLiteral("\"%1\" has an out-of-bounds tile rectangle.").arg(path));
            for (std::int64_t ty = range->first.y; ty <= range->last.y; ++ty)
                for (std::int64_t tx = range->first.x; tx <= range->last.x; ++tx) {
                    neededTiles.insert(TilePoint{tx, ty});
                    if (neededTiles.size() > maxTiles)
                        return fail(QStringLiteral("The artwork in \"%1\" exceeds the layer memory limit.").arg(path));
                }
        }
        std::map<TilePoint, std::vector<std::uint32_t>, TilePointLess> staged;
        // Re-tiling can amplify a tiny hostile file into many full tiles.
        // Charge staged buffers to the document budget as they are allocated;
        // keep the charge until restoreTiles finishes its own atomic staging.
        struct StagingReservation {
            std::shared_ptr<DocumentMemoryBudget> budget;
            std::size_t bytes = 0;
            ~StagingReservation() { if (budget) budget->release(DocumentMemoryBudget::Category::RestoreStaging, bytes); }
        } reservation{store.documentBudget()};
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
                    if (buffer.empty()) {
                        const auto tileBytes = store.tileBytes();
                        if (tileBytes > store.pixelBudget() ||
                            reservation.bytes > store.pixelBudget() - tileBytes ||
                            (reservation.budget && !reservation.budget->reserve(DocumentMemoryBudget::Category::RestoreStaging, tileBytes)))
                            return fail(QStringLiteral("The artwork in \"%1\" exceeds available memory or the layer memory limit.").arg(path));
                        reservation.bytes += tileBytes;
                        buffer.assign(tilePixelCount, 0);
                    }
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

        if (!record.materialCompositions.empty()) {
            auto& materialStore = layer->materialTiles();
            if (!materialStore.importCompositions(record.materialCompositions))
                return fail(QStringLiteral("The material composition data in \"%1\" exceeds available memory or is invalid.").arg(path));
            const auto& geometry = materialStore.geometry();
            std::map<TilePoint, std::vector<material::MaterialEntry>, TilePointLess> stagedMaterial;
            for (const auto& pixel : record.materialPixels) {
                const auto address = geometry.address({static_cast<std::int64_t>(pixel.x),
                                                       static_cast<std::int64_t>(pixel.y)});
                if (!address) return fail(QStringLiteral("The material content in \"%1\" is out of bounds.").arg(path));
                const auto localIndex = static_cast<std::size_t>(address->local.y)
                    * static_cast<std::size_t>(geometry.tileSize())
                    + static_cast<std::size_t>(address->local.x);
                stagedMaterial[address->tile].push_back({localIndex, pixel.cell});
            }
            std::vector<material::MaterialTileSnapshot> materialSnapshots;
            materialSnapshots.reserve(stagedMaterial.size());
            for (auto& [tile, entries] : stagedMaterial)
                materialSnapshots.push_back({tile, std::move(entries)});
            if (!materialSnapshots.empty()
                && materialStore.restoreTiles(materialSnapshots) != material::MaterialTileStore::Result::Changed)
                return fail(QStringLiteral("The material content in \"%1\" exceeds available memory or cannot be restored.").arg(path));
        }
    }
    document->layers()->setActiveLayerIndex(static_cast<int>(payload.activeLayer));
    document->setTitle(payload.title);
    if (payload.hasSurface)
        document->setSurfaceProfile(payload.surface);
    document->setModified(false);
    return document.release();
}

} // namespace

quint32 DocumentFile::crc32(const QByteArray& bytes)
{
    Crc32 crc;
    crc.add(bytes);
    return crc.value();
}

bool DocumentFile::save(const CanvasDocument& document, const QString& path, QString* error) try
{
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (path.isEmpty()) return fail(QStringLiteral("No filename was given."));
    const QSize size = document.size();
    if (!CanvasDocument::validSize(size)) return fail(QStringLiteral("The document has invalid dimensions."));

    // Serialize the illustration document first. If that fails (e.g. an
    // internal inconsistency), the destination file is never touched.
    quint16 writeVersion = FlatEnvelopeVersion;
    for (int row = 0; row < document.layers()->rowCount(); ++row) {
        const auto* layer = document.layers()->layerAt(row);
        if (layer->blendMode() != 0 || layer->colorLabel() != 0)
            writeVersion = std::max(writeVersion, LayerMetadataVersion);
        if (layer->materialTiles().allocatedTiles() != 0)
            writeVersion = FormatVersion;
    }
    if (document.hasCustomSurfaceSettings())
        writeVersion = FormatVersion;
    QByteArray payload;
    payload.reserve(1024 * 1024);
    if (!buildIllustrationPayload(document, writeVersion, &payload, error)) return false;

    QSaveFile file(path);
    file.setDirectWriteFallback(false); // Never overwrite the original unless the temp file is complete.
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Could not write to \"%1\". The file was not changed.").arg(path));

    Crc32 crc;
    const auto writeChunk = [&file, &crc](const QByteArray& chunk) {
        crc.add(chunk);
        return file.write(chunk) == chunk.size();
    };
    const auto failAndCancel = [&](const QString& message) {
        file.cancelWriting();
        if (error) *error = message;
        return false;
    };

    // Version 2-5 envelope: one document, empty asset section and CRC.
    // Version 3 adds layer metadata; v4 adds material composition; v5 stores drying and settling state.
    QByteArray header;
    header.reserve(64);
    header.append(Magic, MagicSize);
    putU16(header, writeVersion);
    putU32(header, 1);                 // documentCount
    putU8(header, DocumentKindIllustration);
    putU64(header, static_cast<quint64>(payload.size()));
    if (!writeChunk(header)) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    if (!writeChunk(payload)) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    QByteArray tail;
    putU32(tail, 0);                   // assetCount (shared assets reserved, unused in this version)
    if (!writeChunk(tail)) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    QByteArray trailer;
    putU32(trailer, crc.value());
    // The trailer bytes are written directly, not through writeChunk, so the
    // checksum never covers itself.
    if (file.write(trailer) != trailer.size()) return failAndCancel(QStringLiteral("Could not write \"%1\" (disk full?).").arg(path));
    if (!file.commit()) return failAndCancel(QStringLiteral("Could not replace \"%1\". The previous file was left unchanged.").arg(path));
    return true;
} catch (const std::bad_alloc&) {
    if (error) *error = QStringLiteral("Not enough memory to save the document. The file was not changed.");
    return false;
} catch (const std::length_error&) {
    if (error) *error = QStringLiteral("The document is too large to save. The file was not changed.");
    return false;
}

CanvasDocument* DocumentFile::open(const QString& path, QString* error) try
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
        return fail(QStringLiteral("\"%1\" is not a KomaPaint file.").arg(path));
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

    const qsizetype contentEnd = data.size() - 4;

    // ---- fully validate the envelope before constructing anything ----
    Reader head(data, contentEnd);
    const char* magic = nullptr;
    if (!head.readBytes(MagicSize, magic) || std::memcmp(magic, Magic, MagicSize) != 0)
        return fail(QStringLiteral("\"%1\" is not a KomaPaint file.").arg(path));
    quint16 version = 0;
    if (!head.readU16(version)) return fail(QStringLiteral("\"%1\" is truncated.").arg(path));

    IllustrationPayload payload;
    if (version == LegacyFormatVersion) {
        // Version 1: flat illustration layout (magic + version + payload).
        const qsizetype payloadBegin = MagicSize + 2;
        if (!parseIllustrationPayload(data, payloadBegin, contentEnd, version, path, &payload, error))
            return nullptr;
    } else if (version == FlatEnvelopeVersion || version == LayerMetadataVersion
               || version == MaterialVersion || version == MaterialLifecycleVersion
               || version == FormatVersion) {
        // Versions 2-5: unified project envelope.
        quint32 documentCount = 0;
        if (!head.readU32(documentCount)) return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
        if (documentCount == 0 || documentCount > MaxDocuments)
            return fail(QStringLiteral("\"%1\" declares an invalid document count.").arg(path));
        if (documentCount != 1)
            return fail(QStringLiteral("\"%1\" is a multi-document project, which this version of KomaPaint does not support yet.").arg(path));

        quint8 kind = 0;
        quint64 payloadLength = 0;
        if (!head.readU8(kind) || !head.readU64(payloadLength))
            return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
        if (kind != DocumentKindIllustration)
            return fail(QStringLiteral("\"%1\" contains an unsupported document type (kind %2).").arg(path).arg(kind));
        // Read exactly payloadLength bytes; never skip, never guess.
        if (payloadLength > static_cast<quint64>(head.remaining()))
            return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
        const qsizetype payloadBegin = head.position();
        const qsizetype payloadEnd = payloadBegin + static_cast<qsizetype>(payloadLength);
        if (!parseIllustrationPayload(data, payloadBegin, payloadEnd, version, path, &payload, error))
            return nullptr;
        Reader tail(data, payloadEnd, contentEnd);
        quint32 assetCount = 0;
        if (!tail.readU32(assetCount)) return fail(QStringLiteral("\"%1\" is truncated.").arg(path));
        if (assetCount != 0)
            return fail(QStringLiteral("\"%1\" uses shared assets, which this version of KomaPaint does not support yet.").arg(path));
        if (tail.remaining() != 0)
            return fail(QStringLiteral("\"%1\" has trailing data.").arg(path));
    } else {
        return fail(QStringLiteral("\"%1\" uses format version %2, which this version of KomaPaint cannot read.").arg(path).arg(version));
    }

    return buildDocument(payload, path, error);
} catch (const std::bad_alloc&) {
    if (error) *error = QStringLiteral("Not enough memory to open the document. No artwork was changed.");
    return nullptr;
} catch (const std::length_error&) {
    if (error) *error = QStringLiteral("The document is too large to open. No artwork was changed.");
    return nullptr;
}

} // namespace komapaint
