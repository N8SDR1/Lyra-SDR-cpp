// Drum — a cylinder behind a bezel. The scale slides under a fixed
// red index. The center numeral is the live reading, in one hue.

import QtQuick

Item {
    id: drum

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
            var x = w * 0.18, y = h * 0.12, bw = w * 0.64, bh = h * 0.76
            var g = ctx.createLinearGradient(x, 0, x + bw, 0)
            g.addColorStop(0, "#070b0e")
            g.addColorStop(0.18, "#2c3842")
            g.addColorStop(0.5, "#141c22")
            g.addColorStop(0.82, "#2c3842")
            g.addColorStop(1, "#070b0e")
            drum.round(ctx, x, y, bw, bh, 8)
            ctx.fillStyle = g; ctx.fill()
            ctx.save()
            drum.round(ctx, x + 3, y + 3, bw - 6, bh - 6, 6)
            ctx.clip()
            var ticks = Meter.tickMarks()
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            for (var i = 0; i < ticks.length; ++i) {
                var t = ticks[i]
                var dy = (Meter.level - t.pos) * bh * 1.35
                if (Math.abs(dy) > bh * 0.55) continue
                var near = 1 - Math.min(1, Math.abs(dy) / (bh * 0.48))
                ctx.globalAlpha = 0.18 + 0.55 * near * near
                ctx.fillStyle = "#9aa8b4"
                ctx.font = Math.round(h * 0.07 + near * h * 0.04) + "px Consolas, Menlo, monospace"
                ctx.fillText(t.label, x + bw / 2, y + bh / 2 + dy)
            }
            ctx.globalAlpha = 1
            ctx.fillStyle = drum.ink(Meter.level)
            ctx.font = "600 " + Math.round(Math.max(18, h * 0.16)) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, x + bw / 2, y + bh / 2)
            ctx.strokeStyle = "#ff4136"
            ctx.lineWidth = 1.25
            ctx.beginPath()
            ctx.moveTo(x + 8, y + bh / 2)
            ctx.lineTo(x + bw - 8, y + bh / 2)
            ctx.stroke()
            var vg = ctx.createLinearGradient(0, y, 0, y + bh)
            vg.addColorStop(0, "rgba(0,0,0,0.72)")
            vg.addColorStop(0.22, "rgba(0,0,0,0)")
            vg.addColorStop(0.78, "rgba(0,0,0,0)")
            vg.addColorStop(1, "rgba(0,0,0,0.72)")
            ctx.fillStyle = vg
            ctx.fillRect(x, y, bw, bh)
            ctx.restore()
            ctx.strokeStyle = "rgba(255,255,255,0.14)"
            drum.round(ctx, x, y, bw, bh, 8)
            ctx.stroke()
            if (Meter.peak > 0.01) {
                ctx.fillStyle = "#ffe7a3"
                ctx.font = Math.max(10, h * 0.07) + "px Consolas, Menlo, monospace"
                ctx.textAlign = "left"
                ctx.textBaseline = "alphabetic"
                ctx.fillText("pk", x, h * 0.10)
            }
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
