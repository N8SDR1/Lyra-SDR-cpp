// Phosphor — port of paintPhosphor() in the browser mock.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: phos
    anchors.fill: parent

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
            var g = ctx.createRadialGradient(w / 2, h / 2, 10, w / 2, h / 2, w * 0.7)
            g.addColorStop(0, "#10241c")
            g.addColorStop(1, "#070d0b")
            ctx.fillStyle = g; ctx.fillRect(0, 0, w, h)
            var L = w * 0.08, R = w * 0.96, T = h * 0.16, B = h * 0.88
            ctx.strokeStyle = "rgba(120,180,150,0.18)"; ctx.lineWidth = 1
            var i
            for (i = 0; i <= 4; ++i) {
                var gy = T + (B - T) * i / 4
                ctx.beginPath(); ctx.moveTo(L, gy); ctx.lineTo(R, gy); ctx.stroke()
            }
            for (i = 0; i <= 8; ++i) {
                var gx = L + (R - L) * i / 8
                ctx.beginPath(); ctx.moveTo(gx, T); ctx.lineTo(gx, B); ctx.stroke()
            }
            var danger = Meter.normAtS9
            function ink(v) { return MeterInk.ink(v, danger) }
            ctx.strokeStyle = "rgba(255,150,80,0.35)"
            var s9y = B - (B - T) * danger
            ctx.beginPath(); ctx.moveTo(L, s9y); ctx.lineTo(R, s9y); ctx.stroke()
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            ctx.font = Math.round(Math.max(8, h * 0.04)) + "px Consolas, Menlo, monospace"
            var ticks = Meter.tickMarks()
            for (i = 0; i < ticks.length; ++i) {
                if (!ticks[i].major) continue
                var ty = B - (B - T) * ticks[i].pos
                ctx.fillStyle = ticks[i].pos >= danger ? "#ffb199" : "rgba(180,220,200,0.7)"
                ctx.fillText(ticks[i].label, L - 4, ty)
            }
            var hist = Meter.history
            var n = hist ? hist.length : 0
            if (n > 1) {
                function pt(idx) {
                    var v = Math.max(0, Math.min(1, hist[idx]))
                    return [L + (R - L) * idx / (n - 1), B - (B - T) * v, v]
                }
                ctx.beginPath()
                var p0 = pt(0)
                ctx.moveTo(p0[0], p0[1])
                for (i = 1; i < n; ++i) {
                    var p = pt(i)
                    ctx.lineTo(p[0], p[1])
                }
                ctx.lineTo(R, B); ctx.lineTo(L, B); ctx.closePath()
                ctx.fillStyle = ink(Meter.level)
                ctx.globalAlpha = 0.16
                ctx.fill()
                ctx.globalAlpha = 1
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                for (i = 1; i < n; ++i) {
                    var a = pt(i - 1)
                    var b = pt(i)
                    ctx.beginPath()
                    ctx.moveTo(a[0], a[1])
                    ctx.lineTo(b[0], b[1])
                    ctx.strokeStyle = ink(b[2])
                    ctx.globalAlpha = 0.22
                    ctx.lineWidth = Math.max(5, h * 0.02)
                    ctx.stroke()
                    ctx.globalAlpha = 1
                    ctx.lineWidth = Math.max(1.6, h * 0.006)
                    ctx.stroke()
                }
                var last = pt(n - 1)
                var dot = ctx.createRadialGradient(last[0], last[1], 0, last[0], last[1], 10)
                dot.addColorStop(0, "#ffffff")
                dot.addColorStop(0.4, ink(last[2]))
                dot.addColorStop(1, "rgba(0,0,0,0)")
                ctx.fillStyle = dot
                ctx.beginPath(); ctx.arc(last[0], last[1], 10, 0, 7); ctx.fill()
            }
            ctx.setLineDash([3, 4])
            ctx.strokeStyle = "rgba(255,220,140,0.7)"; ctx.lineWidth = 1
            var py = B - (B - T) * Meter.peak
            ctx.beginPath(); ctx.moveTo(L, py); ctx.lineTo(R, py); ctx.stroke()
            ctx.setLineDash([])
            ctx.fillStyle = "rgba(0,0,0,0.13)"
            for (var y = 0; y < h; y += 3) ctx.fillRect(0, y, w, 1)
            var sheen = ctx.createLinearGradient(0, 0, 0, h * 0.22)
            sheen.addColorStop(0, "rgba(255,255,255,0.14)")
            sheen.addColorStop(0.45, "rgba(255,255,255,0.03)")
            sheen.addColorStop(1, "rgba(255,255,255,0)")
            ctx.fillStyle = sheen
            ctx.fillRect(0, 0, w, h * 0.22)
            ctx.textAlign = "left"; ctx.textBaseline = "alphabetic"
            ctx.fillStyle = "#d8ffe9"
            ctx.font = "bold " + Math.round(Math.max(12, h * 0.09)) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.08, h * 0.12)
            // dBm starts after the reading, however long it is.
            var dbmX = Math.max(w * 0.22, w * 0.08 + ctx.measureText(Meter.text).width + w * 0.03)
            ctx.fillStyle = "rgba(184,255,216,0.7)"
            ctx.font = Math.round(Math.max(10, h * 0.065)) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, dbmX, h * 0.115)
            ctx.strokeStyle = "rgba(180,255,210,0.35)"; ctx.lineWidth = 1.5
            var m = 8, bn = 14
            var corners = [[m, m, 1, 1], [w - m, m, -1, 1], [m, h - m, 1, -1], [w - m, h - m, -1, -1]]
            for (i = 0; i < corners.length; ++i) {
                var c = corners[i]
                ctx.beginPath()
                ctx.moveTo(c[0], c[1] + c[3] * bn)
                ctx.lineTo(c[0], c[1])
                ctx.lineTo(c[0] + c[2] * bn, c[1])
                ctx.stroke()
            }
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
