// Diversity dock. The toolbar chip only shows and hides this panel.
// The lit switch arms the mix. Opening the dock does not.
// The polar plane is the same two numbers as the steppers:
// distance from the centre is gain, angle is phase.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 520
    implicitHeight: 420
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property int lyraMinWidth:  440
    readonly property int lyraMinHeight: 360

    readonly property color cText:  "#cdd9e5"
    readonly property color cMuted: "#8a9aac"
    readonly property color cOn:    "#5dff9a"
    readonly property color cCyan:  "#36d6ff"

    readonly property bool blocked: Stream.moxActive
    readonly property bool lit: Stream.diversityArmed && !blocked
    readonly property real gainDb: Stream.diversityGainDb
    readonly property real phaseDeg: Stream.diversityPhaseDeg
    readonly property string tip: blocked
        ? qsTr("Diversity is off while transmitting or PureSignal is live. It comes back when you unkey.")
        : qsTr("Two converters, same frequency. Second RF is the RX2 jack (Ext1), not ANT2. Off while transmitting or PureSignal is live.")

    readonly property color vectorColor: blocked ? cMuted : (lit ? cOn : cCyan)

    function radFor(db, radius) {
        var t = (db + 27) / 54
        if (t < 0) t = 0
        if (t > 1) t = 1
        return t * radius
    }

    function applyPolar(x, y) {
        if (blocked)
            return
        var g = polar.geom()
        var dx = x - g.cx
        var dy = y - g.cy
        var dist = Math.sqrt(dx * dx + dy * dy)
        if (dist > g.rad && dist > 0) {
            dx *= g.rad / dist
            dy *= g.rad / dist
            dist = g.rad
        }
        var t = g.rad > 0 ? dist / g.rad : 0
        // 0.05 dB and 0.1° — the same grid as the steppers.
        var db = Math.round((t * 54 - 27) * 20) / 20
        var deg = Math.round(Math.atan2(-dy, dx) * 1800 / Math.PI) / 10
        if (deg > 180) deg = 180
        if (deg < -180) deg = -180
        if (db > 27) db = 27
        if (db < -27) db = -27
        Stream.setDiversityGainDb(db)
        Stream.setDiversityPhaseDeg(deg)
    }

    // Decoration only. The polar disc covers the middle, so the band and
    // the two comets live in the corner gutters that stay visible.
    Canvas {
        id: sky
        anchors.fill: parent
        z: 0
        renderStrategy: Canvas.Cooperative
        property real t: 0
        property var field: []

        Component.onCompleted: {
            var s = []
            for (var i = 0; i < 140; ++i) {
                var x = Math.random()
                var y = Math.random()
                var dx = x - 0.5
                var dy = y - 0.58
                if (Math.sqrt(dx * dx + dy * dy) < 0.28 && Math.random() < 0.7)
                    continue
                s.push({
                    x: x,
                    y: y,
                    r: Math.random() < 0.05 ? 1.4 : (0.3 + Math.random() * 0.55),
                    a: 0.14 + Math.random() * 0.5,
                    tw: Math.random() * 6.2832,
                    spike: Math.random() < 0.045
                })
            }
            field = s
            requestPaint()
        }

        Timer {
            interval: 50
            repeat: true
            running: sky.window !== null && sky.window.visible
            onTriggered: {
                sky.t = (sky.t + 0.00115) % 1
                sky.requestPaint()
            }
        }

        // Orbit sits outside the disc so a comet shows in each corner, then
        // slips behind the instrument.
        function stage(w, h) {
            var head = 46
            var cx = w * 0.5
            var cy = head + (h - head) * 0.50
            var corner = Math.sqrt(w * w * 0.25 + (h - head) * (h - head) * 0.25)
            return { cx: cx, cy: cy, rad: corner * 0.72 }
        }

        function comet(ctx, ang, st, rgb) {
            var n = 16
            var span = 0.55
            var pts = []
            for (var i = 0; i <= n; ++i) {
                var a = ang - span * (1 - i / n)
                pts.push({
                    x: st.cx + Math.cos(a) * st.rad,
                    y: st.cy + Math.sin(a) * st.rad * 0.90
                })
            }
            var tail = pts[0]
            var hd = pts[n]
            var g = ctx.createLinearGradient(tail.x, tail.y, hd.x, hd.y)
            g.addColorStop(0, "rgba(" + rgb + ",0)")
            g.addColorStop(0.40, "rgba(" + rgb + ",0.08)")
            g.addColorStop(0.78, "rgba(" + rgb + ",0.40)")
            g.addColorStop(1, "rgba(" + rgb + ",0.92)")
            ctx.strokeStyle = g
            ctx.lineWidth = 2.4
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(pts[0].x, pts[0].y)
            for (var k = 1; k <= n; ++k)
                ctx.lineTo(pts[k].x, pts[k].y)
            ctx.stroke()
            ctx.fillStyle = "rgba(" + rgb + ",0.28)"
            ctx.beginPath()
            ctx.arc(hd.x, hd.y, 4.2, 0, 6.2832)
            ctx.fill()
            ctx.fillStyle = "rgba(255,255,255,0.95)"
            ctx.beginPath()
            ctx.arc(hd.x, hd.y, 1.5, 0, 6.2832)
            ctx.fill()
        }

        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            ctx.clearRect(0, 0, w, h)
            if (w < 8 || h < 8)
                return
            var st = stage(w, h)

            ctx.save()
            ctx.translate(st.cx, st.cy)
            ctx.scale(1, 0.90)
            ctx.beginPath()
            ctx.arc(0, 0, st.rad, 0, 6.2832)
            ctx.lineWidth = 54
            ctx.strokeStyle = "rgba(42, 74, 128, 0.20)"
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(0, 0, st.rad, 0, 6.2832)
            ctx.lineWidth = 16
            ctx.strokeStyle = "rgba(186, 210, 255, 0.11)"
            ctx.stroke()
            ctx.restore()

            var n = field.length
            for (var i = 0; i < n; ++i) {
                var s = field[i]
                var tw = s.spike
                        ? (0.62 + 0.38 * Math.sin(t * 12.566 + s.tw))
                        : (0.84 + 0.16 * Math.sin(t * 6.2832 + s.tw))
                var px = s.x * w
                var py = s.y * h
                ctx.globalAlpha = s.a * tw
                ctx.fillStyle = s.spike ? "#f7f9ff" : "#d5e6ff"
                ctx.beginPath()
                ctx.arc(px, py, s.r, 0, 6.2832)
                ctx.fill()
                if (s.spike) {
                    ctx.strokeStyle = "rgba(220,232,255,0.50)"
                    ctx.lineWidth = 0.6
                    var arm = 4 + s.r * 3
                    ctx.beginPath()
                    ctx.moveTo(px - arm, py)
                    ctx.lineTo(px + arm, py)
                    ctx.moveTo(px, py - arm)
                    ctx.lineTo(px, py + arm)
                    ctx.stroke()
                }
            }
            ctx.globalAlpha = 1

            var vg = ctx.createRadialGradient(st.cx, st.cy, st.rad * 0.55,
                                               st.cx, st.cy, Math.max(w, h) * 0.75)
            vg.addColorStop(0, "rgba(6,10,16,0)")
            vg.addColorStop(1, "rgba(3,7,12,0.42)")
            ctx.fillStyle = vg
            ctx.fillRect(0, 0, w, h)

            comet(ctx, t * 6.2832, st, "214,228,255")
            comet(ctx, t * 6.2832 + Math.PI, st, "232,196,130")
        }
    }

    ColumnLayout {
        z: 1
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Label {
                text: qsTr("DIV")
                color: root.lit ? root.cOn : root.cCyan
                font.bold: true
                font.pixelSize: 13
            }

            Button {
                id: onBtn
                focusPolicy: Qt.NoFocus
                implicitWidth: 56
                implicitHeight: 26
                text: root.lit ? qsTr("ON") : qsTr("OFF")
                font.bold: true
                font.pixelSize: 12
                enabled: !root.blocked
                hoverEnabled: true
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.delay: 800
                ToolTip.text: root.tip
                onClicked: Stream.setDiversityArmed(!Stream.diversityArmed)
                background: Rectangle {
                    radius: 4
                    color: root.lit ? "#0c2418" : "#161e28"
                    border.color: root.lit ? root.cOn : "#2a3a4a"
                    border.width: 2
                }
                contentItem: Text {
                    text: onBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: root.lit ? root.cOn : root.cMuted
                    font.bold: true
                    font.pixelSize: 12
                }
            }

            Label {
                text: qsTr("Gain")
                color: root.cMuted
                font.pixelSize: 11
            }
            LyraSpinBox {
                // Integer box, scaled: 1 step = 0.05 dB. Range −27…+27.
                from: -540
                to: 540
                stepSize: 1
                value: Math.round(root.gainDb * 20)
                enabled: !root.blocked
                Layout.preferredWidth: 88
                textFromValue: function (v) { return (v / 20).toFixed(2) }
                valueFromText: function (t) { return Math.round(parseFloat(t) * 20) }
                onValueModified: Stream.setDiversityGainDb(value / 20)
            }
            Label {
                text: qsTr("dB")
                color: root.cMuted
                font.pixelSize: 11
            }

            Label {
                text: qsTr("Phase")
                color: root.cMuted
                font.pixelSize: 11
            }
            LyraSpinBox {
                // Integer box, scaled: 1 step = 0.1°. Range −180…+180.
                from: -1800
                to: 1800
                stepSize: 1
                value: Math.round(root.phaseDeg * 10)
                enabled: !root.blocked
                Layout.preferredWidth: 88
                textFromValue: function (v) { return (v / 10).toFixed(1) }
                valueFromText: function (t) { return Math.round(parseFloat(t) * 10) }
                onValueModified: Stream.setDiversityPhaseDeg(value / 10)
            }
            Label {
                text: qsTr("deg")
                color: root.cMuted
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }
        }

        Canvas {
            id: polar
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 260
            renderStrategy: Canvas.Cooperative

            function geom() {
                var pad = 22
                var cx = width / 2
                var cy = height / 2
                var rad = Math.max(36, Math.min(width, height) / 2 - pad)
                return { cx: cx, cy: cy, rad: rad }
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var g = geom()
                var cx = g.cx
                var cy = g.cy
                var rad = g.rad
                var col = root.vectorColor

                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, Math.PI * 2)
                ctx.fillStyle = "#0c161d"
                ctx.fill()

                var rings = [-18, -9, 0, 9, 18, 27]
                for (var i = 0; i < rings.length; ++i) {
                    var rr = root.radFor(rings[i], rad)
                    ctx.beginPath()
                    ctx.arc(cx, cy, rr, 0, Math.PI * 2)
                    ctx.strokeStyle = rings[i] === 0
                            ? "rgba(54,214,255,0.55)" : "#1a3340"
                    ctx.lineWidth = rings[i] === 0 ? 1.6 : 1
                    ctx.stroke()
                }

                ctx.strokeStyle = "#163040"
                ctx.lineWidth = 1
                for (var a = 0; a < 12; ++a) {
                    var th = a * Math.PI / 6
                    var major = (a % 3) === 0
                    ctx.globalAlpha = major ? 0.9 : 0.45
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.lineTo(cx + Math.cos(th) * rad, cy - Math.sin(th) * rad)
                    ctx.stroke()
                }
                ctx.globalAlpha = 1

                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, Math.PI * 2)
                ctx.strokeStyle = "#2a4a5a"
                ctx.lineWidth = 2
                ctx.stroke()

                ctx.beginPath()
                ctx.arc(cx, cy, rad - 3, -0.85 * Math.PI, -0.15 * Math.PI, true)
                ctx.strokeStyle = "rgba(54,214,255,0.40)"
                ctx.lineWidth = 2
                ctx.stroke()

                var pr = root.radFor(root.gainDb, rad)
                var ph = root.phaseDeg * Math.PI / 180
                var px = cx + Math.cos(ph) * pr
                var py = cy - Math.sin(ph) * pr

                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.lineTo(px, py)
                ctx.strokeStyle = col
                ctx.globalAlpha = root.blocked ? 0.45 : 0.9
                ctx.lineWidth = 2
                ctx.stroke()

                ctx.beginPath()
                ctx.arc(px, py, 11, 0, Math.PI * 2)
                ctx.fillStyle = col
                ctx.globalAlpha = 0.22
                ctx.fill()

                ctx.beginPath()
                ctx.arc(px, py, 4.5, 0, Math.PI * 2)
                ctx.globalAlpha = 1
                ctx.fillStyle = col
                ctx.fill()

                ctx.fillStyle = root.cMuted
                ctx.font = "11px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText("0", cx + rad + 12, cy)
                ctx.fillText("90", cx, cy - rad - 12)
                ctx.fillText("180", cx - rad - 16, cy)
                ctx.fillText("-90", cx, cy + rad + 12)

                var u = root.radFor(0, rad)
                ctx.textAlign = "left"
                ctx.fillStyle = root.cCyan
                ctx.globalAlpha = 0.8
                ctx.fillText("0 dB", cx + u * 0.72 + 4, cy - u * 0.72)
                ctx.globalAlpha = 1
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Connections {
                target: root
                function onGainDbChanged() { polar.requestPaint() }
                function onPhaseDegChanged() { polar.requestPaint() }
                function onVectorColorChanged() { polar.requestPaint() }
                function onBlockedChanged() { polar.requestPaint() }
            }

            MouseArea {
                anchors.fill: parent
                enabled: !root.blocked
                hoverEnabled: true
                preventStealing: true
                onPressed: (mouse) => root.applyPolar(mouse.x, mouse.y)
                onPositionChanged: (mouse) => {
                    if (pressed)
                        root.applyPolar(mouse.x, mouse.y)
                }
                onDoubleClicked: {
                    Stream.setDiversityGainDb(0)
                    Stream.setDiversityPhaseDeg(0)
                }
                ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
                ToolTip.delay: 800
                ToolTip.text: root.blocked
                    ? root.tip
                    : qsTr("Drag the point. Distance is the RX2-jack gain (−27 at the centre, 0 dB on the bright ring, +27 at the rim). Angle is its phase, 0° to the right, 180° to the left. Steps are 0.05 dB and 0.1°. Double-click returns to 0 dB, 0°.")
            }
        }
    }
}
