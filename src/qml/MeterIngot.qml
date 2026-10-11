// Ingot — one bar of metal. History changes brightness along the bar,
// not the hue. The hue is cyan until the live reading crosses danger.

import QtQuick

Item {
    id: ingot

    function ink(v) {
        var d = Meter.normAtS9
        if (v >= d + 0.16) return "#ff4136"
        if (v >= d) return "#ffab47"
        return "#00e5ff"
    }

    function round(ctx, x, y, w, h, r) {
        var rr = Math.min(r, w / 2, h / 2)
        ctx.beginPath()
        ctx.moveTo(x + rr, y)
        ctx.arcTo(x + w, y, x + w, y + h, rr)
        ctx.arcTo(x + w, y + h, x, y + h, rr)
        ctx.arcTo(x, y + h, x, y, rr)
        ctx.arcTo(x, y, x + w, y, rr)
        ctx.closePath()
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var col = ingot.ink(Meter.level)
            ctx.fillStyle = col
            ctx.font = "600 " + Math.max(16, h * 0.14) + "px Consolas, Menlo, monospace"
            ctx.textAlign = "left"
            ctx.textBaseline = "alphabetic"
            ctx.fillText(Meter.text, w * 0.06, h * 0.22)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.max(10, h * 0.07) + "px Consolas, Menlo, monospace"
            var sub = Meter.dbmText
            if (Meter.snrText.length) sub += (sub.length ? "   " : "") + Meter.snrText
            if (sub.length) ctx.fillText(sub, w * 0.06, h * 0.34)
            var x = w * 0.06, y = h * 0.46, bw = w * 0.88, bh = Math.max(22, h * 0.28)
            ctx.fillStyle = "rgba(0,0,0,0.35)"
            ingot.round(ctx, x + 1, y + 5, bw, bh, 4); ctx.fill()
            ctx.save()
            ingot.round(ctx, x, y, bw, bh, 4); ctx.clip()
            var hist = Meter.history
            var n = hist.length
            ctx.fillStyle = col
            for (var i = 0; i < n; ++i) {
                var v = Math.max(0, Math.min(1, hist[i]))
                ctx.globalAlpha = 0.18 + 0.82 * v
                ctx.fillRect(x + bw * i / n, y, bw / n + 1, bh)
            }
            ctx.globalAlpha = 1
            var bevel = ctx.createLinearGradient(0, y, 0, y + bh)
            bevel.addColorStop(0, "rgba(255,255,255,0.40)")
            bevel.addColorStop(0.18, "rgba(255,255,255,0.05)")
            bevel.addColorStop(0.72, "rgba(0,0,0,0)")
            bevel.addColorStop(1, "rgba(0,0,0,0.40)")
            ctx.fillStyle = bevel
            ctx.fillRect(x, y, bw, bh)
            ctx.restore()
            ctx.strokeStyle = "rgba(255,255,255,0.2)"
            ingot.round(ctx, x, y, bw, bh, 4); ctx.stroke()
            if (Meter.peak > 0.01) {
                var px = x + bw * Meter.peak
                ctx.fillStyle = "#ffe7a3"
                ctx.beginPath()
                ctx.moveTo(px, y - 1); ctx.lineTo(px - 4, y - 8); ctx.lineTo(px + 4, y - 8)
                ctx.closePath(); ctx.fill()
            }
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
