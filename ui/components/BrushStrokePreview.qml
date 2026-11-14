import QtQuick
import "../themes"

// A stroke preview drawn from real brush settings.
//
// Presets are recognised by the stroke they make, not by an icon: this stamps
// dabs along an S-curve using the same values the engine uses (size, hardness,
// opacity, flow, spacing, textured), so a soft 40 px brush and a spiky textured
// one look different at a glance. Nothing here is painted from a bitmap and no
// value is invented — change a slider and the preview moves with it.
//
// Dabs are drawn as plain circles plus one wider halo when the tip is soft,
// which keeps repaints cheap enough for a list of presets on integrated
// graphics.
Canvas {
    id: preview

    // `size` is a final Canvas property (the pixel size of the surface), so the
    // brush diameter this preview paints lives under its own name.
    property int brushSize: 40
    property int hardness: 80
    // `opacity` is final on Item, so the brush's own opacity is named apart.
    property int brushOpacity: 100
    property int flow: 100
    property int spacing: 25
    property bool textured: false
    property color ink: KP.text
    // Compresses the drawing into the top-left; used by compact rows.
    property real scaleHint: 1.0

    antialiasing: true
    // Painted with QPainter rather than an FBO, so it also works when the scene
    // graph falls back to the software backend on old hardware.
    renderTarget: Canvas.Image

    // Repaint whenever a setting changes.
    onHardnessChanged: requestPaint()
    onBrushOpacityChanged: requestPaint()
    onFlowChanged: requestPaint()
    onSpacingChanged: requestPaint()
    onTexturedChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    // Deterministic pseudo-random: the same preset always draws the same
    // stroke, so the list does not shimmer when it is scrolled.
    function random(seed) {
        const x = Math.sin(seed * 12.9898) * 43758.5453
        return x - Math.floor(x)
    }

    function pointAt(t) {
        // A gentle S from the lower left to the upper right.
        const p0 = { x: 3, y: preview.height * 0.74 }
        const p1 = { x: preview.width * 0.34, y: preview.height * 0.18 }
        const p2 = { x: preview.width * 0.66, y: preview.height * 0.92 }
        const p3 = { x: preview.width - 3, y: preview.height * 0.34 }
        const u = 1 - t
        return {
            x: u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
            y: u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y
        }
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        if (width <= 4 || height <= 4) return

        const maxRadius = Math.max(1.2, Math.min(height * 0.42, 1.2 + Math.pow(brushSize / 256, 0.5) * height * 0.40)) * scaleHint
        const dabs = Math.max(12, Math.round(width / Math.max(1.5, maxRadius * 0.55 * (spacing / 25))))
        const baseAlpha = Math.max(0.05, Math.min(1, (brushOpacity / 100) * (flow / 100)))
        const softness = 1 - Math.max(0, Math.min(100, hardness)) / 100

        ctx.fillStyle = String(ink)
        for (let i = 0; i < dabs; ++i) {
            if (preview.textured && (i % 3 === 1)) continue
            const t = dabs === 1 ? 0.5 : i / (dabs - 1)
            const p = pointAt(t)
            const jitter = preview.textured ? (random(i + 7) - 0.5) * maxRadius * 0.55 : 0
            const x = p.x + jitter
            const y = p.y + (preview.textured ? (random(i + 21) - 0.5) * maxRadius * 0.7 : 0)
            // Taper in and out, the way a stroke starts and lifts.
            const taper = 0.28 + 0.72 * Math.sin(Math.PI * Math.min(1, Math.max(0, t)))
            const radius = Math.max(0.7, maxRadius * taper)

            if (softness > 0.06) {
                ctx.globalAlpha = baseAlpha * 0.30 * softness
                ctx.beginPath()
                ctx.arc(x, y, radius * (1 + 0.5 * softness), 0, Math.PI * 2)
                ctx.fill()
            }
            ctx.globalAlpha = baseAlpha * (preview.textured ? 0.75 : 1)
            ctx.beginPath()
            ctx.arc(x, y, radius, 0, Math.PI * 2)
            ctx.fill()
        }
        ctx.globalAlpha = 1
    }
}
