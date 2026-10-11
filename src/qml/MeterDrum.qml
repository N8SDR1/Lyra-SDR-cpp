// Drum — port of paintDrum() in the browser mock.
// Horizontal scale behind a brushed bezel. Red index stays put; the
// peak is a pale tick on the scale.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: drum
    anchors.fill: parent

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
            drum.faceBg(ctx, w, h)
            var x = w * 0.08, y = h * 0.22, bw = w * 0.84, bh = h * 0.46
            var housing = ctx.createLinearGradient(x, y, x, y + bh)
            housing.addColorStop(0, "#0a0e12")
            housing.addColorStop(0.15, "#1c262f")
            housing.addColorStop(0.5, "#10161c")
            housing.addColorStop(1, "#070b0e")
            drum.round(ctx, x, y, bw, bh, 8); ctx.fillStyle = housing; ctx.fill()
            ctx.save()
            drum.round(ctx, x + 3, y + 3, bw - 6, bh - 6, 6); ctx.clip()
            var b
            for (b = 0; b < bh; b += 2) {
                ctx.fillStyle = (b % 4 === 0) ? "rgba(255,255,255,0.015)" : "rgba(0,0,0,0.15)"
                ctx.fillRect(x, y + b, bw, 1)
            }
            // Scale travel: one full scale is 1.6 window widths of drum.
            var travel = bw * 1.6
            var ticks = Meter.tickMarks()
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            for (var i = 0; i < ticks.length; ++i) {
                var tk = ticks[i]
                var dx = (tk.pos - Meter.level) * travel
                var px = x + bw / 2 + dx
                if (px < x - 30 || px > x + bw + 30) continue
                var near = 1 - Math.min(1, Math.abs(dx) / (bw * 0.34))
                ctx.globalAlpha = 0.15 + 0.85 * near * near
                ctx.fillStyle = near > 0.75 ? "#f4fbff" : "#9aa8b4"
                var pxSize = Math.max(12, h * 0.06 + near * h * 0.1)
                ctx.font = (near > 0.7 ? "bold " : "") + Math.round(pxSize) + "px Consolas, Menlo, monospace"
                ctx.fillText(tk.label, px, y + bh / 2)
            }
            ctx.globalAlpha = 1
            ctx.fillStyle = "#ff4136"
            ctx.fillRect(x + bw / 2 - 0.75, y + 6, 1.5, bh - 12)
            ctx.beginPath()
            ctx.moveTo(x + bw / 2, y + 8)
            ctx.lineTo(x + bw / 2 - 5, y)
            ctx.lineTo(x + bw / 2 + 5, y)
            ctx.fill()
            var top = ctx.createLinearGradient(0, y, 0, y + bh * 0.28)
            top.addColorStop(0, "rgba(8,12,16,0.85)"); top.addColorStop(1, "rgba(8,12,16,0)")
            ctx.fillStyle = top; ctx.fillRect(x, y, bw, bh * 0.28)
            var bot = ctx.createLinearGradient(0, y + bh * 0.72, 0, y + bh)
            bot.addColorStop(0, "rgba(8,12,16,0)"); bot.addColorStop(1, "rgba(8,12,16,0.85)")
            ctx.fillStyle = bot; ctx.fillRect(x, y + bh * 0.72, bw, bh * 0.28)
            // Peak: a pale tick on the drum scale itself, riding with the
            // figures (it sits right of the index while the level is below it).
            var pkx = x + bw / 2 + (Meter.peak - Meter.level) * travel
            if (Meter.peak > 0.01 && pkx >= x && pkx <= x + bw) {
                ctx.fillStyle = "#ffe7a3"
                ctx.fillRect(pkx - 1, y + 4, 2, bh * 0.22)
                ctx.fillRect(pkx - 1, y + bh - 4 - bh * 0.22, 2, bh * 0.22)
            }
            ctx.restore()
            ctx.strokeStyle = "rgba(255,255,255,0.12)"; ctx.lineWidth = 1
            drum.round(ctx, x, y, bw, bh, 8); ctx.stroke()
            var sheen = ctx.createLinearGradient(x, y, x, y + bh * 0.35)
            sheen.addColorStop(0, "rgba(255,255,255,0.14)")
            sheen.addColorStop(0.45, "rgba(255,255,255,0.03)")
            sheen.addColorStop(1, "rgba(255,255,255,0)")
            ctx.fillStyle = sheen
            ctx.fillRect(x, y, bw, bh * 0.35)
            ctx.textAlign = "left"
            ctx.fillStyle = MeterInk.ink(Meter.level, Meter.normAtS9)
            ctx.font = "bold " + Math.round(Math.max(16, h * 0.12)) + "px Consolas, Menlo, monospace"
            ctx.textBaseline = "alphabetic"
            ctx.fillText(Meter.text, x, h * 0.16)
            // Side readouts start after the reading, however long it is.
            var sideX = Math.max(x + w * 0.16, x + ctx.measureText(Meter.text).width + w * 0.03)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.round(Math.max(11, h * 0.075)) + "px Consolas, Menlo, monospace"
            var side = Meter.dbmText
            if (Meter.snrText.length)
                side = (side.length ? side + "     " : "") + Meter.snrText
            if (side.length) ctx.fillText(side, sideX, h * 0.155)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
