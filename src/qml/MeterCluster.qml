// Cluster — the instrument in the reference picture, fed by the
// ladder rows. The first row is the arc and the digital window.
// The rest are the segmented bars, split into two columns.
// RX is S / NF / SNR. TX is the full ladder stack.
//
// Layout, top to bottom: the arc (its scale figures outside it) ends a
// fixed gap above the bar block, so figures and rows never overlap.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: cluster
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

    // t = 0 at the left foot of the arc, 1 at the right foot.
    function arcPoint(cx, cy, r, t) {
        var a = Math.PI + t * Math.PI
        return [cx + r * Math.cos(a), cy + r * Math.sin(a)]
    }

    // Row values from MeterModel often repeat the row's name ("SNR 22 dB",
    // "PWR 4 W", "V 13.8 V").  Drop a leading word before the number; the
    // row label already says it.
    function bareValue(v) {
        return v ? v.replace(/^[A-Za-z][A-Za-z ]*?\s+(?=[-+≥\d])/, "") : ""
    }

    // Split a hero reading into prefix / number / suffix:
    // "S9+20" → S | 9 | +20,  "4.2 W" → | 4.2 | W,  "≥3.0:1" → ≥ | 3.0 | :1.
    // No number ("—") gives "--" so the window still shows segments.
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
            ctx.fillStyle = "#050505"
            ctx.fillRect(0, 0, w, h)

            var rows = Meter.ladderRows ? Meter.ladderRows : []
            var nRows = rows.length
            var hero = nRows > 0 ? rows[0] : null
            var level = hero ? Math.max(0, Math.min(1, hero.level)) : 0
            var danger = hero ? hero.danger : Meter.normAtS9

            // Vertical budget: scale-figure font, bar block top, arc centre.
            // The feet figures sit centred on cy, so cy is half a figure plus
            // a gap above the bars; the apex figure must clear the top.
            var tickPx = Math.round(Math.max(9, h * 0.032))
            var barTop = Math.round(h * 0.58)
            var cx = w * 0.50
            var cy = barTop - tickPx / 2 - 4
            var R = Math.max(10, Math.min(w * 0.38, cy - tickPx * 1.5 - 2))
            var tickR = R + tickPx * 0.9

            // Faint vertical rules and the left-hand 100..25 scale.
            var gridTop = h * 0.04
            var gridBot = cy - tickPx
            ctx.strokeStyle = "rgba(255,255,255,0.28)"
            ctx.fillStyle = "rgba(255,255,255,0.55)"
            ctx.lineWidth = Math.max(1, w * 0.002)
            ctx.font = Math.round(Math.max(8, h * 0.035)) + "px Consolas, Menlo, monospace"
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            var marks = [100, 75, 50, 25]
            var gx0 = Math.max(w * 0.06, ctx.measureText("100").width + w * 0.04)
            for (var g = 0; g < 5; ++g) {
                var gx = gx0 + g * (w - gx0 - w * 0.06) / 4
                ctx.beginPath()
                ctx.moveTo(gx, gridTop)
                ctx.lineTo(gx, gridBot)
                ctx.stroke()
                if (g === 0) {
                    for (var m = 0; m < marks.length; ++m) {
                        var gy = gridTop + (gridBot - gridTop) * (m / (marks.length - 1)) * 0.85
                        ctx.beginPath()
                        ctx.moveTo(gx - w * 0.02, gy)
                        ctx.lineTo(gx + w * 0.04, gy)
                        ctx.stroke()
                        ctx.fillText(String(marks[m]), gx - w * 0.025, gy)
                    }
                }
            }

            // Arc of lamps. Lit lamps run from the left foot up to the level.
            var dots = Math.max(28, Math.round(w / 16))
            var rad = Math.max(2.2, Math.min(w, h) * 0.011)
            var i
            for (i = 0; i < dots; ++i) {
                var t = dots === 1 ? 0 : i / (dots - 1)
                var p = cluster.arcPoint(cx, cy, R, t)
                var on = t <= level + 0.5 / dots
                if (on) {
                    var glow = ctx.createRadialGradient(p[0], p[1], 0, p[0], p[1], rad * 3.2)
                    glow.addColorStop(0, "rgba(255,170,40,0.85)")
                    glow.addColorStop(1, "rgba(255,120,0,0)")
                    ctx.fillStyle = glow
                    ctx.beginPath()
                    ctx.arc(p[0], p[1], rad * 3.2, 0, 7)
                    ctx.fill()
                    ctx.fillStyle = "#fff4c8"
                    ctx.beginPath()
                    ctx.arc(p[0], p[1], rad * 0.55, 0, 7)
                    ctx.fill()
                    ctx.fillStyle = "#ff9a18"
                } else {
                    ctx.fillStyle = "#2a1608"
                }
                ctx.beginPath()
                ctx.arc(p[0], p[1], rad, 0, 7)
                ctx.fill()
            }

            // Scale figures outside the arc, from the first row's own scale
            // so they match its level and readout (the live source can differ).
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.font = tickPx + "px Consolas, Menlo, monospace"
            var ticks = hero && hero.source !== undefined ? Meter.tickMarksFor(hero.source) : []
            for (i = 0; i < ticks.length; ++i) {
                var tk = ticks[i]
                if (!tk.major && ticks.length > 6) continue
                var tp = cluster.arcPoint(cx, cy, tickR, tk.pos)
                // Keep a foot figure inside the panel edge.
                var half = ctx.measureText(tk.label).width / 2
                var tx = Math.max(half + 1, Math.min(w - half - 1, tp[0]))
                ctx.fillStyle = tk.pos >= danger ? "#ffb199" : "rgba(230,230,230,0.8)"
                ctx.fillText(tk.label, tx, tp[1])
            }

            // Digital window in the bowl of the arc.  The number is DSEG7 in a
            // fixed right-aligned field; the prefix ("S", "≥") and suffix
            // ("+20", "W", ":1") are monospace in fixed fields either side, so no
            // font or position changes as the reading changes.
            var boxW = Math.min(w * 0.40, R * 1.3)
            var boxH = Math.min(h * 0.16, R * 0.40)
            var boxX = cx - boxW / 2
            var boxY = cy - R * 0.62
            cluster.round(ctx, boxX, boxY, boxW, boxH, boxH * 0.12)
            var bezel = ctx.createLinearGradient(boxX, boxY, boxX, boxY + boxH)
            bezel.addColorStop(0, "#ff3a2a")
            bezel.addColorStop(0.45, "#c01010")
            bezel.addColorStop(1, "#6a0606")
            ctx.fillStyle = bezel
            ctx.fill()
            var rd = cluster.splitReading(hero ? hero.value : "")
            var pad = Math.max(3, boxH * 0.14)
            var sidePx = Math.round(Math.max(8, boxH * 0.40))
            ctx.font = "bold " + sidePx + "px Consolas, Menlo, monospace"
            var preW = ctx.measureText("≥").width
            var sufW = ctx.measureText("+60").width
            var numRight = boxX + boxW - pad - sufW - pad * 0.5
            var numRoom = numRight - (boxX + pad + preW + pad * 0.5)
            // Digit size: the widest reading ("-88.8") fills the field.
            var digitPx = Math.round(boxH * 0.66)
            ctx.font = digitPx + "px \"DSEG7 Classic\""
            var reserve = ctx.measureText("-88.8").width
            if (reserve > numRoom && reserve > 0)
                digitPx = Math.max(8, Math.floor(digitPx * numRoom / reserve))
            var midY = boxY + boxH * 0.54
            ctx.textBaseline = "middle"
            ctx.fillStyle = "#ffe14a"
            ctx.font = "bold " + digitPx + "px \"DSEG7 Classic\""
            ctx.textAlign = "right"
            ctx.fillText(rd.num, numRight, midY)
            ctx.font = "bold " + sidePx + "px Consolas, Menlo, monospace"
            ctx.textAlign = "left"
            if (rd.prefix.length) ctx.fillText(rd.prefix, boxX + pad, midY)
            if (rd.suffix.length) ctx.fillText(rd.suffix, numRight + pad * 0.5, midY)
            // Row name centred under the window (clear of the arc and its
            // figures), unless the prefix already says it.
            var unit = hero && hero.label ? hero.label : ""
            if (unit.length && unit !== rd.prefix) {
                ctx.font = "bold " + Math.round(Math.max(8, Math.min(tickPx, boxH * 0.36))) + "px Consolas, Menlo, monospace"
                ctx.fillStyle = "#b6ff4a"
                ctx.textAlign = "center"
                ctx.fillText(unit, cx, (boxY + boxH + cy) / 2)
            }

            // Segmented bars for every ladder row after the first.
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

        // One row per slot: a text line (label left, value right) on top,
        // the segmented bar under it, both inside the slot.
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
                ctx.fillStyle = MeterInk.clusterInk(lv, dg)
                ctx.font = "bold " + textPx + "px Consolas, Menlo, monospace"
                ctx.fillText(row.label, x0, y)
                var labelW = ctx.measureText(row.label).width
                // Value right-aligned; shrink it only if it would reach the label.
                var value = cluster.bareValue(row.value)
                var valPx = Math.round(textPx * 0.9)
                ctx.font = valPx + "px Consolas, Menlo, monospace"
                var room = bw - labelW - textPx * 0.5
                var vw = ctx.measureText(value).width
                if (vw > room && vw > 0) {
                    valPx = Math.max(6, Math.floor(valPx * room / vw))
                    ctx.font = valPx + "px Consolas, Menlo, monospace"
                }
                ctx.textAlign = "right"
                ctx.fillStyle = "rgba(220,220,220,0.75)"
                ctx.fillText(value, x1, y + (textPx - valPx))

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
                    ctx.fillStyle = on ? MeterInk.clusterInk(pos, dg) : "#101010"
                    ctx.fillRect(sx, by, sw, bh)
                    if (on && s === lit - 1) {
                        ctx.fillStyle = "rgba(255,255,255,0.65)"
                        ctx.fillRect(sx, by, sw, bh * 0.35)
                    }
                }
            }
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
