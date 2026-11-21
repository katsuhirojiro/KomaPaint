#include "brush/BrushFormat.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace komapaint::brush {
namespace {

constexpr std::size_t kHeaderSize = 8 + 2 + 2 + 4;

// ---- writing ---------------------------------------------------------------

class Writer {
public:
    void u8(std::uint8_t value) { m_bytes.push_back(value); }
    void u16(std::uint16_t value) {
        m_bytes.push_back(std::uint8_t(value & 0xFF));
        m_bytes.push_back(std::uint8_t((value >> 8) & 0xFF));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            m_bytes.push_back(std::uint8_t((value >> shift) & 0xFF));
        }
    }
    void u32Size(std::size_t count) {
        if (count > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Value does not fit the .kb v1 representation");
        u32(static_cast<std::uint32_t>(count));
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            m_bytes.push_back(std::uint8_t((value >> shift) & 0xFF));
        }
    }
    void f64(double value) {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void rawBytes(std::string_view value) {
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }
    void text(std::string_view value) {
        u32Size(value.size());
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }
    void blob(const std::vector<std::uint8_t>& value) {
        u64(static_cast<std::uint64_t>(value.size()));
        m_bytes.insert(m_bytes.end(), value.begin(), value.end());
    }
    void bag(const ParamBag& bag) {
        u32Size(bag.entries().size());
        for (const auto& [key, value] : bag.entries()) {
            text(key);
            f64(value);
        }
    }
    void curve(const ResponseCurve& curve) {
        u32Size(curve.points().size());
        for (const auto& point : curve.points()) {
            f64(point.x);
            f64(point.y);
        }
    }
    const std::vector<std::uint8_t>& bytes() const { return m_bytes; }
    std::vector<std::uint8_t> take() { return std::move(m_bytes); }
private:
    std::vector<std::uint8_t> m_bytes;
};

// ---- reading ---------------------------------------------------------------

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}
    bool ok() const { return m_ok; }
    std::size_t remaining() const { return m_ok ? m_size - m_offset : 0; }
    std::size_t offset() const { return m_offset; }

    bool need(std::size_t count) {
        if (!m_ok || count > m_size - m_offset) {
            m_ok = false;
            return false;
        }
        return true;
    }
    std::uint8_t u8() {
        if (!need(1)) return 0;
        return m_data[m_offset++];
    }
    std::uint16_t u16() {
        if (!need(2)) return 0;
        const std::uint16_t value = std::uint16_t(m_data[m_offset]) |
                                    std::uint16_t(std::uint16_t(m_data[m_offset + 1]) << 8);
        m_offset += 2;
        return value;
    }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= std::uint32_t(m_data[m_offset + std::size_t(i)]) << (8 * i);
        }
        m_offset += 4;
        return value;
    }
    std::uint64_t u64() {
        if (!need(8)) return 0;
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= std::uint64_t(m_data[m_offset + std::size_t(i)]) << (8 * i);
        }
        m_offset += 8;
        return value;
    }
    double f64() {
        const std::uint64_t bits = u64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    bool boolean() { return u8() != 0; }
    bool rawBytes(std::size_t count, std::string* out) {
        if (!need(count)) return false;
        out->assign(reinterpret_cast<const char*>(m_data + m_offset), count);
        m_offset += count;
        return true;
    }
    bool text(std::string* out) {
        const std::uint32_t length = u32();
        if (!need(length)) return false;
        out->assign(reinterpret_cast<const char*>(m_data + m_offset), length);
        m_offset += length;
        return true;
    }
    bool blob(std::vector<std::uint8_t>* out) {
        const std::uint64_t length = u64();
        if (length > remaining()) {
            m_ok = false;
            return false;
        }
        out->assign(m_data + m_offset, m_data + m_offset + std::size_t(length));
        m_offset += std::size_t(length);
        return true;
    }
    bool bag(ParamBag* out) {
        const std::uint32_t count = u32();
        if (count > remaining()) {
            m_ok = false;
            return false;
        }
        out->clear();
        for (std::uint32_t i = 0; i < count; ++i) {
            std::string key;
            if (!text(&key)) return false;
            out->set(std::move(key), f64());
            if (!m_ok) return false;
        }
        return true;
    }
    bool curve(ResponseCurve* out) {
        const std::uint32_t count = u32();
        if (count > remaining()) {
            m_ok = false;
            return false;
        }
        std::vector<CurvePoint> points;
        points.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CurvePoint point;
            point.x = f64();
            point.y = f64();
            points.push_back(point);
            if (!m_ok) return false;
        }
        if (!out->setPoints(std::move(points))) {
            m_ok = false;
            return false;
        }
        return true;
    }
    bool skip(std::size_t count) {
        if (!need(count)) return false;
        m_offset += count;
        return true;
    }
private:
    const std::uint8_t* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_offset = 0;
    bool m_ok = true;
};

// Enum helpers that turn an unreadable value into a hard failure instead of a
// silent default: a brush whose kinds were guessed is a corrupted brush.
template <typename T, typename ParseFn>
bool readEnum(Reader& reader, ParseFn parse, T* out) {
    std::string text;
    if (!reader.text(&text)) return false;
    if (!parse(text, out)) return false;
    return true;
}

bool writeChunk(Writer* out, const char tag[5], const Writer& payload) {
    out->rawBytes(std::string_view(tag, 4));
    out->u32Size(payload.bytes().size());
    for (std::uint8_t byte : payload.bytes()) out->u8(byte);
    return true;
}

std::string reason(std::string code, std::string detail) {
    return code + ": " + std::move(detail);
}

// ---- per-chunk encoders ----------------------------------------------------

void encodeMeta(const BrushDocument& doc, Writer* w) {
    w->text(doc.id);
    w->text(doc.name);
    w->text(doc.description);
    w->text(toString(doc.engine));
    w->text(doc.category);
    w->u32Size(doc.tags.size());
    for (const auto& tag : doc.tags) w->text(tag);
    w->text(doc.thumbnail);
    w->u32(doc.seed);
    w->bag(doc.parameters);
}

void encodeResources(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.resources.size());
    for (const auto& r : doc.resources) {
        w->text(r.id);
        w->text(r.name);
        w->text(toString(r.kind));
        w->u32(static_cast<std::uint32_t>(r.width));
        w->u32(static_cast<std::uint32_t>(r.height));
        w->blob(r.pixels());
    }
}

void encodeGroups(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.groups.size());
    for (const auto& g : doc.groups) {
        w->text(g.id);
        w->text(g.name);
        w->text(g.parentGroupId);
        w->text(toString(g.mode));
        w->u32Size(g.members.size());
        for (const auto& member : g.members) w->text(member);
        w->u32(static_cast<std::uint32_t>(g.membersPerStamp));
        w->u32(g.seed);
    }
}

void encodeComponents(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.components.size());
    for (const auto& c : doc.components) {
        w->text(c.id);
        w->text(c.name);
        w->text(toString(c.kind));
        w->text(c.resourceId);
        w->text(c.groupId);
        w->f64(c.transform.offsetX);
        w->f64(c.transform.offsetY);
        w->f64(c.transform.rotationDegrees);
        w->f64(c.transform.scale);
        w->f64(c.transform.aspect);
        w->boolean(c.transform.flipX);
        w->boolean(c.transform.flipY);
        w->f64(c.opacity);
        w->boolean(c.visible);
        w->text(toString(c.blend));
        w->text(c.maskComponentId);
        w->f64(c.weight);
        w->boolean(c.erases);
        w->bag(c.parameters);
    }
}

void encodeGenerators(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.generators.size());
    for (const auto& g : doc.generators) {
        w->text(g.id);
        w->text(toString(g.kind));
        w->text(g.groupId);
        w->boolean(g.enabled);
        w->bag(g.parameters);
    }
}

void encodeDynamics(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.dynamics.size());
    for (const auto& b : doc.dynamics) {
        w->text(b.parameter);
        w->text(toString(b.signal));
        w->curve(b.curve);
        w->f64(b.amount);
        w->boolean(b.perStamp);
    }
}

void encodeRandomization(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.randomization.size());
    for (const auto& r : doc.randomization) {
        w->text(r.parameter);
        w->boolean(r.enabled);
        w->f64(r.minimum);
        w->f64(r.maximum);
        w->text(toString(r.distribution));
        w->boolean(r.perStamp);
        w->u32(r.seed);
        w->boolean(r.sharedSeed);
    }
}

void encodeExposed(const BrushDocument& doc, Writer* w) {
    w->u32Size(doc.exposed.size());
    for (const auto& p : doc.exposed) {
        w->text(p.parameter);
        w->text(p.label);
        w->text(p.section);
        w->u32(static_cast<std::uint32_t>(p.order));
        w->boolean(p.visible);
        w->text(toString(p.control));
        w->boolean(p.hasRangeOverride);
        w->f64(p.minimumOverride);
        w->f64(p.maximumOverride);
    }
}

} // namespace

// ---- CRC -------------------------------------------------------------------

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            entries[i] = value;
        }
        return entries;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---- encode ----------------------------------------------------------------

std::vector<std::uint8_t> encodeBrush(const BrushDocument& document) {
    Writer payload;
    {
        Writer meta;
        encodeMeta(document, &meta);
        writeChunk(&payload, "META", meta);
    }
    {
        Writer chunk;
        encodeResources(document, &chunk);
        writeChunk(&payload, "RSRC", chunk);
    }
    {
        Writer chunk;
        encodeGroups(document, &chunk);
        writeChunk(&payload, "GRUP", chunk);
    }
    {
        Writer chunk;
        encodeComponents(document, &chunk);
        writeChunk(&payload, "COMP", chunk);
    }
    {
        Writer chunk;
        encodeGenerators(document, &chunk);
        writeChunk(&payload, "GENR", chunk);
    }
    {
        Writer chunk;
        encodeDynamics(document, &chunk);
        writeChunk(&payload, "DYNA", chunk);
    }
    {
        Writer chunk;
        encodeRandomization(document, &chunk);
        writeChunk(&payload, "RAND", chunk);
    }
    {
        Writer chunk;
        encodeExposed(document, &chunk);
        writeChunk(&payload, "EXPO", chunk);
    }

    for (const auto& chunk : document.opaqueChunks) {
        if (chunk.tag.size() != 4) throw std::invalid_argument("Invalid .kb extension tag");
        payload.rawBytes(chunk.tag);
        payload.u32Size(chunk.payload.size());
        payload.rawBytes(chunk.payload);
    }
    const auto& bytes = payload.bytes();
    Writer file;
    for (char c : kBrushFormatMagic) file.u8(static_cast<std::uint8_t>(c));
    file.u16(kBrushFormatVersion);
    file.u16(document.formatFlags);
    file.u32(crc32(bytes.data(), bytes.size()));
    for (std::uint8_t byte : bytes) file.u8(byte);
    return file.take();
}

// ---- decode ----------------------------------------------------------------

bool decodeBrush(const std::uint8_t* data, std::size_t size, BrushDocument* out,
                 DecodeResult* result) {
    DecodeResult local;
    DecodeResult* report = result ? result : &local;
    *report = DecodeResult{};

    if (!data || size < kHeaderSize) {
        report->error = reason("truncated", "file is smaller than the .kb header");
        return false;
    }
    if (std::memcmp(data, kBrushFormatMagic, sizeof(kBrushFormatMagic)) != 0) {
        report->error = reason("not-a-brush", "missing the KOMABRSH signature");
        return false;
    }

    Reader reader(data, size);
    reader.skip(sizeof(kBrushFormatMagic));
    const std::uint16_t version = reader.u16();
    report->version = version;
    const std::uint16_t flags = reader.u16();
    const std::uint32_t expectedCrc = reader.u32();
    if (!reader.ok()) {
        report->error = reason("truncated", "file ends inside the header");
        return false;
    }
    if (version == 0 || version > kBrushFormatVersion) {
        report->error = reason("unsupported-version",
                               "file is version " + std::to_string(version) +
                                   ", this build reads up to " +
                                   std::to_string(kBrushFormatVersion));
        return false;
    }

    const std::size_t payloadOffset = kHeaderSize;
    const std::size_t payloadSize = size - payloadOffset;
    const std::uint32_t actualCrc = crc32(data + payloadOffset, payloadSize);
    if (actualCrc != expectedCrc) {
        report->error = reason("checksum", "payload does not match its CRC-32");
        return false;
    }

    BrushDocument doc;
    doc.formatFlags = flags;
    bool seenMeta = false;
    bool seenResources = false;
    bool seenGroups = false;
    bool seenComponents = false;
    bool seenGenerators = false;
    bool seenDynamics = false;
    bool seenRandomization = false;
    bool seenExposed = false;

    while (reader.remaining() > 0) {
        std::string tag;
        if (!reader.rawBytes(4, &tag)) {
            report->error = reason("truncated", "file ends inside a chunk header");
            return false;
        }
        const std::uint32_t length = reader.u32();
        if (!reader.ok() || length > reader.remaining()) {
            report->error = reason("truncated-chunk", "chunk '" + tag + "' runs past the payload");
            return false;
        }
        Reader r(data + reader.offset(), length);

        if (tag == "META") {
            if (seenMeta) { report->error = reason("duplicate-chunk", "two META chunks"); return false; }
            seenMeta = true;
            if (!r.text(&doc.id) || !r.text(&doc.name) || !r.text(&doc.description)) {
                report->error = reason("bad-meta", "META chunk is incomplete");
                return false;
            }
            if (!readEnum(r, parseEngine, &doc.engine)) {
                report->error = reason("bad-meta", "META declares an unknown engine");
                return false;
            }
            std::uint32_t tagCount = 0;
            if (!r.text(&doc.category) || !(tagCount = r.u32(), r.ok())) {
                report->error = reason("bad-meta", "META chunk is incomplete");
                return false;
            }
            if (tagCount > r.remaining()) {
                report->error = reason("bad-meta", "META declares more tags than it holds");
                return false;
            }
            doc.tags.clear();
            for (std::uint32_t i = 0; i < tagCount; ++i) {
                std::string entry;
                if (!r.text(&entry)) {
                    report->error = reason("bad-meta", "META tag is truncated");
                    return false;
                }
                doc.tags.push_back(std::move(entry));
            }
            if (!r.text(&doc.thumbnail) || !(doc.seed = r.u32(), r.ok()) || !r.bag(&doc.parameters)) {
                report->error = reason("bad-meta", "META chunk is incomplete");
                return false;
            }
        } else if (tag == "RSRC") {
            if (seenResources) { report->error = reason("duplicate-chunk", "two RSRC chunks"); return false; }
            seenResources = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-resource", "RSRC declares more resources than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                BrushResource resource;
                if (!r.text(&resource.id) || !r.text(&resource.name)) {
                    report->error = reason("bad-resource", "RSRC entry is incomplete");
                    return false;
                }
                if (!readEnum(r, parseResourceKind, &resource.kind)) {
                    report->error = reason("bad-resource", "RSRC declares an unknown pixel format");
                    return false;
                }
                resource.width = static_cast<int>(r.u32());
                resource.height = static_cast<int>(r.u32());
                std::vector<std::uint8_t> pixels;
                if (!r.blob(&pixels)) {
                    report->error = reason("bad-resource", "RSRC pixel block is truncated");
                    return false;
                }
                resource.setPixels(std::move(pixels));
                doc.resources.push_back(std::move(resource));
                if (!r.ok()) { report->error = reason("bad-resource", "RSRC entry is truncated"); return false; }
            }
        } else if (tag == "GRUP") {
            if (seenGroups) { report->error = reason("duplicate-chunk", "two GRUP chunks"); return false; }
            seenGroups = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-group", "GRUP declares more groups than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                ComponentGroup group;
                if (!r.text(&group.id) || !r.text(&group.name) || !r.text(&group.parentGroupId)) {
                    report->error = reason("bad-group", "GRUP entry is incomplete");
                    return false;
                }
                if (!readEnum(r, parseSelectionMode, &group.mode)) {
                    report->error = reason("bad-group", "GRUP declares an unknown selection mode");
                    return false;
                }
                const std::uint32_t memberCount = r.u32();
                if (memberCount > r.remaining()) {
                    report->error = reason("bad-group", "GRUP member count exceeds the chunk");
                    return false;
                }
                for (std::uint32_t m = 0; m < memberCount; ++m) {
                    std::string member;
                    if (!r.text(&member)) { report->error = reason("bad-group", "GRUP member is truncated"); return false; }
                    group.members.push_back(std::move(member));
                }
                group.membersPerStamp = static_cast<int>(r.u32());
                group.seed = r.u32();
                if (!r.ok()) { report->error = reason("bad-group", "GRUP entry is truncated"); return false; }
                doc.groups.push_back(std::move(group));
            }
        } else if (tag == "COMP") {
            if (seenComponents) { report->error = reason("duplicate-chunk", "two COMP chunks"); return false; }
            seenComponents = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-component", "COMP declares more components than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                BrushComponent c;
                if (!r.text(&c.id) || !r.text(&c.name)) {
                    report->error = reason("bad-component", "COMP entry is incomplete");
                    return false;
                }
                if (!readEnum(r, parseComponentKind, &c.kind)) {
                    report->error = reason("bad-component", "COMP declares an unknown kind");
                    return false;
                }
                if (!r.text(&c.resourceId) || !r.text(&c.groupId)) {
                    report->error = reason("bad-component", "COMP references are truncated");
                    return false;
                }
                c.transform.offsetX = r.f64();
                c.transform.offsetY = r.f64();
                c.transform.rotationDegrees = r.f64();
                c.transform.scale = r.f64();
                c.transform.aspect = r.f64();
                c.transform.flipX = r.boolean();
                c.transform.flipY = r.boolean();
                c.opacity = r.f64();
                c.visible = r.boolean();
                if (!readEnum(r, parseComponentBlend, &c.blend)) {
                    report->error = reason("bad-component", "COMP declares an unknown blend mode");
                    return false;
                }
                if (!r.text(&c.maskComponentId)) {
                    report->error = reason("bad-component", "COMP mask reference is truncated");
                    return false;
                }
                c.weight = r.f64();
                c.erases = r.boolean();
                if (!r.bag(&c.parameters) || !r.ok()) {
                    report->error = reason("bad-component", "COMP parameters are truncated");
                    return false;
                }
                doc.components.push_back(std::move(c));
            }
        } else if (tag == "GENR") {
            if (seenGenerators) { report->error = reason("duplicate-chunk", "two GENR chunks"); return false; }
            seenGenerators = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-generator", "GENR declares more generators than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                BrushGenerator g;
                if (!r.text(&g.id)) { report->error = reason("bad-generator", "GENR entry is incomplete"); return false; }
                if (!readEnum(r, parseGeneratorKind, &g.kind)) {
                    report->error = reason("bad-generator", "GENR declares an unknown generator");
                    return false;
                }
                if (!r.text(&g.groupId)) { report->error = reason("bad-generator", "GENR group is truncated"); return false; }
                g.enabled = r.boolean();
                if (!r.bag(&g.parameters) || !r.ok()) {
                    report->error = reason("bad-generator", "GENR parameters are truncated");
                    return false;
                }
                doc.generators.push_back(std::move(g));
            }
        } else if (tag == "DYNA") {
            if (seenDynamics) { report->error = reason("duplicate-chunk", "two DYNA chunks"); return false; }
            seenDynamics = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-dynamics", "DYNA declares more bindings than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                DynamicsBinding b;
                if (!r.text(&b.parameter)) { report->error = reason("bad-dynamics", "DYNA target is truncated"); return false; }
                if (!readEnum(r, parseSignal, &b.signal)) {
                    report->error = reason("bad-dynamics", "DYNA declares an unknown signal");
                    return false;
                }
                if (!r.curve(&b.curve)) { report->error = reason("bad-dynamics", "DYNA curve is invalid"); return false; }
                b.amount = r.f64();
                b.perStamp = r.boolean();
                if (!r.ok()) { report->error = reason("bad-dynamics", "DYNA entry is truncated"); return false; }
                doc.dynamics.push_back(std::move(b));
            }
        } else if (tag == "RAND") {
            if (seenRandomization) { report->error = reason("duplicate-chunk", "two RAND chunks"); return false; }
            seenRandomization = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-randomization", "RAND declares more rules than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                RandomizationRule rule;
                if (!r.text(&rule.parameter)) { report->error = reason("bad-randomization", "RAND target is truncated"); return false; }
                rule.enabled = r.boolean();
                rule.minimum = r.f64();
                rule.maximum = r.f64();
                if (!readEnum(r, parseDistribution, &rule.distribution)) {
                    report->error = reason("bad-randomization", "RAND declares an unknown distribution");
                    return false;
                }
                rule.perStamp = r.boolean();
                rule.seed = r.u32();
                rule.sharedSeed = r.boolean();
                if (!r.ok()) { report->error = reason("bad-randomization", "RAND entry is truncated"); return false; }
                doc.randomization.push_back(std::move(rule));
            }
        } else if (tag == "EXPO") {
            if (seenExposed) { report->error = reason("duplicate-chunk", "two EXPO chunks"); return false; }
            seenExposed = true;
            const std::uint32_t count = r.u32();
            if (count > r.remaining()) {
                report->error = reason("bad-exposed", "EXPO declares more settings than it holds");
                return false;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                ExposedProperty p;
                if (!r.text(&p.parameter) || !r.text(&p.label) || !r.text(&p.section)) {
                    report->error = reason("bad-exposed", "EXPO entry is incomplete");
                    return false;
                }
                p.order = static_cast<int>(r.u32());
                p.visible = r.boolean();
                if (!readEnum(r, parseControlType, &p.control)) {
                    report->error = reason("bad-exposed", "EXPO declares an unknown control type");
                    return false;
                }
                p.hasRangeOverride = r.boolean();
                p.minimumOverride = r.f64();
                p.maximumOverride = r.f64();
                if (!r.ok()) { report->error = reason("bad-exposed", "EXPO entry is truncated"); return false; }
                doc.exposed.push_back(std::move(p));
            }
        } else {
            report->ignoredChunks.push_back(tag);
            BrushOpaqueChunk chunk{std::move(tag), {}};
            if (!r.rawBytes(length, &chunk.payload)) return false;
            doc.opaqueChunks.push_back(std::move(chunk));
        }
        if (!r.ok() || r.remaining() != 0) {
            report->error = reason("unsupported-chunk-data", "Unexpected trailing data in '" + tag + "'");
            return false;
        }
        reader.skip(length);
        if (!reader.ok()) {
            report->error = reason("truncated-chunk", "chunk '" + tag + "' overruns the file");
            return false;
        }
    }

    if (!seenMeta) {
        report->error = reason("missing-chunk", "file has no META chunk");
        return false;
    }

    // The file parsed; now prove the brush it describes is coherent before it
    // is handed to the editor.
    const ValidationResult validation = doc.validate();
    if (!validation.ok()) {
        report->error = reason("invalid-brush", validation.summary());
        return false;
    }

    report->ok = true;
    if (out) *out = std::move(doc);
    return true;
}

} // namespace komapaint::brush
