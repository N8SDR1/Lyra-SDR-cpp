// Porthole — port of paintPorthole() in the browser mock.
// Riveted ring, bright meniscus, moving caustics, glass sheen, amber peak tick.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: port
    anchors.fill: parent

    function mix(a, b, t) {
        function comp(hex, i) { return parseInt(hex.substr(i, 2), 16) }
        var out = "#"
        for (var i = 1; i <= 5; i += 2) {
            var v = Math.round(comp(a, i) + (comp(b, i) - comp(a, i)) * t)
            var s = v.toString(16)
            out += s.length < 2 ? "0" + s : s
        }
        return out
    }

    function faceBg(ctx, w, h) {
        var g = ctx.createLinearGradient(0, 0, 0, h)
        g.addColorStop(0, "#18222c")
        g.addColorStop(0.45, "#101820")
        g.addColorStop(1, "#0b1117")
        ctx.fillStyle = g
        ctx.fillRect(0, 0, w, h)
        var v = ctx.createRadialGradient(w * 0.5, h * 0.35, w * 0.05, w * 0.5, h * 0.55, w * 0.72)
        v.addColorStop(0, "rgba(80,120,140,0.09)")
        v.addColorStop(1, "rgba(0,0,0,0.40)")
        ctx.fillStyle = v
        ctx.fillRect(0, 0, w, h)
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var t = Date.now() / 1000
            port.faceBg(ctx, w, h)
            var cx = w * 0.36, cy = h * 0.52, R = Math.min(w, h) * 0.40
            var ring = ctx.createRadialGradient(cx - R * 0.3, cy - R * 0.35, R * 0.2, cx, cy, R * 1.25)
            ring.addColorStop(0, "#6a7c88")
            ring.addColorStop(0.45, "#24303a")
            ring.addColorStop(1, "#0c1216")
            ctx.beginPath(); ctx.arc(cx, cy, R * 1.08, 0, 7); ctx.fillStyle = ring; ctx.fill()
            for (var i = 0; i < 10; ++i) {
                var a = -Math.PI / 2 + i / 10 * Math.PI * 2
                var rx = cx + Math.cos(a) * R * 0.98, ry = cy + Math.sin(a) * R * 0.98
                ctx.beginPath(); ctx.arc(rx, ry, Math.max(2.2, R * 0.045), 0, 7)
                ctx.fillStyle = "#10161c"; ctx.fill()
                ctx.beginPath(); ctx.arc(rx - 0.6, ry - 0.6, Math.max(0.8, R * 0.015), 0, 7)
                ctx.fillStyle = "rgba(255,255,255,0.45)"; ctx.fill()
            }
            ctx.save()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.82, 0, 7); ctx.clip()
            ctx.fillStyle = "#061016"
            ctx.fillRect(cx - R, cy - R, R * 2, R * 2)
            var span = R * 1.64
            var waterTop = cy + R * 0.82 - span * Meter.level
            var col = MeterInk.ink(Meter.level, Meter.normAtS9)
            var wg = ctx.createLinearGradient(0, waterTop, 0, cy + R)
            wg.addColorStop(0, "#f4fdff")
            wg.addColorStop(0.07, col)
            wg.addColorStop(1, port.mix(col, "#021018", 0.72))
            ctx.fillStyle = wg
            ctx.fillRect(cx - R, waterTop, R * 2, cy + R - waterTop + 2)
            ctx.globalAlpha = 0.35; ctx.strokeStyle = "rgba(255,255,255,0.8)"; ctx.lineWidth = 1.2
            for (var k = 0; k < 3; ++k) {
                var yy = waterTop + R * 0.12 + k * R * 0.16
                ctx.beginPath()
                ctx.moveTo(cx - R, yy)
                ctx.bezierCurveTo(cx - R * 0.2, yy + 5 * Math.sin(t * 1.4 + k),
                                  cx + R * 0.3, yy - 4 * Math.sin(t + k), cx + R, yy)
                ctx.stroke()
            }
            ctx.globalAlpha = 0.22
            var refl = ctx.createLinearGradient(0, waterTop - R * 0.35, 0, waterTop)
            refl.addColorStop(0, "rgba(0,0,0,0)")
            refl.addColorStop(1, col)
            ctx.fillStyle = refl
            ctx.fillRect(cx - R, waterTop - R * 0.35, R * 2, R * 0.35)
            ctx.globalAlpha = 1
            var sheen = ctx.createLinearGradient(cx - R, cy - R, cx, cy)
            sheen.addColorStop(0, "rgba(255,255,255,0.28)")
            sheen.addColorStop(0.45, "rgba(255,255,255,0.02)")
            sheen.addColorStop(1, "rgba(255,255,255,0)")
            ctx.fillStyle = sheen
            ctx.fillRect(cx - R, cy - R, R * 1.4, R * 1.1)
            ctx.restore()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.82, 0, 7)
            ctx.strokeStyle = "rgba(255,255,255,0.18)"; ctx.lineWidth = 1.5; ctx.stroke()
            var pk = cy + R * 0.82 - span * Meter.peak
            ctx.strokeStyle = "#ffe7a3"; ctx.lineWidth = 2
            ctx.beginPath(); ctx.moveTo(cx + R * 0.88, pk); ctx.lineTo(cx + R * 1.02, pk); ctx.stroke()
            if (Meter.maxPeakEnabled && Meter.maxPeak > 0.01) {
                // Max-peak hold: the red high-water mark, as on the other faces.
                var mx = cy + R * 0.82 - span * Math.min(1, Meter.maxPeak)
                ctx.strokeStyle = "#ff4136"; ctx.lineWidth = 1.4
                ctx.beginPath(); ctx.moveTo(cx + R * 0.86, mx); ctx.lineTo(cx + R * 1.06, mx); ctx.stroke()
            }
            ctx.textAlign = "left"; ctx.textBaseline = "middle"
            ctx.fillStyle = col
            ctx.font = "bold " + Math.round(Math.max(18, h * 0.15)) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.66, h * 0.40)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.round(Math.max(11, h * 0.075)) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.66, h * 0.54)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.66, h * 0.66)
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.66, h * 0.78)
        }
    }

    // Caustics animation.  The face is only loaded while it is the active
    // style, and Canvas defers requestPaint() until it is rendered.
    Timer { interval: 50; running: true; repeat: true; onTriggered: cv.requestPaint() }
    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
