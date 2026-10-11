// Iris — aperture blades. The opening is the level. The light is one
// hue: cyan, amber at the danger point, red above it.

import QtQuick

Item {
    id: iris

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
            var col = iris.ink(Meter.level)
            var cx = w * 0.38, cy = h * 0.52
            var R = Math.min(w, h) * 0.38
            var house = ctx.createRadialGradient(cx - R * 0.2, cy - R * 0.3, R * 0.15, cx, cy, R * 1.15)
            house.addColorStop(0, "#3a4854")
            house.addColorStop(0.55, "#141c24")
            house.addColorStop(1, "#0a1014")
            ctx.beginPath(); ctx.arc(cx, cy, R * 1.08, 0, 7)
            ctx.fillStyle = house; ctx.fill()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.98, 0, 7)
            ctx.fillStyle = "#070b0e"; ctx.fill()
            var blades = 9
            var hole = R * (0.12 + 0.70 * Meter.level)
            ctx.save()
            ctx.beginPath(); ctx.arc(cx, cy, R * 0.96, 0, 7); ctx.clip()
            for (var i = 0; i < blades; ++i) {
                var a = (i / blades) * Math.PI * 2 - Math.PI / 2
                var a2 = a + (Math.PI * 2 / blades) * 0.95
                var pivot = R * 0.98
                var px = cx + Math.cos(a) * pivot, py = cy + Math.sin(a) * pivot
                var ox = cx + Math.cos(a2) * pivot, oy = cy + Math.sin(a2) * pivot
                var ix = cx + Math.cos(a + 0.55) * hole, iy = cy + Math.sin(a + 0.55) * hole
                ctx.beginPath()
                ctx.moveTo(px, py)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.3) * R * 1.05,
                                     cy + Math.sin(a + 0.3) * R * 1.05, ox, oy)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.7) * hole * 1.15,
                                     cy + Math.sin(a + 0.7) * hole * 1.15, ix, iy)
                ctx.closePath()
                var lg = ctx.createLinearGradient(px, py, ix, iy)
                lg.addColorStop(0, i % 2 ? "#2c3844" : "#3c4c58")
                lg.addColorStop(0.6, "#141b22")
                lg.addColorStop(1, "#0c1116")
                ctx.fillStyle = lg; ctx.fill()
                ctx.strokeStyle = "rgba(255,255,255,0.14)"; ctx.lineWidth = 0.8
                ctx.beginPath(); ctx.moveTo(px, py)
                ctx.quadraticCurveTo(cx + Math.cos(a + 0.3) * R, cy + Math.sin(a + 0.3) * R, ox, oy)
                ctx.stroke()
            }
            ctx.restore()
            var light = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.max(4, hole))
            light.addColorStop(0, "#ffffff")
            light.addColorStop(0.35, "#ffffff")
            light.addColorStop(0.7, col)
            light.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = light
            ctx.beginPath(); ctx.arc(cx, cy, Math.max(4, hole), 0, 7); ctx.fill()
            if (Meter.peak > 0.02) {
                ctx.beginPath()
                ctx.arc(cx, cy, R * (0.12 + 0.70 * Meter.peak), 0, 7)
                ctx.strokeStyle = "rgba(255,214,130,0.65)"; ctx.lineWidth = 1; ctx.stroke()
            }
            ctx.textAlign = "left"
            ctx.textBaseline = "middle"
            ctx.fillStyle = col
            ctx.font = "600 " + Math.max(16, h * 0.14) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.68, h * 0.40)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.max(10, h * 0.07) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.68, h * 0.54)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.68, h * 0.66)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
