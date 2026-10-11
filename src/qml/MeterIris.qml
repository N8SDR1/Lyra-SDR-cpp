// Iris — port of paintIris() in the browser mock.
// Nine shaded blades, white-hot hole, flare streak, amber peak ring.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: iris
    anchors.fill: parent

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
            iris.faceBg(ctx, w, h)
            var cx = w * 0.40, cy = h * 0.52, R = Math.min(w, h) * 0.36
            var house = ctx.createRadialGradient(cx - R * 0.2, cy - R * 0.3, R * 0.2, cx, cy, R * 1.15)
            house.addColorStop(0, "#3a4854"); house.addColorStop(0.55, "#141c24"); house.addColorStop(1, "#0a1014")
            ctx.beginPath(); ctx.arc(cx, cy, R * 1.08, 0, 7); ctx.fillStyle = house; ctx.fill()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.98, 0, 7); ctx.fillStyle = "#070b0e"; ctx.fill()
            var blades = 9
            var hole = R * (0.10 + 0.72 * Meter.level)
            ctx.save()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.96, 0, 7); ctx.clip()
            for (var i = 0; i < blades; ++i) {
                var a = (i / blades) * Math.PI * 2 - Math.PI / 2
                var a2 = a + (Math.PI * 2 / blades) * 0.95
                var pivot = R * 0.98
                var px = cx + Math.cos(a) * pivot, py = cy + Math.sin(a) * pivot
                var ix = cx + Math.cos(a + 0.55) * hole, iy = cy + Math.sin(a + 0.55) * hole
                var ox = cx + Math.cos(a2) * pivot, oy = cy + Math.sin(a2) * pivot
                ctx.beginPath()
                ctx.moveTo(px, py)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.3) * R * 1.05, cy + Math.sin(a + 0.3) * R * 1.05, ox, oy)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.7) * hole * 1.1, cy + Math.sin(a + 0.7) * hole * 1.1, ix, iy)
                ctx.closePath()
                var lg = ctx.createLinearGradient(px, py, ix, iy)
                lg.addColorStop(0, i % 2 ? "#2c3844" : "#384858")
                lg.addColorStop(0.55, "#141b22")
                lg.addColorStop(1, "#0c1116")
                ctx.fillStyle = lg; ctx.fill()
                ctx.strokeStyle = "rgba(255,255,255,0.10)"; ctx.lineWidth = 0.8; ctx.stroke()
                ctx.strokeStyle = "rgba(255,255,255,0.18)"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(px, py)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.3) * R * 1.02, cy + Math.sin(a + 0.3) * R * 1.02, ox, oy)
                ctx.stroke()
            }
            ctx.restore()
            var col = MeterInk.ink(Meter.level, Meter.normAtS9)
            var light = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.max(1, hole))
            light.addColorStop(0, "#ffffff")
            light.addColorStop(0.25, "#fff")
            light.addColorStop(0.55, col)
            light.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = light
            ctx.beginPath(); ctx.arc(cx, cy, Math.max(1, hole), 0, 7); ctx.fill()
            ctx.save(); ctx.translate(cx, cy); ctx.rotate(-0.5)
            var flare = ctx.createLinearGradient(-hole, 0, hole, 0)
            flare.addColorStop(0, "rgba(255,255,255,0)")
            flare.addColorStop(0.5, "rgba(255,255,255,0.55)")
            flare.addColorStop(1, "rgba(255,255,255,0)")
            ctx.fillStyle = flare; ctx.fillRect(-hole * 1.3, -0.6, hole * 2.6, 1.2)
            ctx.restore()
            ctx.beginPath(); ctx.arc(cx, cy, R * (0.10 + 0.72 * Meter.peak), 0, 7)
            ctx.strokeStyle = "rgba(255,214,130,0.55)"; ctx.lineWidth = 1; ctx.stroke()
            ctx.textAlign = "left"; ctx.textBaseline = "middle"
            ctx.fillStyle = col
            ctx.font = "bold " + Math.round(Math.max(18, h * 0.14)) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.70, h * 0.42)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.round(Math.max(11, h * 0.075)) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.70, h * 0.56)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.70, h * 0.68)
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.70, h * 0.80)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
