// Sonar — PPI with compass ring.  The return rides the sweep all the
// way round: each bearing keeps the level it saw and fades over one
// turn.  Distance from the rim is strength (strong near the centre).
// Same calibrated 0…1 as Arc.  Replaces the Radar face (style 3).

import QtQuick
import "MeterInk.js" as MeterInk

Item {
    id: sonar
    anchors.fill: parent

    readonly property int bins: 120
    property var hits: []
    property real lastSweep: -1

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
            var labPx = Math.round(Math.max(8, h * 0.055))
            var cx = w * 0.36, cy = h * 0.5
            var R = Math.max(10, Math.min(h * 0.5 - labPx * 2.0, w * 0.70 - cx - labPx * 2.6))
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
            ctx.lineWidth = 1
            for (var d = 0; d < 360; d += 10) {
                var ca = -Math.PI / 2 + d * Math.PI / 180
                var major = d % 30 === 0
                var r0 = R + 1, r1 = R + (major ? labPx * 0.55 : labPx * 0.3)
                ctx.strokeStyle = major ? "rgba(0,229,255,0.55)" : "rgba(0,229,255,0.28)"
                ctx.beginPath()
                ctx.moveTo(cx + r0 * Math.cos(ca), cy + r0 * Math.sin(ca))
                ctx.lineTo(cx + r1 * Math.cos(ca), cy + r1 * Math.sin(ca))
                ctx.stroke()
            }
            ctx.textAlign = "center"; ctx.textBaseline = "middle"
            var cardinal = { 0: "N", 90: "E", 180: "S", 270: "W" }
            for (var b = 0; b < 360; b += 30) {
                var la = -Math.PI / 2 + b * Math.PI / 180
                var lr = R + labPx * 1.3
                var isCard = cardinal[b] !== undefined
                ctx.fillStyle = isCard ? "#00e5ff" : "#8a9aac"
                ctx.font = (isCard ? "bold " + labPx : Math.round(labPx * 0.85)) + "px Consolas"
                ctx.fillText(isCard ? cardinal[b] : String(b),
                             cx + lr * Math.cos(la), cy + lr * Math.sin(la))
            }

            var twoPi = Math.PI * 2
            var sweep = (t * 0.35) % twoPi
            if (sonar.hits.length !== sonar.bins) {
                var fresh = []
                for (var z = 0; z < sonar.bins; ++z) fresh.push(-1)
                sonar.hits = fresh
                sonar.lastSweep = sweep
            }
            var binW = twoPi / sonar.bins
            var from = Math.floor(sonar.lastSweep / binW)
            var to = Math.floor(sweep / binW)
            var steps = Math.max(1, (to - from + sonar.bins) % sonar.bins)
            for (var st = 0; st < steps; ++st)
                sonar.hits[(to - st + sonar.bins) % sonar.bins] = Meter.level
            sonar.lastSweep = sweep
            for (var hb = 0; hb < sonar.bins; ++hb) {
                var lv = sonar.hits[hb]
                if (lv < 0) continue
                var ba0 = (hb + 0.5) * binW
                var age = ((sweep - ba0) % twoPi + twoPi) % twoPi / twoPi
                var fade = Math.pow(1 - age, 1.6)
                if (fade < 0.03) continue
                var hr = R * (1 - Math.max(0, Math.min(1, lv)))
                ctx.globalAlpha = fade
                ctx.fillStyle = MeterInk.ink(lv, Meter.normAtS9)
                ctx.beginPath()
                ctx.arc(cx + Math.cos(ba0) * hr, cy + Math.sin(ba0) * hr,
                        Math.max(1.5, R * 0.025), 0, 7)
                ctx.fill()
            }
            ctx.globalAlpha = 1
            ctx.save()
            ctx.translate(cx, cy); ctx.rotate(sweep)
            var sg = ctx.createLinearGradient(0, 0, R, 0)
            sg.addColorStop(0, "rgba(0,229,255,0.0)")
            sg.addColorStop(1, "rgba(0,229,255,0.16)")
            ctx.fillStyle = sg
            ctx.beginPath(); ctx.moveTo(0, 0); ctx.arc(0, 0, R, -0.4, 0); ctx.closePath(); ctx.fill()
            ctx.restore()
            var ba = sweep
            var br = R * (1 - Meter.level)
            var bx = cx + Math.cos(ba) * br, by = cy + Math.sin(ba) * br
            var blipR = Math.max(6, R * 0.12)
            var blip = ctx.createRadialGradient(bx, by, 0, bx, by, blipR)
            blip.addColorStop(0, "#ffffff")
            blip.addColorStop(0.25, MeterInk.ink(Meter.level, Meter.normAtS9))
            blip.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = blip; ctx.beginPath(); ctx.arc(bx, by, blipR, 0, 7); ctx.fill()
            ctx.beginPath(); ctx.arc(cx, cy, R * (1 - Meter.peak), 0, 7)
            ctx.strokeStyle = "rgba(255,214,130,0.45)"; ctx.lineWidth = 1; ctx.stroke()
            ctx.textAlign = "left"; ctx.textBaseline = "middle"
            ctx.fillStyle = "#d7f7ff"
            ctx.font = "bold " + Math.round(Math.max(16, h * 0.12)) + "px Consolas"
            ctx.fillText(Meter.text, w * 0.70, h * 0.40)
            ctx.fillStyle = "#8ec9c4"
            ctx.font = Math.round(Math.max(10, h * 0.07)) + "px Consolas"
            if (Meter.dbmText.length) ctx.fillText(Meter.dbmText, w * 0.70, h * 0.54)
            ctx.fillStyle = "#6fe0b0"
            if (Meter.snrText.length) ctx.fillText(Meter.snrText, w * 0.70, h * 0.66)
            ctx.fillStyle = "#a0d8e0"
            if (Meter.secondary2Text.length) ctx.fillText(Meter.secondary2Text, w * 0.70, h * 0.78)
        }
    }

    Timer { interval: 50; running: true; repeat: true; onTriggered: cv.requestPaint() }
    Connections { target: Meter; function onUpdated() { cv.requestPaint() } }
    Component.onCompleted: cv.requestPaint()
}
