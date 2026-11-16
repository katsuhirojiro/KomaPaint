import QtQuick
import "../themes"

// A stroke preview drawn from real brush settings.
//
// Presets are recognised by the stroke they make: this stamps dabs along an
// S-curve using the engine's values (size, hardness, opacity, flow, spacing,
// tipStyle, aspectRatio, pressureSize, blendStrength), so flat chisels,
// bristles, graphite pencils, tapered ink pens, airbrushes, and blenders look
// distinct at a glance.
Canvas {
    id: preview

    property int brushSize: 40
    property int hardness: 80
    property int brushOpacity: 100
    property int flow: 100
    property int spacing: 25
    property bool textured: false
    property string tipStyle: textured ? "textured" : "round"
    property int aspectRatio: 100
    property int tipAngle: 0
    property int pressureSize: 0
    property int blendStrength: 0
    property color ink: KP.text
    property real scaleHint: 1.0

    antialiasing: true
    renderTarget: Canvas.Image

    onBrushSizeChanged: requestPaint()
    onHardnessChanged: requestPaint()
    onBrushOpacityChanged: requestPaint()
    onFlowChanged: requestPaint()
    onSpacingChanged: requestPaint()
    onTexturedChanged: requestPaint()
    onTipStyleChanged: requestPaint()
    onAspectRatioChanged: requestPaint()
    onTipAngleChanged: requestPaint()
    onPressureSizeChanged: requestPaint()
    onBlendStrengthChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function random(seed) {
        const x = Math.sin(seed * 12.9898) * 43758.5453
        return x - Math.floor(x)
    }

    function pointAt(t) {
        const p0 = { x: 4, y: preview.height * 0.74 }
        const p1 = { x: preview.width * 0.34, y: preview.height * 0.18 }
        const p2 = { x: preview.width * 0.66, y: preview.height * 0.92 }
        const p3 = { x: preview.width - 4, y: preview.height * 0.34 }
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
        const dabs = Math.max(14, Math.round(width / Math.max(1.4, maxRadius * 0.5 * (spacing / 25))))
        const baseAlpha = Math.max(0.08, Math.min(1, (brushOpacity / 100) * (flow / 100)))
        const softness = 1 - Math.max(0, Math.min(100, hardness)) / 100
        const aspect = Math.max(1.0, Math.min(4.0, aspectRatio / 100))
        const angleRad = tipAngle * Math.PI / 180
        const minTaper = 0.28 + 0.55 * (1 - Math.min(100, Math.max(0, pressureSize)) / 100)

        ctx.fillStyle = String(ink)
        for (let i = 0; i < dabs; ++i) {
            const t = dabs === 1 ? 0.5 : i / (dabs - 1)
            const p = pointAt(t)
            const bell = Math.sin(Math.PI * Math.min(1, Math.max(0, t)))
            const taper = minTaper + (1 - minTaper) * bell
            const blendFade = blendStrength > 0 ? (1 - 0.55 * (blendStrength / 100) * t) : 1.0
            const radius = Math.max(0.7, maxRadius * taper)

            if (tipStyle === "stipple") {
                for (let s = 0; s < 3; ++s) {
                    const rx = (random(i * 11 + s * 3) - 0.5) * radius * 1.6
                    const ry = (random(i * 17 + s * 5) - 0.5) * radius * 1.6
                    ctx.globalAlpha = baseAlpha * 0.55 * (0.5 + 0.5 * random(i + s))
                    ctx.beginPath()
                    ctx.arc(p.x + rx, p.y + ry, Math.max(0.6, radius * 0.28), 0, Math.PI * 2)
                    ctx.fill()
                }
                continue
            }

            if (tipStyle === "bristle") {
                for (let b = -2; b <= 2; ++b) {
                    const offset = (b / 2.2) * radius * 0.72
                    const jitter = (random(i * 7 + b + 9) - 0.5) * radius * 0.22
                    ctx.globalAlpha = baseAlpha * (0.45 + 0.4 * random(i + b * 13))
                    ctx.beginPath()
                    ctx.arc(p.x + jitter, p.y + offset, Math.max(0.6, radius * 0.24), 0, Math.PI * 2)
                    ctx.fill()
                }
                continue
            }

            const jitterX = (textured || tipStyle === "graphite") ? (random(i + 7) - 0.5) * maxRadius * 0.42 : 0
            const jitterY = (textured || tipStyle === "graphite") ? (random(i + 21) - 0.5) * maxRadius * 0.45 : 0
            const grainMul = tipStyle === "graphite" ? (0.45 + 0.55 * random(i * 5 + 3)) : 1.0

            ctx.save()
            ctx.translate(p.x + jitterX, p.y + jitterY)
            if (aspect > 1.05 || angleRad !== 0) {
                ctx.rotate(angleRad)
                ctx.scale(1.0, 1.0 / aspect)
            }
            if (softness > 0.06) {
                ctx.globalAlpha = baseAlpha * 0.26 * softness * blendFade * grainMul
                ctx.beginPath()
                ctx.arc(0, 0, radius * (1 + 0.45 * softness), 0, Math.PI * 2)
                ctx.fill()
            }
            ctx.globalAlpha = baseAlpha * (1 - 0.55 * softness) * blendFade * grainMul
            ctx.beginPath()
            ctx.arc(0, 0, radius * (1 - 0.25 * softness), 0, Math.PI * 2)
            ctx.fill()
            ctx.restore()
        }
    }
}
