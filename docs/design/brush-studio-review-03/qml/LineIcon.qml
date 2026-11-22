import QtQuick
import "."

// 1.5 px monochrome line icon. No filled glyphs, no hue.
Canvas {
    id: icon
    property string kind: "stroke"
    property color tint: Theme.secondary
    property real weight: 1.5
    width: 16
    height: 16
    antialiasing: true
    onKindChanged: requestPaint()
    onTintChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        var w = width, h = height
        ctx.clearRect(0, 0, w, h)
        ctx.strokeStyle = tint
        ctx.fillStyle = tint
        ctx.lineWidth = weight
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        var x = w / 2, y = h / 2
        function line(x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke() }
        function circle(cx, cy, r) { ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke() }
        function dot(cx, cy, r) { ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill() }
        if (kind === "stroke") {
            ctx.beginPath(); ctx.moveTo(2, h - 4); ctx.bezierCurveTo(w * .35, 2, w * .62, h - 2, w - 2, 3); ctx.stroke()
            dot(w - 3, 3, 1.4)
        } else if (kind === "stabilise") {
            line(2, h - 3, w - 3, h - 3)
            ctx.beginPath(); ctx.moveTo(w * .28, h - 3); ctx.bezierCurveTo(w * .22, 4, w * .58, 3, w * .52, h - 3); ctx.stroke()
        } else if (kind === "taper") {
            ctx.beginPath(); ctx.moveTo(2, h - 4); ctx.quadraticCurveTo(w * .48, h - 5, w - 2, h / 2); ctx.quadraticCurveTo(w * .48, 3, 2, 3); ctx.closePath(); ctx.stroke()
        } else if (kind === "tip") {
            circle(x, y, 5.2); dot(x, y, 1.2)
        } else if (kind === "texture") {
            for (var i = 0; i < 3; i++) {
                ctx.beginPath()
                for (var j = 0; j < 7; j++) {
                    var px = 2 + j * 1.9, py = 3 + i * 4.5 + (j % 2 ? 1.5 : 0)
                    if (j === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.stroke()
            }
        } else if (kind === "render") {
            ctx.beginPath(); ctx.arc(x, y, 5.4, 0, Math.PI * 2); ctx.fill()
        } else if (kind === "wet") {
            ctx.beginPath(); ctx.moveTo(x, 2); ctx.bezierCurveTo(w - 3, 6, w - 3, 12, x, 13.4); ctx.bezierCurveTo(3, 12, 3, 6, x, 2); ctx.stroke()
            line(x, 10, x, 6)
        } else if (kind === "colour") {
            circle(x, y, 5.2); dot(x + 2, y - 2, 1.4)
        } else if (kind === "dynamics") {
            ctx.beginPath(); ctx.moveTo(2, h - 3); ctx.bezierCurveTo(w * .3, h - 3, w * .3, 3, w - 2, 3); ctx.stroke()
            dot(2, h - 3, 1.2); dot(w - 2, 3, 1.2)
        } else if (kind === "spray") {
            var pts = [[3, 11], [8, 3], [13, 6], [6, 8], [12, 12], [4, 5]]
            for (var k = 0; k < pts.length; k++) dot(pts[k][0], pts[k][1], 1.25)
        } else if (kind === "modes") {
            circle(4.5, 4.5, 2); circle(11.5, 4.5, 2); circle(4.5, 11.5, 2); circle(11.5, 11.5, 2)
        } else if (kind === "secondary") {
            circle(5, 8, 3.6); circle(11, 8, 3.6)
        } else if (kind === "material") {
            ctx.strokeRect(2.5, 2.5, w - 5, h - 5)
            line(2.5, 8, w - 2.5, 8); line(8, 2.5, 8, h - 2.5)
        } else if (kind === "properties") {
            circle(x, y, 4.5)
            for (var a = 0; a < 6; a++) {
                var angle = a * Math.PI / 3
                line(x + Math.cos(angle) * 5.2, y + Math.sin(angle) * 5.2,
                     x + Math.cos(angle) * 7, y + Math.sin(angle) * 7)
            }
        } else if (kind === "construction") {
            ctx.strokeRect(2, 2, 4.5, 4.5); ctx.strokeRect(9.5, 2, 4.5, 4.5)
            ctx.strokeRect(2, 9.5, 4.5, 4.5); ctx.strokeRect(9.5, 9.5, 4.5, 4.5)
        } else if (kind === "quick") {
            ctx.beginPath(); ctx.moveTo(x + 1, 2.5); ctx.lineTo(x + 1, 12.5); ctx.lineTo(x - 4, 8.2); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(x - 4.5, 2.5); ctx.lineTo(x + 4.5, 2.5); ctx.stroke()
        } else if (kind === "assist") {
            circle(x, y, 5); line(x - 2.5, y + 2.5, x + 2.5, y - 2.5)
        } else if (kind === "undo") {
            ctx.beginPath(); ctx.moveTo(3, 7); ctx.lineTo(6, 4); ctx.moveTo(3, 7); ctx.lineTo(6, 10); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(3, 7); ctx.lineTo(9, 7); ctx.bezierCurveTo(13, 7, 13, 13, 9, 13); ctx.stroke()
        } else if (kind === "redo") {
            ctx.beginPath(); ctx.moveTo(13, 7); ctx.lineTo(10, 4); ctx.moveTo(13, 7); ctx.lineTo(10, 10); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(13, 7); ctx.lineTo(7, 7); ctx.bezierCurveTo(3, 7, 3, 13, 7, 13); ctx.stroke()
        } else if (kind === "pin") {
            ctx.beginPath(); ctx.moveTo(5, 2); ctx.lineTo(11, 2); ctx.lineTo(9.5, 6); ctx.lineTo(11, 8); ctx.lineTo(5, 8); ctx.lineTo(6.5, 6); ctx.closePath(); ctx.stroke()
            line(8, 8, 8, 14)
        } else if (kind === "reset") {
            ctx.beginPath(); ctx.arc(x, y, 4.5, -1.2, 4.2); ctx.stroke()
            line(x + 3.4, 3.4, x + 4.6, 1.4)
        } else if (kind === "close") {
            line(3, 3, w - 3, h - 3); line(w - 3, 3, 3, h - 3)
        } else if (kind === "eye") {
            ctx.beginPath(); ctx.moveTo(2, 8); ctx.quadraticCurveTo(x, 1.5, w - 2, 8); ctx.quadraticCurveTo(x, h - 1.5, 2, 8); ctx.stroke(); dot(x, y, 1.7)
        } else if (kind === "plus") {
            line(x, 3, x, h - 3); line(3, y, w - 3, y)
        } else if (kind === "chevron") {
            ctx.beginPath(); ctx.moveTo(5, 6); ctx.lineTo(8, 9); ctx.lineTo(11, 6); ctx.stroke()
        } else if (kind === "more") {
            dot(4, 8, 1.2); dot(8, 8, 1.2); dot(12, 8, 1.2)
        } else {
            circle(x, y, 5)
        }
    }
}
