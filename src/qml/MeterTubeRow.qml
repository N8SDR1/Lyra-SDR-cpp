// Shared glass-digit face for Nixie, Numitron, VFD, and Panaplex.
// One instrument color per kind — the filaments do not run a spectrum.
// The thin line under the digits is the analog level, same hue.

import QtQuick

Item {
    id: root
    anchors.fill: parent
    // nixie | numitron | vfd | panaplex
    property string kind: "nixie"

    function segMask(ch) {
        switch (ch) {
        case "0": return 0x3F
        case "1": return 0x06
        case "2": return 0x5B
        case "3": return 0x4F
        case "4": return 0x66
        case "5": return 0x6D
        case "6": return 0x7D
        case "7": return 0x07
        case "8": return 0x7F
        case "9": return 0x6F
        case "S":
        case "s": return 0x6D
        case "-":
        case "—": return 0x40
        default:  return -1
        }
    }

    function filament() {
        if (kind === "numitron") return "#ffd7a8"
        if (kind === "vfd")      return "#7dffe0"
        if (kind === "panaplex") return "#ff6a2a"
        return "#ffb15a"
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

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

        function strokeSeg(ctx, x0, y0, x1, y1, col, wdt, alpha) {
            ctx.globalAlpha = alpha
            ctx.strokeStyle = col
            ctx.lineWidth = wdt
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(x0, y0)
            ctx.lineTo(x1, y1)
            ctx.stroke()
            ctx.globalAlpha = 1
        }

        function drawMask(ctx, x, y, w, h, mask, col, ghost) {
            var x0 = x + w * 0.22, x1 = x + w * 0.78
            var y0 = y + h * 0.10, y1 = y + h * 0.50, y2 = y + h * 0.90
            var thick = Math.max(1.4, Math.min(w, h) * (root.kind === "numitron" ? 0.045 : 0.07))
            var segs = [
                [1,  x0, y0, x1, y0],
                [2,  x1, y0, x1, y1],
                [4,  x1, y1, x1, y2],
                [8,  x0, y2, x1, y2],
                [16, x0, y1, x0, y2],
                [32, x0, y0, x0, y1],
                [64, x0, y1, x1, y1]
            ]
            for (var i = 0; i < segs.length; ++i) {
                var s = segs[i]
                var on = (mask & s[0]) !== 0
                if (!on && !ghost) continue
                if (on) {
                    strokeSeg(ctx, s[1], s[2], s[3], s[4], col, thick * 2.4, 0.22)
                    strokeSeg(ctx, s[1], s[2], s[3], s[4], col, thick, 1)
                    strokeSeg(ctx, s[1], s[2], s[3], s[4], "#fff6e8", thick * 0.35, 0.9)
                } else {
                    strokeSeg(ctx, s[1], s[2], s[3], s[4], col, thick * 0.7, 0.10)
                }
            }
        }

        function drawExtra(ctx, ch, x, y, w, h, col) {
            var cx = x + w / 2, cy = y + h / 2
            ctx.strokeStyle = col
            ctx.fillStyle = col
            ctx.lineWidth = Math.max(1.4, w * 0.08)
            ctx.lineCap = "round"
            if (ch === "." ) {
                ctx.beginPath()
                ctx.arc(x + w * 0.72, y + h * 0.86, Math.max(1.4, w * 0.07), 0, 7)
                ctx.fill()
            } else if (ch === ":") {
                ctx.beginPath(); ctx.arc(cx, y + h * 0.38, Math.max(1.3, w * 0.06), 0, 7); ctx.fill()
                ctx.beginPath(); ctx.arc(cx, y + h * 0.68, Math.max(1.3, w * 0.06), 0, 7); ctx.fill()
            } else if (ch === "+") {
                ctx.beginPath(); ctx.moveTo(cx, y + h * 0.32); ctx.lineTo(cx, y + h * 0.72); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(x + w * 0.28, cy); ctx.lineTo(x + w * 0.72, cy); ctx.stroke()
            } else if (ch === "W" || ch === "w") {
                ctx.beginPath()
                ctx.moveTo(x + w * 0.16, y + h * 0.22)
                ctx.lineTo(x + w * 0.32, y + h * 0.82)
                ctx.lineTo(cx, y + h * 0.42)
                ctx.lineTo(x + w * 0.68, y + h * 0.82)
                ctx.lineTo(x + w * 0.84, y + h * 0.22)
                ctx.stroke()
            } else if (ch !== " ") {
                ctx.font = Math.round(h * 0.55) + "px Consolas, Menlo, monospace"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(ch, cx, cy)
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var text = Meter.text.length ? Meter.text : "—"
            var chars = []
            for (var i = 0; i < text.length; ++i) chars.push(text.charAt(i))
            var n = Math.max(1, chars.length)
            var top = 8
            var bot = h * 0.72
            var gap = root.kind === "nixie" ? 6 : 3
            var cellW = Math.min((w - 16 - gap * (n - 1)) / n, h * 0.42)
            var rowW = n * cellW + (n - 1) * gap
            var x0 = (w - rowW) / 2
            var col = root.filament()
            var plate = root.kind !== "nixie"

            if (plate) {
                var bg = ctx.createLinearGradient(0, top, 0, bot)
                if (root.kind === "vfd") {
                    bg.addColorStop(0, "#0c241c")
                    bg.addColorStop(1, "#07140f")
                } else if (root.kind === "numitron") {
                    bg.addColorStop(0, "#1a1612")
                    bg.addColorStop(1, "#0c0a08")
                } else {
                    bg.addColorStop(0, "#2a100c")
                    bg.addColorStop(1, "#120806")
                }
                round(ctx, 10, top, w - 20, bot - top, 8)
                ctx.fillStyle = bg
                ctx.fill()
                if (root.kind === "vfd") {
                    ctx.strokeStyle = "rgba(125,255,224,0.08)"
                    ctx.lineWidth = 1
                    for (var gy = top + 6; gy < bot; gy += 4) {
                        ctx.beginPath(); ctx.moveTo(14, gy); ctx.lineTo(w - 14, gy); ctx.stroke()
                    }
                }
                // glass
                var sheen = ctx.createLinearGradient(0, top, 0, top + (bot - top) * 0.45)
                sheen.addColorStop(0, "rgba(255,255,255,0.16)")
                sheen.addColorStop(1, "rgba(255,255,255,0)")
                ctx.fillStyle = sheen
                round(ctx, 10, top, w - 20, (bot - top) * 0.45, 8)
                ctx.fill()
            }

            for (var c = 0; c < n; ++c) {
                var x = x0 + c * (cellW + gap)
                var y = top + 4
                var ch = chars[c]
                var cw = cellW
                var chh = bot - top - 8
                if (!plate) {
                    round(ctx, x, y, cw, chh, cw * 0.28)
                    var tube = ctx.createLinearGradient(x, y, x + cw, y)
                    tube.addColorStop(0, "#14080c")
                    tube.addColorStop(0.5, "#2a1216")
                    tube.addColorStop(1, "#10060a")
                    ctx.fillStyle = tube
                    ctx.fill()
                    var gas = ctx.createRadialGradient(x + cw / 2, y + chh * 0.45, 2,
                                                       x + cw / 2, y + chh * 0.5, cw * 0.7)
                    gas.addColorStop(0, "rgba(255,120,50,0.28)")
                    gas.addColorStop(1, "rgba(0,0,0,0)")
                    ctx.fillStyle = gas
                    ctx.fill()
                }
                var mask = root.segMask(ch)
                if (mask >= 0)
                    drawMask(ctx, x, y, cw, chh, mask, col, root.kind === "nixie" || root.kind === "numitron")
                else
                    drawExtra(ctx, ch, x, y, cw, chh, col)
                if (!plate) {
                    ctx.save()
                    round(ctx, x, y, cw, chh, cw * 0.28)
                    ctx.clip()
                    var gl = ctx.createLinearGradient(x, y, x + cw * 0.45, y + chh)
                    gl.addColorStop(0, "rgba(255,255,255,0.18)")
                    gl.addColorStop(0.35, "rgba(255,255,255,0)")
                    ctx.fillStyle = gl
                    ctx.fillRect(x, y, cw, chh * 0.5)
                    ctx.restore()
                    ctx.strokeStyle = "rgba(255,190,140,0.28)"
                    ctx.lineWidth = 1
                    round(ctx, x, y, cw, chh, cw * 0.28)
                    ctx.stroke()
                }
            }

            // Analog position, same hue as the filaments. Peak is a pale cap.
            var bx = 16, bw = w - 32, by = h * 0.78, bh = Math.max(3, h * 0.035)
            round(ctx, bx, by, bw, bh, bh / 2)
            ctx.fillStyle = root.kind === "vfd" ? "#06140f" : "#12080c"
            ctx.fill()
            ctx.save()
            round(ctx, bx, by, bw, bh, bh / 2)
            ctx.clip()
            ctx.fillStyle = col
            ctx.globalAlpha = 0.95
            ctx.fillRect(bx, by, bw * Meter.level, bh)
            ctx.restore()
            ctx.globalAlpha = 1
            if (Meter.peak > 0.01) {
                ctx.fillStyle = "#fff6e0"
                ctx.fillRect(bx + bw * Meter.peak - 1, by - 3, 2, bh + 6)
            }
            if (Meter.maxPeakEnabled && Meter.maxPeak > 0.01) {
                ctx.fillStyle = "#ff4136"
                ctx.fillRect(bx + bw * Meter.maxPeak - 0.7, by - 5, 1.4, bh + 10)
            }

            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            ctx.font = Math.max(10, h * 0.075) + "px Consolas, Menlo, monospace"
            ctx.fillStyle = "#c9b8a4"
            var sub = Meter.dbmText
            if (Meter.snrText.length) sub += (sub.length ? "   " : "") + Meter.snrText
            if (Meter.secondary2Text.length)
                sub += (sub.length ? "   " : "") + Meter.secondary2Text
            if (sub.length) ctx.fillText(sub, w / 2, h * 0.86)
        }
    }

    Connections {
        target: Meter
        function onUpdated() { cv.requestPaint() }
    }
    onKindChanged: cv.requestPaint()
    Component.onCompleted: cv.requestPaint()
}
