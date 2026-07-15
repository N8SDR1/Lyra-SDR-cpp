// Lyra — Horizon Arc S-meter (default style).
//
// A bottom-opening sweep arc against a fixed, calibrated S-scale.
// The value arc fills with a zone colour + glow bloom; a bright comet
// head marks the instantaneous level, a thin pip rides the peak-hold,
// and a faint sparkline in the arc's belly shows the recent signal
// history. Big crisp numerics in the centre.
//
// Drawn with Canvas — only thin strokes/dots carry shadowBlur, so the
// blurred area stays tiny (large-area blur on the GUI thread is what
// would starve the panadapter).

import QtQuick

Item {
    id: arc
    anchors.fill: parent

    // ── EXPERIMENT: "angled gauge" look ───────────────────────────────
    // Widen the dial slightly and tilt the WHOLE thing back at the top
    // with a real 3D X-axis perspective rotation (arc + centre readout
    // tilt together, like an angled dashboard gauge).  Pure render
    // transform — the drawing geometry + readouts are unchanged.  To
    // REVERT to the flat arc, delete this `transform:` block.  If it
    // leans the wrong way (forward instead of back), flip `angle` to -16.
    transform: [
        Scale {
            origin.x: arc.width / 2; origin.y: arc.height / 2
            xScale: 1.18; yScale: 1.0
        },
        Rotation {
            origin.x: arc.width / 2; origin.y: arc.height / 2
            axis { x: 1; y: 0; z: 0 }
            angle: 20
        }
    ]

    // Sweep opening at the bottom (gap centred on south).  A SMALLER
    // sweep widens the gap between the two bottom legs — a lower, wider
    // "stance" that flattens the arc.  startDeg is derived so the gap
    // stays centred on the bottom no matter the sweep (one knob: sweepDeg;
    // 220 = original, 200 = slightly wider stance).
    readonly property real sweepDeg: 200
    readonly property real startDeg: 270 - sweepDeg / 2

    // Responsive geometry: radius bounded by BOTH the half-width and the
    // height, because everything here scales with the radius — including the
    // numerics stacked under the arc centre, which is what the height budget
    // has to pay for.  Walking the panel top-down in units of rad:
    //
    //     mm                      panel margin
    //   + 0.241 · rad             apex clearance (topClear, below)
    //   + 1.000 · rad             the radius itself, down to the centre
    //   + 0.536 · rad             the readout column: it starts 0.42·rad ABOVE
    //                             the centre and runs three Consolas lines
    //                             (0.42 + 0.22 + 0.17)·rad deep, ~1.18 line box
    //   + mm                      panel margin
    //
    // which totals 2·mm + 1.777·rad.  Round the divisor up to 1.80 for font
    // metrics.  The old 1.62 paid for the arc but not the numerics under it,
    // so on a wide dock (where the radius grows until HEIGHT is what binds)
    // the SNR line ran off the bottom of the panel.
    readonly property real mm:  4
    readonly property real ccx: width / 2
    readonly property real rad: Math.max(20,
        Math.min((width / 2 - mm) / 1.222, (height - 2 * mm) / 1.80))
    readonly property real lw:  Math.max(7, rad * 0.17)
    // Tick-label font + the top clearance it (and the band) need above the
    // arc apex.  Outer labels sit at rad + lw*0.78; this reserves that
    // plus half the glyph height so the apex label clears the panel top.
    readonly property real tickFont: Math.max(11, rad * 0.17)
    readonly property real topClear: lw * 0.82 + tickFont * 0.6
    readonly property real ccy: mm + topClear + rad

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
            var cx = arc.ccx, cy = arc.ccy, r = arc.rad, lw = arc.lw
            var a0 = arc.startDeg * Math.PI / 180
            var sweep = arc.sweepDeg * Math.PI / 180

            var level = Meter.level
            var peak  = Meter.peak
            var maxPk = Meter.maxPeak
            var maxOn = Meter.maxPeakEnabled
            var glow  = Meter.glow
            var col   = arc.zoneColor(level)

            ctx.lineCap = "round"

            // ── Glass well: recessed groove (dark wide backing) so the
            // track sits IN a glassy channel.  Static, just two strokes. ──
            ctx.lineWidth = lw * 1.7
            ctx.strokeStyle = "#070d12"
            ctx.beginPath()
            ctx.arc(cx, cy, r, a0, a0 + sweep, false)
            ctx.stroke()

            // ── Track ──
            ctx.lineWidth = lw
            ctx.strokeStyle = "#16242e"
            ctx.beginPath()
            ctx.arc(cx, cy, r, a0, a0 + sweep, false)
            ctx.stroke()

            // Inner-edge highlight — a faint top-light on the glass rim.
            ctx.globalAlpha = 0.10
            ctx.strokeStyle = "#bfe9ff"
            ctx.lineWidth = Math.max(1.5, lw * 0.12)
            ctx.beginPath()
            ctx.arc(cx, cy, r - lw * 0.42, a0, a0 + sweep, false)
            ctx.stroke()
            ctx.globalAlpha = 1.0

            // ── Noise-floor zone: a dim band from the start up to the
            // current noise floor (everything under it is "just noise"). ──
            if (Meter.noiseLevel > 0.001) {
                ctx.globalAlpha = 0.55
                ctx.strokeStyle = "#3a5666"
                ctx.lineWidth = lw * 0.5
                ctx.beginPath()
                ctx.arc(cx, cy, r, a0, a0 + sweep * Meter.noiseLevel, false)
                ctx.stroke()
                ctx.globalAlpha = 1.0
            }

            // ── Afterglow ghost ──
            if (glow > level + 0.002) {
                ctx.globalAlpha = 0.28
                ctx.strokeStyle = col
                ctx.beginPath()
                ctx.arc(cx, cy, r, a0, a0 + sweep * glow, false)
                ctx.stroke()
                ctx.globalAlpha = 1.0
            }

            // ── Value arc + glow ──
            // Glow is faked with stacked translucent strokes (NOT
            // ctx.shadowBlur — that's a software blur whose cost scales
            // with area×radius and stalled the panadapter when the panel
            // grew).  Plain fills are GPU-rasterized and scale linearly.
            if (level > 0.001) {
                var aEnd = a0 + sweep * level
                ctx.strokeStyle = col
                // wide soft halo → mid → core
                ctx.globalAlpha = 0.12; ctx.lineWidth = lw * 2.1
                ctx.beginPath(); ctx.arc(cx, cy, r, a0, aEnd, false); ctx.stroke()
                ctx.globalAlpha = 0.22; ctx.lineWidth = lw * 1.45
                ctx.beginPath(); ctx.arc(cx, cy, r, a0, aEnd, false); ctx.stroke()
                ctx.globalAlpha = 1.0;  ctx.lineWidth = lw
                ctx.beginPath(); ctx.arc(cx, cy, r, a0, aEnd, false); ctx.stroke()

                // Comet head — layered discs (halo + bright core).
                var hx = cx + r * Math.cos(aEnd)
                var hy = cy + r * Math.sin(aEnd)
                ctx.fillStyle = col
                ctx.globalAlpha = 0.20
                ctx.beginPath(); ctx.arc(hx, hy, lw * 1.05, 0, Math.PI * 2); ctx.fill()
                ctx.globalAlpha = 1.0
                ctx.fillStyle = "#ffffff"
                ctx.beginPath(); ctx.arc(hx, hy, lw * 0.42, 0, Math.PI * 2); ctx.fill()
            }

            // ── Peak-hold pip ──
            if (peak > 0.01) {
                var aP = a0 + sweep * peak
                var c1 = Math.cos(aP), s1 = Math.sin(aP)
                ctx.strokeStyle = "#ffe48a"
                ctx.lineWidth = Math.max(2, lw * 0.18)
                ctx.beginPath()
                ctx.moveTo(cx + (r - lw * 0.62) * c1, cy + (r - lw * 0.62) * s1)
                ctx.lineTo(cx + (r + lw * 0.58) * c1, cy + (r + lw * 0.58) * s1)
                ctx.stroke()
            }

            // ── Max-hold high-water marker (distinct red, longer reach,
            // eases down gently — see MeterModel max-hold) ──
            if (maxOn && maxPk > 0.01) {
                var aM = a0 + sweep * maxPk
                var cM = Math.cos(aM), sM = Math.sin(aM)
                ctx.strokeStyle = "#ff5a5a"
                ctx.lineWidth = Math.max(2, lw * 0.16)
                ctx.beginPath()
                ctx.moveTo(cx + (r - lw * 0.78) * cM, cy + (r - lw * 0.78) * sM)
                ctx.lineTo(cx + (r + lw * 0.72) * cM, cy + (r + lw * 0.72) * sM)
                ctx.stroke()
            }

            // ── Tick labels around the outer edge ──
            ctx.font = Math.round(arc.tickFont) + "px Consolas"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            var ticks = Meter.tickMarks()
            for (var i = 0; i < ticks.length; ++i) {
                var t = ticks[i]
                var aa = a0 + sweep * t.pos
                var tr = r + lw * 0.78
                ctx.fillStyle = (t.pos >= Meter.normAtS9) ? "#ff8a6a" : "#9fb6c6"
                ctx.fillText(t.label,
                             cx + tr * Math.cos(aa),
                             cy + tr * Math.sin(aa))
            }

            // ── History sparkline in the arc's belly ──
            var hist = Meter.history
            if (hist.length > 1) {
                var gw = r * 1.0, gh = r * 0.22
                var gx = cx - gw / 2
                var gy = cy + r * 0.30
                ctx.save()
                ctx.globalAlpha = 0.5
                ctx.strokeStyle = "#36d6ff"
                ctx.lineWidth = 1.5
                ctx.beginPath()
                for (var k = 0; k < hist.length; ++k) {
                    var px = gx + gw * (k / (hist.length - 1))
                    var py = gy + gh - gh * Math.max(0, Math.min(1, hist[k]))
                    if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.stroke()
                ctx.restore()
            }

            // ── Glass gloss: soft bright crescents along the upper rim,
            // like light glinting off a glass cover over the gauge.
            // "lighter" blend so it glows over the coloured arc. ──
            ctx.save()
            ctx.globalCompositeOperation = "lighter"
            ctx.lineCap = "round"
            ctx.strokeStyle = "rgba(255,255,255,0.11)"
            ctx.lineWidth = lw * 0.44
            ctx.beginPath()
            ctx.arc(cx, cy, r - lw * 0.10, a0 + sweep * 0.06, a0 + sweep * 0.44, false)
            ctx.stroke()
            ctx.strokeStyle = "rgba(255,255,255,0.10)"
            ctx.lineWidth = lw * 0.18
            ctx.beginPath()
            ctx.arc(cx, cy, r + lw * 0.34, a0 + sweep * 0.10, a0 + sweep * 0.30, false)
            ctx.stroke()
            ctx.restore()
        }
    }

    // ── Centre numerics (crisp QML text) ──
    Column {
        x: arc.ccx - width / 2
        y: arc.ccy - arc.rad * 0.42
        width: arc.width
        spacing: 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Meter.text
            color: arc.zoneColor(Meter.level)
            font.family: "Consolas"; font.bold: true
            font.pixelSize: Math.max(20, arc.rad * 0.42)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Meter.dbmText
            color: "#a8bccb"
            font.family: "Consolas"
            font.pixelSize: Math.max(12, arc.rad * 0.22)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Meter.snrText
            color: "#6fe0b0"
            font.family: "Consolas"
            font.pixelSize: Math.max(10, arc.rad * 0.17)
        }
        // Second secondary digital readout (task #37) — only TX uses it;
        // RX leaves it empty so the Column visually collapses to two
        // small lines (dBm + SNR) like before.  Slightly cooler tint
        // than snrText's green so the two are visually distinct.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Meter.secondary2Text
            visible: text.length > 0
            color: "#a0d8e0"
            font.family: "Consolas"
            font.pixelSize: Math.max(10, arc.rad * 0.17)
        }
    }

    Connections {
        target: Meter
        function onUpdated() { cv.requestPaint() }
    }
    Component.onCompleted: cv.requestPaint()
}
