// Sonar — range rings and one return. Distance from center is the level.
// The return is cyan (amber / red only past the danger point).

import QtQuick

Item {
    id: sonar

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
            var col = sonar.ink(Meter.level)
            var g = ctx.createRadialGradient(w * 0.38, h * 0.52, 8, w * 0.38, h * 0.52, w * 0.55)
            g.addColorStop(0, "#102028")
            g.addColorStop(1, "#070c10")
            ctx.fillStyle = g; ctx.fillRect(0, 0, w, h)
            var cx = w * 0.36, cy = h * 0.52
            var R = Math.min(w, h) * 0.40
            ctx.strokeStyle = "rgba(0,229,255,0.20)"; ctx.lineWidth = 1
            for (var i = 1; i <= 4; ++i) {
                ctx.beginPath(); ctx.arc(cx, cy, R * i / 4, 0, 7); ctx.stroke()
            }
            ctx.strokeStyle = "rgba(0,229,255,0.10)"
            for (var k = 0; k < 12; ++k) {
                var a = k / 12 * Math.PI * 2
                ctx.beginPath(); ctx.moveTo(cx, cy)
                ctx.lineTo(cx + R * Math.cos(a), cy + R * Math.sin(a)); ctx.stroke()
            }
            if (Meter.noiseLevel > 0.02) {
                ctx.save()
                ctx.beginPath(); ctx.arc(cx, cy, R * Meter.noiseLevel, 0, 7); ctx.clip()
                ctx.fillStyle = "rgba(0,229,255,0.35)"
                for (var n = 0; n < 28; ++n) {
                    var ang = n * 2.4
                    var rr = Math.sqrt((n * 17 % 28) / 28) * R * Meter.noiseLevel
                    ctx.fillRect(cx + Math.cos(ang) * rr, cy + Math.sin(ang) * rr, 1.2, 1.2)
                }
                ctx.restore()
            }
            var sweep = (Date.now() / 1000 * 0.7) % (Math.PI * 2)
            ctx.save()
            ctx.translate(cx, cy); ctx.rotate(sweep)
            var sg = ctx.createLinearGradient(0, 0, R, 0)
            sg.addColorStop(0, "rgba(0,229,255,0)")
            sg.addColorStop(1, "rgba(0,229,255,0.18)")
            ctx.fillStyle = sg
            ctx.beginPath(); ctx.moveTo(0, 0); ctx.arc(0, 0, R, -0.45, 0); ctx.closePath(); ctx.fill()
            ctx.restore()
            var ba = -Math.PI / 2 + 0.25 * Math.sin(Date.now() / 1800)
            var br = R * Meter.level
            var bx = cx + Math.cos(ba) * br, by = cy + Math.sin(ba) * br
            var blip = ctx.createRadialGradient(bx, by, 0, bx, by, 12)
            blip.addColorStop(0, "#ffffff")
            blip.addColorStop(0.3, col)
            blip.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = blip
            ctx.beginPath(); ctx.arc(bx, by, 12, 0, 7); ctx.fill()
            if (Meter.peak > 0.02) {
                ctx.beginPath(); ctx.arc(cx, cy, R * Meter.peak, 0, 7)
                ctx.strokeStyle = "rgba(255,214,130,0.55)"; ctx.stroke()
            }
            ctx.textAlign = "left"
            ctx.textBaseline = "middle"
            ctx.fillStyle = col
            ctx.font = "600 " + Math.max(15, h * 0.13) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.66, h * 0.38)
            ctx.fillStyle = "#a8bccb"
            ctx.font = Math.max(10, h * 0.068) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.66, h * 0.52)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.66, h * 0.64)
        }
    }

    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
