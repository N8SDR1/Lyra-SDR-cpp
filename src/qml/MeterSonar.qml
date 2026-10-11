// Sonar — port of paintSonar() in the browser mock.
// Range rings, dim sweep, noise disc, one bloomed return, peak ring.

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: sonar
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
            var t = Date.now() / 1000
            var g = ctx.createRadialGradient(w * 0.42, h * 0.52, 8, w * 0.42, h * 0.52, w * 0.5)
            g.addColorStop(0, "#102028"); g.addColorStop(1, "#070c10")
            ctx.fillStyle = g; ctx.fillRect(0, 0, w, h)
            var cx = w * 0.40, cy = h * 0.52, R = Math.min(w, h) * 0.40
            ctx.strokeStyle = "rgba(0,229,255,0.18)"; ctx.lineWidth = 1
            for (var i = 1; i <= 4; ++i) {
                ctx.beginPath(); ctx.arc(cx, cy, R * i / 4, 0, 7); ctx.stroke()
            }
            ctx.strokeStyle = "rgba(0,229,255,0.10)"
            for (var k = 0; k < 12; ++k) {
                var a = k / 12 * Math.PI * 2
                ctx.beginPath(); ctx.moveTo(cx, cy)
                ctx.lineTo(cx + R * Math.cos(a), cy + R * Math.sin(a)); ctx.stroke()
            }
            var noise = Meter.noiseLevel > 0.02 ? Meter.noiseLevel : 0.16
            ctx.save()
            ctx.beginPath()
            ctx.arc(cx, cy, R, 0, Math.PI * 2)
            ctx.arc(cx, cy, R * (1 - noise), 0, Math.PI * 2, true)
            ctx.clip()
            for (var n = 0; n < 40; ++n) {
                var ang = n * 2.399
                var rr = R * (1 - noise * Math.sqrt((n * 17 % 40) / 40))
                ctx.fillStyle = "rgba(120,180,190,0.35)"
                ctx.fillRect(cx + Math.cos(ang) * rr, cy + Math.sin(ang) * rr, 1.2, 1.2)
            }
            ctx.restore()
            var sweep = (t * 0.35) % (Math.PI * 2)
            ctx.save()
            ctx.translate(cx, cy); ctx.rotate(sweep)
            var sg = ctx.createLinearGradient(0, 0, R, 0)
            sg.addColorStop(0, "rgba(0,229,255,0.0)")
            sg.addColorStop(1, "rgba(0,229,255,0.16)")
            ctx.fillStyle = sg
            ctx.beginPath(); ctx.moveTo(0, 0); ctx.arc(0, 0, R, -0.4, 0); ctx.closePath(); ctx.fill()
            ctx.restore()
            // Strongest return sits on the center. A weak reading stays on the rim.
            var ba = -Math.PI / 2 + 0.35 * Math.sin(t * 0.4)
            var br = R * (1 - Meter.level)
            var bx = cx + Math.cos(ba) * br, by = cy + Math.sin(ba) * br
            var blip = ctx.createRadialGradient(bx, by, 0, bx, by, 14)
            blip.addColorStop(0, "#ffffff")
            blip.addColorStop(0.25, MeterInk.ink(Meter.level, Meter.normAtS9))
            blip.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = blip; ctx.beginPath(); ctx.arc(bx, by, 14, 0, 7); ctx.fill()
            ctx.beginPath(); ctx.arc(cx, cy, R * (1 - Meter.peak), 0, 7)
            ctx.strokeStyle = "rgba(255,214,130,0.45)"; ctx.lineWidth = 1; ctx.stroke()
            ctx.textAlign = "left"; ctx.textBaseline = "middle"
            ctx.fillStyle = "#d7f7ff"
            ctx.font = "bold " + Math.round(Math.max(16, h * 0.12)) + "px Consolas, Menlo, monospace"
            ctx.fillText(Meter.text, w * 0.70, h * 0.40)
            ctx.fillStyle = "#8ec9c4"
            ctx.font = Math.round(Math.max(10, h * 0.07)) + "px Consolas, Menlo, monospace"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.70, h * 0.54)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.70, h * 0.66)
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.70, h * 0.78)
        }
    }

    // Sweep animation.  The face is only loaded while it is the active
    // style, and Canvas defers requestPaint() until it is rendered.
    Timer { interval: 50; running: true; repeat: true; onTriggered: cv.requestPaint() }
    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
