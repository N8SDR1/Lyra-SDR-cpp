// Fader — a console cap in a recessed slot. The slot under the cap
// is one hue, not a spectrum. Amber mark = peak, red mark = max-hold.

import QtQuick

Item {
    id: fader

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
            var col = fader.ink(Meter.level)
            var top = h * 0.16, bot = h * 0.90
            var slotX = w * 0.40, slotW = Math.max(8, w * 0.045)
            fader.round(ctx, slotX - 3, top - 2, slotW + 6, bot - top + 4, 5)
            ctx.fillStyle = "#05080b"; ctx.fill()
            var capY = bot - (bot - top) * Meter.level
            ctx.save()
            fader.round(ctx, slotX, top, slotW, bot - top, slotW / 2)
            ctx.clip()
            ctx.fillStyle = col
            ctx.globalAlpha = 0.9
            ctx.fillRect(slotX, capY, slotW, bot - capY)
            ctx.restore()
            ctx.globalAlpha = 1
            var ticks = Meter.tickMarks()
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            ctx.font = Math.max(9, h * 0.065) + "px Consolas, Menlo, monospace"
            for (var i = 0; i < ticks.length; ++i) {
                var t = ticks[i]
                var y = bot - (bot - top) * t.pos
                ctx.strokeStyle = t.major ? "#8aa0b0" : "#314858"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(slotX - 14, y); ctx.lineTo(slotX - 5, y); ctx.stroke()
                if (t.major) {
                    ctx.fillStyle = t.pos >= Meter.normAtS9 ? "#ffb199" : "#c5d4de"
                    ctx.fillText(t.label, slotX - 16, y)
                }
            }
            var capH = Math.max(22, h * 0.18), capW = Math.max(40, w * 0.18)
            var cx = slotX + slotW / 2 - capW / 2
            var cy = Math.max(top, Math.min(bot - capH, capY - capH / 2))
            var capg = ctx.createLinearGradient(0, cy, 0, cy + capH)
            capg.addColorStop(0, "#4a5864")
            capg.addColorStop(0.45, "#1c262e")
            capg.addColorStop(1, "#12181e")
            fader.round(ctx, cx, cy, capW, capH, 4)
            ctx.fillStyle = capg; ctx.fill()
            ctx.strokeStyle = "rgba(255,255,255,0.16)"; ctx.stroke()
            ctx.strokeStyle = "rgba(0,0,0,0.5)"
            for (var g = -1; g <= 1; ++g) {
                ctx.beginPath()
                ctx.moveTo(cx + 7, cy + capH * 0.62 + g * 3)
                ctx.lineTo(cx + capW - 7, cy + capH * 0.62 + g * 3)
                ctx.stroke()
            }
            var led = ctx.createRadialGradient(cx + capW / 2, cy + 7, 0, cx + capW / 2, cy + 7, 6)
            led.addColorStop(0, "#ffffff")
            led.addColorStop(0.5, col)
            led.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = led
            ctx.beginPath(); ctx.arc(cx + capW / 2, cy + 7, 6, 0, 7); ctx.fill()
            if (Meter.peak > 0.01) {
                var py = bot - (bot - top) * Meter.peak
                ctx.fillStyle = "#ffe7a3"
                ctx.fillRect(slotX + slotW + 5, py - 1, 9, 2)
            }
            ctx.textAlign = "left"
            ctx.textBaseline = "alphabetic"
            ctx.fillStyle = col
            ctx.font = "600 " + Math.max(16, h * 0.15) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.62, h * 0.40)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.max(10, h * 0.07) + "px Consolas, Menlo, monospace"
            var ty = h * 0.54
            if (Meter.dbmText.length) { ctx.fillText(Meter.dbmText, w * 0.62, ty); ty += h * 0.11 }
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) { ctx.fillText(Meter.snrText, w * 0.62, ty); ty += h * 0.11 }
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.62, ty)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
