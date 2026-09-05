// Lyra — Plasma Bar S-meter (alternate style).
//
// A continuous horizontal fill that climbs through palette zones
// cyan→green→amber→red, with a glowing meniscus, a phosphor afterglow
// that trails the drop, a peak-hold cap, a noise-floor marker + SNR
// readout, a faint segment overlay (fluid + stepped hybrid), and a
// glass reflection shelf beneath. Fixed S-scale ticks below.
//
// Canvas-drawn with NO shadowBlur — glow is stacked translucent fills
// so per-frame cost scales linearly with size (a software blur whose
// cost grows with area would starve the panadapter).

import QtQuick

Item {
    id: bar
    anchors.fill: parent

    function zoneColor(v) {
        var s9 = Meter.normAtS9
        if (v >= s9 + 0.16) return "#ff4d4d"
        if (v >= s9)        return "#ff9a3c"
        if (v >= s9 * 0.55) return "#5dff9a"
        return "#36d6ff"
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var mL = 12, mR = 12
            var barX = mL
            var barW = w - mL - mR
            var barH = Math.max(16, h * 0.20)
            var barY = h * 0.30

            var level = Meter.level
            var peak  = Meter.peak
            var maxPk = Meter.maxPeak
            var maxOn = Meter.maxPeakEnabled
            var glow  = Meter.glow
            var noise = Meter.noiseLevel
            var s9    = Meter.normAtS9
            var col   = bar.zoneColor(level)

            // Zone gradient (shared by fill, afterglow, reflection).
            var grad = ctx.createLinearGradient(barX, 0, barX + barW, 0)
            grad.addColorStop(0.0, "#36d6ff")
            grad.addColorStop(Math.min(0.33, s9 - 0.02), "#5dff9a")
            grad.addColorStop(s9, "#ff9a3c")
            grad.addColorStop(1.0, "#ff4d4d")

            // ── Track (recessed) ──
            ctx.fillStyle = "#0c161d"
            ctx.fillRect(barX, barY, barW, barH)

            // ── Noise-floor zone (dim fill up to the floor) ──
            if (noise > 0.001) {
                ctx.globalAlpha = 0.5
                ctx.fillStyle = "#1b2c38"
                ctx.fillRect(barX, barY, barW * noise, barH)
                ctx.globalAlpha = 1.0
            }

            // ── Afterglow (trails the drop) ──
            if (glow > level + 0.002) {
                ctx.globalAlpha = 0.22
                ctx.fillStyle = grad
                ctx.fillRect(barX, barY, barW * glow, barH)
                ctx.globalAlpha = 1.0
            }

            // ── Value fill (flat gradient, no blur) ──
            if (level > 0.001) {
                ctx.fillStyle = grad
                ctx.fillRect(barX, barY, barW * level, barH)
            }

            // ── #7 Segment overlay: faint dividers across the bar so it
            // reads as fluid + stepped (neither LED nor plain fill) ──
            var segN = 30
            ctx.globalAlpha = 0.5
            ctx.fillStyle = "#0c161d"
            for (var sgi = 1; sgi < segN; ++sgi) {
                var sgx = barX + barW * (sgi / segN)
                ctx.fillRect(sgx - 0.75, barY, 1.5, barH)
            }
            ctx.globalAlpha = 1.0

            // ── Meniscus glow (stacked translucent rects) ──
            if (level > 0.001) {
                var mx = barX + barW * level
                var gw = Math.max(8, barH * 0.5)
                ctx.fillStyle = col
                ctx.globalAlpha = 0.14
                ctx.fillRect(mx - gw, barY, gw * 2, barH)
                ctx.globalAlpha = 0.30
                ctx.fillRect(mx - gw * 0.45, barY, gw * 0.9, barH)
                ctx.globalAlpha = 1.0
                ctx.fillStyle = "#ffffff"
                ctx.fillRect(mx - 1.5, barY, 3, barH)
            }

            // Track border (drawn last so it frames everything).
            ctx.strokeStyle = "#243843"
            ctx.lineWidth = 1
            ctx.strokeRect(barX + 0.5, barY + 0.5, barW - 1, barH - 1)

            // ── Noise-floor marker line ──
            if (noise > 0.001) {
                var nx = barX + barW * noise
                ctx.strokeStyle = "#8fb2c6"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(nx, barY); ctx.lineTo(nx, barY + barH)
                ctx.stroke()
            }

            // ── Peak-hold cap ──
            if (peak > 0.01) {
                var px = barX + barW * peak
                ctx.fillStyle = "#ffe48a"
                ctx.fillRect(px - 1.5, barY - 2, 3, barH + 4)
            }

            // ── Max-hold high-water marker (distinct red, taller, eases
            // down gently — see MeterModel max-hold) ──
            if (maxOn && maxPk > 0.01) {
                var mx = barX + barW * maxPk
                ctx.fillStyle = "#ff5a5a"
                ctx.fillRect(mx - 1, barY - 5, 2, barH + 10)
                // small downward tick so it reads as a distinct cap
                ctx.beginPath()
                ctx.moveTo(mx - 4, barY - 5)
                ctx.lineTo(mx + 4, barY - 5)
                ctx.lineTo(mx, barY - 1)
                ctx.closePath(); ctx.fill()
            }

            // ── S9 boundary (faint divider into the red zone) ──
            var sx = barX + barW * s9
            ctx.strokeStyle = "#5a4030"
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(sx, barY - 2); ctx.lineTo(sx, barY + barH + 2)
            ctx.stroke()

            // ── #8 Reflection shelf (mirrored fading copy below) ──
            var refY = barY + barH + 3
            var refH = barH * 0.5
            if (level > 0.001) {
                ctx.globalAlpha = 0.16
                ctx.fillStyle = grad
                ctx.fillRect(barX, refY, barW * level, refH)
                ctx.globalAlpha = 1.0
            }
            // Fade the reflection into the panel (transparent → bg).
            var mg = ctx.createLinearGradient(0, refY, 0, refY + refH)
            mg.addColorStop(0.0, "rgba(16,24,32,0.0)")
            mg.addColorStop(1.0, "rgba(16,24,32,1.0)")
            ctx.fillStyle = mg
            ctx.fillRect(barX, refY, barW, refH)

            // ── Tick marks + labels (below the reflection) ──
            var tickTop = refY + refH + 2
            // Canvas font strings bypass QFont::insertSubstitutions — spell out
            // the fallback list or macOS logs an invalid-family warning per repaint.
            ctx.font = Math.round(barH * 0.42) + "px Consolas, Menlo, monospace"
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            var ticks = Meter.tickMarks()
            for (var i = 0; i < ticks.length; ++i) {
                var t = ticks[i]
                var tx = barX + barW * t.pos
                ctx.strokeStyle = (t.pos >= s9) ? "#7a4a3a" : "#34505d"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(tx, tickTop)
                ctx.lineTo(tx, tickTop + (t.major ? 7 : 4))
                ctx.stroke()
                ctx.fillStyle = (t.pos >= s9) ? "#ff8a6a" : "#8aa0b0"
                ctx.fillText(t.label, tx, tickTop + 8)
            }
        }
    }

    // ── Readout (crisp QML text, top-left) ──
    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 14
        anchors.topMargin: 4
        spacing: 10
        Text {
            text: Meter.text
            color: bar.zoneColor(Meter.level)
            font.family: "Consolas"; font.bold: true
            font.pixelSize: Math.max(15, bar.height * 0.18)
        }
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            text: Meter.dbmText
            color: "#9fb4c4"
            font.family: "Consolas"
            font.pixelSize: Math.max(9, bar.height * 0.09)
        }
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            text: Meter.snrText
            color: "#6fe0b0"
            font.family: "Consolas"
            font.pixelSize: Math.max(9, bar.height * 0.09)
        }
        // Second secondary digital readout (task #37) — TX-only; RX
        // leaves it empty.  Cooler tint than snrText for visual
        // separation.
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            text: Meter.secondary2Text
            visible: text.length > 0
            color: "#a0d8e0"
            font.family: "Consolas"
            font.pixelSize: Math.max(9, bar.height * 0.09)
        }
    }

    Connections {
        target: Meter
        function onUpdated() { cv.requestPaint() }
    }
    Component.onCompleted: cv.requestPaint()
}
