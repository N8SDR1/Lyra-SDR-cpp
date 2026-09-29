// Lyra — Amp View dock (PS transfer). Separate from the compact
// PureSignal dock. Plasma-fill envelope + glow correction, not a scatter.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 560
    implicitHeight: 448
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property int lyraMinWidth:  420
    readonly property int lyraMinHeight: 280

    readonly property color cText:  "#cdd9e5"
    readonly property color cMuted: "#8a9aac"
    readonly property color cOn:    "#ff9a3c"
    readonly property color cFbOk:  "#3ecf6a"
    readonly property color cFbHot: "#e23d3d"
    readonly property color cFbLow: "#2ec4d4"

    readonly property var info: Stream.psInfo

    function fbZoneColor() {
        if (!(Stream.moxActive && Stream.psLiveArmed))
            return root.cMuted
        var fb = Number(Stream.psFeedbackLevel)
        if (fb > 181)
            return root.cFbHot
        if (fb >= 129)
            return root.cFbOk
        return root.cFbLow
    }

    function psSlot(i) {
        var a = root.info
        if (!a || i < 0 || i >= a.length)
            return 0
        return Number(a[i])
    }
    function hexChk(v) {
        var n = Number(v)
        if (!(n >= 0))
            n = 0
        return "0x" + n.toString(16)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Label {
                text: qsTr("Amp View")
                color: root.cOn
                font.bold: true
                font.pixelSize: 13
            }

            Rectangle {
                id: fbChip
                implicitHeight: 22
                implicitWidth: fbTxt.implicitWidth + 14
                radius: 4
                color: "transparent"
                border.color: root.fbZoneColor()
                border.width: Stream.moxActive && Stream.psLiveArmed ? 2 : 1
                HoverHandler { id: fbHover }
                Label {
                    id: fbTxt
                    anchors.centerIn: parent
                    text: qsTr("FB %1").arg(Stream.psFeedbackLevel)
                    color: root.fbZoneColor()
                    font.bold: Stream.moxActive && Stream.psLiveArmed
                    font.pixelSize: 12
                }
                ToolTip.text: qsTr("Coupler FB. Teal below 129 (low), green 129–181, red above 181 (hot).")
                ToolTip.delay: 800
                ToolTip.visible: fbHover.hovered && Prefs.tooltipsEnabled
            }

            Label {
                Layout.fillWidth: true
                text: {
                    var d0s = Stream.psFeedSpr <= 0 ? "n/a"
                              : (Stream.psDdc0Dbfs <= -999 ? "n/a"
                                 : (Stream.psDdc0Dbfs + " dB"))
                    var d1s = Stream.psFeedSpr <= 0 ? "n/a"
                              : (Stream.psDdc1Dbfs <= -999 ? "n/a"
                                 : (Stream.psDdc1Dbfs + " dB"))
                    return qsTr("ATT %1 dB  st %2  cal %3  %4    D0 %5  D1 %6")
                        .arg(Stream.psAutoAttDb)
                        .arg(Stream.psFsmState)
                        .arg(Stream.psCalCount)
                        .arg(Stream.psCorrecting ? qsTr("correcting") : qsTr("idle"))
                        .arg(d0s).arg(d1s)
                }
                color: Stream.moxActive && Stream.psLiveArmed ? root.cText : root.cMuted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 14

            Label {
                text: "txrx " + root.psSlot(0)
                color: root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "mag " + root.psSlot(1)
                color: root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "phc " + root.psSlot(2)
                color: root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "phs " + root.psSlot(3)
                color: root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "sln.chk " + root.hexChk(root.psSlot(6))
                color: root.psSlot(6) !== 0 ? root.cOn : root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "rxs " + root.psSlot(7)
                color: root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
            Label {
                text: "dg.cnt " + root.psSlot(13)
                color: root.psSlot(13) !== 0 ? root.cOn : root.cMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }
        }

        Canvas {
            id: plot
            Layout.fillWidth: true
            Layout.fillHeight: true
            antialiasing: true
            renderStrategy: Canvas.Cooperative

            function clamp01(v) {
                if (!(v >= 0)) return 0
                if (v > 1.15) return 1.15
                return v
            }

            function mapX(v, x0, pw) { return x0 + clamp01(v) / 1.15 * pw }
            function mapY(v, y0, ph) { return y0 + ph - clamp01(v) / 1.15 * ph }

            function walk(ctx, xs, ys, x0, y0, pw, ph, fromAxis) {
                if (!xs || !ys) return 0
                var n = Math.min(xs.length, ys.length)
                var started = false
                for (var i = 0; i < n; ++i) {
                    var x = Number(xs[i])
                    var y = Number(ys[i])
                    if (!(x >= 0) || !(y >= 0)) continue
                    var px = mapX(x, x0, pw)
                    var py = mapY(y, y0, ph)
                    if (!started) {
                        if (fromAxis) {
                            ctx.moveTo(px, y0 + ph)
                            ctx.lineTo(px, py)
                        } else {
                            ctx.moveTo(px, py)
                        }
                        started = true
                    } else {
                        ctx.lineTo(px, py)
                    }
                }
                return started ? 1 : 0
            }

            onPaint: {
                var ctx = getContext("2d")
                var w = width, h = height
                ctx.reset()
                ctx.fillStyle = "#0c161d"
                ctx.fillRect(0, 0, w, h)

                var mL = 52, mR = 12, mT = 22, mB = 28
                var shelfH = Math.max(22, h * 0.12)
                var x0 = mL
                var pw = Math.max(8, w - mL - mR)
                var y0 = mT
                var ph = Math.max(8, h - mT - mB - shelfH - 22)
                var shelfY = y0 + ph + 4
                var xLabelY = shelfY + shelfH + 5

                // Recessed plot well.
                ctx.fillStyle = "#081218"
                ctx.fillRect(x0, y0, pw, ph)
                ctx.strokeStyle = "#243843"
                ctx.lineWidth = 1
                ctx.strokeRect(x0 + 0.5, y0 + 0.5, pw - 1, ph - 1)

                // Edge ticks only (no full lattice).
                ctx.font = "13px Consolas"
                ctx.textAlign = "center"
                ctx.textBaseline = "top"
                var ticks = [0, 0.25, 0.5, 0.75, 1.0]
                for (var ti = 0; ti < ticks.length; ++ti) {
                    var t = ticks[ti]
                    var tx = mapX(t, x0, pw)
                    ctx.strokeStyle = "#1b2c38"
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(tx, y0)
                    ctx.lineTo(tx, y0 + ph)
                    ctx.stroke()
                    var ty = mapY(t, y0, ph)
                    ctx.beginPath()
                    ctx.moveTo(x0, ty)
                    ctx.lineTo(x0 + pw, ty)
                    ctx.stroke()
                    ctx.fillStyle = "#d8e4ef"
                    ctx.fillText(t.toFixed(2), tx, xLabelY)
                    ctx.save()
                    ctx.textAlign = "right"
                    ctx.textBaseline = "middle"
                    ctx.fillText(t.toFixed(2), x0 - 8, ty)
                    ctx.restore()
                }

                ctx.lineJoin = "round"
                ctx.lineCap = "round"

                var xsM = Stream.psAmpMagX
                var ysM = Stream.psAmpMagY
                var xsC = Stream.psAmpCorrX
                var ysC = Stream.psAmpCorrY

                // Measured envelope: plasma fill from the floor up (cyan→green→amber).
                if (xsM && ysM && xsM.length >= 2) {
                    var grad = ctx.createLinearGradient(x0, y0 + ph, x0, y0)
                    grad.addColorStop(0.0, "rgba(54,214,255,0.08)")
                    grad.addColorStop(0.45, "rgba(93,255,154,0.28)")
                    grad.addColorStop(0.78, "rgba(255,154,60,0.38)")
                    grad.addColorStop(1.0, "rgba(255,77,77,0.22)")
                    ctx.beginPath()
                    var nM = walk(ctx, xsM, ysM, x0, y0, pw, ph, true)
                    if (nM > 0) {
                        ctx.lineTo(mapX(Number(xsM[xsM.length - 1]), x0, pw), y0 + ph)
                        ctx.closePath()
                        ctx.fillStyle = grad
                        ctx.fill()

                        ctx.strokeStyle = "rgba(93,255,154,0.22)"
                        ctx.lineWidth = 7
                        ctx.beginPath()
                        walk(ctx, xsM, ysM, x0, y0, pw, ph, false)
                        ctx.stroke()
                        ctx.strokeStyle = "#5dff9a"
                        ctx.lineWidth = 2.2
                        ctx.beginPath()
                        walk(ctx, xsM, ysM, x0, y0, pw, ph, false)
                        ctx.stroke()
                    }
                }

                // Correction: stacked cyan glow (no scatter).
                if (xsC && ysC && xsC.length >= 2) {
                    ctx.strokeStyle = "rgba(54,214,255,0.16)"
                    ctx.lineWidth = 10
                    ctx.beginPath()
                    walk(ctx, xsC, ysC, x0, y0, pw, ph, false)
                    ctx.stroke()
                    ctx.strokeStyle = "rgba(54,214,255,0.55)"
                    ctx.lineWidth = 4
                    ctx.beginPath()
                    walk(ctx, xsC, ysC, x0, y0, pw, ph, false)
                    ctx.stroke()
                    ctx.strokeStyle = "#c8f6ff"
                    ctx.lineWidth = 1.6
                    ctx.beginPath()
                    walk(ctx, xsC, ysC, x0, y0, pw, ph, false)
                    ctx.stroke()
                }

                // Linear reference on top so lock vs diagonal is readable.
                ctx.strokeStyle = "rgba(255,255,255,0.85)"
                ctx.lineWidth = 1.4
                ctx.beginPath()
                ctx.moveTo(mapX(0, x0, pw), mapY(0, y0, ph))
                ctx.lineTo(mapX(1, x0, pw), mapY(1, y0, ph))
                ctx.stroke()

                // Compression shelf: |out − in| as a plasma bar along drive.
                ctx.fillStyle = "#081218"
                ctx.fillRect(x0, shelfY, pw, shelfH)
                ctx.strokeStyle = "#243843"
                ctx.strokeRect(x0 + 0.5, shelfY + 0.5, pw - 1, shelfH - 1)
                if (xsM && ysM) {
                    var n = Math.min(xsM.length, ysM.length)
                    var barW = pw / Math.max(n, 1)
                    for (var i = 0; i < n; ++i) {
                        var xin = Number(xsM[i])
                        var yout = Number(ysM[i])
                        if (!(xin > 0) || !(yout >= 0)) continue
                        var err = Math.abs(yout - xin)
                        var eh = Math.min(1, err * 4) * (shelfH - 4)
                        var bx = mapX(xin, x0, pw)
                        var col = err < 0.04 ? "#36d6ff"
                                : err < 0.12 ? "#5dff9a"
                                : err < 0.22 ? "#ff9a3c"
                                : "#ff4d4d"
                        ctx.globalAlpha = 0.72
                        ctx.fillStyle = col
                        ctx.fillRect(bx - barW * 0.4, shelfY + shelfH - 2 - eh,
                                     Math.max(1.5, barW * 0.8), eh)
                    }
                    ctx.globalAlpha = 1.0
                }

                // Legend chips.
                ctx.font = "11px sans-serif"
                ctx.textAlign = "left"
                ctx.textBaseline = "alphabetic"
                ctx.fillStyle = "#5dff9a"
                ctx.fillText(qsTr("measured"), x0 + 6, y0 + 14)
                ctx.fillStyle = "#36d6ff"
                ctx.fillText(qsTr("correction"), x0 + 86, y0 + 14)
                ctx.fillStyle = "#8a9aac"
                ctx.fillText(qsTr("compression"), x0 + 6, shelfY + 13)
                ctx.fillStyle = "#9eb0c0"
                ctx.textAlign = "center"
                ctx.fillText(qsTr("drive"), x0 + pw / 2, h - 4)
            }

            Connections {
                target: Stream
                function onPsAmpPlotChanged() { plot.requestPaint() }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: root.cMuted
            text: qsTr("Locked: green measured rides the white diagonal; cyan is the inverse bow (shelf cyan/green). Snap copies live GetPk. Hold freezes the plot.")
        }

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Label { text: qsTr("GetPk"); color: root.cMuted; font.pixelSize: 12 }
            Label {
                text: Stream.psMaxTx.toFixed(3)
                color: root.cText
                font.pixelSize: 13
                font.family: "Consolas"
            }
            Button {
                id: snapBtn
                focusPolicy: Qt.NoFocus
                implicitWidth: 52
                implicitHeight: 24
                text: qsTr("Snap")
                font.pixelSize: 11
                onClicked: Stream.capturePsGetPk()
                background: Rectangle {
                    radius: 3
                    color: "#161e28"
                    border.color: "#2a3a4a"
                    border.width: 1
                }
                contentItem: Text {
                    text: snapBtn.text
                    color: root.cText
                    font: snapBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.text: qsTr("Copy the live TX peak into the held GetPk value. Plot keeps updating.")
                ToolTip.delay: 800
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
            }
            Label {
                text: Stream.psGetPk.toFixed(3)
                color: root.cOn
                font.pixelSize: 13
                font.family: "Consolas"
            }
            Button {
                id: holdBtn
                focusPolicy: Qt.NoFocus
                checkable: true
                checked: Stream.psAmpPlotHeld
                implicitWidth: 52
                implicitHeight: 24
                text: qsTr("Hold")
                font.pixelSize: 11
                onClicked: Stream.setPsAmpPlotHeld(checked)
                background: Rectangle {
                    radius: 3
                    color: holdBtn.checked ? "#30200c" : "#161e28"
                    border.color: holdBtn.checked ? root.cOn : "#2a3a4a"
                    border.width: holdBtn.checked ? 2 : 1
                }
                contentItem: Text {
                    text: holdBtn.text
                    color: holdBtn.checked ? root.cOn : root.cText
                    font: holdBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.text: qsTr("Freeze the transfer plot. Uncheck to live-follow. Does not change GetPk.")
                ToolTip.delay: 800
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
            }

            Item { Layout.fillWidth: true }

            Label { text: qsTr("SetPk"); color: root.cMuted; font.pixelSize: 12 }
            LyraSpinBox {
                id: pkSpin
                from: 50
                to: 1000
                stepSize: 1
                value: Math.round(Stream.psHwPeak * 1000)
                implicitWidth: 88
                textFromValue: function (v) { return (v / 1000).toFixed(3) }
                valueFromText: function (t) {
                    var n = parseFloat(t)
                    if (isNaN(n)) return pkSpin.value
                    return Math.round(n * 1000)
                }
                onValueModified: Stream.setPsHwPeak(value / 1000.0)
                Connections {
                    target: Stream
                    function onPsHwPeakChanged() {
                        var v = Math.round(Stream.psHwPeak * 1000)
                        if (pkSpin.value !== v) pkSpin.value = v
                    }
                }
            }
        }
    }
}
