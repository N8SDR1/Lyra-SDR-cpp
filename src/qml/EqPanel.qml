// Lyra — TX parametric-EQ dock panel (#50 Stage 2a).
//
// The operator-approved EESDR3-style layout, made live against the
// native ParamEq engine (Eq context property = lyra::ui::EqModel):
//   • white glowing summed-response curve (eq.magnitudeDb → what you hear)
//   • 8 numbered typed nodes on the curve; click selects (white ring),
//     drag = freq + gain, wheel = Q
//   • per-band tile row: #, Type (click to cycle), freq, gain/Q;
//     right-click a tile resets that band
//   • top bar: ON (bypass) · Makeup trim · collapse ▼/▲
//
// Stage 2b adds the analyzer behind the curve (live spectrum, Accumulate
// peak-hold, pink-noise reference, Before/After-Mod, optional waterfall).
// Wire-INERT until Stage 3 routes the TX mic rack through eq.engine().
//
// QML can't bind to a band struct, so node/tile bindings read index-keyed
// eq.bandX(i) invokables and re-evaluate off `rev` (bumped on bandsChanged).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 860
    implicitHeight: collapsed ? 40 : 320
    color: "#0d141b"
    border.color: "#2a4a5a"

    // ── palette (matches the glassy panadapter language) ───────────────
    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cGrid:   "#2a3e4d"
    readonly property color cGrid0:  "#36546a"   // the 0 dB line
    readonly property color cCurve:  "#ffffff"

    // Per-band node colours (10) — distinct rainbow spread, readable on
    // the dark graph.
    readonly property var bandColor: [
        "#ff4d4d", "#ff8c3c", "#ffd23c", "#b6ff4d", "#4dff7a",
        "#3cffd2", "#3cc8ff", "#6c8cff", "#a96cff", "#ff6cf0"
    ]

    // Collapse is local for 2a (persisted as Prefs.eqCollapsed in Stage 4).
    property bool collapsed: false

    // #59 — one panel serves BOTH the TX and the RX EQ docks.  Defaults match
    // the TX EQ so the existing TX dock is unchanged; the RX dock overrides:
    //   * eq          — the model/view bridge (TX context prop by default; the
    //                   RX dock sets RxEq).  All band calls go through `eq.`.
    //   * bwHz        — the band-edge source the graph x-range tracks (TX BW by
    //                   default; the RX dock binds the RX bandwidth, #168 mirror).
    //   * bypassModes — modes where the EQ is auto-bypassed (dimmed header +
    //                   "bypassed" flag).  TX dims in digital + CW; the RX dock
    //                   passes just the digital data modes.
    property var  eq:    Eq
    property real bwHz:  Prefs.txBandwidth
    property var  bypassModes: ["DIGU", "DIGL", "CWU", "CWL"]

    // The TX rack is bypassed in the digital data modes (DIGU/DIGL, gated by
    // SetTxRackBypass) and is moot in CW.  Gray the ON lamp + flag the header
    // so the operator sees the EQ isn't shaping audio there.  Purely visual —
    // the engine/profile state is untouched and re-lights on USB/LSB.
    readonly property bool rackInactive: {
        var m = WdspEngine.mode.toUpperCase()
        return bypassModes.indexOf(m) >= 0
    }

    // Revision tick: any engine change bumps it so the invokable-derived
    // node/tile/curve bindings re-evaluate (QML doesn't auto-track
    // Q_INVOKABLE calls).
    property int rev: 0
    onRevChanged: curveCanvas.requestPaint()
    Connections {
        target: eq
        function onBandsChanged()       { root.rev++ }
        function onSelectedBandChanged() { root.rev++ }
        function onBypassChanged()      { root.rev++ }
        function onMakeupDbChanged()    { root.rev++ }
        // Stage-2b analyzer: a new mic-spectrum frame → repaint the graph;
        // a toggle change → repaint (and drop peak-hold when Accumulate off).
        function onSpectrumChanged()    { curveCanvas.requestPaint() }
        function onSpectrumOptsChanged() {
            if (!eq.accumulate) { curveCanvas.peakHold = []; curveCanvas.peakHang = [] }
            if (!eq.accumulate || !eq.beforeAfterMod) {
                curveCanvas.peakHoldPre = []; curveCanvas.peakHangPre = []
            }
            curveCanvas.requestPaint()
        }
    }

    function fmtFreq(f) {
        if (f >= 1000) return (f / 1000).toFixed(f < 10000 ? 2 : 1) + "k"
        return Math.round(f).toString()
    }

    // Per-band filter-TYPE picker.  Opened from a tile's Type chip; lists
    // ALL seven RBJ types so Notch / Bandpass / LP / HP are visibly
    // selectable (not hidden behind a click-cycle).  band = the tile that
    // opened it; the checked item is that band's current type.
    Menu {
        id: typeMenu
        property int band: -1
        function pick(t) { if (band >= 0) eq.setBandType(band, t) }
        function isCur(t) { root.rev; return band >= 0 && eq.bandType(band) === t }
        MenuItem { text: qsTr("Peak");     checkable: true; checked: typeMenu.isCur(0); onTriggered: typeMenu.pick(0) }
        MenuItem { text: qsTr("Lo-Shelf"); checkable: true; checked: typeMenu.isCur(1); onTriggered: typeMenu.pick(1) }
        MenuItem { text: qsTr("Hi-Shelf"); checkable: true; checked: typeMenu.isCur(2); onTriggered: typeMenu.pick(2) }
        MenuItem { text: qsTr("Lo-Pass");  checkable: true; checked: typeMenu.isCur(3); onTriggered: typeMenu.pick(3) }
        MenuItem { text: qsTr("Hi-Pass");  checkable: true; checked: typeMenu.isCur(4); onTriggered: typeMenu.pick(4) }
        MenuItem { text: qsTr("Bandpass"); checkable: true; checked: typeMenu.isCur(5); onTriggered: typeMenu.pick(5) }
        MenuItem { text: qsTr("Notch");    checkable: true; checked: typeMenu.isCur(6); onTriggered: typeMenu.pick(6) }
    }

    // Peak-hold hang/decay preset — right-click the Acc chip.  Live tracks the
    // instantaneous spectrum (no hold); Fast/Med/Slow set how long a peak
    // lingers then how fast it falls; Hold never decays (toggle Acc off→on to
    // clear).
    Menu {
        id: peakMenu
        function pick(m) { eq.peakDecayMode = m }
        function isCur(m) { return eq.peakDecayMode === m }
        MenuItem { text: qsTr("Peak decay: Live");   checkable: true; checked: peakMenu.isCur(4); onTriggered: peakMenu.pick(4) }
        MenuItem { text: qsTr("Peak decay: Fast");   checkable: true; checked: peakMenu.isCur(0); onTriggered: peakMenu.pick(0) }
        MenuItem { text: qsTr("Peak decay: Medium"); checkable: true; checked: peakMenu.isCur(1); onTriggered: peakMenu.pick(1) }
        MenuItem { text: qsTr("Peak decay: Slow");   checkable: true; checked: peakMenu.isCur(2); onTriggered: peakMenu.pick(2) }
        MenuItem { text: qsTr("Hold (no decay)");    checkable: true; checked: peakMenu.isCur(3); onTriggered: peakMenu.pick(3) }
    }

    // Scroll when the panel is compressed below its natural content size
    // — both axes, AsNeeded — so a floated-small or racked panel stays
    // fully usable instead of silently clipping (SizeRootObjectToView
    // gives no scrollbar on its own).  Same idiom as SpeechPanel / the CW
    // decoder.  Above the natural size the content fills; below it scrolls.
    ScrollView {
        id: bodyScroll
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        contentWidth: Math.max(828, availableWidth)
        // The EQ graph sizes itself with Layout.fillHeight, so the column needs
        // a definite height to grow into: fill the viewport when the panel is
        // tall enough, else hold at the ~300px natural height and let the
        // ScrollView scroll.  Bind to bodyScroll.height (stable) rather than
        // availableHeight so the fill can't feed a scrollbar-visibility loop.
        contentHeight: root.collapsed ? eqBody.implicitHeight
                                      : Math.max(300, bodyScroll.height)
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded
        ScrollBar.vertical.policy:   ScrollBar.AsNeeded

        ColumnLayout {
            id: eqBody
            width: bodyScroll.contentWidth
            height: bodyScroll.contentHeight
            spacing: 6

        // ── Top bar ────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: qsTr("TX EQ")
                color: root.cAccent
                font.bold: true
                font.pixelSize: 14
            }

            // ON = engine active (inverse of bypass).
            Button {
                id: onBtn
                text: qsTr("ON")
                checkable: true
                implicitWidth: 50
                implicitHeight: 24
                checked: { root.rev; return !eq.bypass }
                onClicked: eq.bypass = !checked
                background: Rectangle {
                    radius: 4
                    color: (onBtn.checked && !root.rackInactive) ? "#0b3a44" : "#1f2a35"
                    border.color: (onBtn.checked && !root.rackInactive) ? root.cAccent : "#3a5060"
                    border.width: 2
                }
                contentItem: Text {
                    text: onBtn.text
                    color: (onBtn.checked && !root.rackInactive) ? root.cAccent : root.cMuted
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label {
                visible: root.rackInactive
                text: qsTr("• bypassed (") + WdspEngine.mode.toUpperCase() + ")"
                color: "#e0a030"; font.pixelSize: 11
            }

            Item { width: 8 }

            // Makeup output trim.
            Label { text: qsTr("Makeup"); color: root.cMuted }
            RoundButton {
                text: "−"; implicitWidth: 24; implicitHeight: 24
                onClicked: eq.makeupDb = eq.makeupDb - 1
            }
            Label {
                text: { root.rev; return (eq.makeupDb >= 0 ? "+" : "")
                        + eq.makeupDb.toFixed(0) + qsTr(" dB") }
                color: root.cText
                font.family: "Consolas"
                font.bold: true
                Layout.preferredWidth: 52
                horizontalAlignment: Text.AlignHCenter
            }
            RoundButton {
                text: "+"; implicitWidth: 24; implicitHeight: 24
                onClicked: eq.makeupDb = eq.makeupDb + 1
            }

            Item { width: 10 }

            // Reset — flatten every band back to its default freq/gain/Q.
            Button {
                id: resetBtn
                text: qsTr("Reset"); implicitWidth: 52; implicitHeight: 22
                onClicked: eq.resetAll()
                ToolTip.text: qsTr("Reset all bands to their default "
                                   + "freq / gain / Q")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
                background: Rectangle { radius: 4
                    color: resetBtn.down ? "#0b3a44" : "#1f2a35"
                    border.color: "#3a5060" }
                contentItem: Text { text: resetBtn.text; font.pixelSize: 11
                    font.bold: true
                    color: root.cMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }

            Item { width: 10 }

            // ── Analyzer toggles (Stage 2b) ────────────────────────────
            Button {
                id: specBtn
                readonly property bool active: eq.analyzerMode > 0
                text: ["Off", "Spec", "RTA"][eq.analyzerMode]
                implicitWidth: 50; implicitHeight: 22
                onClicked: eq.analyzerMode = (eq.analyzerMode + 1) % 3
                ToolTip.text: qsTr("Analyzer behind the curve — click to cycle "
                                   + "Off → Spectrum → RTA (live on the mic)")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
                background: Rectangle { radius: 4
                    color: specBtn.active ? "#0b3a44" : "#1f2a35"
                    border.color: specBtn.active ? root.cAccent : "#3a5060" }
                contentItem: Text { text: specBtn.text; font.pixelSize: 11
                    font.bold: true
                    color: specBtn.active ? root.cAccent : root.cMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }
            Button {
                id: accBtn
                text: qsTr("Acc"); checkable: true
                implicitWidth: 44; implicitHeight: 22
                enabled: eq.analyzerMode > 0
                checked: eq.accumulate
                onClicked: eq.accumulate = checked
                ToolTip.text: qsTr("Accumulate — peak-hold line over the "
                                   + "spectrum.  Right-click for hang/decay.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
                background: Rectangle { radius: 4
                    color: accBtn.checked ? "#0b3a44" : "#1f2a35"
                    border.color: accBtn.checked ? root.cAccent : "#3a5060"
                    opacity: accBtn.enabled ? 1.0 : 0.4 }
                contentItem: Text { text: accBtn.text; font.pixelSize: 11
                    font.bold: true
                    color: accBtn.checked ? root.cAccent : root.cMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
                // Right-click → hang/decay preset menu (left-click still
                // toggles, since this MouseArea ignores the left button).
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: peakMenu.popup()
                }
            }
            Button {
                id: baBtn
                text: qsTr("B/A"); checkable: true
                implicitWidth: 44; implicitHeight: 22
                enabled: eq.analyzerMode > 0
                checked: eq.beforeAfterMod
                onClicked: eq.beforeAfterMod = checked
                ToolTip.text: qsTr("Before/After-Mod — overlay the pre-EQ "
                                   + "(before) trace so you see what the EQ did")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
                background: Rectangle { radius: 4
                    color: baBtn.checked ? "#0b3a44" : "#1f2a35"
                    border.color: baBtn.checked ? root.cAccent : "#3a5060"
                    opacity: baBtn.enabled ? 1.0 : 0.4 }
                contentItem: Text { text: baBtn.text; font.pixelSize: 11
                    font.bold: true
                    color: baBtn.checked ? root.cAccent : root.cMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }

            Item { Layout.fillWidth: true }

            // Selected-band quick readout.
            Label {
                visible: !root.collapsed
                text: { root.rev
                        var i = eq.selectedBand
                        if (i < 0) return ""
                        return qsTr("Band ") + (i + 1) + " · " + eq.typeName(eq.bandType(i)) }
                color: root.cMuted
                font.pixelSize: 12
            }

            // Collapse ▼/▲ (mirrors the waterfall idiom).
            Button {
                implicitWidth: 28; implicitHeight: 24
                text: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
                ToolTip.text: root.collapsed ? qsTr("Show EQ graph")
                                             : qsTr("Collapse EQ graph")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                ToolTip.delay: 800
                background: Rectangle { radius: 4; color: "#1f2a35"; border.color: "#3a5060" }
                contentItem: Text {
                    text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // ── Graph (curve + grid + draggable nodes) ─────────────────────
        Item {
            id: graph
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.collapsed
            opacity: eq.bypass ? 0.45 : 1.0

            // Axis maps: log freq, linear ±15 dB.
            // #168 — the upper frequency tracks the TX audio passband so the
            // visible curve reflects what actually goes on the air: SSB/digital
            // audio runs out to the TX BW high edge; the double-sideband modes
            // (AM/DSB/FM/SAM) mirror audio into both sidebands, so their audio
            // top is HALF the occupied TX BW.  Clamped to a sane, always-usable
            // window (4 k floor so low bands never vanish; 12 k ceiling for
            // ultra-wide ESSB).  txEdgeHz is the exact audio top (the dashed
            // marker), fMax adds a little headroom so the edge isn't jammed at
            // the right.
            readonly property real fMin: 30
            readonly property real txEdgeHz: {
                var bw = root.bwHz
                if (!bw || bw <= 0) return 0
                var m = WdspEngine.mode.toUpperCase()
                if (m === "AM" || m === "SAM" || m === "DSB" || m === "FM")
                    bw = bw / 2
                return bw
            }
            readonly property real fMax: {
                var top = txEdgeHz
                if (top <= 0) return 12000
                return Math.max(4000, Math.min(12000, top * 1.15))
            }
            onFMaxChanged: root.rev++      // re-eval node/curve bindings + repaint
            readonly property real dbR: 15
            function xForFreq(f) { return width * Math.log(f / fMin) / Math.log(fMax / fMin) }
            function freqForX(x) {
                var t = Math.max(0, Math.min(1, x / width))
                return fMin * Math.pow(fMax / fMin, t)
            }
            function yForGain(g) { return height * (dbR - g) / (2 * dbR) }
            function gainForY(y) { return dbR - (y / height) * 2 * dbR }
            function yForCurveFreq(f) { return yForGain(eq.magnitudeDb(f)) }
            // Hard floor/ceiling: a node can never be drawn (or dragged)
            // outside the graph box — so a deep cut stays grabbable at the
            // bottom edge instead of disappearing into the tile field below.
            function clampY(y) { return Math.max(11, Math.min(height - 11, y)) }
            // Analyzer backdrop has its OWN vertical scale (dBFS), independent
            // of the ±15 dB gain grid the EQ curve rides — they share the box.
            readonly property real specFloorDb: -100
            readonly property real specTopDb: 0
            function yForDb(db) {
                var t = (specTopDb - db) / (specTopDb - specFloorDb)
                return Math.max(0, Math.min(height, height * t))
            }

            Rectangle {
                anchors.fill: parent; color: "#0e1924"
                border.color: root.cGrid0; border.width: 1   // the bounded box
            }

            Canvas {
                id: curveCanvas
                anchors.fill: parent
                property var peakHold: []     // Accumulate peak-hold dB per bin (after/post)
                property var peakHang: []     // remaining hang frames per bin (after/post)
                property var peakHoldPre: []  // peak-hold dB per bin (before/pre, B/A on)
                property var peakHangPre: []  // remaining hang frames (before/pre)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Component.onCompleted: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var w = width, h = height
                    ctx.clearRect(0, 0, w, h)

                    // ── Live mic analyzer backdrop (Stage 2b) ───────────
                    // Drawn FIRST so the grid + EQ curve sit on top.  Mode:
                    // 1=Spectrum (line/fill), 2=RTA (band bars).  Live on the
                    // mic (continuous on the HL2+ codec → animates on RX too).
                    var amode = eq.analyzerMode
                    if (amode > 0) {
                        var post = eq.spectrumPost()
                        var nb = post.length
                        if (nb > 2) {
                            var nyq = eq.specNyquist
                            var fMin = graph.fMin, fMax = graph.fMax

                            // Trace a per-bin dB array as a line (skip DC).
                            function trace(arr) {
                                ctx.beginPath()
                                var go = false
                                for (var i = 1; i < nb; ++i) {
                                    var f = (i / (nb - 1)) * nyq
                                    if (f < fMin) continue
                                    if (f > fMax) break
                                    var x = graph.xForFreq(f), y = graph.yForDb(arr[i])
                                    if (!go) { ctx.moveTo(x, y); go = true } else ctx.lineTo(x, y)
                                }
                                return go
                            }
                            // Max dB of a per-bin array over a freq band (RTA).
                            function bandMax(arr, fLo, fHi) {
                                var m = -200
                                var i0 = Math.max(1, Math.floor(fLo / nyq * (nb - 1)))
                                var i1 = Math.min(nb - 1, Math.ceil(fHi / nyq * (nb - 1)))
                                for (var i = i0; i <= i1; ++i) if (arr[i] > m) m = arr[i]
                                return m
                            }

                            // before (pre-EQ) source when B/A engaged.
                            var preArr = null
                            if (eq.beforeAfterMod) {
                                preArr = eq.spectrumPre()
                                if (preArr.length !== nb) preArr = null
                            }

                            // ── shared peak-hold state (per bin) — runs for
                            // BOTH modes; right-click Acc sets the hang/decay
                            // (poll ~25 fps → hang frames ≈ seconds×25).
                            if (eq.accumulate) {
                                var hangF, decF
                                switch (eq.peakDecayMode) {
                                case 4: hangF = 0;     decF = 1e9;  break  // Live (track instantly)
                                case 0: hangF = 8;     decF = 1.5;  break  // Fast
                                case 2: hangF = 50;    decF = 0.25; break  // Slow
                                case 3: hangF = 1e9;   decF = 0.0;  break  // Hold
                                default: hangF = 20;   decF = 0.6;  break  // Medium
                                }
                                var ph = peakHold, pg = peakHang
                                if (!ph || ph.length !== nb) {
                                    ph = new Array(nb); pg = new Array(nb)
                                    for (var z = 0; z < nb; ++z) { ph[z] = -200; pg[z] = 0 }
                                }
                                for (var p = 0; p < nb; ++p) {
                                    var d = post[p]
                                    if (d >= ph[p]) { ph[p] = d; pg[p] = hangF }
                                    else if (pg[p] > 0) { pg[p]-- }
                                    else { ph[p] = Math.max(d, ph[p] - decF) }
                                }
                                peakHold = ph; peakHang = pg
                                if (preArr) {
                                    var ph2 = peakHoldPre, pg2 = peakHangPre
                                    if (!ph2 || ph2.length !== nb) {
                                        ph2 = new Array(nb); pg2 = new Array(nb)
                                        for (var z2 = 0; z2 < nb; ++z2) { ph2[z2] = -200; pg2[z2] = 0 }
                                    }
                                    for (var q = 0; q < nb; ++q) {
                                        var d2 = preArr[q]
                                        if (d2 >= ph2[q]) { ph2[q] = d2; pg2[q] = hangF }
                                        else if (pg2[q] > 0) { pg2[q]-- }
                                        else { ph2[q] = Math.max(d2, ph2[q] - decF) }
                                    }
                                    peakHoldPre = ph2; peakHangPre = pg2
                                }
                            }

                            if (amode === 1) {
                                // ── SPECTRUM (line / fill) ──────────────────
                                // Pink-noise reference (-3 dB/oct) — tilt guide.
                                ctx.strokeStyle = "rgba(255,120,200,0.22)"; ctx.lineWidth = 1
                                ctx.beginPath()
                                var pf0 = true
                                for (var pf = fMin; pf <= fMax; pf *= 1.15) {
                                    var ppx = graph.xForFreq(pf)
                                    var ppy = graph.yForDb(-45 - 3 * (Math.log(pf / 1000) / Math.LN2))
                                    if (pf0) { ctx.moveTo(ppx, ppy); pf0 = false } else ctx.lineTo(ppx, ppy)
                                }
                                ctx.stroke()
                                // Filled "after" (post-EQ) spectrum.
                                ctx.beginPath()
                                var fs = false, lastX = 0
                                for (var i2 = 1; i2 < nb; ++i2) {
                                    var f2 = (i2 / (nb - 1)) * nyq
                                    if (f2 < fMin) continue
                                    if (f2 > fMax) break
                                    var x2 = graph.xForFreq(f2), y2 = graph.yForDb(post[i2])
                                    if (!fs) { ctx.moveTo(x2, h); ctx.lineTo(x2, y2); fs = true } else ctx.lineTo(x2, y2)
                                    lastX = x2
                                }
                                if (fs) {
                                    ctx.lineTo(lastX, h); ctx.closePath()
                                    ctx.fillStyle = "rgba(60,200,255,0.16)"; ctx.fill()
                                    ctx.strokeStyle = "rgba(90,210,255,0.85)"; ctx.lineWidth = 1.5
                                    trace(post); ctx.stroke()
                                }
                                // Before (pre-EQ) overlay — white line.
                                if (preArr) {
                                    ctx.strokeStyle = "rgba(255,255,255,0.9)"; ctx.lineWidth = 1.5
                                    trace(preArr); ctx.stroke()
                                }
                                // Peak-hold lines: red (after) + white (before).
                                if (eq.accumulate) {
                                    ctx.strokeStyle = "rgba(255,70,70,0.95)"; ctx.lineWidth = 1.5
                                    trace(peakHold); ctx.stroke()
                                    if (preArr) {
                                        ctx.strokeStyle = "rgba(255,255,255,0.95)"; ctx.lineWidth = 1.5
                                        trace(peakHoldPre); ctx.stroke()
                                    }
                                }
                            } else {
                                // ── RTA (1/N-octave band bars) ──────────────
                                var N = 28
                                var rr = Math.pow(fMax / fMin, 1 / N)
                                for (var b = 0; b < N; ++b) {
                                    var fLo = fMin * Math.pow(rr, b)
                                    var fHi = fMin * Math.pow(rr, b + 1)
                                    var x0 = graph.xForFreq(fLo), x1 = graph.xForFreq(fHi)
                                    var bwf = x1 - x0
                                    var gap = Math.min(2, bwf * 0.18)
                                    var bx = x0 + gap * 0.5, bwid = Math.max(1, bwf - gap)
                                    // after bar (cyan fill)
                                    var ay = graph.yForDb(bandMax(post, fLo, fHi))
                                    ctx.fillStyle = "rgba(60,200,255,0.5)"
                                    ctx.fillRect(bx, ay, bwid, h - ay)
                                    // after peak cap (cyan)
                                    if (eq.accumulate) {
                                        var apy = graph.yForDb(bandMax(peakHold, fLo, fHi))
                                        ctx.fillStyle = "rgba(255,70,70,0.95)"
                                        ctx.fillRect(bx, apy - 1.5, bwid, 1.5)
                                    }
                                    // before (pre-EQ): white outline + peak cap
                                    if (preArr) {
                                        var py = graph.yForDb(bandMax(preArr, fLo, fHi))
                                        ctx.strokeStyle = "rgba(255,255,255,0.9)"; ctx.lineWidth = 1
                                        ctx.strokeRect(bx + 0.5, py + 0.5, Math.max(1, bwid - 1), Math.max(0, h - py - 1))
                                        if (eq.accumulate) {
                                            var ppy2 = graph.yForDb(bandMax(peakHoldPre, fLo, fHi))
                                            ctx.fillStyle = "rgba(255,255,255,0.95)"
                                            ctx.fillRect(bx, ppy2 - 1.5, bwid, 1.5)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Vertical grid at decade-ish freqs + a frequency marker
                    // row across the top (#167) so you can read where on the
                    // curve a given frequency sits.  Only the freqs inside the
                    // current window are drawn/labelled (fMax tracks TX BW, #168).
                    ctx.lineWidth = 1
                    ctx.font = "bold 11px 'Segoe UI', sans-serif"
                    ctx.textBaseline = "top"
                    ctx.textAlign = "center"
                    var fl = [50, 100, 200, 500, 1000, 2000, 5000, 10000]
                    for (var k = 0; k < fl.length; ++k) {
                        if (fl[k] < graph.fMin || fl[k] > graph.fMax) continue
                        var gx = graph.xForFreq(fl[k])
                        ctx.strokeStyle = root.cGrid
                        ctx.beginPath(); ctx.moveTo(gx, 13); ctx.lineTo(gx, h); ctx.stroke()
                        ctx.fillStyle = root.cText
                        ctx.fillText(root.fmtFreq(fl[k]), gx, 1)
                    }
                    // TX passband-edge marker (#168) — the audio top frequency
                    // the TX bandwidth allows on air.  Amber dashed line + label
                    // so you see where your transmitted passband ends.
                    if (graph.txEdgeHz > graph.fMin && graph.txEdgeHz <= graph.fMax) {
                        var ex = graph.xForFreq(graph.txEdgeHz)
                        ctx.strokeStyle = "rgba(224,160,48,0.8)"
                        ctx.setLineDash([4, 3])
                        ctx.beginPath(); ctx.moveTo(ex, 13); ctx.lineTo(ex, h); ctx.stroke()
                        ctx.setLineDash([])
                        ctx.fillStyle = "rgba(224,160,48,0.95)"
                        ctx.textAlign = "right"
                        ctx.fillText("TX " + root.fmtFreq(graph.txEdgeHz), ex - 3, 1)
                    }
                    ctx.textAlign = "left"   // restore default for the dB labels
                    // Horizontal dB grid every 3 dB across the ±15 axis.
                    // Major lines (every 6 dB) are brighter + labelled on the
                    // left so the operator reads gain vs flat; the 3 dB
                    // intermediates are dim, unlabelled tick lines.  0 dB =
                    // flat/centre, emphasised.
                    // Every 3 dB line is now labelled (operator ask: add the
                    // ±3 / ±9 rows).  Majors (0 / ±6 / ±12) keep a brighter
                    // gridline; the ±3 / ±9 intermediates get a mid line so
                    // their label has a visible rule.  All labels bright; 0 dB
                    // in a bright blue.
                    ctx.font = "bold 11px 'Segoe UI', sans-serif"
                    ctx.textBaseline = "bottom"
                    for (var db = -12; db <= 12; db += 3) {
                        var gy = graph.yForGain(db)
                        var major = (db % 6) === 0
                        ctx.strokeStyle = (db === 0) ? root.cGrid0
                                        : (major ? root.cGrid : "#243a48")
                        ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke()
                        ctx.fillStyle = (db === 0) ? "#7fb8d4" : root.cText
                        ctx.fillText((db > 0 ? "+" : "") + db
                                     + (db === 0 ? " dB (flat)" : " dB"),
                                     5, gy - 2)
                    }

                    // Summed response curve — sample magnitudeDb per ~3 px.
                    function plot() {
                        ctx.beginPath()
                        var first = true
                        for (var x = 0; x <= w; x += 3) {
                            var f = graph.freqForX(x)
                            var y = graph.yForGain(eq.magnitudeDb(f))
                            if (first) { ctx.moveTo(x, y); first = false }
                            else ctx.lineTo(x, y)
                        }
                    }
                    // Soft glow pass + crisp pass.
                    ctx.strokeStyle = "rgba(120,225,255,0.30)"; ctx.lineWidth = 6
                    plot(); ctx.stroke()
                    ctx.strokeStyle = root.cCurve; ctx.lineWidth = 2
                    plot(); ctx.stroke()
                }
            }

            // Visual band nodes (event handling is the MouseArea below).
            Repeater {
                model: eq.numBands
                delegate: Item {
                    id: nodeWrap
                    required property int index
                    readonly property bool usesGain: { root.rev; return eq.typeUsesGain(eq.bandType(index)) }
                    readonly property real nx: { root.rev; return graph.xForFreq(eq.bandFreq(index)) }
                    // Gain types (Peak/shelves) ride their gain handle; cut/
                    // pass types (LP/HP/BP/Notch) ride their response curve.
                    // clampY keeps the handle inside the graph box so even a
                    // deep notch's node stays grabbable at the bottom edge.
                    readonly property real ny: { root.rev
                        var raw = usesGain ? graph.yForGain(eq.bandGain(index))
                                           : graph.yForCurveFreq(eq.bandFreq(index))
                        return graph.clampY(raw) }
                    readonly property bool sel: { root.rev; return eq.selectedBand === index }
                    readonly property bool en:  { root.rev; return eq.bandEnabled(index) }
                    x: nx - 11; y: ny - 11; width: 22; height: 22

                    Rectangle {       // selection ring
                        anchors.centerIn: parent
                        width: 22; height: 22; radius: 11
                        color: "transparent"
                        visible: nodeWrap.sel
                        border.color: "#ffffff"; border.width: 2
                    }
                    Rectangle {       // the node
                        anchors.centerIn: parent
                        width: 15; height: 15; radius: 8
                        color: nodeWrap.en ? root.bandColor[index] : "#3a4350"
                        border.color: "#0d141b"; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: nodeWrap.index + 1
                            color: "#0d141b"; font.bold: true; font.pixelSize: 9
                        }
                    }
                }
            }

            // Single interaction layer: press hit-tests the nearest node
            // (select + start drag), drag edits freq (+ gain for gain
            // types), double-click toggles enable, wheel = selected Q.
            MouseArea {
                id: graphMouse
                anchors.fill: parent
                property int dragBand: -1
                function nearest(mx, my) {
                    var best = -1, bd = 1e9
                    for (var i = 0; i < eq.numBands; ++i) {
                        var nx = graph.xForFreq(eq.bandFreq(i))
                        var ny = graph.clampY(eq.typeUsesGain(eq.bandType(i))
                                 ? graph.yForGain(eq.bandGain(i))
                                 : graph.yForCurveFreq(eq.bandFreq(i)))
                        var d = Math.hypot(mx - nx, my - ny)
                        if (d < bd) { bd = d; best = i }
                    }
                    return (best >= 0 && bd <= 22) ? best : -1
                }
                onPressed: (m) => {
                    var b = nearest(m.x, m.y)
                    dragBand = b
                    if (b >= 0) eq.selectedBand = b
                }
                onPositionChanged: (m) => {
                    if (dragBand < 0) return
                    eq.setBandFreq(dragBand, graph.freqForX(m.x))
                    if (eq.typeUsesGain(eq.bandType(dragBand)))
                        eq.setBandGain(dragBand, graph.gainForY(m.y))
                }
                onReleased: dragBand = -1
                onDoubleClicked: (m) => {
                    var b = nearest(m.x, m.y)
                    if (b >= 0) eq.setBandEnabled(b, !eq.bandEnabled(b))
                }
                WheelHandler {
                    onWheel: (ev) => {
                        var i = eq.selectedBand
                        if (i < 0) return
                        var q = eq.bandQ(i) * (ev.angleDelta.y > 0 ? 1.15 : 0.87)
                        eq.setBandQ(i, q)
                    }
                }
            }
        }

        // ── Tile row (the EESDR3 idiom) ────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed
            spacing: 4
            Repeater {
                model: eq.numBands
                delegate: Rectangle {
                    id: tile
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 58
                    radius: 4
                    readonly property bool sel: { root.rev; return eq.selectedBand === index }
                    readonly property bool en:  { root.rev; return eq.bandEnabled(index) }
                    readonly property int ty:   { root.rev; return eq.bandType(index) }
                    color: sel ? "#13222c" : "#0b141b"
                    border.color: sel ? root.bandColor[index] : "#1c2a36"
                    border.width: sel ? 2 : 1
                    opacity: en ? 1.0 : 0.5

                    // Select / right-click-reset layer — declared FIRST so it
                    // sits BELOW the column; the Type label's own MouseArea
                    // (in the column above) wins clicks on the Type chip, and
                    // the mouse-transparent #/freq/gain labels fall through to
                    // here for select + reset.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (m) => {
                            if (m.button === Qt.RightButton) eq.resetBand(tile.index)
                            else eq.selectedBand = tile.index
                        }
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 1
                        Label {
                            text: "#" + (tile.index + 1)
                            color: root.bandColor[tile.index]
                            font.bold: true; font.pixelSize: 10
                            Layout.alignment: Qt.AlignHCenter
                        }
                        // Type chip — click opens the full 7-type picker
                        // (the ▾ signals it's a dropdown).  Sized to its text
                        // but the MouseArea is padded so it's an easy target.
                        Label {
                            id: typeLabel
                            text: { root.rev; return eq.typeName(tile.ty) + " ▾" }
                            color: root.cAccent; font.pixelSize: 10
                            font.bold: true
                            Layout.alignment: Qt.AlignHCenter
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4   // bigger hit target
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    eq.selectedBand = tile.index
                                    typeMenu.band = tile.index
                                    typeMenu.popup()
                                }
                            }
                        }
                        Label {
                            text: { root.rev; return root.fmtFreq(eq.bandFreq(tile.index)) }
                            color: root.cMuted; font.pixelSize: 10
                            font.family: "Consolas"
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Label {
                            text: { root.rev
                                if (eq.typeUsesGain(tile.ty)) {
                                    var g = eq.bandGain(tile.index)
                                    return (g >= 0 ? "+" : "") + g.toFixed(1)
                                }
                                return "Q " + eq.bandQ(tile.index).toFixed(2) }
                            color: root.cText; font.pixelSize: 10
                            font.family: "Consolas"
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }
        }
        }
    }
}
