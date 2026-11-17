#pragma once

#include "../region/RegionEngine.h"
#include "../region/SelectionMask.h"

#include <QColor>
#include <QPointF>
#include <QString>

namespace komapaint {

class BrushSettings;
class CanvasDocument;
class Layer;
class UndoStack;

namespace material {
class MaterialEngine;
struct BrushDefinition;
struct PaintBatch;
}

// Per-pixel mask context passed into BrushDefinition / material::BrushDefinition.
struct PixelMaskContext {
    const region::SelectionMask* selection = nullptr;
    bool hasSelection = false;
    const region::RegionResult* protectedRegion = nullptr;
    bool protectInterior = false;
};

double evaluatePixelMask(void* ctx, std::int64_t x, std::int64_t y) noexcept;

// Pure document/undo execution for non-stroke and intelligent region tools
// (Conventional Fill, Smart Fill, Magic Wand, Selection operations, Move/Transform,
// Gradient, Geometric Shapes, Text, Boundary-Aware Eraser sampling).
class CanvasToolExecutor {
public:
    static region::ReferenceSet buildReferenceSet(CanvasDocument* doc, int referenceMode);

    static bool applyRegionFill(CanvasDocument* doc, UndoStack* history,
                                BrushSettings* brush, const QColor& color,
                                const region::RegionResult& result,
                                QString* outError = nullptr);

    static bool applyGradient(CanvasDocument* doc, UndoStack* history,
                              BrushSettings* brush, const QColor& color,
                              qreal x0, qreal y0, qreal x1, qreal y1,
                              const QString& gradientType,
                              QString* outError = nullptr);

    static bool applyShape(CanvasDocument* doc, UndoStack* history,
                           BrushSettings* brush, const QColor& color,
                           const QString& shapeType, const QString& shapeMode,
                           int strokeWidth,
                           qreal x0, qreal y0, qreal x1, qreal y1,
                           qreal ctrlX, qreal ctrlY,
                           QString* outError = nullptr);

    static bool applyText(CanvasDocument* doc, UndoStack* history,
                          BrushSettings* brush, const QColor& color,
                          qreal docX, qreal docY,
                          const QString& text, int fontSize,
                          QString* outError = nullptr);

    static bool clearSelectionContent(CanvasDocument* doc, UndoStack* history,
                                      BrushSettings* brush,
                                      QString* outError = nullptr);

    static bool moveOrTransform(CanvasDocument* doc, UndoStack* history,
                                BrushSettings* brush,
                                qreal dx, qreal dy,
                                qreal scaleX, qreal scaleY, qreal rotationDeg,
                                QString* outError = nullptr);
};

} // namespace komapaint
