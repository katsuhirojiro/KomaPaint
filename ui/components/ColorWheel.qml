import QtQuick

// HSV wheel with a square or a triangular saturation/value selector.
// The selected colour and pointer use the same HSV-to-barycentric mapping.
Item {
    id: root
    property real hue: 0
    property real saturation: 0
    property real value: 0
    property bool triangle: false
    signal pickedHue(real hue)
    signal pickedSV(real saturation, real value)
    implicitWidth: 200
    implicitHeight: 200
    readonly property real side: Math.min(width, height)
    readonly property real cx: width / 2
    readonly property real cy: height / 2
    readonly property real radius: side / 2 - 3
    readonly property real ring: Math.max(12, side * 0.075)
    readonly property real inner: radius - ring - 5
    readonly property real halfSquare: inner * 0.69
    readonly property real topY: cy - inner * 0.92
    readonly property real bottomY: cy + inner * 0.55
    readonly property real leftX: cx - inner * 0.86
    readonly property real rightX: cx + inner * 0.86

    Canvas {
        id: ringCanvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const segments = 180
            for (let i = 0; i < segments; ++i) {
                const a = i * 2 * Math.PI / segments
                ctx.beginPath()
                ctx.arc(root.cx, root.cy, root.radius - root.ring / 2, a, a + 2 * Math.PI / segments + 0.01)
                ctx.lineWidth = root.ring
                ctx.strokeStyle = "hsl(" + (i * 360 / segments) + ",100%,50%)"
                ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
    Canvas {
        id: svCanvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (width < 4 || height < 4) return
            if (!root.triangle) {
                const x = root.cx - root.halfSquare, y = root.cy - root.halfSquare
                const w = root.halfSquare * 2
                ctx.fillStyle = "hsl(" + root.hue + ",100%,50%)"
                ctx.fillRect(x, y, w, w)
                const white = ctx.createLinearGradient(x, y, x + w, y)
                white.addColorStop(0, "white"); white.addColorStop(1, "rgba(255,255,255,0)")
                ctx.fillStyle = white; ctx.fillRect(x, y, w, w)
                const black = ctx.createLinearGradient(x, y, x, y + w)
                black.addColorStop(0, "rgba(0,0,0,0)"); black.addColorStop(1, "black")
                ctx.fillStyle = black; ctx.fillRect(x, y, w, w)
            } else {
                // Barycentric interpolation between hue, white and black.
                // A scanline gradient provides the same HSV mapping as picking.
                const rows = Math.max(1, Math.ceil(root.bottomY - root.topY))
                for (let i = 0; i < rows; ++i) {
                    const t = (i + 0.5) / rows
                    const left = root.cx + (root.leftX - root.cx) * t
                    const right = root.cx + (root.rightX - root.cx) * t
                    const hue = Qt.hsva(root.hue / 360, 1, 1, 1)
                    const a = 1 - t
                    const grad = ctx.createLinearGradient(left, 0, Math.max(left + 0.01, right), 0)
                    grad.addColorStop(0, "rgb(" + Math.round(255 * (a * hue.r + t)) + "," + Math.round(255 * (a * hue.g + t)) + "," + Math.round(255 * (a * hue.b + t)) + ")")
                    grad.addColorStop(1, "rgb(" + Math.round(255 * a * hue.r) + "," + Math.round(255 * a * hue.g) + "," + Math.round(255 * a * hue.b) + ")")
                    ctx.fillStyle = grad
                    ctx.fillRect(left, root.topY + i, right - left, 1.5)
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onHueChanged() { svCanvas.requestPaint() }
            function onTriangleChanged() { svCanvas.requestPaint() }
        }
    }
    // Cursors remain legible on all colours.
    Rectangle {
        readonly property real angle: root.hue * Math.PI / 180
        x: root.cx + (root.radius - root.ring / 2) * Math.cos(angle) - width / 2
        y: root.cy + (root.radius - root.ring / 2) * Math.sin(angle) - height / 2
        width: 10; height: 10; radius: 5; color: "transparent"; border.width: 2; border.color: "white"
        Rectangle { anchors.fill: parent; anchors.margins: -2; radius: width / 2; color: "transparent"; border.width: 1; border.color: "black" }
    }
    Rectangle {
        readonly property real a: root.value * root.saturation
        readonly property real b: root.value * (1 - root.saturation)
        readonly property real c: 1 - root.value
        x: (root.triangle ? a * root.cx + b * root.leftX + c * root.rightX : root.cx - root.halfSquare + root.saturation * 2 * root.halfSquare) - width / 2
        y: (root.triangle ? a * root.topY + (b + c) * root.bottomY : root.cy - root.halfSquare + (1 - root.value) * 2 * root.halfSquare) - height / 2
        width: 10; height: 10; radius: 5; color: "transparent"; border.width: 2; border.color: "white"
        Rectangle { anchors.fill: parent; anchors.margins: -2; radius: width / 2; color: "transparent"; border.width: 1; border.color: "black" }
    }
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        property string zone: ""
        function pick(x, y) {
            if (zone === "hue") {
                root.pickedHue((Math.atan2(y - root.cy, x - root.cx) * 180 / Math.PI + 360) % 360)
            } else if (zone === "sv") {
                if (!root.triangle) {
                    root.pickedSV(Math.max(0, Math.min(1, (x - root.cx + root.halfSquare) / (2 * root.halfSquare))),
                                  Math.max(0, Math.min(1, 1 - (y - root.cy + root.halfSquare) / (2 * root.halfSquare))))
                } else {
                    const a = Math.max(0, Math.min(1, (root.bottomY - y) / (root.bottomY - root.topY)))
                    const span = (root.rightX - root.leftX) * (1 - a)
                    const l = root.cx * a + root.leftX * (1 - a)
                    const c = (1 - a) * Math.max(0, Math.min(1, (x - l) / Math.max(0.0001, span)))
                    const v = 1 - c
                    root.pickedSV(v > 0.0001 ? a / v : 0, v)
                }
            }
        }
        onPressed: function(mouse) {
            const dx = mouse.x - root.cx, dy = mouse.y - root.cy
            const d = Math.sqrt(dx * dx + dy * dy)
            if (d >= root.radius - root.ring - 4 && d <= root.radius + 4) zone = "hue"
            else if (!root.triangle && Math.abs(dx) <= root.halfSquare && Math.abs(dy) <= root.halfSquare) zone = "sv"
            else if (root.triangle && mouse.y >= root.topY && mouse.y <= root.bottomY &&
                     Math.abs(dx) <= (mouse.y - root.topY) / (root.bottomY - root.topY) * (root.rightX - root.cx)) zone = "sv"
            else zone = ""
            pick(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) { if (pressed) pick(mouse.x, mouse.y) }
    }
}
