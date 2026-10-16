// VFD meter face (MeterVfd.qml): a row of glass tubes after paintNixie()
// in the browser mock — rounded envelopes, wire cathodes, glass sheen —
// with phosphor-green gas.  Digit cells are fixed per source; a separate
// red OVER lamp lights in the over zone (MeterInk.js).

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: root
    anchors.fill: parent
    readonly property string filament: "#7dffe0"   // wire / text
    readonly property string glow: "#2ee6c0"       // halo around the wire

    // Wire cathodes in the mock's coordinate box. S, 9, and W are the
    // browser paths. The other characters use the same stroke weight.
    function wirePath(ctx, ch) {
        ctx.beginPath()
        if (ch === "S" || ch === "s") {
            ctx.moveTo(18, 6); ctx.bezierCurveTo(4, 6, 2, 16, 10, 22)
            ctx.bezierCurveTo(20, 28, 22, 36, 8, 42)
        } else if (ch === "5") {
            ctx.moveTo(22, 7); ctx.lineTo(6, 8); ctx.lineTo(7, 20)
            ctx.bezierCurveTo(14, 16, 24, 18, 22, 28)
            ctx.bezierCurveTo(20, 42, 6, 42, 6, 32)
        } else if (ch === "9") {
            ctx.arc(12, 14, 8, 0.2, Math.PI * 1.7)
            ctx.moveTo(18, 10); ctx.lineTo(18, 40)
        } else if (ch === "W" || ch === "w") {
            ctx.moveTo(2, 6); ctx.lineTo(8, 40); ctx.lineTo(14, 16); ctx.lineTo(20, 40); ctx.lineTo(26, 6)
        } else if (ch === "0" || ch === "O" || ch === "o") {
            ctx.moveTo(14, 6)
            ctx.bezierCurveTo(4, 6, 3, 16, 4, 24)
            ctx.bezierCurveTo(5, 36, 8, 44, 14, 44)
            ctx.bezierCurveTo(22, 44, 25, 34, 24, 24)
            ctx.bezierCurveTo(23, 14, 22, 6, 14, 6)
        } else if (ch === "1") {
            ctx.moveTo(8, 12); ctx.lineTo(14, 6); ctx.lineTo(14, 42)
        } else if (ch === "2") {
            ctx.moveTo(4, 14)
            ctx.bezierCurveTo(4, 4, 24, 4, 24, 14)
            ctx.bezierCurveTo(24, 24, 4, 28, 4, 42)
            ctx.lineTo(24, 42)
        } else if (ch === "3") {
            ctx.moveTo(4, 8)
            ctx.bezierCurveTo(12, 4, 26, 8, 20, 16)
            ctx.bezierCurveTo(16, 22, 12, 22, 16, 22)
            ctx.bezierCurveTo(26, 22, 26, 36, 14, 42)
            ctx.bezierCurveTo(4, 40, 4, 30, 10, 28)
        } else if (ch === "4") {
            ctx.moveTo(20, 42); ctx.lineTo(20, 6); ctx.lineTo(4, 28); ctx.lineTo(24, 28)
        } else if (ch === "6") {
            ctx.moveTo(22, 10)
            ctx.bezierCurveTo(10, 4, 3, 16, 5, 26)
            ctx.bezierCurveTo(7, 40, 22, 42, 22, 30)
            ctx.bezierCurveTo(22, 20, 8, 20, 6, 28)
        } else if (ch === "7") {
            ctx.moveTo(4, 8); ctx.lineTo(24, 8); ctx.lineTo(12, 42)
        } else if (ch === "8") {
            ctx.moveTo(14, 22)
            ctx.bezierCurveTo(4, 22, 3, 6, 14, 6)
            ctx.bezierCurveTo(25, 6, 26, 20, 14, 22)
            ctx.bezierCurveTo(3, 24, 3, 42, 14, 42)
            ctx.bezierCurveTo(25, 42, 26, 24, 14, 22)
        } else if (ch === "-" || ch === "—" || ch === "–") {
            ctx.moveTo(6, 24); ctx.lineTo(22, 24)
        } else if (ch === "+") {
            ctx.moveTo(14, 12); ctx.lineTo(14, 34)
            ctx.moveTo(6, 23); ctx.lineTo(22, 23)
        } else if (ch === ".") {
            ctx.arc(18, 40, 1.5, 0, 7)
        } else if (ch === ":") {
            ctx.arc(14, 16, 1.4, 0, 7)
            ctx.moveTo(14, 32); ctx.arc(14, 32, 1.4, 0, 7)
        } else {
            return false
        }
        return true
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

    function tube(ctx, x, y, w, h, ch) {
        var rad = w * 0.28
        root.round(ctx, x, y, w, h, rad)
        var g = ctx.createLinearGradient(x, y, x + w, y)
        g.addColorStop(0, "#061610"); g.addColorStop(0.5, "#0c241c"); g.addColorStop(1, "#06140f")
        ctx.fillStyle = g; ctx.fill()
        var gas = ctx.createRadialGradient(x + w / 2, y + h * 0.45, 2, x + w / 2, y + h * 0.5, w * 0.7)
        gas.addColorStop(0, "rgba(80,255,200,0.07)")
        gas.addColorStop(0.6, "rgba(10,80,60,0.03)")
        gas.addColorStop(1, "rgba(0,0,0,0)")
        ctx.fillStyle = gas; ctx.fill()

        ctx.save()
        // Same placement and stroke as the VFD VFO: the wire fills the tube.
        root.round(ctx, x, y, w, h, rad)
        ctx.clip()
        var gx = 4, gy = 5, gw = 22, gh = 40
        var s = Math.min((w * 0.92) / gw, (h * 0.90) / gh)
        ctx.translate(x + (w - gw * s) / 2 - gx * s,
                      y + (h - gh * s) / 2 - gy * s)
        ctx.scale(s, s)
        ctx.lineCap = "round"; ctx.lineJoin = "round"
        if (root.wirePath(ctx, ch)) {
            ctx.strokeStyle = root.glow
            ctx.globalAlpha = 0.12; ctx.lineWidth = 2.2; ctx.stroke()
            ctx.globalAlpha = 0.20; ctx.lineWidth = 1.4; ctx.stroke()
            ctx.globalAlpha = 1
            ctx.strokeStyle = root.filament
            ctx.lineWidth = 1.35
            ctx.stroke()
            ctx.strokeStyle = "#eafff8"
            ctx.lineWidth = 0.5
            ctx.stroke()
        }
        ctx.restore()
        if (ch !== " " && !root.wirePath(ctx, ch)) {
            ctx.fillStyle = root.filament
            ctx.font = "bold " + Math.round(h * 0.42) + "px Consolas, Menlo, monospace"
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            ctx.fillText(ch, x + w / 2, y + h / 2)
        }

        ctx.save()
        root.round(ctx, x, y, w, h, rad); ctx.clip()
        var sheen = ctx.createLinearGradient(x, y, x + w * 0.5, y + h)
        sheen.addColorStop(0, "rgba(255,255,255,0.18)")
        sheen.addColorStop(0.3, "rgba(255,255,255,0.02)")
        sheen.addColorStop(1, "rgba(255,255,255,0)")
        ctx.fillStyle = sheen; ctx.fillRect(x, y, w, h * 0.55)
        ctx.restore()
        ctx.strokeStyle = "rgba(160,255,220,0.25)"
        ctx.lineWidth = 1
        root.round(ctx, x, y, w, h, rad); ctx.stroke()
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
            root.faceBg(ctx, w, h)
            var text = Meter.text.length ? Meter.text : "—"
            var rx = Meter.source === 0
            // Fixed digit cells per source (MeterInk.readoutCells): unused
            // cells are unlit tubes, so the row never resizes as the reading
            // changes length.  On the S-meter the over-S9 suffix ("+20")
            // sits in three half-size cells and the S digits stay centred.
            var plusAt = rx ? text.indexOf("+") : -1
            var head = plusAt >= 0 ? text.substring(0, plusAt) : text
            var tail = plusAt >= 0 ? text.substring(plusAt) : ""
            var headCells = rx ? Math.max(2, head.length)
                                  : MeterInk.readoutCells(Meter.source, head)
            var tailSlots = rx ? Math.max(3, tail.length) : 0
            head = MeterInk.padCells(head, headCells, rx)
            tail = MeterInk.padCells(tail, tailSlots, true)
            var i
            var th0 = h * 0.52
            var th = th0
            var tw = th * 0.62
            var gap = Math.max(2, w * 0.03)
            var nH = headCells
            function rowMetrics(tw, gap, slots) {
                var tw2 = tw * 0.5
                var gap2 = gap * 0.5
                var headTotal = nH > 0 ? nH * tw + Math.max(0, nH - 1) * gap : 0
                var slotTotal = slots > 0 ? slots * tw2 + (slots - 1) * gap2 : 0
                var bridge = (nH > 0 && slots > 0) ? gap : 0
                return { tw2: tw2, gap2: gap2, headTotal: headTotal,
                         slotTotal: slotTotal, bridge: bridge }
            }
            var m = rowMetrics(tw, gap, tailSlots)
            var rightLimit = w * 0.96
            var xHead = nH > 0 ? (w - m.headTotal) / 2 : w / 2
            var right = xHead + m.headTotal + m.bridge + m.slotTotal
            if (tailSlots > 0 && right > rightLimit) {
                var denom = m.headTotal / 2 + m.bridge + m.slotTotal
                var k = denom > 0 ? (rightLimit - w / 2) / denom : 1
                k = Math.max(0.4, Math.min(1, k))
                tw *= k
                th *= k
                gap *= k
                m = rowMetrics(tw, gap, tailSlots)
                xHead = nH > 0 ? (w - m.headTotal) / 2 : w / 2
            } else if (tailSlots === 0) {
                var total = nH > 0 ? m.headTotal : tw
                if (total > w * 0.92 && nH > 0) {
                    tw = (w * 0.92 - Math.max(0, nH - 1) * gap) / nH
                    th = tw / 0.62
                    m = rowMetrics(tw, gap, 0)
                }
                xHead = (w - (nH > 0 ? m.headTotal : tw)) / 2
            }
            var y = h * 0.16 + (th0 - th) / 2
            for (i = 0; i < nH; ++i)
                root.tube(ctx, xHead + i * (tw + gap), y, tw, th, head.charAt(i))
            if (tailSlots > 0) {
                var tw2 = m.tw2
                var th2 = th * 0.5
                var y2 = y + (th - th2) / 2
                var xT = xHead + m.headTotal + m.bridge
                for (i = 0; i < tailSlots; ++i)
                    root.tube(ctx, xT + i * (tw2 + m.gap2), y2, tw2, th2, tail.charAt(i))
            }

            var x = w * 0.12, bw = w * 0.76, yb = h * 0.80, bh = Math.max(3, h * 0.028)
            var col = root.filament
            root.round(ctx, x, yb, bw, bh, bh / 2); ctx.fillStyle = "#06140f"; ctx.fill()
            var lv = Math.max(0, Math.min(1, Meter.level))
            var ng = ctx.createLinearGradient(x, 0, x + bw, 0)
            ng.addColorStop(0, "#0c3a30")
            if (lv > 0.001)
                ng.addColorStop(Math.min(0.98, lv), col)
            var fillEnd = Math.min(1, lv + 0.02)
            if (fillEnd > 0.001)
                ng.addColorStop(fillEnd, "#062018")
            ctx.save()
            root.round(ctx, x, yb, bw, bh, bh / 2); ctx.clip()
            ctx.fillStyle = ng; ctx.globalAlpha = 0.95
            ctx.fillRect(x, yb - 2, bw * lv, bh + 4)
            ctx.restore()
            ctx.globalAlpha = 0.9; ctx.fillStyle = "#eafff8"
            ctx.fillRect(x + bw * Meter.level - 1, yb - 1, 2, bh + 2)
            ctx.globalAlpha = 1
            ctx.fillStyle = "#ffcc88"
            ctx.fillRect(x + bw * Meter.peak - 1, yb - 4, 1.5, bh + 8)
            if (Meter.maxPeakEnabled && Meter.maxPeak > 0.01) {
                ctx.fillStyle = "#ff4136"
                ctx.fillRect(x + bw * Meter.maxPeak - 0.7, yb - 5, 1.4, bh + 10)
            }
            ctx.textAlign = "center"; ctx.fillStyle = "#9ad8c8"
            ctx.font = Math.round(Math.max(10, h * 0.07)) + "px Consolas, Menlo, monospace"
            ctx.textBaseline = "alphabetic"
            var sub = Meter.dbmText
            if (Meter.snrText.length)
                sub = (sub.length ? sub + "    " : "") + Meter.snrText
            if (Meter.secondary2Text.length)
                sub = (sub.length ? sub + "    " : "") + Meter.secondary2Text
            if (sub.length) ctx.fillText(sub, w / 2, h * 0.95)

            // OVER lamp (top left): the tubes keep their colour, this lights
            // red in the over zone (where the other faces turn red).
            var lr = Math.max(3, Math.round(h * 0.025))
            var lx = lr + 4, ly = lr + 4
            var lamp = MeterInk.lamp(Meter.level, Meter.normAtS9)
            var lit = MeterInk.lampLit(Meter.level, Meter.normAtS9)
            if (lit) {
                var halo = ctx.createRadialGradient(lx, ly, 0, lx, ly, lr * 3)
                halo.addColorStop(0, lamp); halo.addColorStop(1, "rgba(0,0,0,0)")
                ctx.fillStyle = halo
                ctx.beginPath(); ctx.arc(lx, ly, lr * 3, 0, 7); ctx.fill()
            }
            ctx.fillStyle = lamp
            ctx.beginPath(); ctx.arc(lx, ly, lr, 0, 7); ctx.fill()
            ctx.textAlign = "left"; ctx.textBaseline = "middle"
            ctx.fillStyle = lit ? "#ff8a7a" : "#4a3030"
            ctx.font = "bold " + Math.round(Math.max(8, h * 0.05)) + "px Consolas, Menlo, monospace"
            ctx.fillText("OVER", lx + lr + 4, ly)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
