// Cluster — arc of lamps + segmented bars from Meter.ladderRows.
// Theme colours (cyan window, navy face).  Style 5.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: cluster
    anchors.fill: parent

    readonly property string accent: "#00e5ff"
    readonly property string textCol: "#cdd9e5"
    readonly property string labelCol: "#8a9aac"
    readonly property string unlit: "#16222c"
    readonly property string windowBg: "#0c1218"

    function alpha(hex, a) {
        return "rgba(" + parseInt(hex.substr(1, 2), 16) + ","
             + parseInt(hex.substr(3, 2), 16) + ","
             + parseInt(hex.substr(5, 2), 16) + "," + a + ")"
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
    function arcPoint(cx, cy, r, t) {
        var a = Math.PI + t * Math.PI
        return [cx + r * Math.cos(a), cy + r * Math.sin(a)]
    }
    function bareValue(v) {
        return v ? v.replace(/^[A-Za-z][A-Za-z ]*?\s+(?=[-+≥\d])/, "") : ""
    }
    function splitReading(v) {
        var m = /^(\D*?)(-?\d+(?:\.\d+)?)(.*)$/.exec(cluster.bareValue(v))
        if (!m) return { prefix: "", num: "--", suffix: "" }
        return { prefix: m[1].trim(), num: m[2], suffix: m[3].trim() }
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
            var bg = ctx.createLinearGradient(0, 0, 0, h)
            bg.addColorStop(0, "#18222c")
            bg.addColorStop(0.45, "#101820")
            bg.addColorStop(1, "#0b1117")
            ctx.fillStyle = bg
            ctx.fillRect(0, 0, w, h)

            var rows = Meter.ladderRows ? Meter.ladderRows : []
            var nRows = rows.length
            var hero = nRows > 0 ? rows[0] : null
            var level = hero ? Math.max(0, Math.min(1, hero.level)) : 0
            var danger = hero ? hero.danger : Meter.normAtS9

            var tickPx = Math.round(Math.max(9, h * 0.032))
            var barTop = Math.round(h * 0.58)
            var cx = w * 0.50
            var cy = barTop - tickPx / 2 - 4
            var R = Math.max(10, Math.min(w * 0.38, cy - tickPx * 1.5 - 2))
            var tickR = R + tickPx * 0.9

            var dots = Math.max(28, Math.round(w / 16))
            var rad = Math.max(2.2, Math.min(w, h) * 0.011)
            var i
            for (i = 0; i < dots; ++i) {
                var t = dots === 1 ? 0 : i / (dots - 1)
                var p = cluster.arcPoint(cx, cy, R, t)
                var on = t <= level + 0.5 / dots
                if (on) {
                    var lampInk = MeterInk.ink(t, danger)
                    var glow = ctx.createRadialGradient(p[0], p[1], 0, p[0], p[1], rad * 3.2)
                    glow.addColorStop(0, cluster.alpha(lampInk, 0.75))
                    glow.addColorStop(1, cluster.alpha(lampInk, 0))
                    ctx.fillStyle = glow
                    ctx.beginPath()
                    ctx.arc(p[0], p[1], rad * 3.2, 0, 7)
                    ctx.fill()
                    ctx.fillStyle = lampInk
                } else {
                    ctx.fillStyle = cluster.unlit
                }
                ctx.beginPath()
                ctx.arc(p[0], p[1], rad, 0, 7)
                ctx.fill()
            }

            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.font = tickPx + "px Consolas"
            var ticks = hero && hero.source !== undefined
                        ? Meter.tickMarksFor(hero.source) : []
            for (i = 0; i < ticks.length; ++i) {
                var tk = ticks[i]
                if (!tk.major && ticks.length > 6) continue
                var tp = cluster.arcPoint(cx, cy, tickR, tk.pos)
                var half = ctx.measureText(tk.label).width / 2
                var tx = Math.max(half + 1, Math.min(w - half - 1, tp[0]))
                ctx.fillStyle = tk.pos >= danger ? MeterInk.facePalette[2] : cluster.textCol
                ctx.fillText(tk.label, tx, tp[1])
            }

            var boxW = Math.min(w * 0.40, R * 1.3)
            var boxH = Math.min(h * 0.16, R * 0.40)
            var boxX = cx - boxW / 2
            var boxY = cy - R * 0.62
            cluster.round(ctx, boxX, boxY, boxW, boxH, boxH * 0.12)
            ctx.fillStyle = cluster.windowBg
            ctx.fill()
            ctx.strokeStyle = cluster.accent
            ctx.lineWidth = Math.max(1, boxH * 0.03)
            ctx.stroke()
            var rd = cluster.splitReading(hero ? hero.value : "")
            var pad = Math.max(3, boxH * 0.14)
            var sidePx = Math.round(Math.max(8, boxH * 0.40))
            var digitPx = Math.round(boxH * 0.55)
            var midY = boxY + boxH * 0.54
            ctx.fillStyle = cluster.accent
            ctx.font = "bold " + digitPx + "px Consolas"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText((rd.prefix + " " + rd.num + " " + rd.suffix).trim(),
                         cx, midY)

            var barCount = Math.max(0, nRows - 1)
            if (barCount > 0) {
                var leftN = Math.ceil(barCount / 2)
                var rightN = barCount - leftN
                var barBot = h * 0.98
                drawColumn(ctx, rows, 1, leftN, w * 0.04, w * 0.46, barTop, barBot, w, h)
                if (rightN > 0)
                    drawColumn(ctx, rows, 1 + leftN, rightN, w * 0.54, w * 0.96, barTop, barBot, w, h)
            }
        }

        function drawColumn(ctx, rows, start, count, x0, x1, top, bot, w, h) {
            var slot = (bot - top) / count
            var bw = x1 - x0
            var textPx = Math.round(Math.max(8, Math.min(slot * 0.45, h * 0.08)))
            for (var i = 0; i < count; ++i) {
                var row = rows[start + i]
                var y = top + i * slot
                var lv = Math.max(0, Math.min(1, row.level))
                var dg = row.danger
                ctx.textBaseline = "top"
                ctx.textAlign = "left"
                ctx.fillStyle = MeterInk.ink(lv, dg)
                ctx.font = "bold " + textPx + "px Consolas"
                ctx.fillText(row.label, x0, y)
                var value = cluster.bareValue(row.value)
                ctx.textAlign = "right"
                ctx.fillStyle = cluster.textCol
                ctx.font = Math.round(textPx * 0.9) + "px Consolas"
                ctx.fillText(value, x1, y)
                var by = y + textPx + 2
                var bh = Math.max(3, Math.min(slot - textPx - 4, slot * 0.28))
                var segs = Math.max(12, Math.round(bw / Math.max(6, w * 0.012)))
                var gap = Math.max(1, bw * 0.004)
                var sw = (bw - gap * (segs - 1)) / segs
                var lit = Math.round(lv * segs)
                for (var s = 0; s < segs; ++s) {
                    var pos = segs === 1 ? 0 : s / (segs - 1)
                    var on = s < lit
                    var sx = x0 + s * (sw + gap)
                    ctx.fillStyle = on ? MeterInk.ink(pos, dg) : cluster.unlit
                    ctx.fillRect(sx, by, sw, bh)
                }
            }
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
