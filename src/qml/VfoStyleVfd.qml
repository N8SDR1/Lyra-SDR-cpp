// Lyra VFO style — VFD.
//
// Same glass as the VFD meter: green phosphor tubes, wire cathodes, gas
// glow drawn with stacked strokes. Ten frequency tubes, dim leading zeros,
// group dots between MHz / kHz / Hz. Style contract: see VfoBox.qml.
import QtQuick

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 420
    readonly property real naturalHeight: 148
    readonly property rect tuneRect: Qt.rect(22, 20, 376, 82)
    readonly property rect stepModeRect: Qt.rect(14, 118, 392, 24)
    readonly property rect txPipRect: Qt.rect(378, 6, 28, 15)
    readonly property var theme: ({
        label: "#9ad8c8", field: "#061410", fieldBorder: "#1e4a3c",
        text: "#7dffe0", accent: "#2ee6c0",
        pipIdle: "#061410", pipIdleBorder: "#1e4a3c", pipIdleText: "#6a9a88",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    // First installed family, like the old QFont::setFamilies fallback list.
    readonly property string monoFamily: {
        var have = Qt.fontFamilies(), list = ["Consolas", "Menlo", "Courier New"]
        for (var i = 0; i < list.length; ++i)
            if (have.indexOf(list[i]) >= 0) return list[i]
        return "monospace"
    }
    readonly property real tubeW: 34
    readonly property real tubeH: 74
    readonly property real tubeY: 24
    readonly property real pitch: 36
    readonly property real groupGap: 6
    readonly property real x0: 22 + Math.round((376 - (9 * pitch + tubeW + 2 * groupGap)) / 2)
    function tubeX(i) { return x0 + i * pitch + (i > 3 ? groupGap : 0) + (i > 6 ? groupGap : 0) }
    function leadingZero(i) {
        if (9 - i < 6) return false
        for (var j = 0; j <= i; ++j)
            if (digitString.charAt(j) !== "0") return false
        return true
    }
    readonly property var digitRects: {
        var a = []
        for (var place = 0; place < 10; ++place)
            a.push(Qt.rect(tubeX(9 - place), tubeY, tubeW, tubeH))
        return a
    }

    function filament() { return "#7dffe0" }
    function glow() { return "#2ee6c0" }

    function wirePath(ctx, ch) {
        ctx.beginPath()
        if (ch === "0") {
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
        } else if (ch === "5") {
            ctx.moveTo(22, 7); ctx.lineTo(6, 8); ctx.lineTo(7, 20)
            ctx.bezierCurveTo(14, 16, 24, 18, 22, 28)
            ctx.bezierCurveTo(20, 42, 6, 42, 6, 32)
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
        } else if (ch === "9") {
            ctx.arc(12, 14, 8, 0.2, Math.PI * 1.7)
            ctx.moveTo(18, 10); ctx.lineTo(18, 40)
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
        st.round(ctx, 1, 1, w - 2, h - 2, 8)
        ctx.save()
        ctx.clip()
        var g = ctx.createLinearGradient(0, 0, 0, h)
        g.addColorStop(0, "#18222c")
        g.addColorStop(0.45, "#101820")
        g.addColorStop(1, "#0b1117")
        ctx.fillStyle = g
        ctx.fillRect(0, 0, w, h)
        var v = ctx.createRadialGradient(w * 0.5, h * 0.35, w * 0.05, w * 0.5, h * 0.55, w * 0.72)
        v.addColorStop(0, "rgba(80,160,140,0.10)")
        v.addColorStop(1, "rgba(0,0,0,0.42)")
        ctx.fillStyle = v
        ctx.fillRect(0, 0, w, h)
        ctx.restore()
    }

    function tube(ctx, x, y, w, h, ch, dim, selected) {
        var rad = w * 0.28
        st.round(ctx, x, y, w, h, rad)
        var g = ctx.createLinearGradient(x, y, x + w, y)
        g.addColorStop(0, "#061610")
        g.addColorStop(0.5, "#0c241c")
        g.addColorStop(1, "#06140f")
        ctx.fillStyle = g
        ctx.fill()
        var gas = ctx.createRadialGradient(x + w / 2, y + h * 0.45, 2, x + w / 2, y + h * 0.5, w * 0.7)
        if (dim) {
            gas.addColorStop(0, "rgba(40,120,100,0.04)")
            gas.addColorStop(1, "rgba(0,0,0,0)")
        } else {
            gas.addColorStop(0, "rgba(80,255,200,0.07)")
            gas.addColorStop(0.6, "rgba(10,80,60,0.03)")
            gas.addColorStop(1, "rgba(0,0,0,0)")
        }
        ctx.fillStyle = gas
        ctx.fill()

        ctx.save()
        st.round(ctx, x, y, w, h, rad)
        ctx.clip()
        // Ink of the wire digits sits in x 4..26, y 5..45. Scale that box
        // up so the figure fills the tube instead of floating in the middle.
        var gx = 4, gy = 5, gw = 22, gh = 40
        var s = Math.min((w * 0.92) / gw, (h * 0.90) / gh)
        ctx.translate(x + (w - gw * s) / 2 - gx * s,
                      y + (h - gh * s) / 2 - gy * s)
        ctx.scale(s, s)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        if (st.wirePath(ctx, ch)) {
            if (dim) {
                ctx.globalAlpha = 0.28
                ctx.strokeStyle = "#14382e"
                ctx.lineWidth = 1.3
                ctx.stroke()
            } else {
                ctx.strokeStyle = st.glow()
                ctx.globalAlpha = 0.12
                ctx.lineWidth = 2.2
                ctx.stroke()
                ctx.globalAlpha = 0.20
                ctx.lineWidth = 1.4
                ctx.stroke()
                ctx.globalAlpha = 1
                ctx.strokeStyle = st.filament()
                ctx.lineWidth = 1.35
                ctx.stroke()
                ctx.strokeStyle = "#eafff8"
                ctx.lineWidth = 0.5
                ctx.stroke()
            }
        }
        ctx.restore()

        ctx.save()
        st.round(ctx, x, y, w, h, rad)
        ctx.clip()
        var sheen = ctx.createLinearGradient(x, y, x + w * 0.5, y + h)
        sheen.addColorStop(0, "rgba(255,255,255,0.18)")
        sheen.addColorStop(0.3, "rgba(255,255,255,0.02)")
        sheen.addColorStop(1, "rgba(255,255,255,0)")
        ctx.fillStyle = sheen
        ctx.fillRect(x, y, w, h * 0.55)
        ctx.restore()
        ctx.strokeStyle = selected ? "rgba(234,255,248,0.85)" : "rgba(160,255,220,0.25)"
        ctx.lineWidth = selected ? 1.6 : 1
        st.round(ctx, x, y, w, h, rad)
        ctx.stroke()
    }

    function phosphorDot(ctx, x, y) {
        ctx.beginPath()
        ctx.arc(x, y, 3.2, 0, 7)
        ctx.fillStyle = st.glow()
        ctx.globalAlpha = 0.35
        ctx.fill()
        ctx.beginPath()
        ctx.arc(x, y, 1.6, 0, 7)
        ctx.fillStyle = "#eafff8"
        ctx.globalAlpha = 1
        ctx.fill()
    }

    width: naturalWidth
    height: naturalHeight

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "transparent"
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }

    Canvas {
        id: cv
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width
            var h = height
            st.faceBg(ctx, w, h)
            var i
            for (i = 0; i < 10; ++i) {
                var place = 9 - i
                st.tube(ctx, st.tubeX(i), st.tubeY, st.tubeW, st.tubeH,
                        st.digitString.charAt(i), st.leadingZero(i),
                        st.selectedDigit === place)
            }
            st.phosphorDot(ctx,
                           (st.tubeX(3) + st.tubeW + st.tubeX(4)) / 2,
                           st.tubeY + st.tubeH - 12)
            st.phosphorDot(ctx,
                           (st.tubeX(6) + st.tubeW + st.tubeX(7)) / 2,
                           st.tubeY + st.tubeH - 12)
        }
    }

    Repeater {
        model: [{ t: "MHz", a: 0, b: 3 }, { t: "kHz", a: 4, b: 6 }, { t: "Hz", a: 7, b: 9 }]
        Text {
            required property var modelData
            text: modelData.t
            color: "#9ad8c8"
            font.family: st.monoFamily
            font.pixelSize: 10
            x: (st.tubeX(modelData.a) + st.tubeX(modelData.b) + st.tubeW) / 2 - implicitWidth / 2
            y: st.tubeY + st.tubeH + 4
        }
    }

    Text {
        x: 14
        y: 6
        text: st.roleText
        color: st.vfoState === "tx" ? "#ff5a3a" : "#9ad8c8"
        font.family: st.monoFamily
        font.pixelSize: 11
        font.bold: true
    }

    onFreqHzChanged: cv.requestPaint()
    onSelectedDigitChanged: cv.requestPaint()
    Component.onCompleted: cv.requestPaint()
}
