// Dekatron — a glow-transfer counting tube. One neon dot sits on the
// cathode ring at the live level, with a short fading tail behind it.

import QtQuick

Item {
    id: tube

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var cx = w * 0.40, cy = h * 0.54
            var R = Math.min(w, h) * 0.36
            var glass = ctx.createRadialGradient(cx - R * 0.2, cy - R * 0.25, R * 0.1, cx, cy, R * 1.15)
            glass.addColorStop(0, "#3a2a28")
            glass.addColorStop(0.55, "#1a1214")
            glass.addColorStop(1, "#0c080a")
            ctx.beginPath(); ctx.arc(cx, cy, R * 1.08, 0, 7)
            ctx.fillStyle = glass; ctx.fill()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.92, 0, 7)
            ctx.fillStyle = "#140c0e"; ctx.fill()
            var n = 30
            var lit = Math.round(Meter.level * (n - 1))
            for (var i = 0; i < n; ++i) {
                var a = -Math.PI / 2 + i / n * Math.PI * 2
                var x = cx + Math.cos(a) * R * 0.72
                var y = cy + Math.sin(a) * R * 0.72
                var behind = (lit - i + n) % n
                var on = i === lit
                var tail = behind > 0 && behind <= 4
                ctx.beginPath()
                ctx.arc(x, y, on ? R * 0.055 : R * 0.032, 0, 7)
                if (on) ctx.fillStyle = "#fff3d4"
                else if (tail) ctx.fillStyle = "rgba(255,140,60," + (0.55 - behind * 0.1) + ")"
                else ctx.fillStyle = "#3a2420"
                ctx.fill()
            }
            if (Meter.peak > 0.02) {
                var ap = -Math.PI / 2 + Meter.peak * Math.PI * 2
                ctx.beginPath()
                ctx.arc(cx + Math.cos(ap) * R * 0.86, cy + Math.sin(ap) * R * 0.86, 2.2, 0, 7)
                ctx.fillStyle = "#ffe7a3"; ctx.fill()
            }
            var sheen = ctx.createLinearGradient(cx - R, cy - R, cx, cy)
            sheen.addColorStop(0, "rgba(255,255,255,0.16)")
            sheen.addColorStop(0.4, "rgba(255,255,255,0)")
            ctx.fillStyle = sheen
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.92, 0, 7); ctx.fill()
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillStyle = "#ffb15a"
            ctx.font = "600 " + Math.max(14, R * 0.28) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, cx, cy)
            ctx.textAlign = "left"
            ctx.fillStyle = "#c9b8a4"
            ctx.font = Math.max(10, h * 0.07) + "px Consolas, Menlo, monospace"
            var ty = h * 0.38
            if (Meter.dbmText.length) { ctx.fillText(Meter.dbmText, w * 0.70, ty); ty += h * 0.12 }
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) { ctx.fillText(Meter.snrText, w * 0.70, ty); ty += h * 0.12 }
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.70, ty)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
