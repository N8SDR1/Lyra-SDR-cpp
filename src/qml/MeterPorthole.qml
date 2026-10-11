// Porthole — a riveted glass port. The waterline is the level.
// The water is one hue: cyan, amber past the danger point, red above that.

import QtQuick

Item {
    id: port

    function ink(v) {
        var d = Meter.normAtS9
        if (v >= d + 0.16) return "#ff4136"
        if (v >= d) return "#ffab47"
        return "#00e5ff"
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var col = port.ink(Meter.level)
            var cx = w * 0.34, cy = h * 0.56
            var R = Math.min(w * 0.30, h * 0.40)
            var ring = ctx.createRadialGradient(cx - R * 0.3, cy - R * 0.3, R * 0.2, cx, cy, R * 1.2)
            ring.addColorStop(0, "#6a7c88")
            ring.addColorStop(0.5, "#24303a")
            ring.addColorStop(1, "#0c1216")
            ctx.beginPath(); ctx.arc(cx, cy, R * 1.12, 0, 7)
            ctx.fillStyle = ring; ctx.fill()
            for (var i = 0; i < 10; ++i) {
                var a = -Math.PI / 2 + i / 10 * Math.PI * 2
                var rx = cx + Math.cos(a) * R, ry = cy + Math.sin(a) * R
                ctx.beginPath(); ctx.arc(rx, ry, Math.max(1.8, R * 0.055), 0, 7)
                ctx.fillStyle = "#10161c"; ctx.fill()
                ctx.beginPath(); ctx.arc(rx - 0.5, ry - 0.5, Math.max(0.6, R * 0.016), 0, 7)
                ctx.fillStyle = "rgba(255,255,255,0.45)"; ctx.fill()
            }
            ctx.save()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.84, 0, 7); ctx.clip()
            ctx.fillStyle = "#061016"
            ctx.fillRect(cx - R, cy - R, R * 2, R * 2)
            var span = R * 1.68
            var water = cy + R * 0.84 - span * Meter.level
            var wg = ctx.createLinearGradient(0, water, 0, cy + R)
            wg.addColorStop(0, "#f4fdff")
            wg.addColorStop(0.08, col)
            wg.addColorStop(1, "#021018")
            ctx.fillStyle = wg
            ctx.fillRect(cx - R, water, R * 2, cy + R - water + 2)
            ctx.globalAlpha = 0.28
            ctx.strokeStyle = "rgba(255,255,255,0.85)"
            ctx.lineWidth = 1
            for (var k = 0; k < 3; ++k) {
                var yy = water + R * (0.16 + k * 0.18)
                ctx.beginPath()
                ctx.moveTo(cx - R, yy)
                ctx.bezierCurveTo(cx - R * 0.3, yy + 3, cx + R * 0.3, yy - 3, cx + R, yy)
                ctx.stroke()
            }
            ctx.globalAlpha = 0.20
            var refl = ctx.createLinearGradient(0, water - R * 0.28, 0, water)
            refl.addColorStop(0, "rgba(0,0,0,0)")
            refl.addColorStop(1, col)
            ctx.fillStyle = refl
            ctx.fillRect(cx - R, water - R * 0.28, R * 2, R * 0.28)
            ctx.globalAlpha = 1
            var sheen = ctx.createLinearGradient(cx - R, cy - R, cx, cy)
            sheen.addColorStop(0, "rgba(255,255,255,0.26)")
            sheen.addColorStop(0.5, "rgba(255,255,255,0)")
            ctx.fillStyle = sheen
            ctx.fillRect(cx - R, cy - R, R * 1.5, R)
            ctx.restore()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.84, 0, 7)
            ctx.strokeStyle = "rgba(255,255,255,0.16)"; ctx.lineWidth = 1.2; ctx.stroke()
            if (Meter.peak > 0.01) {
                var pk = cy + R * 0.84 - span * Meter.peak
                ctx.strokeStyle = "#ffe7a3"; ctx.lineWidth = 2
                ctx.beginPath(); ctx.moveTo(cx + R * 0.9, pk); ctx.lineTo(cx + R * 1.08, pk); ctx.stroke()
            }
            ctx.textAlign = "left"
            ctx.textBaseline = "alphabetic"
            ctx.fillStyle = col
            ctx.font = "600 " + Math.max(16, h * 0.16) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.64, h * 0.42)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.max(10, h * 0.075) + "px Consolas, Menlo, monospace"
            var y = h * 0.56
            if (Meter.dbmText.length) { ctx.fillText(Meter.dbmText, w * 0.64, y); y += h * 0.12 }
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) { ctx.fillText(Meter.snrText, w * 0.64, y); y += h * 0.11 }
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.64, y)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
