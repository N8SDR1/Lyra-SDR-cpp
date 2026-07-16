// Lyra — Panadapter dock panel (spectrum over waterfall).
//
// Hosted by a QQuickWidget inside a QDockWidget (see mainwindow.cpp).
// Root is a plain Item so the QQuickWidget can host it.  A vertical
// SplitView stacks the Vulkan scene-graph Panadapter (top) over the
// Waterfall (bottom) — both share the exact same frequency extent and
// dB window, so the spectrum trace and its rolling history read as one
// instrument.  The spectrum carries the dBm scale (both edges) + the
// 3-zone right-edge dB drag + the shared frequency scale (drawn at the
// boundary so it labels both); the waterfall is GPU-textured and
// palette-coloured.  Display options come from the shared Prefs object
// (Settings -> Visuals).

import QtQuick
import QtQuick.Controls
import LyraUI

Item {
    id: root
    implicitWidth: 600
    implicitHeight: 360

    // Honest floor — see AudioPanel.  The implicit size above is what the
    // panadapter PREFERS; this is the smallest it can be and still be usable:
    // the spectrum's own 90px minimum, the waterfall's 60px, the frequency
    // scale, and the dB labels down the sides.  Below ~560 wide the frequency
    // labels start to collide, but it stays workable and click-to-tune still
    // lands, so that is a legibility limit rather than a hard floor.
    readonly property int lyraMinWidth:  320
    readonly property int lyraMinHeight: 200

    // RX1 centre frequency.  The panadapter is always centred on the
    // tuned DDC freq, so this drives the frequency scale AND click-to-
    // tune.  Held in a LOCAL property updated via the rx1FreqChanged
    // SIGNAL — a direct binding to Stream.rx1FreqHz is unreliable in
    // this QQuickWidget setup (same lesson as the Band panel), which
    // would leave the freq labels stale after a tune.
    property int centerHz: 0
    // #174 CTUN: the dial's offset from the LOCKED display centre.  Under
    // CTUN the spectrum is frozen at ctuneCenterHz, so the carrier marker +
    // filter passband must slide to (rx1FreqHz − centre) for the operator to
    // drag the marker onto signals.  0 when CTUN is off (centre == dial) →
    // all non-CTUN rendering stays byte-identical.
    readonly property real ctunDialOffHz:
        Stream.ctuneEnabled ? (Stream.rx1FreqHz - centerHz) : 0
    // Effective RX-DDC freq = dial + RIT offset (when RIT on).  The DDC is
    // tuned HERE, so the spectrum + freq scale + click-to-tune all centre on
    // it.  With RIT engaged the panadapter re-centres on where you're actually
    // listening (the pale-blue DIAL marker shows where the VFO reads).
    function effCenterHz() {
        // #174 CTUNE: when engaged the DDC is LOCKED at ctuneCenterHz, so the
        // panadapter centre + freq scale lock there too and the dial marker
        // (drawn at rx1FreqHz − centre) slides within the frozen span.
        if (Stream.ctuneEnabled)
            return Stream.ctuneCenterHz
        return Stream.rx1FreqHz
             + (Stream.ritEnabled ? Stream.ritOffsetHz : 0)
    }
    Component.onCompleted: {
        centerHz = effCenterHz()
        effMin = pan.effDbMin
        effMax = pan.effDbMax
    }
    Connections {
        target: Stream
        function onRx1FreqChanged() {
            // #174 CTUNE Stage 2: the C++ RX-NCO writer (pushEffectiveRxFreq)
            // now owns ALL re-centering — smooth-scroll at the display margin,
            // far-jump re-center, zoom revert-to-tracking, and the IQ clamp,
            // Thetis-faithful — and emits ctuneChanged whenever it moves the
            // centre.  QML is a pure reader: just track the (possibly re-locked)
            // centre.  (The old QML hard ±halfSpan snap is gone.)
            root.centerHz = root.effCenterHz()
        }
        function onRitChanged()     { root.centerHz = root.effCenterHz() }
        function onCtuneChanged()   { root.centerHz = root.effCenterHz() }
    }

    // Display panel "Clear" button → flush the peak-hold buffer (the
    // panadapter lives in this dock, the button in another).
    Connections {
        target: Prefs
        function onPeakClearRequested() { pan.clearPeaks() }
    }

    // Drive the engine's zoom from the shared Prefs (Display panel).  The
    // engine crops the centre 1/zoom of the full-resolution spectrum;
    // WdspEngine.spanHz reports inRate/zoom so the frequency scale,
    // drag-pan and cursor readout all track the zoom for free.
    Binding {
        target: WdspEngine
        property: "zoom"
        value: Prefs.zoom
    }

    // Effective dB range the panadapter is actually rendering with
    // (auto-fit when on, else the manual dbMin/dbMax).  Mirrored from
    // the panadapter's effRangeChanged SIGNAL into LOCAL properties so
    // the dB-scale labels track it reliably (same proven pattern as
    // centerHz — avoids the context-property binding flakiness).
    property real effMin: -130
    property real effMax: -20
    Connections {
        target: pan
        function onEffRangeChanged() {
            root.effMin = pan.effDbMin
            root.effMax = pan.effDbMax
        }
    }

    // dB labels at the 8 horizontal grid divisions: dbMax at the top
    // down to dbMin at the bottom.  Dim, outlined, tracks Prefs live.
    component DbLabels : Repeater {
        id: rep
        property bool rightSide: false
        property Item area      // the spectrum area these label
        property real dMax: 0   // effective ceiling (auto or manual)
        property real dMin: 0   // effective floor
        model: 9   // 0..8 -> top edge .. bottom edge
        delegate: Text {
            required property int index
            readonly property real frac: index / 8.0
            x: rep.rightSide ? (rep.area.width - width - 5) : 5
            y: Math.max(0, Math.min(rep.area.height - height,
                                    frac * rep.area.height - height / 2))
            text: Math.round(rep.dMax - frac * (rep.dMax - rep.dMin))
            color: "#8fa6ba"
            font.pixelSize: 13
            font.family: "Consolas"
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    SplitView {
        id: splitV
        anchors.fill: parent
        orientation: Qt.Vertical

        // The divider position is NOT covered by QMainWindow.saveState()
        // (it lives inside this dock's QML content), so persist it via
        // Prefs (→ QSettings) using the Qt-intended SplitView
        // saveState()/restoreState().  This ties into session restore
        // AND the View → Save/Restore/Reset layout actions (which write
        // Prefs.panadapterSplit from C++).  `_applying` guards the
        // save↔notify feedback loop.  Same approach old Lyra used for
        // its central spectrum/waterfall splitter.
        property bool _applying: false
        // The waterfall height the operator actually chose, in pixels (-1 = none
        // saved, use the 60/40 factory split).  restoreState() carries an
        // ABSOLUTE height, so on a shorter dock than the one it was saved on it
        // can be nearly all of it — the spectrum gets squeezed to a sliver and
        // the operator can't see the signal they're tuning.  So we remember what
        // they wanted and re-apply it clamped against the CURRENT height, every
        // time the dock resizes.  Clamping once at startup is not enough: the
        // SplitView has no meaningful height yet when the panel is constructed,
        // so a one-shot clamp measures against a stale number and permanently
        // shrinks the waterfall.
        property real _wantWf: -1
        function applyFromPrefs() {
            _applying = true
            const st = Prefs.panadapterSplit
            if (st === undefined || st === null || st === "") {
                _wantWf = -1
            } else {
                splitV.restoreState(st)
                _wantWf = wf.SplitView.preferredHeight
            }
            _applying = false
            reflow()
        }
        // Re-apply the operator's chosen waterfall height against the CURRENT dock
        // height.  The ONLY thing that may override their choice is a dock too
        // short to honour it: the cap is what the spectrum itself declares it
        // needs (SplitView.minimumHeight), nothing more.  An earlier version of
        // this capped the waterfall at 60% of the dock, which quietly refused any
        // taller waterfall the operator had deliberately dragged out and saved —
        // it came back shrunk on every restart.  A percentage cap here is
        // second-guessing; the SplitView's own minimums already stop either pane
        // from vanishing (spectrum 90px, waterfall 60px), and they are the honest
        // limit.  _wantWf is deliberately NOT rewritten when we cap, so the full
        // height comes back the moment the dock is tall enough for it again.
        function reflow() {
            if (splitV.height <= 0 || splitV.resizing || _applying) return
            _applying = true
            const want = _wantWf > 0 ? _wantWf : splitV.height * 0.4   // factory 60/40
            const hi = splitV.height - spectrumArea.SplitView.minimumHeight
            wf.SplitView.preferredHeight = Math.min(hi, want)
            _applying = false
        }
        Component.onCompleted: applyFromPrefs()
        onHeightChanged: reflow()
        onResizingChanged: {
            // Persist only when the operator finishes dragging the handle.
            if (!resizing && !_applying) {
                _applying = true
                _wantWf = wf.SplitView.preferredHeight
                Prefs.panadapterSplit = splitV.saveState()
                _applying = false
            }
        }
        Connections {
            target: Prefs
            // External change (Restore/Reset from the View menu) re-applies.
            function onPanadapterSplitChanged() {
                if (!splitV._applying) splitV.applyFromPrefs()
            }
        }

        // ===== Spectrum area (panadapter + overlays) =====
        Item {
            id: spectrumArea
            SplitView.fillHeight: true
            SplitView.minimumHeight: 90

            Panadapter {
                id: pan
                anchors.fill: parent
                engine: WdspEngine
                gridLevel: Prefs.gridLevel
                glassSheen: Prefs.glassSheen
                targetFps: Prefs.targetFps
                // Auto-fit when enabled; dbMin/dbMax used otherwise.
                autoScale: Prefs.dbAuto
                dbMin: Prefs.dbMin
                dbMax: Prefs.dbMax
                traceMode: Prefs.traceMode
                traceColor: Prefs.traceColor
                palette: Prefs.palette
                strengthColor: Prefs.strengthColor
                fillEnabled: Prefs.fillEnabled
                fillColor: Prefs.fillColor
                smoothing: Prefs.smoothing
                // Peak-hold markers (Display panel + Settings → Visuals).
                peakEnabled: Prefs.peakEnabled
                peakHoldSecs: Prefs.peakHoldSecs
                peakDecayDbps: Prefs.peakDecayDbps
                peakStyle: Prefs.peakStyle
                peakColor: Prefs.peakColor
                peakShowDb: Prefs.peakShowDb

                // Peak glow: render to a layer texture + additive-bloom
                // ShaderEffect (off when glow == 0, so no GPU cost).
                layer.enabled: Prefs.glow > 0
                layer.effect: ShaderEffect {
                    fragmentShader: "qrc:/shaders/glow.frag.qsb"
                    property real glowStrength: Prefs.glow / 100.0 * 1.6
                    property real glowThreshold: 0.12
                    property real glowSpread: 2.0
                    property size texSize: Qt.size(width, height)
                }
            }

            // ---- Lyra constellation watermark (luminance-keyed) ----
            Image {
                id: wmSource
                source: "qrc:/qt/qml/Lyra/src/assets/watermarks/lyra-watermark.jpg"
                visible: false          // texture provider only
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            ShaderEffect {
                id: watermark
                visible: Prefs.watermark
                fragmentShader: "qrc:/shaders/watermark.frag.qsb"
                property variant source: wmSource
                anchors.centerIn: parent
                height: pan.height * 0.92
                width: height * 1.3
                opacity: 0.42
                z: 1
            }

            // ---- Vega pulse ----
            Rectangle {
                id: vegaGlow
                visible: Prefs.watermark
                width: 24; height: 24; radius: 12
                color: "#bfe6ff"
                opacity: vegaCore.opacity * 0.22
                x: pan.width * 0.5 - width / 2
                y: pan.height * 0.20 - height / 2
                z: 1
            }
            Rectangle {
                id: vegaCore
                visible: Prefs.watermark
                width: 6; height: 6; radius: 3
                color: "#e8f6ff"
                x: pan.width * 0.5 - width / 2
                y: pan.height * 0.20 - height / 2
                z: 2
                SequentialAnimation on opacity {
                    running: vegaCore.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.30; to: 0.95; duration: 1900
                                      easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.95; to: 0.30; duration: 1900
                                      easing.type: Easing.InOutSine }
                }
            }

            // ---- Meteor streaks (rare ambient shooting stars) ----
            Canvas {
                id: meteorCanvas
                anchors.fill: parent
                z: 3
                visible: Prefs.meteors
                renderStrategy: Canvas.Cooperative

                property var meteors: []
                property real nextSpawn: 0
                property bool schedInit: false

                readonly property real durMin: 0.50
                readonly property real durMax: 0.85
                readonly property real headR: 2.5
                readonly property int  trailN: 12
                readonly property real trailLenS: 0.18
                readonly property real fadeInS: 0.05
                readonly property real fadeOutS: 0.15
                readonly property real trailMaxA: 0.55
                readonly property real gravity: 600.0
                readonly property real lateral: 400.0

                function nowS() { return Date.now() / 1000.0; }
                function rnd(a, b) { return a + Math.random() * (b - a); }

                function scheduleNext(now) {
                    var avg = Prefs.meteorGap;
                    nextSpawn = now + rnd(avg * 0.5, avg * 1.6);
                }

                function spawn() {
                    var w = width, h = height;
                    var roll = Math.random();
                    var sx, sy;
                    if (roll < 0.6)      { sx = rnd(0.15 * w, 0.85 * w); sy = -10; }
                    else if (roll < 0.8) { sx = -10;      sy = rnd(0, 0.30 * h); }
                    else                 { sx = w + 10;   sy = rnd(0, 0.30 * h); }
                    var tx = (w - sx) + rnd(-0.15 * w, 0.15 * w);
                    var ty = h + 20;
                    var dur = rnd(durMin, durMax);
                    var g = gravity;
                    var vx = (tx - sx) / dur;
                    var vy = (ty - sy - 0.5 * g * dur * dur) / dur;
                    if (lateral > 0) vx += rnd(-lateral, lateral);
                    var gold = Math.random() < (Prefs.meteorGold / 100.0);
                    return { t0: nowS(), sx: sx, sy: sy, vx: vx, vy: vy, ay: g,
                             dur: dur, gold: gold };
                }

                Timer {
                    interval: 33; repeat: true; running: meteorCanvas.visible
                    onTriggered: meteorCanvas.tick()
                }

                function tick() {
                    var now = nowS();
                    if (!schedInit) { scheduleNext(now); schedInit = true; }
                    if (now >= nextSpawn && meteors.length < 1) {
                        meteors.push(spawn());
                        scheduleNext(now);
                    }
                    var alive = [];
                    for (var i = 0; i < meteors.length; i++)
                        if (now - meteors[i].t0 < meteors[i].dur) alive.push(meteors[i]);
                    var wasActive = meteors.length > 0;
                    meteors = alive;
                    if (meteors.length > 0 || wasActive) requestPaint();
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    if (meteors.length === 0) return;
                    var now = nowS();
                    ctx.globalCompositeOperation = "lighter";
                    for (var i = 0; i < meteors.length; i++)
                        drawOne(ctx, meteors[i], now);
                    ctx.globalCompositeOperation = "source-over";
                }

                function drawOne(ctx, m, now) {
                    var age = now - m.t0;
                    if (age >= m.dur) return;
                    var lifeA;
                    if (age < fadeInS)            lifeA = age / fadeInS;
                    else if (age > m.dur - fadeOutS) lifeA = Math.max(0, (m.dur - age) / fadeOutS);
                    else                         lifeA = 1.0;
                    if (lifeA <= 0) return;
                    var rgb = m.gold ? "255,215,130" : "225,235,255";
                    for (var k = trailN; k > 0; k--) {
                        var tBack = (k / trailN) * trailLenS;
                        var pa = age - tBack;
                        if (pa < 0) continue;
                        var tx = m.sx + m.vx * pa;
                        var ty = m.sy + m.vy * pa + 0.5 * m.ay * pa * pa;
                        var frac = 1.0 - (k / trailN);
                        var a = lifeA * frac * trailMaxA;
                        if (a < 0.01) continue;
                        var rad = headR * (0.35 + 0.65 * frac);
                        ctx.fillStyle = "rgba(" + rgb + "," + a.toFixed(3) + ")";
                        ctx.beginPath(); ctx.arc(tx, ty, rad, 0, 2 * Math.PI); ctx.fill();
                    }
                    var hx = m.sx + m.vx * age;
                    var hy = m.sy + m.vy * age + 0.5 * m.ay * age * age;
                    ctx.fillStyle = "rgba(" + rgb + "," + lifeA.toFixed(3) + ")";
                    ctx.beginPath(); ctx.arc(hx, hy, headR, 0, 2 * Math.PI); ctx.fill();
                }
            }

            // ---- dBm vertical scale, BOTH edges ----
            DbLabels { rightSide: false; area: spectrumArea
                       dMax: root.effMax; dMin: root.effMin }
            DbLabels { rightSide: true;  area: spectrumArea
                       dMax: root.effMax; dMin: root.effMin }

            // ---- waterfall collapse toggle (old-Lyra triangle) ----
            // ▼ = waterfall shown (click to collapse it away); ▲ =
            // collapsed (click to bring it back).  Lives in the spectrum
            // area so it stays reachable even when the waterfall is gone.
            Rectangle {
                id: wfToggle
                z: 6
                width: 20; height: 18; radius: 3
                x: 4
                y: spectrumArea.height - height - 20
                color: wfToggleMA.containsMouse ? "#1f3a4a" : "#cc14202a"
                border.color: "#2a4a5a"
                Text {
                    anchors.centerIn: parent
                    text: Prefs.waterfallCollapsed ? "▲" : "▼"
                    color: "#8fd0ff"
                    font.pixelSize: 11
                }
                MouseArea {
                    id: wfToggleMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Prefs.waterfallCollapsed =
                                   !Prefs.waterfallCollapsed
                    ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                    ToolTip.text: Prefs.waterfallCollapsed
                                  ? qsTr("Show waterfall")
                                  : qsTr("Collapse waterfall")
                }
            }

            // #174 CTUNE lives as a header chip (next to RX EQ) — see
            // MainWindow's RX-DSP chip strip.  The panadapter just locks
            // centerHz to ctuneCenterHz (effCenterHz) + auto-re-centres on
            // tune-past-edge (the Connections handler above).

            // ---- frequency scale at the spectrum/waterfall boundary ----
            // MHz labels at the vertical grid divisions; shared X axis for
            // the spectrum above and the waterfall below.  Centre = RX1
            // DDC freq; full width spans WdspEngine.spanHz (the IQ rate).
            Repeater {
                model: 11   // 0..10 -> left edge .. right edge
                delegate: Text {
                    required property int index
                    readonly property real frac: index / 10.0
                    readonly property real fhz:
                        root.centerHz + (frac - 0.5) * WdspEngine.spanHz
                    text: (fhz / 1.0e6).toFixed(3)
                    color: "#8fa6ba"
                    font.pixelSize: 12
                    font.family: "Consolas"
                    style: Text.Outline
                    styleColor: "#80000000"
                    x: Math.max(0, Math.min(spectrumArea.width - width,
                                            frac * spectrumArea.width - width / 2))
                    y: spectrumArea.height - height - 2
                }
            }

            // ---- VFO marker — RX1 sits at the panadapter centre ----
            // The DDC is tuned to rx1FreqHz, so the spectrum is always
            // centred on the receive frequency.  A SOLID 1px orange
            // (#ffaa50) line at centre, slightly translucent so the trace
            // shows through (operator pref 2026-06-20 — solid reads cleaner
            // than the old dashes, esp. alongside the TX marker in split).
            //
            // Built from a plain Rectangle, NOT a QtQuick.Shapes Shape:
            // a Shape here regressed the shared scene-graph render path
            // and turned the waterfall (lower pane) patchy.  Pure Items
            // keep the scene on the same renderer the waterfall needs.
            Rectangle {
                z: 4
                width: 1
                height: spectrumArea.height
                color: "#ffaa50"
                opacity: 0.7
                // Carrier marker: centre in non-CW; offset by the CW pitch
                // (markerOffsetHz) so it sits on the carrier the operator
                // hears (which is +pitch above the DDS centre).
                x: Math.round(spectrumArea.width
                              * (0.5 + (WdspEngine.markerOffsetHz
                                        + root.ctunDialOffHz)
                                       / Math.max(1, WdspEngine.spanHz)))
                y: 0
            }

            // ---- SPLIT TX marker — VFO B (the transmit freq) ----
            // Shown only in SPLIT: a SOLID vertical line at VFO B's offset
            // from the RX centre.  LIME while armed (where TX WILL go),
            // RED on key (the locked indication model — red = on the air).
            // VFO B is the carrier, so its offset from the RX DDS centre =
            // (vfoBHz − centerHz).  Off-span → it clips off-screen.
            Rectangle {
                visible: Stream.splitEnabled
                z: 4
                width: 2
                height: spectrumArea.height
                color: Stream.txDisplayActive ? "#ff4136" : "#a6ff00"
                opacity: 0.85
                x: Math.round(spectrumArea.width
                              * (0.5 + (Stream.vfoBHz - root.centerHz)
                                       / Math.max(1, WdspEngine.spanHz)))
                y: 0
            }

            // ---- RIT dial marker — where the VFO reads vs where you listen ----
            // With RIT on, the panadapter re-centres on the listening freq
            // (DDC = dial + RIT); this pale-blue line marks the operator's
            // dial so the offset reads at a glance.  rx1FreqHz − centerHz =
            // −RIT offset.  Off-span → clips off-screen.
            Rectangle {
                visible: Stream.ritEnabled && Stream.ritOffsetHz !== 0
                z: 4
                width: 1
                height: spectrumArea.height
                color: "#7fb0ff"
                opacity: 0.7
                x: Math.round(spectrumArea.width
                              * (0.5 + (Stream.rx1FreqHz - root.centerHz)
                                       / Math.max(1, WdspEngine.spanHz)))
                y: 0
                Text {
                    text: qsTr("DIAL"); color: "#7fb0ff"
                    font.pixelSize: 9; font.bold: true
                    x: 3; y: 2
                }
            }

            // ---- XIT TX marker (simplex) — where TX lands with XIT on ----
            // SPLIT has its own VFO-B TX marker; in simplex with XIT the TX
            // freq = dial + XIT.  LIME armed / RED on key (locked model).
            Rectangle {
                visible: Stream.xitEnabled && !Stream.splitEnabled
                         && Stream.xitOffsetHz !== 0
                z: 4
                width: 2
                height: spectrumArea.height
                color: Stream.txDisplayActive ? "#ff4136" : "#a6ff00"
                opacity: 0.85
                x: Math.round(spectrumArea.width
                              * (0.5 + (Stream.rx1FreqHz + Stream.xitOffsetHz
                                        - root.centerHz)
                                       / Math.max(1, WdspEngine.spanHz)))
                y: 0
            }

            // ---- peak dB labels (strongest in-view peaks) ----
            // Numeric readout for the top peaks the panadapter detected
            // (peakLabels = [{frac, db}, …]); only populated when Peak
            // Hold + "Show dB" are on.  Positioned by fraction-of-width
            // and mapped to the live effective dB range.
            Repeater {
                model: pan.peakLabels
                delegate: Text {
                    required property var modelData
                    readonly property real db: modelData.db
                    readonly property real norm: Math.max(0, Math.min(1,
                        (db - root.effMin)
                        / Math.max(1, root.effMax - root.effMin)))
                    z: 5
                    text: (db >= 0 ? "+" : "") + Math.round(db)
                    color: Prefs.peakColor
                    font.pixelSize: 11
                    font.bold: true
                    font.family: "Consolas"
                    style: Text.Outline
                    styleColor: "#cc000000"
                    x: Math.max(0, Math.min(spectrumArea.width - width,
                                 modelData.frac * spectrumArea.width + 4))
                    y: Math.max(2, Math.min(spectrumArea.height - height - 2,
                                 (1 - norm) * spectrumArea.height - height - 2))
                }
            }

            // ---- noise-floor reference line + NF label ----
            // Dashed line at the rolling ~20th-percentile floor
            // (pan.noiseFloorDb), mapped to the live effective dB range,
            // with an "NF -NN dBFS" label.  Colour + on/off from Prefs.
            Item {
                id: nfLine
                anchors.fill: parent
                z: 4
                readonly property real nfNorm:
                    (pan.noiseFloorDb - root.effMin)
                    / Math.max(1, root.effMax - root.effMin)
                visible: Prefs.noiseFloorEnabled
                         && nfNorm >= 0 && nfNorm <= 1
                readonly property real nfY:
                    Math.max(0, Math.min(spectrumArea.height - 1,
                             (1 - nfNorm) * spectrumArea.height))
                Repeater {                 // 5 px dash + 5 px gap
                    model: nfLine.visible
                           ? Math.max(1, Math.ceil(spectrumArea.width / 10)) : 0
                    delegate: Rectangle {
                        required property int index
                        x: index * 10
                        y: nfLine.nfY
                        width: 5; height: 1
                        color: Prefs.noiseFloorColor
                        opacity: 0.7
                    }
                }
                Text {
                    visible: nfLine.visible
                    text: "NF " + (pan.noiseFloorDb >= 0 ? "+" : "")
                          + Math.round(pan.noiseFloorDb) + qsTr(" dBFS")
                    color: Prefs.noiseFloorColor
                    font.pixelSize: 11
                    font.family: "Consolas"
                    style: Text.Outline
                    styleColor: "#cc000000"
                    // Sit just left of the right-edge dB scale numbers.
                    x: Math.max(2, spectrumArea.width - width - 46)
                    y: Math.max(0, nfLine.nfY - height - 1)
                }
            }

            // ---- cursor frequency readout (floats near the pointer) ----
            // Operator-toggleable (Settings → Visuals → Cursor readout).
            // Follows the cursor instead of pinning to the top, so it
            // never collides with the reserved top overlay strip or the
            // bottom frequency scale.
            Text {
                z: 5
                visible: Prefs.cursorReadout && specMouse.containsMouse
                         && !specMouse.tuning && specMouse.dbMode === ""
                // Full-Hz, dot-grouped (14.234.723) so the readout shows the
                // exact frequency under the cursor, not a 100 Hz-rounded one.
                text: specMouse.fmtHzGrouped(specMouse.cursorHz)
                color: "#cdd9e5"
                font.pixelSize: 12
                font.family: "Consolas"
                style: Text.Outline
                styleColor: "#cc000000"
                x: Math.max(2, Math.min(spectrumArea.width - width - 2,
                                        specMouse.mouseX + 12))
                y: Math.max(2, Math.min(spectrumArea.height - height - 2,
                                        specMouse.mouseY - 18))
            }

            // ---- spectrum mouse: click / drag / wheel tune + dB-drag ----
            // Click the spectrum to tune RX1 there; drag to pan across
            // the band; wheel to step.  The right-edge strip (3 zones)
            // still adjusts the dB scale.  Reference: old Lyra
            // spectrum_gpu.py (freq_at_pixel, release-based click-tune,
            // drag-pan throttle, wheel-tune).
            MouseArea {
                id: specMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                readonly property int zonePx: 50
                readonly property int dragThreshPx: 6
                // Right-button = manual-notch placement on empty spectrum
                // (right-click ON a notch is handled by the notch's own
                // hit-area MouseArea in notchOverlay, which steals the press).
                property bool rightPress: false
                property bool rightDragged: false
                property real rightX: 0

                // dB-drag (right edge) state
                property string dbMode: ""
                property real startY: 0
                property real startMin: 0
                property real startMax: 0
                // tune state
                property bool tuning: false
                property bool dragged: false
                property real startX: 0
                property int  startCenter: 0
                property real lastEmitMs: 0
                // #174 CTUN gesture state (Thetis console.cs:50318-50368).
                //   ""     → CTUN off, body-drag pans the spectrum
                //   "grab" → CTUN on, body-drag grabs the VFO marker
                //   "lopan"→ CTUN on, top-15px-strip drag pans the LO
                property string ctunGesture: ""
                property bool   pressInPassband: false
                property real   lastDragX: 0
                // live cursor freq for the hover readout
                readonly property real cursorHz: freqAtX(mouseX)
                // Full-Hz, dot-grouped like the VFO readout: 14.234.723.
                function fmtHzGrouped(hz) {
                    var s = Math.round(hz).toString()
                    var out = ""
                    while (s.length > 3) { out = "." + s.slice(-3) + out
                                           s = s.slice(0, -3) }
                    return s + out
                }

                function dbModeAt(mx, my) {
                    if (mx < spectrumArea.width - zonePx) return ""
                    if (my < 24) return ""          // reserve top strip
                    var third = spectrumArea.height / 3
                    if (my < third) return "max"
                    if (my > 2 * third) return "min"
                    return "pan"
                }
                function freqAtX(mx) {
                    return root.centerHz
                           + (mx / Math.max(1, width) - 0.5) * WdspEngine.spanHz
                }
                // #174 is pixel x inside the RX filter passband?  A CTUN
                // click here must NOT jump the 0-beat to the cursor (Thetis
                // m_bCTUNputsZeroOnMouse=false, console.cs:50382).
                function inPassband(mx) {
                    // offset of the cursor from the DIAL (subtract the CTUN
                    // dial offset so the test rides with the passband box).
                    var off = (mx / Math.max(1, width) - 0.5) * WdspEngine.spanHz
                              - root.ctunDialOffHz
                    var lo = Math.min(WdspEngine.passbandLowHz,
                                      WdspEngine.passbandHighHz)
                    var hi = Math.max(WdspEngine.passbandLowHz,
                                      WdspEngine.passbandHighHz)
                    return off > lo && off < hi
                }
                // Tune to an operator-facing CARRIER freq, snapped to a grid:
                //   • "100 Hz" toggle → round to the 100 Hz grid (override).
                //   • "Exact"        → snap to the selected Panafall step
                //                      grid (step 1 Hz = truly exact; 10/50/
                //                      1k… land on that grid).
                // Then offset by the CW pitch so the DDS lands the signal in
                // the filter.
                function tuneCarrier(carrierHz) {
                    var c = carrierHz
                    if (Prefs.panRound100)
                        c = Math.round(c / 100) * 100
                    else if (Prefs.panScrollStepHz > 1)
                        c = Math.round(c / Prefs.panScrollStepHz)
                              * Prefs.panScrollStepHz
                    // Dial = carrier − CW pitch − RIT offset, so the RX DDC
                    // (dial + RIT) still lands the carrier in the filter even
                    // with RIT engaged.
                    var ritOff = Stream.ritEnabled ? Stream.ritOffsetHz : 0
                    Stream.setRx1FreqHz(Math.round(
                        c - WdspEngine.markerOffsetHz - ritOff))
                }

                cursorShape: dbModeAt(mouseX, mouseY) !== "" ? Qt.SizeVerCursor
                             : (tuning && dragged ? Qt.ClosedHandCursor
                                                  : Qt.CrossCursor)

                onPressed: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        rightPress = true
                        rightDragged = false
                        rightX = mouse.x
                        return
                    }
                    dbMode = dbModeAt(mouse.x, mouse.y)
                    if (dbMode !== "") {
                        // Dragging the dB scale takes MANUAL control: if
                        // auto is on, seed the manual range from the
                        // current auto fit and turn auto off, then drag.
                        if (Prefs.dbAuto) {
                            Prefs.dbMin = Math.round(pan.effDbMin)
                            Prefs.dbMax = Math.round(pan.effDbMax)
                            Prefs.dbAuto = false
                        }
                        startY = mouse.y
                        startMin = Prefs.dbMin
                        startMax = Prefs.dbMax
                    } else {
                        tuning = true
                        dragged = false
                        startX = mouse.x
                        startCenter = root.centerHz
                        lastEmitMs = 0
                        // #174 CTUN gesture pick (Thetis console.cs:50318-
                        // 50368): top 15 px = LO/display pan (rx1_spectrum_
                        // tune_drag); body = grab the VFO marker (rx1_click_
                        // tune_drag, relative).  An inside-passband press
                        // suppresses the click-retune in onReleased.  CTUN
                        // off → unchanged spectrum-pan.
                        lastDragX = mouse.x
                        if (Stream.ctuneEnabled) {
                            ctunGesture = (mouse.y < 15) ? "lopan" : "grab"
                            pressInPassband = inPassband(mouse.x)
                        } else {
                            ctunGesture = ""
                            pressInPassband = false
                        }
                    }
                }
                onReleased: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        // Right-click on empty spectrum drops a 200 Hz notch
                        // (default 200 Hz width) at the clicked frequency.
                        if (!rightDragged)
                            WdspEngine.addNotch(
                                freqAtX(rightX) - root.centerHz, 200)
                        rightPress = false
                        rightDragged = false
                        return
                    }
                    if (dbMode === "" && tuning && !dragged) {
                        // plain click → tune the CARRIER to the clicked RF
                        // point (round-to-100 + CW pitch offset handled in
                        // tuneCarrier).  Under CTUN, a click INSIDE the
                        // passband does NOT jump the 0-beat to the cursor
                        // (Thetis m_bCTUNputsZeroOnMouse=false, 50382).
                        if (!(Stream.ctuneEnabled && pressInPassband))
                            tuneCarrier(freqAtX(startX))
                    }
                    dbMode = ""
                    tuning = false
                    dragged = false
                    ctunGesture = ""
                    pressInPassband = false
                }
                onPositionChanged: (mouse) => {
                    if (rightPress) {
                        if (Math.abs(mouse.x - rightX) > dragThreshPx)
                            rightDragged = true
                        return
                    }
                    if (dbMode !== "") {
                        var h = Math.max(1, spectrumArea.height)
                        var span = startMax - startMin
                        var dbDelta = -(mouse.y - startY) * (span / h)
                        var nMin = startMin
                        var nMax = startMax
                        if (dbMode === "max")      nMax = startMax + dbDelta
                        else if (dbMode === "min") nMin = startMin + dbDelta
                        else { nMin = startMin + dbDelta
                               nMax = startMax + dbDelta }
                        // Task #44 Phase 1 — widened ceiling clamp from
                        // 0 → +30 dBFS so the TX-state pair (which
                        // defaults to +20 dBFS ceiling) is draggable
                        // across its natural range while keyed.
                        // RX-state drag is unaffected in practice
                        // (RX traces never reach 0 dBFS so the wider
                        // cap is dormant on RX).
                        nMin = Math.max(-150, Math.min(-3, nMin))
                        nMax = Math.max(nMin + 3, Math.min(30, nMax))
                        Prefs.dbMin = nMin
                        Prefs.dbMax = nMax
                        return
                    }
                    if (!tuning) return
                    var dx = mouse.x - startX
                    if (!dragged) {
                        if (Math.abs(dx) < dragThreshPx) return
                        dragged = true
                    }
                    // throttle ~30 Hz so the render pipeline keeps up
                    var nowMs = Date.now()
                    if (nowMs - lastEmitMs < 33) return
                    lastEmitMs = nowMs
                    if (ctunGesture === "lopan") {
                        // #174 top-strip → pan the LOCKED display centre
                        // (Thetis rx1_spectrum_tune_drag → CentreFrequency
                        // -= delta).  The dial/VFO stays put; the C++ RX-NCO
                        // keeps the demod on the signal as the spectrum slides.
                        var dLo = freqAtX(mouse.x) - freqAtX(lastDragX)
                        lastDragX = mouse.x
                        Stream.setCtuneCenterHz(
                            Math.round(Math.max(0, Stream.ctuneCenterHz - dLo)))
                    } else if (ctunGesture === "grab") {
                        // #174 body → grab the VFO marker; it FOLLOWS the
                        // cursor 1:1 (Thetis rx1_click_tune_drag → VFOA +=
                        // end-start, relative — preserves the grab offset).
                        // Apply the raw Hz delta to the dial: markerOffset/RIT
                        // are constant across the drag so they cancel.
                        var dG = freqAtX(mouse.x) - freqAtX(lastDragX)
                        lastDragX = mouse.x
                        Stream.setRx1FreqHz(
                            Math.round(Math.max(0, Stream.rx1FreqHz + dG)))
                    } else {
                        // CTUN off — spectrum-pan (Thetis rx1_spectrum_drag):
                        // cursor right → spectrum follows finger → centre down.
                        var hzPerPx = WdspEngine.spanHz / Math.max(1, width)
                        var newCenter = startCenter - dx * hzPerPx
                        tuneCarrier(newCenter + WdspEngine.markerOffsetHz)
                    }
                }
                onWheel: (wheel) => {
                    if (wheel.modifiers & Qt.ControlModifier) {
                        // Ctrl + wheel = zoom (escape hatch, old-Lyra gesture)
                        WdspEngine.setZoom(WdspEngine.zoom
                            * (wheel.angleDelta.y > 0 ? 1.25 : 0.8))
                    } else {
                        // Wheel = tune by the Panafall step (Display panel).
                        var dir = wheel.angleDelta.y > 0 ? 1 : -1
                        var carrier = Stream.rx1FreqHz + WdspEngine.markerOffsetHz
                                      + (Stream.ritEnabled ? Stream.ritOffsetHz : 0)
                        tuneCarrier(carrier + dir * Prefs.panScrollStepHz)
                    }
                    wheel.accepted = true
                }
            }

            // ---- Manual-notch overlay (NF) ----
            // Translucent red bands over each notch.  Right-click empty
            // spectrum drops one (handled in specMouse); here each band
            // can be DRAGGED to move, WHEELED to widen/narrow, and
            // RIGHT-CLICKED to remove.  Notch depth itself is deep
            // (WDSP NBP0: rectangular window, 2048-tap FIR) — see engine.
            Item {
                id: notchOverlay
                anchors.fill: parent
                z: 5
                readonly property real spanHz: Math.max(1, WdspEngine.spanHz)
                function xOf(offHz) {
                    return spectrumArea.width * (0.5 + offHz / spanHz)
                }
                Repeater {
                    model: WdspEngine.notches
                    delegate: Item {
                        id: notchItem
                        required property int index
                        required property var modelData
                        readonly property real cx: notchOverlay.xOf(modelData.offsetHz)
                        readonly property real wPx: Math.max(3,
                            spectrumArea.width * modelData.widthHz
                            / notchOverlay.spanHz)
                        readonly property real hitW: Math.max(wPx, 14)
                        x: cx - hitW / 2
                        y: 0; width: hitW; height: spectrumArea.height
                        // Translucent fill band.  Kept low so it doesn't
                        // wash out the trace — the brightness comes from the
                        // crisp edges below, NOT the fill (a Rectangle's
                        // opacity dims its border too, which is what made the
                        // old single-rect notch read as a mushy brown smear
                        // against the glassy sheen).
                        Rectangle {
                            id: notchFill
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 0; width: notchItem.wPx; height: parent.height
                            color: notchItem.modelData.active
                                   ? "#ff4d4d" : "#ff7a7a"
                            opacity: notchItem.modelData.active ? 0.16 : 0.08
                        }
                        // Crisp full-opacity edges — these are what the eye
                        // catches.  Bright hot-coral so they pop over the
                        // panadapter glow; dimmer/thinner when the notch is
                        // bypassed (inactive).
                        Repeater {
                            model: [0, 1]
                            delegate: Rectangle {
                                required property int modelData
                                x: notchFill.x + (modelData === 0
                                       ? 0 : notchFill.width - width)
                                y: 0
                                width: notchItem.modelData.active ? 2 : 1
                                height: parent.height
                                color: notchItem.modelData.active
                                       ? "#ff5555" : "#b86a6a"
                                opacity: notchItem.modelData.active ? 0.95 : 0.6
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property bool moved: false
                            property real downX: 0
                            property double lastMs: 0
                            onPressed: (m) => { moved = false; downX = m.x }
                            onPositionChanged: (m) => {
                                if (!(pressedButtons
                                      & (Qt.LeftButton | Qt.RightButton))) return
                                if (Math.abs(m.x - downX) > 4) moved = true
                                var nowMs = Date.now()
                                if (nowMs - lastMs < 33) return
                                lastMs = nowMs
                                // local x -> absolute spectrum x -> centre offset
                                var gx = notchItem.x + m.x
                                var offHz = (gx / Math.max(1, spectrumArea.width)
                                             - 0.5) * notchOverlay.spanHz
                                WdspEngine.moveNotch(notchItem.index, offHz)
                            }
                            onReleased: (m) => {
                                if (!moved && m.button === Qt.RightButton)
                                    WdspEngine.removeNotch(notchItem.index)
                            }
                            // Wheel: down = wider, up = narrower (old-Lyra gesture).
                            onWheel: (w) => {
                                var f = w.angleDelta.y > 0 ? 0.8 : 1.25
                                WdspEngine.setNotchWidth(
                                    notchItem.index, notchItem.modelData.widthHz * f)
                                w.accepted = true
                            }
                        }
                    }
                }
            }

            // ---- RX filter passband overlay (drag an edge to resize) ----
            // Translucent box over the tuned signal showing the current
            // RX filter; the edges are draggable to widen/narrow the
            // bandwidth, which updates Prefs.rxBandwidth (→ engine filter
            // + the Mode+Filter RX BW readout, persisted per mode).
            Item {
                id: passband
                anchors.fill: parent
                z: 4
                readonly property real spanHz: Math.max(1, WdspEngine.spanHz)
                function xOf(offHz) {
                    // #174 under CTUN the passband rides the dial marker
                    // (offset from the frozen centre), not the centre.
                    return spectrumArea.width
                           * (0.5 + (offHz + root.ctunDialOffHz) / spanHz)
                }
                readonly property real loX: xOf(WdspEngine.passbandLowHz)
                readonly property real hiX: xOf(WdspEngine.passbandHighHz)

                Rectangle {            // translucent fill
                    x: Math.min(passband.loX, passband.hiX)
                    width: Math.abs(passband.hiX - passband.loX)
                    y: 0; height: spectrumArea.height
                    color: "#3aa0ff"
                    opacity: 0.13
                }
                Repeater {             // two draggable edges
                    model: 2
                    delegate: Item {
                        required property int index
                        readonly property bool isLo: index === 0
                        x: (isLo ? passband.loX : passband.hiX) - width / 2
                        y: 0; width: 9; height: spectrumArea.height
                        Rectangle {    // visible edge line
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 0; width: 2; height: parent.height
                            color: "#8fd0ff"; opacity: 0.75
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property double lastMs: 0
                            // Task #54 + #80 (sideband symmetry fix) —
                            // distinguish lo-edge vs hi-edge drag, then
                            // map to filterLow vs rxBandwidth by mode.
                            //
                            // The Repeater spawns two edges by ARRAY
                            // INDEX (isLo = index 0); the SEMANTIC
                            // mapping flips by sideband:
                            //   USB/DIGU: passbandLowHz=+flo near
                            //       carrier, passbandHighHz=+bw far
                            //       out.  Lo edge IS the inner /
                            //       filterLow knob; hi edge IS the
                            //       outer / rxBandwidth knob.
                            //   LSB/DIGL: passbandLowHz=-bw far out,
                            //       passbandHighHz=-flo near carrier.
                            //       MIRRORED — lo edge is now the
                            //       OUTER (rxBandwidth), hi edge is
                            //       the INNER (filterLow).  Pre-fix
                            //       routed by isLo alone → in LSB the
                            //       outer drag wrote filterLow which
                            //       clamped to 500 Hz, collapsing the
                            //       inner edge instead of extending
                            //       the outer (#80 operator-reported).
                            //
                            // For symmetric modes (AM/DSB/FM) and CW
                            // (pitch-centred), both edges write
                            // rxBandwidth — the asymmetric SSB/DIG
                            // case is the only one where filterLow
                            // is independently meaningful.
                            onPositionChanged: (m) => {
                                var nowMs = Date.now()
                                if (nowMs - lastMs < 33) return
                                lastMs = nowMs
                                var px = mapToItem(spectrumArea, m.x, m.y).x
                                // #174 the passband rides the dial under CTUN
                                // → subtract that offset to recover the true
                                // filter offset before writing the BW knobs.
                                var off = (px / Math.max(1, spectrumArea.width)
                                           - 0.5) * passband.spanHz
                                          - root.ctunDialOffHz
                                var mode = Prefs.mode
                                var isSsbOrDig = (mode === "USB" || mode === "LSB"
                                               || mode === "DIGU" || mode === "DIGL")
                                if (isSsbOrDig) {
                                    var isLsb = (mode === "LSB" || mode === "DIGL")
                                    // Lower-sideband mirrors the
                                    // inner/outer role of the two
                                    // edges (see comment above).
                                    var isInnerEdge = isLsb ? !isLo : isLo
                                    if (isInnerEdge) {
                                        // |off| at the inner edge IS
                                        // the low cutoff for SSB/DIG.
                                        // Clamp to Prefs's 0..500
                                        // range; drag-below-0 just
                                        // pins at 0.
                                        var flo = Math.abs(off)
                                        Prefs.filterLow =
                                            Math.max(0, Math.min(500, Math.round(flo)))
                                    } else {
                                        // Outer edge → bandwidth.
                                        Prefs.rxBandwidth =
                                            WdspEngine.bandwidthForEdge(off)
                                    }
                                } else {
                                    Prefs.rxBandwidth =
                                        WdspEngine.bandwidthForEdge(off)
                                }
                            }
                        }
                    }
                }
            }

            // ---- Band-plan overlay (top strip: segments / edges /
            // landmarks) ----  Advisory amateur band plan from BandPlan
            // (region + data) gated by the Prefs layer toggles.  Maps a
            // frequency to x the same way the freq scale + markers do.
            Item {
                id: bandPlan
                anchors.fill: parent
                z: 6
                readonly property real spanHz: Math.max(1, WdspEngine.spanHz)
                function xOf(hz) {
                    return spectrumArea.width
                           * (0.5 + (hz - root.centerHz) / bandPlan.spanHz)
                }
                // All four bindings reference reactive deps (BandPlan.region
                // has a NOTIFY; the Prefs toggles + centerHz + spanHz are
                // QML props) so the overlay re-queries C++ on any change.
                readonly property var segs:
                    (Prefs.bandPlanSegments && BandPlan.region !== "NONE")
                    ? BandPlan.segments(root.centerHz, bandPlan.spanHz) : []
                readonly property var edges:
                    (Prefs.bandPlanEdges && BandPlan.region !== "NONE")
                    ? BandPlan.edges(root.centerHz, bandPlan.spanHz) : []
                // US license-class phone-edge markers.  Gated on its own
                // opt-in toggle AND region == "US" (classEdges() also returns
                // [] for any other region, so this is belt-and-braces).
                readonly property var classEdges:
                    (Prefs.bandPlanClassEdges && BandPlan.region === "US")
                    ? BandPlan.classEdges(root.centerHz, bandPlan.spanHz) : []
                readonly property var marks:
                    (BandPlan.region !== "NONE"
                     && (Prefs.bandPlanLandmarks || Prefs.bandPlanBeacons))
                    ? BandPlan.landmarks(root.centerHz, bandPlan.spanHz,
                                         Prefs.bandPlanLandmarks,
                                         Prefs.bandPlanBeacons) : []
                // TX out-of-band advisory (emission-aware).  Reactive: the
                // binding touches Stream.txDisplayActive + the TX carrier
                // (VFO B when split) + mode/filter, so it re-evaluates LIVE —
                // if the operator wheels the dial past the edge WHILE keyed,
                // the banner appears immediately, not just at the next
                // key-down.  Returns { out: bool, text: string }.  Advisory
                // only; nothing here inhibits TX.
                readonly property var txWarn: {
                    if (!Stream.txDisplayActive || !Prefs.bandPlanTxWarn
                        || BandPlan.region === "NONE")
                        return { out: false, text: "" }
                    var carrier = Stream.splitEnabled ? Stream.vfoBHz
                                                      : Stream.rx1FreqHz
                    // If the operator has enabled the 11m/CB band and is
                    // tuned there, this is a deliberate non-amateur band —
                    // don't nag "outside the amateur bands".
                    if (Prefs.cbBandEnabled && BandPlan.inCbBand(carrier))
                        return { out: false, text: "" }
                    var mode = (Prefs.mode || "").toUpperCase()
                    var hi = Prefs.txBandwidth       // TX high edge (Hz)
                    var lo = Prefs.filterLow         // TX low edge (Hz)
                    var minF, maxF
                    if (mode === "CWU" || mode === "CWL" || mode === "CW") {
                        minF = carrier - 150; maxF = carrier + 150
                    } else if (mode === "USB" || mode === "DIGU") {
                        minF = carrier + lo; maxF = carrier + hi
                    } else if (mode === "LSB" || mode === "DIGL") {
                        minF = carrier - hi; maxF = carrier - lo
                    } else if (mode === "FM") {
                        var half = Stream.fmDeviationHz + 3000
                        minF = carrier - half; maxF = carrier + half
                    } else {   // AM / DSB / SAM — symmetric ±hi
                        minF = carrier - hi; maxF = carrier + hi
                    }
                    var bMin = BandPlan.bandContaining(minF)
                    var bMax = BandPlan.bandContaining(maxF)
                    var reg = BandPlan.region
                    var bCar = BandPlan.bandContaining(carrier)
                    if (bMin !== "" && bMax !== "") {
                        // Emission fits within the band span — but a
                        // CHANNELIZED band (US/Canada 60m) also requires being
                        // ON a channel AND within its 2.8 kHz width.
                        var cs = BandPlan.channelStatus(carrier, minF, maxF)
                        if (cs === "offchannel")
                            return { out: true,
                                     text: "⚠  OFF CHANNEL\n"
                                           + (carrier / 1e6).toFixed(4) + " MHz — "
                                           + bCar + " is channel-only; tune to a "
                                           + "channel" }
                        if (cs === "toowide")
                            return { out: true,
                                     text: "⚠  TX TOO WIDE FOR CHANNEL\nyour "
                                           + mode + " signal is wider than the "
                                           + bCar + " channel (2.8 kHz max)" }
                        return { out: false, text: "" }
                    }
                    if (bCar === "")
                        return { out: true,
                                 text: "⚠  TRANSMITTING OUT OF BAND\n"
                                       + (carrier / 1e6).toFixed(3)
                                       + " MHz — outside the " + reg
                                       + " amateur bands" }
                    var edge = (bMin === "") ? minF : maxF
                    return { out: true,
                             text: "⚠  TX BANDWIDTH OVER THE EDGE\nyour "
                                   + mode + " signal reaches "
                                   + (edge / 1e6).toFixed(3)
                                   + " MHz — past the " + bCar + " edge" }
                }

                // CB channel markers — only when the 11m/CB band is enabled.
                readonly property var cbChans:
                    Prefs.cbBandEnabled
                    ? BandPlan.cbChannels(root.centerHz, bandPlan.spanHz) : []

                // EiBi shortwave broadcasters active in this span.  Eibi
                // gates internally (returns [] when the overlay is off, no
                // DB is loaded, or we're inside a ham band).  eibiRev is
                // bumped on a download/settings change so the binding
                // re-queries C++ even though those aren't QML properties.
                property int eibiRev: 0
                readonly property var eibiList:
                    (bandPlan.eibiRev,
                     Eibi.entriesInSpan(root.centerHz, bandPlan.spanHz))
                Connections {
                    target: Eibi
                    function onChanged()         { bandPlan.eibiRev++ }
                    function onSettingsChanged()  { bandPlan.eibiRev++ }
                }

                // DX-cluster spots pushed over TCI (SDRLogger+ / cluster).
                property int spotRev: 0
                readonly property var spotsList:
                    (bandPlan.spotRev,
                     Spots.spotsInSpan(root.centerHz, bandPlan.spanHz))
                // #182 — colour legend entries (empty unless legend enabled).
                readonly property var legendList:
                    (bandPlan.spotRev, Spots.legendEntries())
                Connections {
                    target: Spots
                    function onChanged() { bandPlan.spotRev++ }
                }

                // Sub-band segment bars (top 10 px), coloured by mode.
                Repeater {
                    model: bandPlan.segs
                    delegate: Item {
                        id: segItem
                        required property var modelData
                        readonly property real x0: bandPlan.xOf(modelData.lo)
                        readonly property real x1: bandPlan.xOf(modelData.hi)
                        // Channelized sub-bands (60m CH1..CH5) get a bigger,
                        // always-visible, click-to-QSY label.
                        readonly property bool chan: modelData.channel === true
                        Rectangle {
                            x: Math.min(segItem.x0, segItem.x1)
                            y: 0
                            width: Math.max(0, Math.abs(segItem.x1 - segItem.x0))
                            height: 10
                            color: segItem.modelData.color
                            opacity: 0.82
                        }
                        Text {
                            id: segLabel
                            // Channels always show (they're narrow); other
                            // sub-bands only when wide enough to read.
                            visible: segItem.chan
                                     || Math.abs(segItem.x1 - segItem.x0) >= 24
                            x: Math.min(segItem.x0, segItem.x1) + 3
                            y: 0
                            text: segItem.modelData.label
                            color: "#f0f0f0"
                            font.pixelSize: segItem.chan ? 11 : 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#a0000000"
                        }
                        // 60m channel click-to-tune (mirrors the watering-hole
                        // FT8/FT4/PSK markers): QSY to the channel centre + USB.
                        MouseArea {
                            visible: segItem.chan
                            enabled: segItem.chan
                            x: Math.min(segItem.x0, segItem.x1)
                            y: 0
                            width: Math.max(Math.abs(segItem.x1 - segItem.x0),
                                            segLabel.width + 6)
                            height: 12
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Prefs.mode = "USB"
                                Stream.setRx1FreqHz(
                                    Math.round(segItem.modelData.qsy))
                                Status.show(qsTr("Tuned to ")
                                    + (segItem.modelData.qsy / 1.0e6).toFixed(3)
                                    + " MHz USB · " + segItem.modelData.label,
                                    2500)
                            }
                            ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                            ToolTip.text: segItem.modelData.label + " — "
                                + (segItem.modelData.qsy / 1.0e6).toFixed(3)
                                + " MHz USB"
                        }
                    }
                }

                // Band-edge warning lines — full-height red dashed + label.
                Repeater {
                    model: bandPlan.edges
                    delegate: Item {
                        id: edgeItem
                        required property var modelData
                        readonly property real ex: bandPlan.xOf(modelData.freq)
                        Repeater {           // dashed: 5 px dash + 5 px gap
                            model: Math.max(1, Math.ceil(spectrumArea.height / 10))
                            delegate: Rectangle {
                                required property int index
                                x: edgeItem.ex - 1
                                y: index * 10
                                width: 2; height: 5
                                color: "#ff5050"
                                opacity: 0.85
                            }
                        }
                        Text {
                            x: Math.max(0, Math.min(spectrumArea.width - width,
                                                    edgeItem.ex + 2))
                            y: spectrumArea.height - height - 16
                            text: edgeItem.modelData.name + qsTr(" EDGE")
                            color: "#ff8080"
                            font.pixelSize: 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#cc000000"
                        }
                    }
                }

                // US license-class phone edges — amber dashed lines with a
                // class label, deliberately distinct from the red band-EDGE
                // lines.  Opt-in, US-only; a passive reference (no tune).
                Repeater {
                    model: bandPlan.classEdges
                    delegate: Item {
                        id: classEdgeItem
                        required property var modelData
                        readonly property real cx: bandPlan.xOf(modelData.freq)
                        Repeater {           // dashed: 4 px dash + 6 px gap
                            model: Math.max(1, Math.ceil(spectrumArea.height / 10))
                            delegate: Rectangle {
                                required property int index
                                x: classEdgeItem.cx - 0.5
                                y: index * 10
                                width: 1; height: 4
                                color: "#ffb000"
                                opacity: 0.7
                            }
                        }
                        Text {
                            x: Math.max(0, Math.min(spectrumArea.width - width,
                                                    classEdgeItem.cx + 2))
                            y: spectrumArea.height - height - 30
                            text: classEdgeItem.modelData.label
                            color: "#ffc65a"
                            font.pixelSize: 9
                            font.bold: true
                            style: Text.Outline
                            styleColor: "#cc000000"
                        }
                    }
                }

                // Watering-hole landmarks — ▼ marker + label + click-to-tune.
                Repeater {
                    model: bandPlan.marks
                    delegate: Item {
                        id: markItem
                        required property var modelData
                        readonly property real mx: bandPlan.xOf(modelData.freq)
                        readonly property color col:
                            modelData.beacon ? "#78dcff" : "#ffd700"
                        Rectangle {          // short tick below the strip
                            x: markItem.mx - 0.5; y: 10; width: 1; height: 9
                            color: markItem.col; opacity: 0.5
                        }
                        Text {
                            x: markItem.mx - width / 2; y: 8
                            text: "▼"; color: markItem.col; font.pixelSize: 10
                        }
                        Text {
                            x: markItem.mx + 6; y: 10
                            text: markItem.modelData.label
                            color: markItem.col
                            font.pixelSize: 9; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: markItem.mx - 9; y: 6; width: 18; height: 18
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Prefs.mode = markItem.modelData.mode
                                Stream.setRx1FreqHz(Math.round(markItem.modelData.freq))
                                Status.show(qsTr("Tuned to ")
                                    + (markItem.modelData.freq / 1.0e6).toFixed(3)
                                    + " MHz " + markItem.modelData.mode
                                    + " · " + markItem.modelData.label, 2500)
                            }
                            ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                            // NCDXF beacons show the LIVE rotating station
                            // (computed on hover); others show label/mode.
                            ToolTip.text: {
                                var f = (markItem.modelData.freq / 1.0e6).toFixed(3)
                                if (markItem.modelData.beacon) {
                                    var who = BandPlan.ncdxfStation(markItem.modelData.freq)
                                    return qsTr("NCDXF ") + f + qsTr(" MHz")
                                           + (who !== "" ? qsTr("\nNow: ") + who : "")
                                           + qsTr("\nClick to QSY (CWU)")
                                }
                                return markItem.modelData.label + " · "
                                       + markItem.modelData.mode + "  " + f
                                       + qsTr(" MHz\nClick to tune")
                            }
                        }
                    }
                }

                // CB channel ticks + numbers (Ch1…Ch40) — click to QSY (AM).
                Repeater {
                    model: bandPlan.cbChans
                    delegate: Item {
                        id: cbItem
                        required property var modelData
                        readonly property real cx: bandPlan.xOf(modelData.freq)
                        Rectangle {
                            x: cbItem.cx - 0.5; y: 14; width: 1; height: 9
                            color: "#b0c4d4"; opacity: 0.6
                        }
                        Text {
                            x: cbItem.cx - width / 2; y: 9
                            text: cbItem.modelData.label
                            color: "#b0c4d4"
                            font.pixelSize: 11; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        // The fun nicknames (Super Bowl / Emergency / Calling
                        // / Highway / SSB…) shown ON the panadapter for the
                        // tagged channels — visible regardless of the tooltip
                        // setting, in a warm gold to stand out from the Ch#.
                        Text {
                            visible: cbItem.modelData.note !== ""
                            x: cbItem.cx - width / 2; y: 22
                            text: cbItem.modelData.note
                            color: "#ffcf6b"
                            font.pixelSize: 9; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: cbItem.cx - 11; y: 8; width: 22; height: 20
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Prefs.mode = "AM"
                                Stream.setRx1FreqHz(Math.round(cbItem.modelData.freq))
                                Status.show(qsTr("Tuned to CB ") + cbItem.modelData.label
                                    + " — " + (cbItem.modelData.freq / 1.0e6).toFixed(4)
                                    + " MHz AM", 2000)
                            }
                            ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                            ToolTip.text: "CB " + cbItem.modelData.label + "  "
                                + (cbItem.modelData.freq / 1.0e6).toFixed(3)
                                + qsTr(" MHz AM")
                                + (cbItem.modelData.note !== ""
                                   ? "\n" + cbItem.modelData.note : "")
                                + qsTr("\nClick to tune")
                        }
                    }
                }

                // EiBi shortwave broadcasters — station tick + name, cyan
                // when on-air now / grey when scheduled-but-off; staggered
                // over 3 rows to reduce label overlap.  Click tunes in AM.
                Repeater {
                    model: bandPlan.eibiList
                    delegate: Item {
                        id: ebItem
                        required property var modelData
                        required property int index
                        readonly property real ex: bandPlan.xOf(modelData.freqHz)
                        readonly property color col:
                            modelData.onAir ? "#50c8dc" : "#7a8a94"
                        readonly property real labelY: 25 + (index % 3) * 13
                        Rectangle {
                            x: ebItem.ex - 0.5; y: 20
                            width: 1; height: ebItem.labelY - 20
                            color: ebItem.col; opacity: 0.55
                        }
                        Text {
                            x: ebItem.ex + 3; y: ebItem.labelY
                            text: ebItem.modelData.station.length > 22
                                  ? ebItem.modelData.station.substring(0, 22)
                                  : ebItem.modelData.station
                            color: ebItem.col
                            font.pixelSize: 10; font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: ebItem.ex - 6; y: 20
                            width: 52; height: ebItem.labelY - 20 + 14
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Prefs.mode = "AM"
                                Stream.setRx1FreqHz(Math.round(ebItem.modelData.freqHz))
                                Status.show(qsTr("Tuned to ")
                                    + (ebItem.modelData.freqHz / 1.0e6).toFixed(3)
                                    + " MHz AM · " + ebItem.modelData.station, 2500)
                            }
                            ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                            ToolTip.text: ebItem.modelData.station + "  "
                                + (ebItem.modelData.freqHz / 1.0e6).toFixed(3)
                                + qsTr(" MHz")
                                + (ebItem.modelData.onAir ? qsTr("  (ON AIR)")
                                                          : qsTr("  (off air)"))
                                + (ebItem.modelData.language !== ""
                                   ? qsTr("\nLanguage: ") + ebItem.modelData.language : "")
                                + (ebItem.modelData.target !== ""
                                   ? qsTr("\nTarget: ") + ebItem.modelData.target : "")
                                + qsTr("\nClick to tune (AM)")
                        }
                    }
                }

                // DX-cluster spots — colored callsign markers (color set by
                // the spotting client); click to tune + echo spot_activated.
                Repeater {
                    model: bandPlan.spotsList
                    delegate: Item {
                        id: spItem
                        required property var modelData
                        required property int index
                        readonly property real sx: bandPlan.xOf(modelData.freqHz)
                        // 3-row stagger; the C++ list is freq-sorted, so
                        // callsigns near the same frequency land on
                        // different rows instead of stacking on top.
                        readonly property real labelY: 40 + (index % 3) * 13
                        Rectangle {
                            // #172 — a freshly-arrived spot wears the operator's
                            // new-spot colour for a few seconds (own-call highlight
                            // still wins via `mine`).
                            x: spItem.sx - (spItem.modelData.mine ? 1 : 0.5); y: 38
                            width: spItem.modelData.mine ? 2 : 1; height: 41
                            color: spItem.modelData.flash
                                   ? spItem.modelData.flashColor
                                   : spItem.modelData.color
                            opacity: (spItem.modelData.mine
                                      || spItem.modelData.flash) ? 1.0 : 0.8
                        }
                        Text {
                            x: spItem.sx + 3; y: spItem.labelY
                            // "★" marks own call; "+K" = K more spots collapsed
                            // onto this frequency bucket (clutter cap).
                            text: (spItem.modelData.mine ? "★ " : "")
                                  + spItem.modelData.label
                                  + (spItem.modelData.more > 0
                                     ? "  +" + spItem.modelData.more : "")
                            color: spItem.modelData.flash
                                   ? spItem.modelData.flashColor
                                   : spItem.modelData.color
                            font.pixelSize: spItem.modelData.mine ? 13 : 11
                            font.bold: true
                            style: Text.Outline; styleColor: "#cc000000"
                        }
                        MouseArea {
                            x: spItem.sx - 6; y: spItem.labelY
                            width: 72; height: 14
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Spots.activate(spItem.modelData.call) // legend below
                                // #182 — grab the call into the CW-console
                                // contact row ({CALL} token / F-key macros).
                                CwMacros.hisCall =
                                    spItem.modelData.call.toUpperCase()
                                Status.show(qsTr("Spot: ") + spItem.modelData.call
                                    + " — " + (spItem.modelData.freqHz / 1.0e6).toFixed(3)
                                    + " MHz " + spItem.modelData.mode, 2500)
                            }
                            ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                            ToolTip.text: spItem.modelData.call + "  "
                                + (spItem.modelData.freqHz / 1.0e6).toFixed(3)
                                + qsTr(" MHz ") + spItem.modelData.mode
                                + (spItem.modelData.text !== ""
                                   ? "\n" + spItem.modelData.text : "")
                                + qsTr("\nClick to tune + grab call")
                        }
                    }
                }

                // #182 — colour legend (top-right) for by-mode / by-region.
                Rectangle {
                    visible: bandPlan.legendList.length > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 6
                    width: legendCol.width + 12
                    height: legendCol.height + 8
                    radius: 4
                    color: "#cc0a0f14"
                    border.color: "#33ffffff"
                    Column {
                        id: legendCol
                        anchors.centerIn: parent
                        spacing: 2
                        Repeater {
                            model: bandPlan.legendList
                            delegate: Row {
                                required property var modelData
                                spacing: 4
                                Rectangle {
                                    width: 9; height: 9; radius: 2
                                    color: modelData.color
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: modelData.label
                                    color: "#dde6ee"; font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                // TX out-of-band warning banner — big, centred, and pulsing
                // so it's impossible to miss (operator eyesight).  Driven by
                // the reactive txWarn binding, so it appears and updates LIVE
                // as the dial or bandwidth changes mid-transmit, and clears
                // when the signal is back in band or on un-key.
                Rectangle {
                    id: txWarnBanner
                    visible: bandPlan.txWarn.out
                    z: 100
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    width: txWarnLabel.implicitWidth + 32
                    height: txWarnLabel.implicitHeight + 18
                    radius: 6
                    color: "#e0300000"
                    border.color: "#ff4136"
                    border.width: 2
                    SequentialAnimation on opacity {
                        running: txWarnBanner.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.5; duration: 480 }
                        NumberAnimation { from: 0.5; to: 1.0; duration: 480 }
                    }
                    Text {
                        id: txWarnLabel
                        anchors.centerIn: parent
                        text: bandPlan.txWarn.text
                        color: "#ffe0e0"
                        font.pixelSize: 16
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // ===== Waterfall (rolling spectrogram) =====
        Waterfall {
            id: wf
            // Collapse toggle (old-Lyra triangle): when collapsed the
            // pane is hidden and the SplitView gives the panadapter the
            // full height.  Persisted via Prefs.
            visible: !Prefs.waterfallCollapsed
            // Initial/factory height is set imperatively by
            // applyFromPrefs() (a live `root.height * 0.4` binding would
            // fight restoreState on every window resize).
            SplitView.minimumHeight: 60
            engine: WdspEngine
            // Independent waterfall dB range (Settings → Visuals) — NOT
            // the panadapter's dbMin/dbMax, so contrast tunes separately.
            // When autoScale is on, dbMin/dbMax are ignored (the item
            // auto-fits internally).
            autoScale: Prefs.waterfallDbAuto
            dbMin: Prefs.waterfallDbMin
            dbMax: Prefs.waterfallDbMax
            targetFps: Prefs.targetFps
            // The waterfall has its OWN palette + custom colour + speed,
            // independent of the panadapter's by-strength trace palette.
            palette: Prefs.waterfallPalette
            strengthColor: Prefs.waterfallColor
            speed: Prefs.waterfallSpeed

            // Click-to-tune in the waterfall.  It shares the spectrum's exact
            // frequency extent and width, so reuse the spectrum mouse's
            // freqAtX / tuneCarrier helpers: a plain left click tunes RX1 to
            // that point, and the wheel steps by the Panafall step (Ctrl+wheel
            // zooms) — same feel as the spectrum.  A real drag is ignored so a
            // stray wiggle can't retune.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.CrossCursor
                property bool dragged: false
                property real downX: 0
                onPressed: (m) => { dragged = false; downX = m.x }
                onPositionChanged: (m) => {
                    if (Math.abs(m.x - downX) > specMouse.dragThreshPx)
                        dragged = true
                }
                onReleased: (m) => {
                    if (dragged) return
                    // Under CTUN, a click inside the passband must not jump the
                    // 0-beat to the cursor (matches the spectrum).
                    if (!(Stream.ctuneEnabled && specMouse.inPassband(m.x)))
                        specMouse.tuneCarrier(specMouse.freqAtX(m.x))
                }
                onWheel: (wheel) => {
                    if (wheel.modifiers & Qt.ControlModifier) {
                        WdspEngine.setZoom(WdspEngine.zoom
                            * (wheel.angleDelta.y > 0 ? 1.25 : 0.8))
                    } else {
                        var dir = wheel.angleDelta.y > 0 ? 1 : -1
                        var carrier = Stream.rx1FreqHz + WdspEngine.markerOffsetHz
                                      + (Stream.ritEnabled ? Stream.ritOffsetHz : 0)
                        specMouse.tuneCarrier(carrier + dir * Prefs.panScrollStepHz)
                    }
                    wheel.accepted = true
                }
            }
        }
    }
}
