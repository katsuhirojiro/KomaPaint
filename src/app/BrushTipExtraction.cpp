#include "BrushTipExtraction.h"

#include "../canvas/CpuTileCompositor.h"
#include "../material/MaterialEngine.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace komapaint::brushtip {
namespace {

const material::MaterialRenderer& materialRenderer()
{
    static const material::PigmentLibrary pigments;
    static const material::MaterialEngine engine(pigments);
    static const material::MaterialRenderer renderer(engine);
    return renderer;
}

bool composeTile(const CanvasDocument& document, const std::vector<CompositeLayer>& layers,
                 const TileGeometry& geometry, TilePoint tile,
                 std::vector<std::uint32_t>* out, std::int64_t* outWidth, std::int64_t* outHeight)
{
    std::vector<std::vector<std::uint32_t>> scratch(layers.size());
    std::vector<std::span<const std::uint32_t>> overrides(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        if (!layer.visible || layer.opacity <= 0.0 || !layer.materialSource) continue;
        Layer* source = layer.materialSource;
        if (source->materialTiles().tileEntries(tile).empty()) continue;
        if (!materialRenderer().renderTile(source->materialTiles(), tile,
                source->tiles().tilePixels(tile), material::gouacheProfile(),
                document.surfaceProfile(), &scratch[i])) return false;
        overrides[i] = scratch[i];
    }
    return CpuTileCompositor::composeInto(geometry, tile, layers, 0U, overrides,
                                          *out, *outWidth, *outHeight);
}

struct PixelBounds {
    std::int64_t left = std::numeric_limits<std::int64_t>::max();
    std::int64_t top = std::numeric_limits<std::int64_t>::max();
    std::int64_t right = std::numeric_limits<std::int64_t>::min();
    std::int64_t bottom = std::numeric_limits<std::int64_t>::min();
    bool empty() const { return right <= left || bottom <= top; }
};

PixelBounds contentBounds(const CanvasDocument& document, const std::vector<CompositeLayer>& layers)
{
    PixelBounds bounds;
    TileGeometry geometry(document.width(), document.height());
    if (!geometry.isValid()) return bounds;
    const auto range = geometry.tilesForRect({0, 0, document.width(), document.height()});
    if (!range) return bounds;
    std::vector<std::uint32_t> pixels;
    for (std::int64_t ty = range->first.y; ty <= range->last.y; ++ty) {
        for (std::int64_t tx = range->first.x; tx <= range->last.x; ++tx) {
            const TilePoint tile{tx, ty};
            const auto tileBounds = geometry.tileBounds(tile);
            if (!tileBounds) continue;
            std::int64_t width = 0, height = 0;
            if (!composeTile(document, layers, geometry, tile, &pixels, &width, &height)) continue;
            for (std::int64_t y = 0; y < height; ++y) {
                for (std::int64_t x = 0; x < width; ++x) {
                    const auto index = std::size_t(y) * std::size_t(width) + std::size_t(x);
                    if (index >= pixels.size()) continue;
                    if (((pixels[index] >> 24) & 255U) == 0U) continue;
                    const auto docX = tileBounds->left + x;
                    const auto docY = tileBounds->top + y;
                    bounds.left = std::min(bounds.left, docX);
                    bounds.top = std::min(bounds.top, docY);
                    bounds.right = std::max(bounds.right, docX + 1);
                    bounds.bottom = std::max(bounds.bottom, docY + 1);
                }
            }
        }
    }
    return bounds;
}

QRect clippedDocumentRect(const CanvasDocument& document, std::int64_t left, std::int64_t top,
                          std::int64_t right, std::int64_t bottom)
{
    left = std::clamp<std::int64_t>(left, 0, document.width());
    top = std::clamp<std::int64_t>(top, 0, document.height());
    right = std::clamp<std::int64_t>(right, left, document.width());
    bottom = std::clamp<std::int64_t>(bottom, top, document.height());
    return QRect(int(left), int(top), int(right - left), int(bottom - top));
}

std::vector<CompositeLayer> layersFor(const CanvasDocument& document, const Request& request,
                                      QString* error)
{
    std::vector<CompositeLayer> layers;
    LayerModel* model = document.layers();
    if (!model) {
        if (error) *error = QStringLiteral("There is no layer to read.");
        return layers;
    }
    auto add = [&](int row) {
        Layer* layer = model->layerAt(row);
        if (!layer) return;
        layers.push_back(CompositeLayer{&layer->tiles(), layer->isVisible(), layer->opacity(),
                                        static_cast<BlendMode>(layer->blendMode()), layer});
    };
    if (request.source == Source::MergedVisible) {
        for (int row = 0; row < model->rowCount(); ++row) add(row);
    } else if (request.source == Source::CurrentLayer) {
        add(model->activeLayerIndex());
    } else {
        for (int row : request.layerRows) add(row);
    }
    if (layers.empty() && error)
        *error = request.source == Source::CurrentLayer
            ? QStringLiteral("Select a layer with artwork first.")
            : QStringLiteral("Select the layer or layers you want to use first.");
    return layers;
}

} // namespace

QString sourceName(Source source)
{
    switch (source) {
    case Source::CurrentLayer: return QStringLiteral("Current layer");
    case Source::SelectedLayers: return QStringLiteral("Selected layers");
    case Source::MergedVisible: return QStringLiteral("Merged visible artwork");
    }
    return {};
}

QString coverageName(Coverage coverage)
{
    switch (coverage) {
    case Coverage::Colour: return QStringLiteral("Full colour");
    case Coverage::Alpha: return QStringLiteral("Transparency outline");
    case Coverage::Luminance: return QStringLiteral("Darkness");
    }
    return {};
}

bool parseSource(const QString& text, Source* out)
{
    if (!out) return false;
    if (text == QStringLiteral("current")) { *out = Source::CurrentLayer; return true; }
    if (text == QStringLiteral("selected")) { *out = Source::SelectedLayers; return true; }
    if (text == QStringLiteral("merged")) { *out = Source::MergedVisible; return true; }
    return false;
}

bool parseCoverage(const QString& text, Coverage* out)
{
    if (!out) return false;
    if (text == QStringLiteral("colour") || text == QStringLiteral("color")) { *out = Coverage::Colour; return true; }
    if (text == QStringLiteral("alpha")) { *out = Coverage::Alpha; return true; }
    if (text == QStringLiteral("luminance")) { *out = Coverage::Luminance; return true; }
    return false;
}

Availability availability(const CanvasDocument& document, const std::vector<int>& layerRows)
{
    Availability result;
    result.hasSelection = document.hasSelection();
    result.selectedLayerCount = int(layerRows.size());
    LayerModel* model = document.layers();
    if (!model) {
        result.reason = QStringLiteral("There is no document open.");
        return result;
    }
    Layer* active = model->layerAt(model->activeLayerIndex());
    if (active) {
        result.currentLayerName = active->name();
        result.currentLayer = !active->tiles().allocatedTilePoints().empty() ||
                              !active->materialTiles().allocatedTilePoints().empty();
    }
    int visibleWithArtwork = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        Layer* layer = model->layerAt(row);
        if (!layer || !layer->isVisible()) continue;
        if (!layer->tiles().allocatedTilePoints().empty() ||
            !layer->materialTiles().allocatedTilePoints().empty()) ++visibleWithArtwork;
    }
    result.mergedVisible = visibleWithArtwork > 0;
    for (int row : layerRows) {
        Layer* layer = model->layerAt(row);
        if (layer && (!layer->tiles().allocatedTilePoints().empty() ||
                      !layer->materialTiles().allocatedTilePoints().empty())) {
            result.selectedLayers = true;
            break;
        }
    }
    if (!result.currentLayer && !result.selectedLayers && !result.mergedVisible)
        result.reason = QStringLiteral("This document has no drawn pixels yet. Draw or place something first.");
    return result;
}

Result extract(const CanvasDocument& document, const Request& request)
{
    Result result;
    if (!document.layers() || document.width() <= 0 || document.height() <= 0) {
        result.error = QStringLiteral("There is no artwork open.");
        return result;
    }
    QString layerError;
    auto layers = layersFor(document, request, &layerError);
    if (layers.empty()) {
        result.error = layerError;
        return result;
    }
    if (std::none_of(layers.begin(), layers.end(), [](const auto& layer) { return layer.visible; })) {
        result.error = QStringLiteral("The chosen layers are hidden. Show at least one layer to make a tip.");
        return result;
    }

    std::int64_t left = 0, top = 0, right = document.width(), bottom = document.height();
    if (request.useSelection && document.hasSelection()) {
        const auto bounds = document.selection().bounds();
        if (bounds.empty()) {
            result.error = QStringLiteral("The selection is empty. Select an area of artwork first.");
            return result;
        }
        left = bounds.x;
        top = bounds.y;
        right = bounds.x + bounds.width;
        bottom = bounds.y + bounds.height;
    } else {
        const auto bounds = contentBounds(document, layers);
        if (bounds.empty()) {
            result.error = QStringLiteral("The chosen artwork is transparent. Draw something in the selected area first.");
            return result;
        }
        left = bounds.left;
        top = bounds.top;
        right = bounds.right;
        bottom = bounds.bottom;
    }
    const QRect rect = clippedDocumentRect(document, left, top, right, bottom);
    if (rect.isEmpty()) {
        result.error = QStringLiteral("The selected area does not overlap the artwork.");
        return result;
    }
    if (std::int64_t(rect.width()) * std::int64_t(rect.height()) > std::numeric_limits<int>::max()) {
        result.error = QStringLiteral("That area is too large to use as a brush tip. Select a smaller area.");
        return result;
    }
    QImage image(rect.size(), QImage::Format_RGBA8888);
    if (image.isNull()) {
        result.error = QStringLiteral("There is not enough memory to make a brush tip from that area.");
        return result;
    }
    image.fill(Qt::transparent);

    TileGeometry geometry(document.width(), document.height());
    // QRect::right()/bottom() are inclusive; TileGeometry uses exclusive
    // edges. Using right() directly made a one-pixel tip look empty.
    const auto range = geometry.tilesForRect({rect.left(), rect.top(),
                                              rect.left() + rect.width(),
                                              rect.top() + rect.height()});
    if (!range) {
        result.error = QStringLiteral("That area is not on the canvas.");
        return result;
    }
    std::vector<std::uint32_t> pixels;
    bool anyVisible = false;
    for (std::int64_t ty = range->first.y; ty <= range->last.y; ++ty) {
        for (std::int64_t tx = range->first.x; tx <= range->last.x; ++tx) {
            const TilePoint tile{tx, ty};
            const auto tileBounds = geometry.tileBounds(tile);
            if (!tileBounds) continue;
            std::int64_t tileWidth = 0, tileHeight = 0;
            if (!composeTile(document, layers, geometry, tile, &pixels, &tileWidth, &tileHeight)) {
                result.error = QStringLiteral("There is not enough memory to read the chosen artwork.");
                return result;
            }
            const std::int64_t startX = std::max<std::int64_t>(rect.left(), tileBounds->left);
            const std::int64_t startY = std::max<std::int64_t>(rect.top(), tileBounds->top);
            const std::int64_t endX = std::min<std::int64_t>(rect.right(), tileBounds->left + tileWidth);
            const std::int64_t endY = std::min<std::int64_t>(rect.bottom(), tileBounds->top + tileHeight);
            for (std::int64_t y = startY; y < endY; ++y) {
                for (std::int64_t x = startX; x < endX; ++x) {
                    const auto localX = x - tileBounds->left;
                    const auto localY = y - tileBounds->top;
                    const auto index = std::size_t(localY) * std::size_t(tileWidth) + std::size_t(localX);
                    if (index >= pixels.size()) continue;
                    const auto pixel = pixels[index]; // premultiplied ARGB
                    const auto alpha = (pixel >> 24) & 255U;
                    if (alpha == 0U) continue;
                    const auto red = std::min(255U, (((pixel >> 16) & 255U) * 255U + alpha / 2U) / alpha);
                    const auto green = std::min(255U, (((pixel >> 8) & 255U) * 255U + alpha / 2U) / alpha);
                    const auto blue = std::min(255U, ((pixel & 255U) * 255U + alpha / 2U) / alpha);
                    unsigned coverage255 = 255U;
                    unsigned mask = 255U;
                    if (request.useSelection && document.hasSelection())
                        mask = document.selection().at(x, y);
                    const auto effectiveAlpha = (alpha * mask + 127U) / 255U;
                    if (effectiveAlpha == 0U && mask == 0U) {
                        continue;
                    }
                    auto* scan = image.scanLine(int(y - rect.top()));
                    auto* out = reinterpret_cast<uchar*>(scan) + std::size_t(x - rect.left()) * 4u;
                    switch (request.coverage) {
                    case Coverage::Colour:
                        out[0] = uchar(red);
                        out[1] = uchar(green);
                        out[2] = uchar(blue);
                        out[3] = uchar((alpha * mask + 127U) / 255U);
                        break;
                    case Coverage::Alpha:
                        out[0] = out[1] = out[2] = 255;
                        out[3] = uchar(effectiveAlpha);
                        break;
                    case Coverage::Luminance: {
                        const auto luminance = unsigned(std::lround(0.2126 * red + 0.7152 * green + 0.0722 * blue));
                        out[0] = out[1] = out[2] = 255;
                        out[3] = uchar(std::min(255U, (effectiveAlpha * luminance + 127U) / 255U));
                        break;
                    }
                    }
                    coverage255 = out[3];
                    anyVisible = anyVisible || coverage255 != 0U;
                }
            }
        }
    }
    if (!anyVisible) {
        result.error = request.coverage == Coverage::Luminance
            ? QStringLiteral("That artwork is completely white or transparent. Choose a darker drawing or full colour.")
            : QStringLiteral("The chosen artwork is transparent. Draw something in the selected area first.");
        return result;
    }
    result.image = std::move(image);
    result.sourceRect = rect;
    return result;
}

} // namespace komapaint::brushtip
