"""QQuickPaintedItem that paints the review's pad and tip room."""
from math import cos, sin

import render
from assets import ARGB, qimage
from PySide6.QtCore import Property, QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QImage, QPainter, QPen
from PySide6.QtQuick import QQuickPaintedItem


class Artwork(QQuickPaintedItem):
    appearanceChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._kind = "pad"
        self._source = ""
        self._cache = None
        self._cache_key = None
        self._active_layer = None
        self._active_key = None
        self._active_rendered = 0
        self.setAntialiasing(True)
        studio = render.STUDIO
        if studio is not None:
            studio.changed.connect(self.update)
            studio.padChanged.connect(self.update)
            studio.tipChanged.connect(self.update)
            studio.editStateChanged.connect(self.update)

    @Property(str, notify=appearanceChanged)
    def kind(self):
        return self._kind

    @kind.setter
    def kind(self, value):
        self._kind = value
        self.appearanceChanged.emit()

    @Property(str, notify=appearanceChanged)
    def source(self):
        return self._source

    @source.setter
    def source(self, value):
        self._source = value
        self.appearanceChanged.emit()

    def _tip(self, painter, w, h):
        studio = render.STUDIO
        painter.fillRect(0, 0, w, h, QColor("#E9E6DE"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(QPen(QColor(0, 0, 0, 20), 1))
        for i in range(1, 5):
            painter.drawLine(QPointF(w * i / 5, 10), QPointF(w * i / 5, h - 10))
            painter.drawLine(QPointF(10, h * i / 5), QPointF(w - 10, h * i / 5))
        painter.setPen(QPen(QColor("#2B2927"), studio.tipSize, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        for stroke in studio.tipStrokes:
            points = [QPointF(x * w, y * h) for x, y, *_ in stroke["points"]]
            if len(points) == 1:
                painter.drawPoint(points[0])
            elif len(points) > 1:
                for a, b in zip(points, points[1:]):
                    painter.drawLine(a, b)
        painter.setPen(QPen(QColor(0, 0, 0, 28), 1))
        painter.drawRect(QRectF(0.5, 0.5, w - 1, h - 1))

    def _pad_static(self, w, h, editing=False):
        studio = render.STUDIO
        doc = studio.doc
        image = render.allocate(w, h)
        painter = QPainter(image)
        try:
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            render.paint_surface(painter, doc, w, h, studio.assets)
            if studio.testMode == "Mixing":
                render.paint_mixing_base(painter, w, h)
            render.paint_guides(painter, w, h)
            for stroke in render.sample_strokes(studio.testMode.lower(), doc, w, h):
                if editing:
                    render.render_stroke_proxy(painter, doc, stroke, w, h)
                else:
                    render.render_stroke(painter, doc, stroke, w, h, studio.assets, image)
            if studio.compare and studio.hasRetained:
                ghost = render.allocate(w, h)
                ghost_painter = QPainter(ghost)
                try:
                    ghost_painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                    for stroke in studio.retainedStrokes:
                        render.render_stroke(ghost_painter, studio.retainedDocument, stroke,
                                             w, h, studio.assets, image)
                finally:
                    ghost_painter.end()
                painter.save()
                painter.setOpacity(0.30)
                painter.drawImage(0, 0, ghost)
                painter.restore()
            for stroke in studio.strokes:
                if editing:
                    render.render_stroke_proxy(painter, doc, stroke, w, h)
                else:
                    render.render_stroke(painter, doc, stroke, w, h, studio.assets, image)
            render.paint_labels(painter, studio.testMode.lower(), w, h)
        finally:
            painter.end()
        return image

    def paint(self, painter):
        w, h = int(self.width()), int(self.height())
        if min(w, h) < 8:
            return
        studio = render.STUDIO
        if studio is None:
            return
        if self._kind == "tip":
            self._tip(painter, w, h)
            return
        if self._kind != "pad":
            return
        editing = bool(studio.editing)
        key = (w, h, studio.revision, len(studio.strokes), studio.testMode,
               studio.compare, studio.hasRetained, editing,
               tuple(stroke.get("seed") for stroke in studio.strokes))
        if key != self._cache_key:
            self._cache = self._pad_static(w, h, editing)
            self._cache_key = key
        painter.drawImage(0, 0, self._cache)
        active = studio.activeStroke
        if active:
            # Draw the live path as one smooth pressure-shaped body.  Stamping
            # every mark of an in-progress stroke either costs seconds per
            # frame or shows its individual marks; the finished stroke is
            # stamped at full quality when the pointer is released.
            stroke = active[0]
            layer_key = (w, h, studio.revision, stroke.get("seed"))
            if self._active_layer is None or self._active_key != layer_key:
                self._active_layer = render.allocate(w, h)
                self._active_key = layer_key
            else:
                self._active_layer.fill(Qt.GlobalColor.transparent)
            layer_painter = QPainter(self._active_layer)
            try:
                layer_painter.setRenderHint(QPainter.RenderHint.Antialiasing)
                render.render_stroke_body(layer_painter, studio.doc, stroke, w, h, studio.assets)
            finally:
                layer_painter.end()
            painter.drawImage(0, 0, self._active_layer)
        else:
            self._active_layer = None
            self._active_key = None
        # Honest frame label and comparison marker, drawn in the pad rather
        # than as a fake UI card.
        painter.save()
        painter.setFont(QFont("Inter", 8))
        painter.setPen(QColor(0, 0, 0, 86))
        label = "SAMPLE INPUT · " + studio.testMode.upper()
        painter.drawText(QRectF(20, 18, 300, 16), Qt.AlignmentFlag.AlignLeft, label)
        if studio.compare and studio.hasRetained:
            painter.setPen(QColor(0, 0, 0, 130))
            painter.drawText(QRectF(w - 190, 18, 170, 16), Qt.AlignmentFlag.AlignRight,
                             "GHOST · RETAINED INPUT")
        painter.restore()
