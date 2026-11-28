#include "LevelsController.h"

#include "canvas/CanvasDocument.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace komapaint {
namespace {

std::uint32_t blendPremultiplied(std::uint32_t source, std::uint32_t adjusted,
                                 unsigned coverage) noexcept
{
    if (coverage == 0U) return source;
    if (coverage >= 255U) return adjusted;
    const auto alpha = static_cast<std::uint32_t>(source >> 24);
    const auto channel = [coverage](std::uint32_t from, std::uint32_t to) {
        return (from * (255U - coverage) + to * coverage + 127U) / 255U;
    };
    const auto red = channel((source >> 16) & 255U, (adjusted >> 16) & 255U);
    const auto green = channel((source >> 8) & 255U, (adjusted >> 8) & 255U);
    const auto blue = channel(source & 255U, adjusted & 255U);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

int clampInt(int value, int lo, int hi)
{
    return std::max(lo, std::min(hi, value));
}

QString formatNumber(double value)
{
    return QString::number(value, 'f', 2);
}

}  // namespace

LevelsController::LevelsController(QObject* parent)
    : QObject(parent), m_previewTimer(new QTimer(this))
{
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(24);
    connect(m_previewTimer, &QTimer::timeout, this, &LevelsController::applyPreviewNow);
    m_lut = adjustments::LevelsLut::fromParameters(m_parameters);
}

QString LevelsController::targetKind() const
{
    switch (m_target) {
    case Target::Document: return QStringLiteral("document");
    case Target::Image: return QStringLiteral("image");
    case Target::None: break;
    }
    return {};
}

int LevelsController::gammaHundredths() const
{
    return clampInt(static_cast<int>(std::lround(m_parameters.gamma * 100.0)), 10, 1000);
}

int LevelsController::midpointPercent() const
{
    return clampInt(static_cast<int>(std::lround(
                        adjustments::midpointForGamma(m_parameters.gamma) * 100.0)), 1, 99);
}

QString LevelsController::gammaText() const
{
    return formatNumber(m_parameters.gamma);
}

void LevelsController::setShowOriginal(bool show)
{
    if (m_showOriginal == show) return;
    m_showOriginal = show;
    emit showOriginalChanged();
    if (m_target == Target::Document) applyPreviewNow();
    else if (m_target == Target::Image) emit previewChanged();
}

void LevelsController::setError(const QString& message)
{
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}

void LevelsController::clearError()
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}

void LevelsController::rebuildLut()
{
    m_lut = adjustments::LevelsLut::fromParameters(m_parameters);
}

void LevelsController::schedulePreview()
{
    if (m_target == Target::None) return;
    if (!m_previewTimer->isActive()) m_previewTimer->start();
}

void LevelsController::setInputBlack(int value)
{
    value = clampInt(value, 0, 254);
    if (m_parameters.inputBlack == value) return;
    m_parameters.inputBlack = value;
    if (m_parameters.inputWhite <= value)
        m_parameters.inputWhite = std::min(255, value + 1);
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::setInputWhite(int value)
{
    value = clampInt(value, 1, 255);
    if (m_parameters.inputWhite == value) return;
    m_parameters.inputWhite = value;
    if (m_parameters.inputBlack >= value)
        m_parameters.inputBlack = std::max(0, value - 1);
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::setOutputBlack(int value)
{
    value = clampInt(value, 0, 255);
    if (m_parameters.outputBlack == value) return;
    m_parameters.outputBlack = value;
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::setOutputWhite(int value)
{
    value = clampInt(value, 0, 255);
    if (m_parameters.outputWhite == value) return;
    m_parameters.outputWhite = value;
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::setGamma(double value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 0.1, 10.0);
    if (std::abs(value - m_parameters.gamma) < 1e-9) return;
    m_parameters.gamma = value;
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::setGammaHundredths(int value)
{
    setGamma(static_cast<double>(clampInt(value, 10, 1000)) / 100.0);
}

void LevelsController::setMidpointPercent(int value)
{
    value = clampInt(value, 1, 99);
    const double gamma = adjustments::gammaForMidpoint(static_cast<double>(value) / 100.0);
    if (std::abs(gamma - m_parameters.gamma) < 1e-9) return;
    m_parameters.gamma = gamma;
    rebuildLut();
    emit parametersChanged();
    schedulePreview();
}

void LevelsController::resetToDefaults()
{
    const bool showChanged = m_showOriginal;
    m_showOriginal = false;
    m_parameters = adjustments::LevelsParameters{};
    rebuildLut();
    emit parametersChanged();
    if (showChanged) emit showOriginalChanged();
}

void LevelsController::reset()
{
    resetToDefaults();
    schedulePreview();
}

QVariantList LevelsController::buildHistogram(const adjustments::Histogram& histogram) const
{
    QVariantList out;
    out.reserve(256);
    for (auto value : histogram) out.push_back(QVariant::fromValue<qulonglong>(value));
    return out;
}

void LevelsController::calculateDocumentHistogram()
{
    adjustments::Histogram total{};
    const auto tileSize = m_layer ? m_layer->tiles().geometry().tileSize() : 0;
    if (!m_layer || tileSize <= 0) {
        m_histogram = buildHistogram(total);
        emit histogramChanged();
        return;
    }
    const auto tilePixels = static_cast<std::size_t>(tileSize) * static_cast<std::size_t>(tileSize);
    std::vector<std::uint8_t> coverage;
    for (const auto& original : m_tiles) {
        const auto bounds = m_layer->tiles().geometry().tileBounds(original.tile);
        if (!bounds) continue;
        std::span<const std::uint8_t> mask;
        if (m_selectionActive && m_document) {
            coverage.assign(tilePixels, 0);
            const auto width = static_cast<std::size_t>(bounds->right - bounds->left);
            const auto height = static_cast<std::size_t>(bounds->bottom - bounds->top);
            for (std::size_t y = 0; y < height; ++y) {
                for (std::size_t x = 0; x < width; ++x) {
                    coverage[y * static_cast<std::size_t>(tileSize) + x] =
                        m_document->selection().at(bounds->left + static_cast<std::int64_t>(x),
                                                   bounds->top + static_cast<std::int64_t>(y));
                }
            }
            mask = coverage;
        }
        const auto tileHistogram = adjustments::histogramPremultipliedArgb(original.pixels, mask);
        for (std::size_t i = 0; i < total.size(); ++i) total[i] += tileHistogram[i];
    }
    m_histogram = buildHistogram(total);
    emit histogramChanged();
}

void LevelsController::calculateImageHistogram()
{
    adjustments::Histogram total{};
    if (!m_beforeImage.isNull()) {
        const auto bytesPerLine = static_cast<std::size_t>(m_beforeImage.bytesPerLine());
        const auto widthBytes = static_cast<std::size_t>(m_beforeImage.width()) * 4U;
        for (int y = 0; y < m_beforeImage.height(); ++y) {
            const auto* line = m_beforeImage.constScanLine(y);
            const auto lineHistogram = adjustments::histogramStraightRgba8(
                std::span<const std::uint8_t>(line, std::min(widthBytes, bytesPerLine)));
            for (std::size_t i = 0; i < total.size(); ++i) total[i] += lineHistogram[i];
        }
    }
    m_histogram = buildHistogram(total);
    emit histogramChanged();
}

std::uint8_t LevelsController::coverageAt(std::int64_t x, std::int64_t y) const
{
    if (!m_selectionActive) return 255U;
    if (!m_document) return 0U;
    return m_document->selection().at(x, y);
}

bool LevelsController::beginDocument(CanvasDocument* document, Layer* layer, UndoStack* history)
{
    if (m_target != Target::None) cancel();
    clearError();
    m_resultPng.clear();
    m_resultName.clear();
    if (!document || !layer || !history) {
        setError(tr("There is no document, active layer or history to adjust."));
        return false;
    }
    if (history->strokeActive()) {
        setError(tr("Finish the current stroke before opening Levels."));
        return false;
    }
    if (layer->isLocked()) {
        setError(tr("The active layer is locked. Unlock it in the Layers panel first."));
        return false;
    }
    if (!layer->materialTiles().allocatedTilePoints().empty()) {
        setError(tr("This layer contains Realistic painting, which is stored as pigment rather than RGB. "
                    "Levels adjusts raster colour; paint on a raster layer or flatten the material first."));
        return false;
    }
    if (layer->tiles().allocatedTilePoints().empty()) {
        setError(tr("The active layer has no pixels to adjust."));
        return false;
    }

    m_document = document;
    m_layer = layer;
    m_history = history;
    m_selectionActive = document->hasSelection();
    m_wasModified = document->isModified();
    m_targetName = layer->name();
    if (m_targetName.isEmpty()) m_targetName = tr("Active layer");
    m_scopeText = m_selectionActive
        ? tr("Selected area of “%1”. Pixels outside the selection are not changed.").arg(m_targetName)
        : tr("Whole active layer “%1”. Other layers and the background are not changed.").arg(m_targetName);

    m_tiles.clear();
    const auto& geometry = layer->tiles().geometry();
    const auto selectionBounds = m_selectionActive ? document->selection().bounds() : region::PixelBounds{};
    for (const auto tile : layer->tiles().allocatedTilePoints()) {
        const auto bounds = geometry.tileBounds(tile);
        if (!bounds) continue;
        if (m_selectionActive && !selectionBounds.empty()) {
            if (bounds->right <= selectionBounds.x || bounds->left >= selectionBounds.x + selectionBounds.width
                || bounds->bottom <= selectionBounds.y || bounds->top >= selectionBounds.y + selectionBounds.height) {
                continue;
            }
        }
        const auto pixels = layer->tiles().tilePixels(tile);
        if (pixels.empty()) continue;
        TileOriginal original;
        original.tile = tile;
        original.pixels.assign(pixels.begin(), pixels.end());
        m_tiles.push_back(std::move(original));
    }
    if (m_tiles.empty()) {
        m_document = nullptr;
        m_layer = nullptr;
        m_history = nullptr;
        setError(tr("The active layer has no pixels in the selected area."));
        return false;
    }

    m_target = Target::Document;
    m_previewTimer->stop();
    resetToDefaults();
    m_previewTimer->stop();
    calculateDocumentHistogram();
    emit stateChanged();
    emit previewChanged();
    return true;
}

bool LevelsController::beginImage(const QByteArray& png, const QString& name)
{
    if (m_target != Target::None) cancel();
    clearError();
    QImage image = QImage::fromData(png, "PNG");
    if (image.isNull()) {
        setError(tr("The extracted artwork could not be read for Levels."));
        return false;
    }
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        setError(tr("The extracted artwork has no pixels to adjust."));
        return false;
    }
    m_beforeImage = image.copy();
    m_afterImage = m_beforeImage.copy();
    m_targetName = name.isEmpty() ? tr("Brush tip artwork") : name;
    m_scopeText = tr("Adjusts the extracted brush-tip image only. The document and stored brush tips are not changed.");
    m_document = nullptr;
    m_layer = nullptr;
    m_history = nullptr;
    m_selectionActive = false;
    m_wasModified = false;
    m_resultPng.clear();
    m_resultName.clear();
    m_tiles.clear();
    m_target = Target::Image;
    m_previewTimer->stop();
    resetToDefaults();
    m_previewTimer->stop();
    rebuildLut();
    m_afterImage = adjustedImage();
    calculateImageHistogram();
    emit stateChanged();
    emit previewChanged();
    return true;
}

bool LevelsController::beginImageBase64(const QString& pngBase64, const QString& name)
{
    return beginImage(QByteArray::fromBase64(pngBase64.toLatin1()), name);
}

QImage LevelsController::adjustedImage() const
{
    if (m_beforeImage.isNull()) return {};
    if (m_lut.identity) return m_beforeImage.copy();
    QImage output = m_beforeImage.copy();
    const auto width = static_cast<std::size_t>(output.width());
    const auto height = static_cast<std::size_t>(output.height());
    if (height != 0 && width > std::numeric_limits<std::size_t>::max() / height) return {};
    const auto pixelCount = width * height;
    if (output.bytesPerLine() < static_cast<int>(width * 4U)) return {};
    adjustments::applyStraightRgba8(output.bits(), pixelCount, m_lut);
    return output;
}

void LevelsController::rebuildImagePreview()
{
    m_afterImage = adjustedImage();
    emit previewChanged();
}

bool LevelsController::restoreDocumentOriginals()
{
    if (!m_layer || m_tiles.empty()) return true;
    std::vector<SparseTileStore::TileSnapshot> snapshots;
    snapshots.reserve(m_tiles.size());
    for (const auto& original : m_tiles)
        snapshots.push_back(SparseTileStore::TileSnapshot{original.tile, original.pixels});
    const auto result = m_layer->tiles().restoreTiles(snapshots, true);
    if (result != SparseTileStore::WriteResult::Changed) {
        setError(tr("Levels could not restore the original pixels."));
        return false;
    }
    return true;
}

bool LevelsController::writeDocumentFromOriginals()
{
    if (!m_layer) return false;
    auto& store = m_layer->tiles();
    const auto tileSize = store.geometry().tileSize();
    m_lastWriteChanged = false;
    for (const auto& original : m_tiles) {
        const auto bounds = store.geometry().tileBounds(original.tile);
        if (!bounds) continue;
        std::uint32_t* pixels = nullptr;
        std::size_t* nonzero = nullptr;
        bool created = false;
        const auto result = store.beginTileEdit(original.tile, *bounds, pixels, nonzero, created);
        Q_UNUSED(created)
        if (result != SparseTileStore::WriteResult::Changed || !pixels || !nonzero) {
            setError(tr("The layer could not be changed. Nothing was applied; the original pixels are kept."));
            return false;
        }
        const auto width = static_cast<std::size_t>(bounds->right - bounds->left);
        const auto height = static_cast<std::size_t>(bounds->bottom - bounds->top);
        bool changed = false;
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const auto index = y * static_cast<std::size_t>(tileSize) + x;
                if (index >= original.pixels.size()) continue;
                const auto source = original.pixels[index];
                const auto coverage = coverageAt(bounds->left + static_cast<std::int64_t>(x),
                                                 bounds->top + static_cast<std::int64_t>(y));
                const auto adjusted = adjustments::applyPremultipliedArgb(source, m_lut);
                const auto next = blendPremultiplied(source, adjusted, coverage);
                if (pixels[index] != next) changed = true;
                pixels[index] = next;
            }
        }
        std::size_t count = 0;
        const auto tilePixels = static_cast<std::size_t>(tileSize) * static_cast<std::size_t>(tileSize);
        for (std::size_t i = 0; i < tilePixels; ++i) {
            if (pixels[i] != 0U) ++count;
        }
        *nonzero = count;
        if (changed) m_lastWriteChanged = true;
        store.endTileEdit(original.tile, changed);
    }
    return true;
}

void LevelsController::applyDocumentPreview()
{
    if (!m_document || !m_layer) {
        setError(tr("The document was closed while Levels was open."));
        return;
    }
    m_previewTimer->stop();
    if (m_showOriginal || m_lut.identity) {
        restoreDocumentOriginals();
        return;
    }
    if (!writeDocumentFromOriginals()) {
        // A failed preview must never leave half-adjusted pixels behind.
        restoreDocumentOriginals();
    }
}

void LevelsController::applyPreviewNow()
{
    if (m_target == Target::None) return;
    // A previous preview failure is not permanent; retry with the values now
    // shown. A new failure sets the message again.
    clearError();
    if (m_target == Target::Document) applyDocumentPreview();
    else if (m_target == Target::Image) rebuildImagePreview();
}

void LevelsController::previewNow()
{
    m_previewTimer->stop();
    applyPreviewNow();
}

bool LevelsController::apply()
{
    if (m_target == Target::None) {
        setError(tr("Levels is not open."));
        return false;
    }
    m_previewTimer->stop();
    if (m_target == Target::Image) {
        const auto image = adjustedImage();
        if (image.isNull()) {
            setError(tr("The adjusted brush-tip image could not be created."));
            return false;
        }
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            setError(tr("The adjusted brush-tip image could not be encoded."));
            return false;
        }
        m_resultPng = QString::fromLatin1(png.toBase64());
        m_resultName = m_targetName;
        finishSession();
        return true;
    }

    if (!m_document || !m_layer || !m_history) {
        setError(tr("The document or its history is no longer available."));
        return false;
    }
    if (m_history->strokeActive()) {
        setError(tr("Another edit is still using the history. Finish it before applying Levels."));
        return false;
    }

    // Start from the exact captured pixels so history records one clean before
    // state, no matter how many previews ran before Apply.
    if (!restoreDocumentOriginals()) {
        m_document->setModified(m_wasModified);
        return false;
    }
    if (m_lut.identity) {
        m_document->setModified(m_wasModified);
        finishSession();
        return true;
    }
    if (!m_history->beginStroke(m_layer, tr("Levels"))) {
        m_document->setModified(m_wasModified);
        setError(tr("Not enough memory to record Levels in undo history. Nothing was applied."));
        return false;
    }
    if (!writeDocumentFromOriginals()) {
        if (m_history->strokeActive()) m_history->cancelStroke();
        restoreDocumentOriginals();
        m_document->setModified(m_wasModified);
        return false;
    }
    if (!m_history->commitStroke()) {
        if (m_history->strokeActive()) m_history->cancelStroke();
        restoreDocumentOriginals();
        m_document->setModified(m_wasModified);
        setError(tr("Levels could not be committed to history. Nothing was applied."));
        return false;
    }
    m_document->setModified(m_lastWriteChanged ? true : m_wasModified);
    finishSession();
    return true;
}

void LevelsController::cancel()
{
    if (m_target == Target::None) return;
    m_previewTimer->stop();
    if (m_target == Target::Document && m_document && m_layer) {
        if (restoreDocumentOriginals()) m_document->setModified(m_wasModified);
    }
    m_resultPng.clear();
    m_resultName.clear();
    finishSession();
}

void LevelsController::finishSession()
{
    m_previewTimer->stop();
    m_target = Target::None;
    m_tiles.clear();
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_histogram.clear();
    m_document = nullptr;
    m_layer = nullptr;
    m_history = nullptr;
    m_selectionActive = false;
    m_showOriginal = false;
    m_parameters = adjustments::LevelsParameters{};
    rebuildLut();
    const bool hadError = !m_error.isEmpty();
    m_error.clear();
    emit stateChanged();
    emit histogramChanged();
    emit previewChanged();
    emit showOriginalChanged();
    emit parametersChanged();
    if (hadError) emit errorChanged();
}

}  // namespace komapaint
