// Lyra Helios — original sun + planet strip (not a stock photo).
// Same calibrated 0…1 as Arc.  Worlds sit in a poster-style row;
// they glow as the S / watts reading reaches them.  Pluto = S9+60.

import QtQuick

Item {
    id: face
    anchors.fill: parent

    readonly property color cCyan: "#00e5ff"
    readonly property color cGold: "#e8c477"
    readonly property color cMute: "#8a9aac"
    readonly property color cOk:   "#5dff9a"
    readonly property color cHot:  "#ff4d4d"
    readonly property bool  tx:    Stream.txDisplayActive
    readonly property int   heroSrc: tx ? 1 : 0

    // Relative radii vs Jupiter.  Colours are ours, not a scan.
    readonly property var roster: [
        { name: "Mer", col: "#c9b8a4", sz: 0.22, tick: "1" },
        { name: "Ven", col: "#d4a15a", sz: 0.36, tick: "3" },
        { name: "Ear", col: "#3aa0d8", sz: 0.38, tick: "5" },
        { name: "Mar", col: "#c45c3a", sz: 0.28, tick: "7" },
        { name: "Jup", col: "#c88840", sz: 1.00, tick: "9" },
        { name: "Sat", col: "#d4c090", sz: 0.82, tick: "+20" },
        { name: "Ura", col: "#8ec8d4", sz: 0.52, tick: "+40" },
        { name: "Nep", col: "#3a6ad4", sz: 0.50, tick: "+40" },
        { name: "Plu", col: "#c8d0d8", sz: 0.18, tick: "+60" }
    ]

    readonly property var fullName: [
        "MERCURY", "VENUS", "EARTH", "MARS", "JUPITER",
        "SATURN", "URANUS", "NEPTUNE", "PLUTO"
    ]

    property real holeAng: 0

    function tickPos(tick) {
        var ticks = Meter.tickMarksFor(0)   // S-scale only
        for (var t = 0; t < ticks.length; ++t)
            if (("" + ticks[t].label) === tick)
                return Math.max(0.06, Math.min(1, ticks[t].pos))
        return 0.5
    }

    // dB above S9 → 0…1 on the Arc scale (lerp between S9 / +20 / +40 / +60).
    function posPlus(db) {
        var s9 = tickPos("9")
        var p20 = tickPos("+20")
        var p40 = tickPos("+40")
        var p60 = tickPos("+60")
        if (db <= 0) return s9
        if (db <= 20) return s9 + (p20 - s9) * (db / 20)
        if (db <= 40) return p20 + (p40 - p20) * ((db - 20) / 20)
        if (db <= 60) return p40 + (p60 - p40) * ((db - 40) / 20)
        return p60
    }

    function holePos() {
        return tx ? 1.0 : posPlus(50)
    }

    // RX: S5–S7 EARTH · ~S8 Mars · S9+ Jupiter · +15 Saturn · +30 Uranus
    // · ~+38 Neptune · +45 Pluto (tickle) · +50 wormhole.
    // TX: even slices of the watts bar; wormhole = peg.
    function worldPos(i) {
        if (tx)
            return (i + 1) / (roster.length + 1)
        if (i === 0) return tickPos("1")
        if (i === 1) return tickPos("3")
        if (i === 2) return tickPos("5")
        if (i === 3) return tickPos("7") * 0.35 + tickPos("9") * 0.65
        if (i === 4) return Meter.normAtS9
        if (i === 5) return posPlus(15)
        if (i === 6) return posPlus(30)
        if (i === 7) return posPlus(38)
        return posPlus(45)
    }

    function reach() { return Math.max(0, Math.min(1, Meter.level)) }

    function litAt(pos) {
        var r = reach()
        var fade = 0.07
        var x = (r - (pos - fade)) / fade
        if (x <= 0) return 0
        if (x >= 1) return 1
        return x * x * (3 - 2 * x)
    }

    function frontier() {
        if (reach() >= holePos() - 0.01)
            return "WORMHOLE"
        var name = "SOL"
        for (var i = 0; i < roster.length; ++i)
            if (reach() >= worldPos(i) - 0.01)
                name = fullName[i]
        return name
    }

    function chipVal(row) {
        var v = "" + (row.value || "")
        var lab = row.label || ""
        if (lab === "GAIN" && v.indexOf("ALC G ") === 0)
            return v.substring(6)
        if (v.indexOf(lab + " ") === 0)
            return v.substring(lab.length + 1)
        return v.length ? v : "—"
    }

    // Title stays muted; the number walks cyan (low) → green (in band) → red (over).
    function chipColor(row) {
        var v = "" + (row.value || "")
        if (!v.length || v.indexOf("—") >= 0)
            return face.cMute
        var n = Math.max(0, Math.min(1, row.level || 0))
        var d = row.danger
        if (d === undefined || d === null) d = 0.9
        if (n >= d) return face.cHot
        if (n >= d * 0.55) return face.cOk
        return face.cCyan
    }

    Canvas {
        id: sky
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 18
        anchors.bottom: chips.top
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            ctx.fillStyle = "#070b14"
            ctx.fillRect(0, 0, w, h)

            // Star dust (deterministic, no flicker)
            ctx.fillStyle = "#cfe8ff"
            var seed = 7919
            function rnd() {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return seed / 0x7fffffff
            }
            for (var s = 0; s < 48; ++s) {
                var sx = rnd() * w, sy = rnd() * h
                ctx.globalAlpha = 0.15 + rnd() * 0.45
                ctx.beginPath()
                ctx.arc(sx, sy, rnd() < 0.15 ? 1.2 : 0.6, 0, Math.PI * 2)
                ctx.fill()
            }
            ctx.globalAlpha = 1

            var cy = h * 0.52
            var sunR = Math.max(14, Math.min(h * 0.28, w * 0.09))
            var xSun = sunR + 8
            var x0 = xSun + sunR * 0.85
            var x1 = w - 14
            var n = face.roster.length
            var slots = n + 1
            var jupR = Math.max(7, Math.min((x1 - x0) / 16, h * 0.16))

            // Faint orbit guides
            ctx.strokeStyle = "rgba(160,190,220,0.16)"
            ctx.lineWidth = 1
            for (var o = 0; o < slots; ++o) {
                var ox = x0 + (x1 - x0) * (o / (slots - 1))
                ctx.beginPath()
                ctx.ellipse(xSun, cy, Math.max(20, ox - xSun), h * 0.42, 0, 0, Math.PI * 2)
                ctx.stroke()
            }

            var sunHot = face.tx ? "#ff6b5a" : "#e8c477"
            var sunCore = face.tx ? "#ffb39a" : "#fff6c8"
            var sg = ctx.createRadialGradient(xSun - sunR * 0.2, cy - sunR * 0.2, 2,
                                              xSun, cy, sunR * 1.6)
            sg.addColorStop(0, sunCore)
            sg.addColorStop(0.4, sunHot)
            sg.addColorStop(0.75, face.tx ? "#a02820" : "#e89a3c")
            sg.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = sg
            ctx.beginPath()
            ctx.arc(xSun, cy, sunR * 1.55, 0, Math.PI * 2)
            ctx.fill()
            ctx.fillStyle = sunHot
            ctx.beginPath()
            ctx.arc(xSun, cy, sunR, 0, Math.PI * 2)
            ctx.fill()
            ctx.fillStyle = face.tx ? "rgba(255,180,160,0.45)" : "rgba(255,250,220,0.55)"
            ctx.beginPath()
            ctx.arc(xSun - sunR * 0.22, cy - sunR * 0.22, sunR * 0.38, 0, Math.PI * 2)
            ctx.fill()

            // Planets — poster row, glow from S-meter
            for (var i = 0; i < n; ++i) {
                var p = face.roster[i]
                var px = x0 + (x1 - x0) * (i / (slots - 1))
                var pr = Math.max(2.5, jupR * p.sz)
                var on = face.litAt(face.worldPos(i))
                var dim = 0.10 + 0.90 * on

                if (on > 0.08) {
                    ctx.globalAlpha = 0.10 + 0.45 * on
                    ctx.fillStyle = p.col
                    ctx.beginPath()
                    ctx.arc(px, cy, pr * (1.55 + 0.55 * on), 0, Math.PI * 2)
                    ctx.fill()
                    ctx.globalAlpha = 1
                }

                if (p.name === "Sat") {
                    ctx.save()
                    ctx.translate(px, cy)
                    ctx.rotate(-0.35)
                    ctx.globalAlpha = 0.25 + 0.7 * on
                    ctx.strokeStyle = "#d4c090"
                    ctx.lineWidth = Math.max(1.5, pr * 0.22)
                    ctx.beginPath()
                    ctx.ellipse(0, 0, pr * 1.85, pr * 0.55, 0, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.restore()
                    ctx.globalAlpha = 1
                }

                if (on >= 0.12) {
                    var g = ctx.createRadialGradient(px - pr * 0.3, cy - pr * 0.3, 1, px, cy, pr)
                    g.addColorStop(0, "#ffffff")
                    g.addColorStop(0.25, p.col)
                    g.addColorStop(1, p.col)
                    ctx.globalAlpha = dim
                    ctx.fillStyle = g
                    ctx.beginPath()
                    ctx.arc(px, cy, pr, 0, Math.PI * 2)
                    ctx.fill()

                    if (p.name === "Ear") {
                        ctx.globalAlpha = dim * 0.55
                        ctx.fillStyle = "#3d8a4a"
                        ctx.beginPath()
                        ctx.arc(px + pr * 0.12, cy - pr * 0.08, pr * 0.42, 0, Math.PI * 2)
                        ctx.fill()
                    }
                    if (p.name === "Jup") {
                        ctx.globalAlpha = dim * 0.35
                        ctx.strokeStyle = "#8a5a28"
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(px - pr * 0.9, cy - pr * 0.15)
                        ctx.quadraticCurveTo(px, cy - pr * 0.05, px + pr * 0.9, cy - pr * 0.18)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(px - pr * 0.85, cy + pr * 0.22)
                        ctx.quadraticCurveTo(px, cy + pr * 0.12, px + pr * 0.85, cy + pr * 0.25)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1
                } else {
                    ctx.globalAlpha = 0.4
                    ctx.strokeStyle = p.col
                    ctx.lineWidth = 1.2
                    ctx.beginPath()
                    ctx.arc(px, cy, pr, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.globalAlpha = 1
                }
            }

            // Wormhole — S9+50 (or TX peg).  Outline until it opens.
            var hx = x0 + (x1 - x0) * ((slots - 1) / (slots - 1))
            var hr = Math.max(8, jupR * 0.72)
            var hon = face.litAt(face.holePos())
            ctx.save()
            ctx.translate(hx, cy)
            ctx.rotate(face.holeAng)
            if (hon < 0.2) {
                ctx.globalAlpha = 0.35
                ctx.strokeStyle = "#7a4cff"
                ctx.lineWidth = 1.2
                ctx.beginPath()
                ctx.ellipse(0, 0, hr * 1.15, hr * 0.55, 0, 0, Math.PI * 2)
                ctx.stroke()
            } else {
                ctx.globalAlpha = 0.18 + 0.55 * hon
                var hg = ctx.createRadialGradient(0, 0, 1, 0, 0, hr * 1.4)
                hg.addColorStop(0, "#e8f4ff")
                hg.addColorStop(0.35, "#00e5ff")
                hg.addColorStop(0.7, "#6a3cff")
                hg.addColorStop(1, "rgba(0,0,0,0)")
                ctx.fillStyle = hg
                ctx.beginPath()
                ctx.arc(0, 0, hr * 1.35, 0, Math.PI * 2)
                ctx.fill()
                ctx.strokeStyle = "#c8f8ff"
                ctx.lineWidth = 1.4
                for (var ring = 0; ring < 3; ++ring) {
                    ctx.beginPath()
                    ctx.ellipse(0, 0, hr * (1.05 - ring * 0.22),
                                hr * (0.42 - ring * 0.08), 0, 0, Math.PI * 2)
                    ctx.stroke()
                }
            }
            ctx.restore()
            ctx.globalAlpha = 1
        }
    }

    Timer {
        interval: 50
        running: face.litAt(face.holePos()) > 0.2
        repeat: true
        onTriggered: {
            face.holeAng += 0.14
            sky.requestPaint()
        }
    }

    Row {
        id: chips
        visible: face.tx
        height: face.tx ? 15 : 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: readout.top
        spacing: 16
        z: 2

        Repeater {
            model: face.tx ? Meter.ladderRows : []
            Row {
                spacing: 4
                Text {
                    text: modelData.label || ""
                    color: face.cMute
                    font.pixelSize: 9
                    font.bold: true
                    font.letterSpacing: 1.0
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: face.chipVal(modelData)
                    color: face.chipColor(modelData)
                    font.family: "Consolas"
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    Rectangle {
        id: readout
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        height: 26
        width: row.implicitWidth + 16
        radius: 4
        color: "#a0070b14"
        z: 2
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Text {
                text: face.tx ? "TX  PWR" : "RX  S"
                color: face.tx ? face.cGold : face.cCyan
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.5
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: Meter.text
                color: face.cCyan
                font.family: "Consolas"
                font.bold: true
                font.pixelSize: 22
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: face.frontier()
                color: face.frontier() === "WORMHOLE" ? "#c9a6ff"
                     : (face.tx ? face.cGold : face.cCyan)
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.2
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: !face.tx
                text: Meter.dbmText
                color: face.cMute
                font.family: "Consolas"
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: {
                    var s = Meter.snrText || ""
                    return (s.indexOf("SNR ") === 0) ? s.substring(4) : s
                }
                visible: !face.tx && text.length > 0 && text !== "—"
                color: face.cCyan
                font.family: "Consolas"
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Connections {
        target: Meter
        function onUpdated() { sky.requestPaint() }
    }
    Connections {
        target: Stream
        function onTxDisplayActiveChanged() { sky.requestPaint() }
    }
    Component.onCompleted: sky.requestPaint()
}
