#include "CanvasToolExecutor.h"
#include "BrushSettings.h"
#include "../canvas/CanvasDocument.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"
#include "../history/UndoStack.h"
#include "../material/MaterialEngine.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace komapaint {
namespace {

constexpr double kPi = 3.14159265358979323846;

const material::MaterialEngine& toolMaterialEngine()
{
    static const material::PigmentLibrary pigments;
    static const material::MaterialEngine engine(pigments);
    return engine;
}

std::uint32_t toPremulArgb(const QColor& c, double coverage01) noexcept
{
    const double a01 = std::clamp(c.alphaF() * coverage01, 0.0, 1.0);
    const unsigned a = static_cast<unsigned>(std::lround(a01 * 255.0));
    if (a == 0U) return 0U;
    const unsigned r = static_cast<unsigned>(std::lround(c.redF() * a01 * 255.0));
    const unsigned g = static_cast<unsigned>(std::lround(c.greenF() * a01 * 255.0));
    const unsigned b = static_cast<unsigned>(std::lround(c.blueF() * a01 * 255.0));
    return (a << 24) | (std::min(a, r) << 16) | (std::min(a, g) << 8) | std::min(a, b);
}

std::uint32_t sourceOverPremul(std::uint32_t dst, std::uint32_t srcPremul) noexcept
{
    const unsigned sa = (srcPremul >> 24) & 255U;
    if (sa == 0U) return dst;
    if (sa == 255U || dst == 0U) return srcPremul;
    const unsigned inv = 255U - sa;
    const unsigned da = (dst >> 24) & 255U;
    const unsigned dr = (dst >> 16) & 255U;
    const unsigned dg = (dst >> 8) & 255U;
    const unsigned db = dst & 255U;
    const unsigned sr = (srcPremul >> 16) & 255U;
    const unsigned sg = (srcPremul >> 8) & 255U;
    const unsigned sb = srcPremul & 255U;
    auto scale = [](unsigned v, unsigned m) { return (v * m + 127U) / 255U; };
    const unsigned oa = std::min(255U, sa + scale(da, inv));
    const unsigned orr = std::min(oa, sr + scale(dr, inv));
    const unsigned og = std::min(oa, sg + scale(dg, inv));
    const unsigned ob = std::min(oa, sb + scale(db, inv));
    return (oa << 24) | (orr << 16) | (og << 8) | ob;
}

material::PaintBatch buildMaterialColorBatch(const BrushSettings* brush, const QColor& color, double coverage01)
{
    const std::string mediumId = (brush && brush->mediumId() == QStringLiteral("watercolour"))
        ? std::string("watercolour") : std::string("gouache");
    const double alpha = std::max(0.05, static_cast<double>(color.alphaF())) * std::clamp(coverage01, 0.0, 1.0);
    const double conc = brush ? brush->pigmentConcentration() / 100.0 : 0.7;
    const double waterRatio = brush ? brush->waterAmount() / 100.0 : 0.35;
    const double bodyRatio = brush ? brush->paintBody() / 100.0 : 0.5;
    const double pigmentMass = alpha * (0.35 + 1.0 * conc);
    const double binderMass = alpha * (mediumId == "watercolour" ? (0.04 + 0.14 * bodyRatio) : (0.12 + 0.45 * bodyRatio));
    const double waterMass = alpha * (0.12 + 1.60 * waterRatio);
    auto batch = material::makeResidualPaint(color.rgba(), pigmentMass, waterMass, binderMass, mediumId);
    if (mediumId == "gouache" && bodyRatio > 0.0) {
        batch.addFiller(alpha * 0.20 * bodyRatio);
    }
    return batch;
}

Layer* activeEditableLayer(CanvasDocument* doc, QString* outError)
{
    if (!doc || !doc->layers()) {
        if (outError) *outError = QStringLiteral("No active document.");
        return nullptr;
    }
    auto* layer = doc->layers()->layerAt(doc->layers()->activeLayerIndex());
    if (!layer || layer->isLocked() || !layer->isVisible()) {
        if (outError) *outError = QStringLiteral("Select a visible, unlocked layer to edit.");
        return nullptr;
    }
    return layer;
}

bool beginLayerEdit(UndoStack* history, Layer* layer, bool realistic, QString* outError)
{
    if (!history) return true;
    const bool ok = realistic ? history->beginMaterialStroke(layer) : history->beginStroke(layer);
    if (!ok && outError) {
        *outError = QStringLiteral("Could not start undo history for this operation.");
    }
    return ok;
}

bool commitLayerEdit(UndoStack* history, QString* outError)
{
    if (!history) return true;
    if (!history->commitStroke()) {
        if (history->strokeActive()) history->cancelStroke();
        if (outError) *outError = QStringLiteral("Could not commit undo history.");
        return false;
    }
    return true;
}

std::uint32_t sampleBilinear(const std::vector<std::uint32_t>& buf, int w, int h, double x, double y) noexcept
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    auto at = [&](int px, int py) -> std::uint32_t {
        if (px < 0 || py < 0 || px >= w || py >= h) return 0U;
        return buf[static_cast<std::size_t>(py * w + px)];
    };
    const std::uint32_t c00 = at(x0, y0);
    const std::uint32_t c10 = at(x0 + 1, y0);
    const std::uint32_t c01 = at(x0, y0 + 1);
    const std::uint32_t c11 = at(x0 + 1, y0 + 1);
    if ((c00 | c10 | c01 | c11) == 0U) return 0U;
    auto ch = [&](int shift) -> unsigned {
        const double v00 = static_cast<double>((c00 >> shift) & 255U);
        const double v10 = static_cast<double>((c10 >> shift) & 255U);
        const double v01 = static_cast<double>((c01 >> shift) & 255U);
        const double v11 = static_cast<double>((c11 >> shift) & 255U);
        const double top = v00 * (1.0 - fx) + v10 * fx;
        const double bot = v01 * (1.0 - fx) + v11 * fx;
        return static_cast<unsigned>(std::clamp(std::lround(top * (1.0 - fy) + bot * fy), 0L, 255L));
    };
    const unsigned a = ch(24);
    if (a == 0U) return 0U;
    return (a << 24) | (std::min(a, ch(16)) << 16) | (std::min(a, ch(8)) << 8) | std::min(a, ch(0));
}

} // namespace

double evaluatePixelMask(void* ctx, std::int64_t x, std::int64_t y) noexcept
{
    const auto* c = static_cast<const PixelMaskContext*>(ctx);
    if (!c) return 1.0;
    double weight = 1.0;
    if (c->hasSelection && c->selection) {
        weight *= static_cast<double>(c->selection->at(x, y)) / 255.0;
        if (weight <= 0.0) return 0.0;
    }
    if (c->protectInterior && c->protectedRegion) {
        const std::uint8_t prot = c->protectedRegion->coverageAt(x, y);
        weight *= static_cast<double>(255 - prot) / 255.0;
    }
    return std::clamp(weight, 0.0, 1.0);
}

region::ReferenceSet CanvasToolExecutor::buildReferenceSet(CanvasDocument* doc, int referenceMode)
{
    region::ReferenceSet refs;
    if (!doc || !doc->layers()) return refs;
    refs.docWidth = doc->width();
    refs.docHeight = doc->height();
    refs.transparentBackground = doc->hasTransparentBackground();
    refs.backgroundArgb = doc->backgroundColor().rgba();
    switch (referenceMode) {
    case 0: refs.mode = region::ReferenceMode::CurrentLayer; break;
    case 1: refs.mode = region::ReferenceMode::AllVisible; break;
    case 2: refs.mode = region::ReferenceMode::SelectedLayers; break;
    case 3: refs.mode = region::ReferenceMode::ExcludeSelected; break;
    default: refs.mode = region::ReferenceMode::AllVisible; break;
    }

    auto* model = doc->layers();
    const int activeIdx = model->activeLayerIndex();
    const auto surface = doc->surfaceProfile();

    // Note: CpuTileCompositor expects topFirst.
    // LayerModel stores layers bottom-to-top or top-to-bottom? Let's match CanvasViewport::layers():
    // CanvasViewport::layers() iterates 0 .. rowCount()-1 directly as topFirst!
    const int count = model->rowCount();
    refs.layersTopFirst.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto* layer = model->layerAt(i);
        if (!layer) continue;
        region::ReferenceLayerDesc desc;
        desc.layerKey = layer;
        desc.digital = &layer->tiles();
        desc.revision = layer->tiles().dirtyVersion() + layer->materialTiles().dirtyVersion() * 1315423911ULL;
        desc.opacity = layer->opacity();
        desc.blendMode = static_cast<BlendMode>(layer->blendMode());
        desc.visible = layer->isVisible();
        desc.selected = (i == activeIdx);
        desc.active = (i == activeIdx);
        if (layer->materialTiles().allocatedTiles() > 0) {
            desc.renderMaterialTile = [layer, surface](TilePoint tile, std::vector<std::uint32_t>& outBuf) -> bool {
                const auto geom = layer->materialTiles().geometry();
                const int ts = geom.tileSize();
                outBuf.assign(static_cast<std::size_t>(ts) * static_cast<std::size_t>(ts), 0U);
                bool any = false;
                const std::int64_t bx = tile.x * ts;
                const std::int64_t by = tile.y * ts;
                for (int ly = 0; ly < ts; ++ly) {
                    const std::int64_t gy = by + ly;
                    if (gy < 0 || gy >= geom.height()) continue;
                    for (int lx = 0; lx < ts; ++lx) {
                        const std::int64_t gx = bx + lx;
                        if (gx < 0 || gx >= geom.width()) continue;
                        const auto* cell = layer->materialTiles().findCell({gx, gy});
                        if (!cell || cell->empty()) continue;
                        const auto batch = layer->materialTiles().batchAt({gx, gy});
                        if (!batch || batch->totalMass() <= 1e-9) continue;
                        const auto* med = material::findMediumProfile(batch->mediumId);
                        const auto& medium = med ? *med : material::gouacheProfile();
                        const auto opt = toolMaterialEngine().opticalResult(*batch, medium, surface, 1.0);
                        outBuf[static_cast<std::size_t>(ly * ts + lx)] =
                            toolMaterialEngine().compositeOptical(0U, opt, surface, 1.0, 1.0);
                        any = true;
                    }
                }
                return any;
            };
        }
        refs.layersTopFirst.push_back(std::move(desc));
    }
    return refs;
}

bool CanvasToolExecutor::applyRegionFill(CanvasDocument* doc, UndoStack* history,
                                         BrushSettings* brush, const QColor& color,
                                         const region::RegionResult& result,
                                         QString* outError)
{
    if (result.empty()) return false;
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;
    const bool realistic = brush && brush->isRealisticEngine();
    if (!beginLayerEdit(history, layer, realistic, outError)) return false;

    const auto& bounds = result.bounds;
    if (realistic) {
        auto& mstore = layer->materialTiles();
        mstore.beginBatch();
        for (std::int64_t ly = 0; ly < bounds.height; ++ly) {
            const std::int64_t gy = bounds.y + ly;
            for (std::int64_t lx = 0; lx < bounds.width; ++lx) {
                const std::int64_t gx = bounds.x + lx;
                const std::uint8_t cov = result.coverage[static_cast<std::size_t>(ly * bounds.width + lx)];
                if (cov == 0) continue;
                const auto batch = buildMaterialColorBatch(brush, color, static_cast<double>(cov) / 255.0);
                const auto r = mstore.deposit({gx, gy}, batch);
                if (r != material::MaterialTileStore::Result::Changed &&
                    r != material::MaterialTileStore::Result::Unchanged) {
                    mstore.endBatch();
                    if (history) history->cancelStroke();
                    if (outError) *outError = QStringLiteral("Material fill failed due to memory limits.");
                    return false;
                }
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        store.beginBatch();
        for (std::int64_t ly = 0; ly < bounds.height; ++ly) {
            const std::int64_t gy = bounds.y + ly;
            for (std::int64_t lx = 0; lx < bounds.width; ++lx) {
                const std::int64_t gx = bounds.x + lx;
                const std::uint8_t cov = result.coverage[static_cast<std::size_t>(ly * bounds.width + lx)];
                if (cov == 0) continue;
                const std::uint32_t src = toPremulArgb(color, static_cast<double>(cov) / 255.0);
                const std::uint32_t old = store.pixel({gx, gy}).value_or(0U);
                const std::uint32_t next = sourceOverPremul(old, src);
                if (next != old) {
                    const auto r = store.setPixel({gx, gy}, next);
                    if (r != SparseTileStore::WriteResult::Changed &&
                        r != SparseTileStore::WriteResult::Unchanged) {
                        store.endBatch();
                        if (history) history->cancelStroke();
                        if (outError) *outError = QStringLiteral("Fill failed due to storage limits.");
                        return false;
                    }
                }
            }
        }
        store.endBatch();
    }
    return commitLayerEdit(history, outError);
}

bool CanvasToolExecutor::applyGradient(CanvasDocument* doc, UndoStack* history,
                                       BrushSettings* brush, const QColor& color,
                                       qreal x0, qreal y0, qreal x1, qreal y1,
                                       const QString& gradientType,
                                       QString* outError)
{
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;
    const double len = std::hypot(x1 - x0, y1 - y0);
    if (len < 1.0) return false;

    const auto& sel = doc->selection();
    const bool hasSel = sel.hasSelection();
    const region::PixelBounds area = hasSel ? sel.bounds() : region::PixelBounds{0, 0, doc->width(), doc->height()};
    if (area.empty()) return false;

    const bool realistic = brush && brush->isRealisticEngine();
    if (!beginLayerEdit(history, layer, realistic, outError)) return false;

    const bool radial = (gradientType == QStringLiteral("radial"));
    const double vx = (x1 - x0) / len;
    const double vy = (y1 - y0) / len;

    if (realistic) {
        auto& mstore = layer->materialTiles();
        mstore.beginBatch();
        for (std::int64_t y = area.y; y < area.y + area.height; ++y) {
            for (std::int64_t x = area.x; x < area.x + area.width; ++x) {
                const double selW = hasSel ? static_cast<double>(sel.at(x, y)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                const double px = static_cast<double>(x) + 0.5 - x0;
                const double py = static_cast<double>(y) + 0.5 - y0;
                const double t = radial ? std::clamp(std::hypot(px, py) / len, 0.0, 1.0)
                                        : std::clamp((px * vx + py * vy) / len, 0.0, 1.0);
                const double w = (1.0 - t) * selW;
                if (w <= 0.01) continue;
                mstore.deposit({x, y}, buildMaterialColorBatch(brush, color, w));
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        store.beginBatch();
        for (std::int64_t y = area.y; y < area.y + area.height; ++y) {
            for (std::int64_t x = area.x; x < area.x + area.width; ++x) {
                const double selW = hasSel ? static_cast<double>(sel.at(x, y)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                const double px = static_cast<double>(x) + 0.5 - x0;
                const double py = static_cast<double>(y) + 0.5 - y0;
                const double t = radial ? std::clamp(std::hypot(px, py) / len, 0.0, 1.0)
                                        : std::clamp((px * vx + py * vy) / len, 0.0, 1.0);
                const double w = (1.0 - t) * selW;
                if (w <= 0.002) continue;
                const std::uint32_t src = toPremulArgb(color, w);
                const std::uint32_t old = store.pixel({x, y}).value_or(0U);
                const std::uint32_t next = sourceOverPremul(old, src);
                if (next != old) store.setPixel({x, y}, next);
            }
        }
        store.endBatch();
    }
    return commitLayerEdit(history, outError);
}

bool CanvasToolExecutor::applyShape(CanvasDocument* doc, UndoStack* history,
                                    BrushSettings* brush, const QColor& color,
                                    const QString& shapeType, const QString& shapeMode,
                                    int strokeWidth,
                                    qreal x0, qreal y0, qreal x1, qreal y1,
                                    qreal ctrlX, qreal ctrlY,
                                    QString* outError)
{
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;

    const double minX = std::min({x0, x1, shapeType == QStringLiteral("curveline") ? ctrlX : x0});
    const double maxX = std::max({x0, x1, shapeType == QStringLiteral("curveline") ? ctrlX : x1});
    const double minY = std::min({y0, y1, shapeType == QStringLiteral("curveline") ? ctrlY : y0});
    const double maxY = std::max({y0, y1, shapeType == QStringLiteral("curveline") ? ctrlY : y1});
    const int pad = std::max(4, strokeWidth + 2);
    const std::int64_t bx0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minX)) - pad);
    const std::int64_t by0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(minY)) - pad);
    const std::int64_t bx1 = std::min<std::int64_t>(doc->width(), static_cast<std::int64_t>(std::ceil(maxX)) + pad);
    const std::int64_t by1 = std::min<std::int64_t>(doc->height(), static_cast<std::int64_t>(std::ceil(maxY)) + pad);
    const int bw = static_cast<int>(bx1 - bx0);
    const int bh = static_cast<int>(by1 - by0);
    if (bw <= 0 || bh <= 0) return false;

    QImage img(bw, bh, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-bx0, -by0);
        const bool doStroke = (shapeMode != QStringLiteral("fill"));
        const bool doFill = (shapeMode == QStringLiteral("fill") || shapeMode == QStringLiteral("both"))
            && (shapeType == QStringLiteral("rectangle") || shapeType == QStringLiteral("circle"));
        if (doStroke) {
            QPen pen(color, std::max(1, strokeWidth), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
        } else {
            p.setPen(Qt::NoPen);
        }
        p.setBrush(doFill ? QBrush(color) : QBrush(Qt::NoBrush));

        if (shapeType == QStringLiteral("line")) {
            p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
        } else if (shapeType == QStringLiteral("curveline")) {
            QPainterPath path(QPointF(x0, y0));
            path.quadTo(QPointF(ctrlX, ctrlY), QPointF(x1, y1));
            p.drawPath(path);
        } else if (shapeType == QStringLiteral("rectangle")) {
            p.drawRect(QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized());
        } else if (shapeType == QStringLiteral("circle")) {
            p.drawEllipse(QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized());
        }
    }

    const auto& sel = doc->selection();
    const bool hasSel = sel.hasSelection();
    const bool realistic = brush && brush->isRealisticEngine();
    if (!beginLayerEdit(history, layer, realistic, outError)) return false;

    if (realistic) {
        auto& mstore = layer->materialTiles();
        mstore.beginBatch();
        for (int ly = 0; ly < bh; ++ly) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(img.constScanLine(ly));
            for (int lx = 0; lx < bw; ++lx) {
                const std::uint32_t src = row[lx];
                const unsigned a = (src >> 24) & 255U;
                if (a == 0U) continue;
                const std::int64_t gx = bx0 + lx;
                const std::int64_t gy = by0 + ly;
                const double selW = hasSel ? static_cast<double>(sel.at(gx, gy)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                mstore.deposit({gx, gy}, buildMaterialColorBatch(brush, color, (a / 255.0) * selW));
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        store.beginBatch();
        for (int ly = 0; ly < bh; ++ly) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(img.constScanLine(ly));
            for (int lx = 0; lx < bw; ++lx) {
                const std::uint32_t src = row[lx];
                const unsigned a = (src >> 24) & 255U;
                if (a == 0U) continue;
                const std::int64_t gx = bx0 + lx;
                const std::int64_t gy = by0 + ly;
                const double selW = hasSel ? static_cast<double>(sel.at(gx, gy)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                std::uint32_t masked = src;
                if (selW < 0.999) {
                    const unsigned sa = static_cast<unsigned>(std::lround(((src >> 24) & 255U) * selW));
                    const unsigned sr = static_cast<unsigned>(std::lround(((src >> 16) & 255U) * selW));
                    const unsigned sg = static_cast<unsigned>(std::lround(((src >> 8) & 255U) * selW));
                    const unsigned sb = static_cast<unsigned>(std::lround((src & 255U) * selW));
                    masked = (sa << 24) | (sr << 16) | (sg << 8) | sb;
                }
                const std::uint32_t old = store.pixel({gx, gy}).value_or(0U);
                const std::uint32_t next = sourceOverPremul(old, masked);
                if (next != old) store.setPixel({gx, gy}, next);
            }
        }
        store.endBatch();
    }
    return commitLayerEdit(history, outError);
}

bool CanvasToolExecutor::applyText(CanvasDocument* doc, UndoStack* history,
                                   BrushSettings* brush, const QColor& color,
                                   qreal docX, qreal docY,
                                   const QString& text, int fontSize,
                                   QString* outError)
{
    if (text.trimmed().isEmpty()) return false;
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;

    QFont font;
    font.setPixelSize(std::clamp(fontSize, 6, 512));
    const QFontMetrics fm(font);
    const QRect textRect = fm.boundingRect(QRect(0, 0, doc->width(), doc->height()),
                                           Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    const int pad = 8;
    const std::int64_t bx0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(docX)) - pad);
    const std::int64_t by0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(docY)) - pad);
    const std::int64_t bx1 = std::min<std::int64_t>(doc->width(), static_cast<std::int64_t>(std::ceil(docX + textRect.width())) + pad * 2);
    const std::int64_t by1 = std::min<std::int64_t>(doc->height(), static_cast<std::int64_t>(std::ceil(docY + textRect.height())) + pad * 2);
    const int bw = static_cast<int>(bx1 - bx0);
    const int bh = static_cast<int>(by1 - by0);
    if (bw <= 0 || bh <= 0) return false;

    QImage img(bw, bh, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setFont(font);
        p.setPen(color);
        p.drawText(QRectF(docX - bx0, docY - by0, bw, bh), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    }

    const auto& sel = doc->selection();
    const bool hasSel = sel.hasSelection();
    const bool realistic = brush && brush->isRealisticEngine();
    if (!beginLayerEdit(history, layer, realistic, outError)) return false;

    if (realistic) {
        auto& mstore = layer->materialTiles();
        mstore.beginBatch();
        for (int ly = 0; ly < bh; ++ly) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(img.constScanLine(ly));
            for (int lx = 0; lx < bw; ++lx) {
                const unsigned a = (row[lx] >> 24) & 255U;
                if (a == 0U) continue;
                const std::int64_t gx = bx0 + lx;
                const std::int64_t gy = by0 + ly;
                const double selW = hasSel ? static_cast<double>(sel.at(gx, gy)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                mstore.deposit({gx, gy}, buildMaterialColorBatch(brush, color, (a / 255.0) * selW));
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        store.beginBatch();
        for (int ly = 0; ly < bh; ++ly) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(img.constScanLine(ly));
            for (int lx = 0; lx < bw; ++lx) {
                const std::uint32_t src = row[lx];
                if ((src >> 24) == 0U) continue;
                const std::int64_t gx = bx0 + lx;
                const std::int64_t gy = by0 + ly;
                const double selW = hasSel ? static_cast<double>(sel.at(gx, gy)) / 255.0 : 1.0;
                if (selW <= 0.0) continue;
                const std::uint32_t old = store.pixel({gx, gy}).value_or(0U);
                const std::uint32_t next = sourceOverPremul(old, src);
                if (next != old) store.setPixel({gx, gy}, next);
            }
        }
        store.endBatch();
    }
    return commitLayerEdit(history, outError);
}

bool CanvasToolExecutor::clearSelectionContent(CanvasDocument* doc, UndoStack* history,
                                               BrushSettings* brush, QString* outError)
{
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;
    const auto& sel = doc->selection();
    if (!sel.hasSelection()) return false;
    const auto b = sel.bounds();
    if (b.empty()) return false;

    const bool realistic = brush && brush->isRealisticEngine();
    if (!beginLayerEdit(history, layer, realistic, outError)) return false;

    if (realistic) {
        auto& mstore = layer->materialTiles();
        mstore.beginBatch();
        for (std::int64_t y = b.y; y < b.y + b.height; ++y) {
            for (std::int64_t x = b.x; x < b.x + b.width; ++x) {
                const std::uint8_t c = sel.at(x, y);
                if (c == 0) continue;
                mstore.scaleCell({x, y}, 1.0 - static_cast<double>(c) / 255.0);
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        store.beginBatch();
        for (std::int64_t y = b.y; y < b.y + b.height; ++y) {
            for (std::int64_t x = b.x; x < b.x + b.width; ++x) {
                const std::uint8_t c = sel.at(x, y);
                if (c == 0) continue;
                const std::uint32_t old = store.pixel({x, y}).value_or(0U);
                if (old == 0U) continue;
                if (c == 255) {
                    store.setPixel({x, y}, 0U);
                } else {
                    const double keep = 1.0 - static_cast<double>(c) / 255.0;
                    const unsigned na = static_cast<unsigned>(std::lround(((old >> 24) & 255U) * keep));
                    const unsigned nr = static_cast<unsigned>(std::lround(((old >> 16) & 255U) * keep));
                    const unsigned ng = static_cast<unsigned>(std::lround(((old >> 8) & 255U) * keep));
                    const unsigned nb = static_cast<unsigned>(std::lround((old & 255U) * keep));
                    store.setPixel({x, y}, (na << 24) | (nr << 16) | (ng << 8) | nb);
                }
            }
        }
        store.endBatch();
    }
    return commitLayerEdit(history, outError);
}

bool CanvasToolExecutor::moveOrTransform(CanvasDocument* doc, UndoStack* history,
                                         BrushSettings* brush,
                                         qreal dx, qreal dy,
                                         qreal scaleX, qreal scaleY, qreal rotationDeg,
                                         QString* outError)
{
    auto* layer = activeEditableLayer(doc, outError);
    if (!layer) return false;
    if (std::abs(dx) < 0.05 && std::abs(dy) < 0.05 &&
        std::abs(scaleX - 1.0) < 1e-4 && std::abs(scaleY - 1.0) < 1e-4 &&
        std::abs(rotationDeg) < 0.05) {
        return false;
    }

    auto& sel = doc->selection();
    const bool hasSel = sel.hasSelection();
    const bool useMaterial = (brush && brush->isRealisticEngine())
        || (layer->materialTiles().allocatedTiles() > 0 && layer->tiles().allocatedTiles() == 0);

    region::PixelBounds srcBounds;
    if (hasSel) {
        srcBounds = sel.bounds();
    } else {
        // Compute tight bounding box of allocated tiles on active layer
        std::int64_t minX = doc->width(), minY = doc->height(), maxX = -1, maxY = -1;
        if (useMaterial) {
            const auto tilePts = layer->materialTiles().allocatedTilePoints();
            const int ts = layer->materialTiles().geometry().tileSize();
            for (const TilePoint tp : tilePts) {
                const auto b = layer->materialTiles().geometry().tileBounds(tp);
                if (!b) continue;
                minX = std::min(minX, b->left);
                minY = std::min(minY, b->top);
                maxX = std::max(maxX, b->right - 1);
                maxY = std::max(maxY, b->bottom - 1);
            }
            (void)ts;
        } else {
            const auto tilePts = layer->tiles().allocatedTilePoints();
            for (const TilePoint tp : tilePts) {
                const auto b = layer->tiles().geometry().tileBounds(tp);
                if (!b) continue;
                minX = std::min(minX, b->left);
                minY = std::min(minY, b->top);
                maxX = std::max(maxX, b->right - 1);
                maxY = std::max(maxY, b->bottom - 1);
            }
        }
        if (maxX < minX || maxY < minY) return false;
        srcBounds = {minX, minY, maxX - minX + 1, maxY - minY + 1};
    }
    if (srcBounds.empty() || srcBounds.width * srcBounds.height > 4096LL * 4096LL) return false;

    const int sw = static_cast<int>(srcBounds.width);
    const int sh = static_cast<int>(srcBounds.height);
    const double cx = static_cast<double>(srcBounds.x) + sw * 0.5;
    const double cy = static_cast<double>(srcBounds.y) + sh * 0.5;
    const double sx = std::max(0.05, static_cast<double>(scaleX));
    const double sy = std::max(0.05, static_cast<double>(scaleY));
    const double rad = static_cast<double>(rotationDeg) * (kPi / 180.0);
    const double cosA = std::cos(-rad);
    const double sinA = std::sin(-rad);

    // Destination bounds conservatively enclosing rotated/scaled rect.
    const double halfDiag = 0.5 * std::hypot(sw * sx, sh * sy) + 2.0;
    const std::int64_t dx0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cx + dx - halfDiag)));
    const std::int64_t dy0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(cy + dy - halfDiag)));
    const std::int64_t dx1 = std::min<std::int64_t>(doc->width(), static_cast<std::int64_t>(std::ceil(cx + dx + halfDiag)));
    const std::int64_t dy1 = std::min<std::int64_t>(doc->height(), static_cast<std::int64_t>(std::ceil(cy + dy + halfDiag)));

    if (!beginLayerEdit(history, layer, useMaterial, outError)) return false;

    if (useMaterial) {
        auto& mstore = layer->materialTiles();
        std::vector<material::PaintBatch> captured(static_cast<std::size_t>(sw * sh));
        std::vector<std::uint8_t> capturedCov(static_cast<std::size_t>(sw * sh), 0);
        mstore.beginBatch();
        for (int ly = 0; ly < sh; ++ly) {
            for (int lx = 0; lx < sw; ++lx) {
                const std::int64_t gx = srcBounds.x + lx;
                const std::int64_t gy = srcBounds.y + ly;
                const std::uint8_t cov = hasSel ? sel.at(gx, gy) : 255;
                if (cov == 0) continue;
                const auto* cell = mstore.findCell({gx, gy});
                if (!cell || cell->empty()) continue;
                const auto bOpt = mstore.batchAt({gx, gy});
                if (!bOpt || bOpt->totalMass() <= 1e-9) continue;
                const std::size_t idx = static_cast<std::size_t>(ly * sw + lx);
                captured[idx] = *bOpt;
                capturedCov[idx] = cov;
                mstore.scaleCell({gx, gy}, 1.0 - static_cast<double>(cov) / 255.0);
            }
        }
        for (std::int64_t gy = dy0; gy < dy1; ++gy) {
            for (std::int64_t gx = dx0; gx < dx1; ++gx) {
                const double ux = (static_cast<double>(gx) + 0.5) - (cx + dx);
                const double uy = (static_cast<double>(gy) + 0.5) - (cy + dy);
                const double rx = (ux * cosA - uy * sinA) / sx + cx - static_cast<double>(srcBounds.x);
                const double ry = (ux * sinA + uy * cosA) / sy + cy - static_cast<double>(srcBounds.y);
                const int ilx = static_cast<int>(std::floor(rx));
                const int ily = static_cast<int>(std::floor(ry));
                if (ilx < 0 || ily < 0 || ilx >= sw || ily >= sh) continue;
                const std::size_t idx = static_cast<std::size_t>(ily * sw + ilx);
                if (capturedCov[idx] == 0) continue;
                material::PaintBatch b = captured[idx];
                b.scale(static_cast<double>(capturedCov[idx]) / 255.0);
                mstore.deposit({gx, gy}, b);
            }
        }
        mstore.endBatch();
    } else {
        auto& store = layer->tiles();
        std::vector<std::uint32_t> captured(static_cast<std::size_t>(sw * sh), 0U);
        store.beginBatch();
        for (int ly = 0; ly < sh; ++ly) {
            for (int lx = 0; lx < sw; ++lx) {
                const std::int64_t gx = srcBounds.x + lx;
                const std::int64_t gy = srcBounds.y + ly;
                const std::uint8_t cov = hasSel ? sel.at(gx, gy) : 255;
                if (cov == 0) continue;
                const std::uint32_t old = store.pixel({gx, gy}).value_or(0U);
                if (old == 0U) continue;
                if (cov == 255) {
                    captured[static_cast<std::size_t>(ly * sw + lx)] = old;
                    store.setPixel({gx, gy}, 0U);
                } else {
                    const double f = static_cast<double>(cov) / 255.0;
                    const unsigned a = static_cast<unsigned>(std::lround(((old >> 24) & 255U) * f));
                    const unsigned r = static_cast<unsigned>(std::lround(((old >> 16) & 255U) * f));
                    const unsigned g = static_cast<unsigned>(std::lround(((old >> 8) & 255U) * f));
                    const unsigned b = static_cast<unsigned>(std::lround((old & 255U) * f));
                    captured[static_cast<std::size_t>(ly * sw + lx)] = (a << 24) | (r << 16) | (g << 8) | b;
                    const double keep = 1.0 - f;
                    const unsigned ka = static_cast<unsigned>(std::lround(((old >> 24) & 255U) * keep));
                    const unsigned kr = static_cast<unsigned>(std::lround(((old >> 16) & 255U) * keep));
                    const unsigned kg = static_cast<unsigned>(std::lround(((old >> 8) & 255U) * keep));
                    const unsigned kb = static_cast<unsigned>(std::lround((old & 255U) * keep));
                    store.setPixel({gx, gy}, (ka << 24) | (kr << 16) | (kg << 8) | kb);
                }
            }
        }
        for (std::int64_t gy = dy0; gy < dy1; ++gy) {
            for (std::int64_t gx = dx0; gx < dx1; ++gx) {
                const double ux = (static_cast<double>(gx) + 0.5) - (cx + dx);
                const double uy = (static_cast<double>(gy) + 0.5) - (cy + dy);
                const double rx = (ux * cosA - uy * sinA) / sx + cx - static_cast<double>(srcBounds.x) - 0.5;
                const double ry = (ux * sinA + uy * cosA) / sy + cy - static_cast<double>(srcBounds.y) - 0.5;
                if (rx < -0.5 || ry < -0.5 || rx > sw - 0.5 || ry > sh - 0.5) continue;
                const std::uint32_t sampled = sampleBilinear(captured, sw, sh, rx, ry);
                if ((sampled >> 24) == 0U) continue;
                const std::uint32_t old = store.pixel({gx, gy}).value_or(0U);
                store.setPixel({gx, gy}, sourceOverPremul(old, sampled));
            }
        }
        store.endBatch();
    }

    // Move/transform the SelectionMask cleanly along with the moved pixels.
    if (hasSel && dy1 > dy0 && dx1 > dx0) {
        std::vector<std::uint8_t> newCov(static_cast<std::size_t>((dx1 - dx0) * (dy1 - dy0)), 0);
        const std::int64_t nw = dx1 - dx0;
        for (std::int64_t gy = dy0; gy < dy1; ++gy) {
            for (std::int64_t gx = dx0; gx < dx1; ++gx) {
                const double ux = (static_cast<double>(gx) + 0.5) - (cx + dx);
                const double uy = (static_cast<double>(gy) + 0.5) - (cy + dy);
                const double rx = (ux * cosA - uy * sinA) / sx + cx;
                const double ry = (ux * sinA + uy * cosA) / sy + cy;
                const std::int64_t ix = static_cast<std::int64_t>(std::llround(rx - 0.5));
                const std::int64_t iy = static_cast<std::int64_t>(std::llround(ry - 0.5));
                if (ix >= srcBounds.x && iy >= srcBounds.y &&
                    ix < srcBounds.x + srcBounds.width && iy < srcBounds.y + srcBounds.height) {
                    newCov[static_cast<std::size_t>((gy - dy0) * nw + (gx - dx0))] = sel.at(ix, iy);
                }
            }
        }
        doc->pushSelectionSnapshot();
        sel.applyRectMask(dx0, dy0, nw, dy1 - dy0, newCov, region::SelectionOp::Replace);
        doc->notifySelectionChanged();
    }

    return commitLayerEdit(history, outError);
}

} // namespace komapaint
