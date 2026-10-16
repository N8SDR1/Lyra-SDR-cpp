// Lyra Radar — PPI blob + giant readout + satellite cards.
//
// Radius is Meter.level on the same 0…1 as Arc (WDSP RXA_S_PK + cal on
// RX; PEP/PEAK/AVG watts on TX).  Layout: disc left, digits right,
// NF/SNR (or SWR/MIC/ALC) cards under.  Chrome uses Lyra cyan/gold;
// the blob still walks cyan → gold → red with the reading.

import QtQuick

Item {
    id: face
    anchors.fill: parent

    readonly property color cCyan:  "#00e5ff"
    readonly property color cGold:  "#e8c477"
    readonly property color cMute:  "#8a9aac"
    readonly property color cPanel: "#0a141c"
    readonly property color cEdge:  "#2a4a5a"

    readonly property bool tx: Stream.txDisplayActive
    readonly property int  heroSrc: tx ? 1 : 0   // PWR : RX_SMETER
    // Slightly shorter than the first pass so the disc can grow; not
    // the tiny strip of the second pass.
    readonly property real satH: Math.max(44, Math.min(56, height * 0.22))
    readonly property real radarH: Math.max(88, height - satH - 6)
    readonly property int  satCount: tx ? 3 : 2

    function zoneColor(v, danger) {
        var d = (danger === undefined) ? Meter.normAtS9 : danger
        if (v >= d + 0.16) return "#ff4d4d"
        if (v >= d)        return cGold
        if (v >= d * 0.55) return "#5dff9a"
        return cCyan
    }

    function satAccent(row) {
        var lab = row.label || ""
        if (lab === "SWR" || lab === "ALC")
            return zoneColor(row.level, row.danger)
        return cCyan
    }

    function heroRow() {
        var want = tx ? "PWR" : "S"
        var rows = Meter.ladderRows
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].label === want) return rows[i]
        return { label: want, value: Meter.text, level: Meter.level,
                 danger: Meter.normAtS9 }
    }

    readonly property var satModel: {
        var out = []
        var rows = Meter.ladderRows
        for (var i = 0; i < rows.length; ++i) {
            var lab = rows[i].label
            if (lab === "S" || lab === "PWR") continue
            out.push(rows[i])
        }
        return out
    }

    function chipValue(row) {
        var v = "" + (row.value || "")
        var p = (row.label || "") + " "
        if (v.indexOf(p) === 0) return v.substring(p.length)
        return v
    }

    Item {
        id: well
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 18
        height: Math.max(70, face.radarH - 18)

        Canvas {
            id: cv
            anchors.fill: parent
            renderStrategy: Canvas.Cooperative

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var w = width, h = height
                var cx = w * 0.38
                var cy = h * 0.52
                var R  = Math.max(32, Math.min(cx - 10, h * 0.48))
                var row = face.heroRow()
                var level = Math.max(0, Math.min(1, row.level))
                var danger = row.danger
                var col = face.zoneColor(level, danger)
                var ticks = Meter.tickMarksFor(face.heroSrc)
                var peak = Meter.peak
                var noise = face.tx ? 0 : Meter.noiseLevel

                ctx.beginPath()
                ctx.arc(cx, cy, R + 7, 0, Math.PI * 2)
                ctx.fillStyle = "#070d12"
                ctx.fill()
                ctx.lineWidth = 2
                ctx.strokeStyle = "#15435a"
                ctx.stroke()

                ctx.font = "bold " + Math.round(Math.max(11, R * 0.15)) + "px Consolas"
                ctx.textAlign = "left"
                ctx.textBaseline = "middle"
                for (var i = 0; i < ticks.length; ++i) {
                    var t = ticks[i]
                    var rr = R * Math.max(0.08, t.pos)
                    ctx.beginPath()
                    ctx.arc(cx, cy, rr, 0, Math.PI * 2)
                    ctx.lineWidth = t.major ? 1.6 : 1
                    ctx.strokeStyle = t.pos >= danger
                        ? "rgba(232,196,119,0.42)"
                        : "rgba(0,229,255,0.26)"
                    ctx.stroke()
                    if (t.major || t.label === "+20" || t.label === "5"
                            || t.label === "9" || t.label === "+40") {
                        ctx.fillStyle = t.pos >= danger ? face.cGold : "#9fd6e4"
                        ctx.fillText(t.label, cx + rr + 6, cy)
                    }
                }

                if (noise > 0.04) {
                    ctx.setLineDash([4, 4])
                    ctx.beginPath()
                    ctx.arc(cx, cy, R * noise, 0, Math.PI * 2)
                    ctx.strokeStyle = "rgba(0,229,255,0.65)"
                    ctx.lineWidth = 2
                    ctx.stroke()
                    ctx.setLineDash([])
                }

                if (level > 0.01) {
                    var rad = R * Math.max(0.06, level)
                    ctx.globalAlpha = 0.22
                    ctx.beginPath()
                    ctx.arc(cx, cy, rad * 1.12, 0, Math.PI * 2)
                    ctx.fillStyle = col
                    ctx.fill()
                    ctx.globalAlpha = 0.80
                    ctx.beginPath()
                    ctx.arc(cx, cy, rad, 0, Math.PI * 2)
                    ctx.fillStyle = col
                    ctx.fill()
                    ctx.globalAlpha = 1
                    ctx.beginPath()
                    ctx.arc(cx, cy, rad, 0, Math.PI * 2)
                    ctx.lineWidth = Math.max(3, R * 0.045)
                    ctx.strokeStyle = col
                    ctx.stroke()
                }

                if (peak > level + 0.01) {
                    ctx.beginPath()
                    ctx.arc(cx, cy, R * peak, 0, Math.PI * 2)
                    ctx.lineWidth = 2
                    ctx.strokeStyle = face.cGold
                    ctx.stroke()
                }

                ctx.strokeStyle = "rgba(0,229,255,0.18)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy)
                ctx.moveTo(cx, cy - R); ctx.lineTo(cx, cy + R)
                ctx.stroke()
            }
        }

        Column {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(96, parent.width * 0.40)
            spacing: 0

            Text {
                text: face.tx ? "TX PWR" : "RX  S"
                color: face.tx ? face.cGold : face.cCyan
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.5
            }
            Text {
                width: parent.width
                text: face.chipValue(face.heroRow())
                color: face.zoneColor(face.heroRow().level, face.heroRow().danger)
                font.family: "Consolas"
                font.bold: true
                font.pixelSize: Math.max(28, Math.min(48, well.height * 0.28))
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
            Text {
                visible: !face.tx
                text: Meter.dbmText
                color: "#c5d6e2"
                font.family: "Consolas"
                font.pixelSize: Math.max(14, well.height * 0.11)
            }
            Text {
                text: "LYRA"
                color: "#3a6a7a"
                font.pixelSize: 10
                font.letterSpacing: 3
                font.bold: true
            }
        }
    }

    Row {
        id: sats
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        height: face.satH
        spacing: 6

        readonly property real cardW: Math.max(40,
            (width - spacing * Math.max(0, face.satCount - 1)) / Math.max(1, face.satCount))

        // NF: dim fill = rolling floor (slow). Bright fill = live S
        // (moves with the hero). Same 0…1 S-scale as Arc.
        Rectangle {
            visible: !face.tx
            width: sats.cardW
            height: sats.height
            radius: 6
            color: face.cPanel
            border.width: 1
            border.color: face.cEdge
            Column {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 1
                Text {
                    text: "NF"
                    color: face.cMute
                    font.pixelSize: 10
                    font.bold: true
                }
                Text {
                    text: Math.round(Meter.noiseFloorDbm) + " dBm"
                    color: "#f4fbff"
                    font.family: "Consolas"
                    font.bold: true
                    font.pixelSize: Math.max(14, face.satH * 0.30)
                }
                Rectangle {
                    id: nfTrack
                    width: parent.width
                    height: 6
                    radius: 3
                    color: "#16242e"
                    Rectangle {
                        height: parent.height
                        width: nfTrack.width * Math.max(0, Math.min(1, Meter.noiseLevel))
                        radius: 3
                        color: "#15435a"
                    }
                    Rectangle {
                        height: parent.height
                        width: nfTrack.width * Math.max(0, Math.min(1, Meter.level))
                        radius: 3
                        color: face.cCyan
                        opacity: 0.85
                    }
                }
            }
        }

        // SNR: 0…30 dB full-scale so a typical 10–20 dB reading actually
        // walks the bar instead of sitting in the first quarter of 0…60.
        Rectangle {
            visible: !face.tx
            width: sats.cardW
            height: sats.height
            radius: 6
            color: face.cPanel
            border.width: 1
            border.color: face.cEdge
            Column {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 1
                Text {
                    text: "SNR"
                    color: face.cMute
                    font.pixelSize: 10
                    font.bold: true
                }
                Text {
                    text: {
                        var s = Meter.snrText || "—"
                        return (s.indexOf("SNR ") === 0) ? s.substring(4) : s
                    }
                    color: "#f4fbff"
                    font.family: "Consolas"
                    font.bold: true
                    font.pixelSize: Math.max(14, face.satH * 0.30)
                }
                Rectangle {
                    id: snrTrack
                    width: parent.width
                    height: 6
                    radius: 3
                    color: "#16242e"
                    Rectangle {
                        height: parent.height
                        width: snrTrack.width * Math.max(0, Math.min(1, Meter.rxSnrDb / 30.0))
                        radius: 3
                        color: face.zoneColor(Math.min(1, Meter.rxSnrDb / 30.0), 0.85)
                    }
                }
            }
        }

        Repeater {
            model: face.tx ? face.satModel : []
            delegate: Rectangle {
                width: sats.cardW
                height: sats.height
                radius: 6
                color: face.cPanel
                border.width: 1
                border.color: face.cEdge
                Column {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 1
                    Text {
                        text: modelData.label
                        color: face.cMute
                        font.pixelSize: 10
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        text: face.chipValue(modelData)
                        color: "#f4fbff"
                        font.family: "Consolas"
                        font.bold: true
                        font.pixelSize: Math.max(14, face.satH * 0.30)
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: txTrack
                        width: parent.width
                        height: 6
                        radius: 3
                        color: "#16242e"
                        Rectangle {
                            height: parent.height
                            width: txTrack.width * Math.max(0, Math.min(1, modelData.level))
                            radius: 3
                            color: face.satAccent(modelData)
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: Meter
        function onUpdated() { cv.requestPaint() }
    }
    Connections {
        target: Stream
        function onTxDisplayActiveChanged() { cv.requestPaint() }
    }
    Component.onCompleted: cv.requestPaint()
}
