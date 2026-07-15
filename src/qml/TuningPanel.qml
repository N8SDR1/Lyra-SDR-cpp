// Lyra — Tuning dock panel (RX1 / DDC0), VFO-cluster layout.
//
// Operator-curated layout (2026-06-20): each VFO is a self-contained
// CLUSTER — a big LED-style readout (FreqDisplay) you tune by clicking a
// digit + wheeling, with its Step + Mode combos directly UNDERNEATH it,
// and the Lyra logo centred between VFO A and the reserved VFO-B slot.
// Mode moved here from the (now "Filters") dock; the Prefs.mode →
// WdspEngine.mode binding came with it.  A controls row under the VFOs
// holds CW Pitch now and reserves space for SUB / 1→2 / 2→1 / ⇄ / RIT /
// XIT / CTCSS / repeater-offset — each rendered functional WHEN its
// feature lands (SPLIT, RIT/XIT, repeaters), never as a dead widget.
//
// VFO indication model:
//   • amber RX/TX text tag, upper-left of the digits = ROLE (which VFO
//     listens, which transmits) — amber lives ONLY in the tag, never
//     blended into the border (an amber→red border blend read pinkish).
//   • border = LIVE state: green = receiving, red = transmitting now
//     (Stream.moxActive, the wire-MOX truth), neutral gray = an armed-
//     but-not-keyed TX VFO (only meaningful once SPLIT lands).
//   • simplex (one VFO): the tag flips RX→TX and the border green→red
//     together on key.
//
// CW carrier convention (standard HF SDR practice): the LED shows the
// signal CARRIER (VFO); the hardware DDS is offset by ±pitch so the
// carrier lands in the pitch-centred filter.  VFO = DDS +
// WdspEngine.markerOffsetHz, so we display centerHz+offset and write
// (vfo − offset) to the wire.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LyraUI

Rectangle {
    id: root
    implicitHeight: 200
    implicitWidth: 680
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor, measured from the content — see AudioPanel for the rationale.
    readonly property int lyraMinWidth:  body.implicitWidth + 24
    readonly property int lyraMinHeight: body.implicitHeight + 16

    // Local mirror of the wire (DDS) freq — signal-driven (a direct
    // binding to Stream.rx1FreqHz is unreliable in this QQuickWidget).
    property int centerHz: 0
    // VFO B (split TX VFO) mirror — vfoBHz IS the carrier (the TX-NCO
    // offset is applied at push time), so display it directly.
    property int vfoBHz: 0
    Component.onCompleted: {
        centerHz = Stream.rx1FreqHz
        vfoBHz = Stream.vfoBHz
        // Seed the RPT dir/offset combo from the restored split (so it
        // reflects a persisted repeater), else the current band's default.
        if (Stream.splitEnabled) syncRptFromState(); else applyBandDefault()
    }
    Connections {
        target: Stream
        function onRx1FreqChanged() {
            root.centerHz = Stream.rx1FreqHz
            // Duplex: while RPT is active (FM split), VFO B tracks A ± offset
            // as you tune across repeater outputs.
            if (root.fmMode && Stream.splitEnabled) root.rptApply()
        }
        function onVfoBHzChanged() {
            root.vfoBHz = Stream.vfoBHz
            // Keep the RPT dir/offset combo in step with the live VFO B, so a
            // memory-recalled (or manually-set) repeater shows its real offset
            // AND the duplex tracker uses THAT offset — not a stale combo value
            // that would otherwise clobber the recalled split on the next tune.
            root.syncRptFromState()
        }
        // Recall / manual SPLIT toggle: re-derive the RPT offset from state.
        function onSplitEnabledChanged() { root.syncRptFromState() }
    }

    // Mode picker moved here from the Filters dock — keep the engine in
    // sync with the operator's mode choice (was in ModeFilterPanel).
    Binding { target: WdspEngine; property: "mode"; value: Prefs.mode }

    // Indication palette (matches the locked VFO indication model).
    readonly property color cRx:   "#34c759"   // receiving (green border)
    readonly property color cTx:   "#ff4136"   // transmitting now (red border)
    readonly property color cArm:  "#3a4250"   // armed TX, not keyed (gray)
    readonly property color cRole: "#efb340"   // amber RX/TX role tag

    readonly property var modeList: ["LSB", "USB", "CWL", "CWU",
                                     "DSB", "AM", "SAM", "FM", "DIGU", "DIGL"]
    readonly property bool cwMode: Prefs.mode === "CWU" || Prefs.mode === "CWL"
    readonly property bool fmMode: Prefs.mode === "FM"

    // RPT (FM repeater) — ad-hoc duplex layered on SPLIT + CTCSS, front-
    // facing in FM only.  Dir + offset are UI state; applying writes the
    // engine (setVfoBHz + setSplitEnabled), so it's the same PS-safe path
    // as manual split / a recalled repeater memory.  In FM, split ⟺ RPT
    // (the raw SPLIT button hides; RPT is the FM-friendly way to split).
    property int rptDirSign: -1                    // − (input below output)
    readonly property var rptOffsetsKHz: [100, 500, 600, 1000, 5000]
    // Standard CTCSS tones (Hz) for the front combo; the engine snaps to
    // its canonical table on setCtcssToneHz.
    readonly property var ctcssTones: [
        67.0, 69.3, 71.9, 74.4, 77.0, 79.7, 82.5, 85.4, 88.5, 91.5, 94.8,
        97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3, 131.8,
        136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9, 171.3,
        173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5, 203.5,
        206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1]
    // VFO B = VFO A ± offset.  Called on RPT-on, dir/offset change, and (as
    // a duplex) every VFO-A retune while RPT is active.
    function rptApply() {
        Stream.setVfoBHz(Stream.rx1FreqHz
            + root.rptDirSign * root.rptOffsetsKHz[rptOffsetCombo.currentIndex] * 1000)
    }
    // Per-band standard repeater offset: 6 m → 1 MHz, else (10 m + generic)
    // → 100 kHz, shift down (−).  Applied when RPT is freshly engaged.
    function bandDefaultOffsetIdx(hz) {
        return (hz >= 50000000 && hz < 54000000) ? 3 : 0
    }
    function applyBandDefault() {
        root.rptDirSign = -1
        rptOffsetCombo.currentIndex = root.bandDefaultOffsetIdx(Stream.rx1FreqHz)
    }
    // Reflect the LIVE split offset (restored at launch / memory-recalled /
    // manual VFO-B) in the RPT dir + offset combo; simplex → band default.
    function syncRptFromState() {
        var off = Stream.vfoBHz - Stream.rx1FreqHz
        if (off === 0) { root.applyBandDefault(); return }
        root.rptDirSign = off < 0 ? -1 : 1
        var magK = Math.abs(off) / 1000, best = 0
        for (var i = 1; i < root.rptOffsetsKHz.length; ++i)
            if (Math.abs(root.rptOffsetsKHz[i] - magK)
                    < Math.abs(root.rptOffsetsKHz[best] - magK)) best = i
        rptOffsetCombo.currentIndex = best
    }
    function nearestCtcssIndex(hz) {
        var best = 0
        for (var i = 1; i < root.ctcssTones.length; ++i)
            if (Math.abs(root.ctcssTones[i] - hz)
                    < Math.abs(root.ctcssTones[best] - hz)) best = i
        return best
    }

    // FM RX channel-width presets (Hz), matching the Filters panel FM list.
    // Picking a Deviation auto-sizes the RX filter to the smallest preset
    // that still passes the Carson occupied width 2·(dev + 3 kHz audio), so
    // ±2.5 k → 12 k and ±5 k → 16 k.  Operator-overridable afterward (the
    // Filters panel still lets you pick any preset).
    readonly property var fmRxPresets: [8000, 10000, 12000, 16000]
    function fmRxBwForDev(devHz) {
        var carson = 2 * (devHz + 3000)
        for (var i = 0; i < root.fmRxPresets.length; ++i)
            if (root.fmRxPresets[i] >= carson) return root.fmRxPresets[i]
        return root.fmRxPresets[root.fmRxPresets.length - 1]   // cap at widest
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 6
        anchors.bottomMargin: 10
        spacing: 14

        // ── VFO row: [VFO-A cluster + its Step/Mode]  [big logo]  [VFO-B] ──
        // Per-VFO Step+Mode sit DIRECTLY under each VFO (old-Lyra idiom);
        // the logo is large and centred between A and B's reserved slot.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // VFO-A cluster — one bordered box wrapping the freq AND its
            // Step+Mode (mockup/old-Lyra idiom).  Fixed snug width so the
            // border outlines the cluster, not a giant block.
            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.maximumWidth: 360
                // A real floor, so the readout can't be crushed to nothing when
                // the row is squeezed — 300px still seats the LED digits and the
                // Step/Mode combos beneath them.
                Layout.minimumWidth: 300
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

              Rectangle {
                id: vfoA
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                color: "#0a0e12"
                radius: 6
                border.width: 2
                // VFO A is the RX.  Red only when transmitting on A —
                // i.e. keyed AND not split (in SPLIT, A stays the receiver
                // and VFO B carries the red TX border).
                border.color: (Stream.txDisplayActive && !Stream.splitEnabled)
                              ? root.cTx : root.cRx

                // Amber role tag, upper-left — flips RX→TX on key (simplex).
                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 8
                    anchors.topMargin: 4
                    text: (Stream.txDisplayActive && !Stream.splitEnabled)
                          ? qsTr("TX") : qsTr("RX")
                    color: root.cRole
                    font.bold: true
                    font.pixelSize: 12
                    z: 2
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 4
                    anchors.bottomMargin: 6
                    spacing: 10

                    // LED VFO readout (carrier) + typed-entry overlay.
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: 58

                        FreqDisplay {
                            id: led
                            anchors.fill: parent
                            // VFO (carrier) = DDS + CW marker offset.
                            freqHz: root.centerHz + WdspEngine.markerOffsetHz
                            externalStepHz: stepCombo.stepVals[stepCombo.currentIndex]
                            // Tune: convert carrier back to the DDS wire freq.
                            onFreqEdited: (hz) =>
                                Stream.setRx1FreqHz(hz - WdspEngine.markerOffsetHz)
                            onEditRequested: {
                                editField.text = (led.freqHz / 1.0e6).toFixed(6)
                                editField.visible = true
                                editField.selectAll()
                                editField.forceActiveFocus()
                            }
                        }
                        TextField {
                            id: editField
                            anchors.fill: parent
                            visible: false
                            horizontalAlignment: TextInput.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            font.family: "Consolas"
                            font.pixelSize: 22
                            font.bold: true
                            color: "#ffb000"
                            background: Rectangle {
                                color: "#000000"
                                border.color: "#00d8ff"
                                border.width: 2
                                radius: 3
                            }
                            function commit() {
                                if (!visible) return
                                var hz = led.parseFreqInput(text)   // entered carrier
                                visible = false
                                if (hz >= 0)
                                    Stream.setRx1FreqHz(hz - WdspEngine.markerOffsetHz)
                            }
                            onAccepted: commit()
                            onActiveFocusChanged: if (!activeFocus) commit()
                            Keys.onEscapePressed: visible = false
                        }
                    }

                    // Step + Mode — VFO A's per-VFO controls, INSIDE the box,
                    // centred (balanced spacers each side).
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Item { Layout.fillWidth: true }
                        Label { text: qsTr("Step"); color: "#cccccc"; font.bold: true }
                        LyraComboBox {
                            id: stepCombo
                            Layout.preferredWidth: 78
                            property var stepVals: [1, 10, 100, 1000, 5000, 10000]
                            model: ["1 Hz", "10 Hz", "100 Hz", "1 kHz", "5 kHz", "10 kHz"]
                            currentIndex: 3   // 1 kHz default
                        }
                        Label { text: qsTr("Mode"); color: "#cccccc"; font.bold: true }
                        LyraComboBox {
                            id: modeCombo
                            Layout.preferredWidth: 78
                            model: root.modeList
                            currentIndex: Math.max(0, root.modeList.indexOf(Prefs.mode))
                            onActivated: Prefs.mode = root.modeList[currentIndex]
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
              }

              // Zero-beat needle strip — a Layout FLOW child of the VFO-A
              // cluster (sibling of the freq box), NOT an anchored overlay.
              // The v0.18.0 anchored-overlay form (anchors.top: vfoA.bottom)
              // destabilised the VFO RowLayout and let the centre logo paint
              // over the frequency readout — an unusable VFO for every user,
              // zero-beat on or off.  As a plain flow child it collapses to
              // zero height when hidden (the default, and every non-CW mode),
              // so the panel is unchanged until zero-beat is enabled; when it
              // is, the wrapper grows just enough to seat the needle + the
              // "Zero Beat" caption below the freq box.  Shown only when the
              // Visuals pref is on AND in a lockable carrier mode
              // (CW / AM / SAM / FM).
              Item {
                id: zbStrip
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 34 : 0
                visible: Prefs.zeroBeatMarkers && WdspEngine.zeroBeatActive
                readonly property real rangeHz: 500
                readonly property bool locked: WdspEngine.zeroBeatValid
                readonly property real offHz:
                    Math.max(-rangeHz, Math.min(rangeHz, WdspEngine.zeroBeatOffsetHz))
                readonly property color zbCol:
                    !locked                 ? "#55708a"
                    : Math.abs(offHz) <= 8  ? "#39e08a"
                    : Math.abs(offHz) <= 45 ? "#00d8ff" : "#e8c477"

                Component.onCompleted:
                    WdspEngine.setZeroBeatEnabled(Prefs.zeroBeatMarkers)
                Connections {
                    target: Prefs
                    function onZeroBeatMarkersChanged() {
                        WdspEngine.setZeroBeatEnabled(Prefs.zeroBeatMarkers)
                    }
                }

                Rectangle {
                    id: zbTrack
                    anchors.left: parent.left
                    anchors.right: zbRead.left
                    anchors.rightMargin: 8
                    anchors.top: parent.top
                    height: 18
                    radius: 4
                    color: "#080d15"
                    border.width: 1
                    border.color: "#1e2a38"

                    Rectangle {   // centre detent
                        width: 1.5; height: parent.height - 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#3a86a0"
                    }
                    Repeater {    // ± quarter ticks
                        model: [0.25, 0.75]
                        Rectangle {
                            width: 1; height: zbTrack.height - 14
                            x: zbTrack.width * modelData
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#22303e"
                        }
                    }
                    Rectangle {   // the needle
                        width: 3; radius: 1.5
                        height: parent.height - 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: zbStrip.zbCol
                        x: Math.round((zbTrack.width - width) / 2
                           + (zbStrip.offHz / zbStrip.rangeHz)
                             * (zbTrack.width - width) / 2)
                        Behavior on x { NumberAnimation { duration: 90 } }
                    }
                }
                Label {
                    id: zbRead
                    anchors.right: parent.right
                    anchors.verticalCenter: zbTrack.verticalCenter
                    width: 62
                    horizontalAlignment: Text.AlignRight
                    font.family: "Consolas"
                    font.pixelSize: 12
                    font.bold: true
                    color: zbStrip.zbCol
                    text: !zbStrip.locked ? "—"
                          : Math.abs(zbStrip.offHz) <= 8 ? "● 0 Hz"
                          : (zbStrip.offHz > 0 ? "+" : "")
                            + Math.round(zbStrip.offHz) + " Hz"
                }
                Label {          // caption under the meter
                    anchors.horizontalCenter: zbTrack.horizontalCenter
                    anchors.top: zbTrack.bottom
                    anchors.topMargin: 1
                    text: qsTr("Zero Beat")
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 0.5
                    color: "#8de04a"
                }
              }
            }

            Item { Layout.fillWidth: true }   // balance: centres the logo

            Image {
                source: "qrc:/qt/qml/Lyra/src/assets/logo/lyra-icon-256.png"
                // Fixed square footprint, centred by the flanking fillWidth
                // spacers.  The old `Layout.fillHeight` + `Layout.preferredWidth:
                // height` form coupled the logo's WIDTH to its own layout-driven
                // HEIGHT; that feedback could mis-resolve and jam the logo a
                // third of the way across the VFO row with dead grey space
                // beside it (v0.18.0 report).  A fixed preferred+maximum size,
                // plus a bounded sourceSize so the 256px asset's implicit size
                // can't drive the layout, removes the coupling entirely.
                //
                // The logo is DECORATION, and it must never be the thing that
                // forces an overlap.  When a RowLayout's children can't fit even
                // at their MINIMUM widths, Qt Quick Layouts overflows and lets
                // them overlap — and the Image's minimum was its 140px
                // sourceSize, which is how the logo ended up painted across the
                // frequency readout on a narrow panel.  A minimum of zero means
                // it gives way and scales down under pressure instead, so the
                // readout — which the operator actually needs — always wins.
                // It only disappears entirely when the panel is too narrow for
                // it to mean anything.
                visible: root.width >= 760
                Layout.preferredWidth: visible ? 132 : 0
                Layout.preferredHeight: 132
                Layout.minimumWidth: 0
                Layout.maximumWidth: visible ? 140 : 0
                Layout.maximumHeight: 140
                Layout.alignment: Qt.AlignVCenter
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 140
                sourceSize.height: 140
                smooth: true
                mipmap: true
            }

            Item { Layout.fillWidth: true }   // balance: VFO-B's reserved gap

            // VFO-B slot — empty (reserved, no dead widget) in simplex;
            // SPLIT fills it with the TX VFO cluster (no relayout).
            //
            // The reservation is deliberate: it keeps the logo centred and stops
            // the row re-laying out when SPLIT toggles.  But it is 360px, and
            // when the panel is too narrow to afford it the reservation is what
            // squeezes VFO-A's readout — paying a third of the row for an empty
            // hole is not a trade worth making on a small screen.  So hold the
            // slot while there is room for the whole row, and give it back when
            // there isn't.  Wide panels behave exactly as before.
            Item {
                id: vfoBSlot
                readonly property bool reserve:
                    Stream.splitEnabled || root.width >= 900
                Layout.preferredWidth: reserve ? 360 : 0
                Layout.maximumWidth: reserve ? 360 : 0
                Layout.minimumWidth: Stream.splitEnabled ? 300 : 0
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignVCenter

                // VFO-B cluster — the split TX VFO.  Shares VFO A's mode
                // (option a): TX uses Prefs.mode, so B shows freq + Step
                // only.  Border = armed-gray → red on key; amber TX tag.
                Rectangle {
                    id: vfoB
                    visible: Stream.splitEnabled
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 108
                    color: "#0a0e12"
                    radius: 6
                    border.width: 2
                    // Armed (gray) until keyed, then red (B is the TX VFO).
                    // txDisplayActive so CW (QSK, no wire MOX) reds it too.
                    border.color: Stream.txDisplayActive ? root.cTx : root.cArm

                    Label {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 8
                        anchors.topMargin: 4
                        text: qsTr("TX")
                        color: root.cRole
                        font.bold: true
                        font.pixelSize: 12
                        z: 2
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 4
                        anchors.bottomMargin: 6
                        spacing: 10

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 58

                            FreqDisplay {
                                id: ledB
                                anchors.fill: parent
                                // vfoBHz IS the carrier (TX-NCO offset
                                // applied at push), so show/tune it directly.
                                freqHz: root.vfoBHz
                                externalStepHz: stepComboB.stepVals[stepComboB.currentIndex]
                                onFreqEdited: (hz) => Stream.setVfoBHz(hz)
                                onEditRequested: {
                                    editFieldB.text = (ledB.freqHz / 1.0e6).toFixed(6)
                                    editFieldB.visible = true
                                    editFieldB.selectAll()
                                    editFieldB.forceActiveFocus()
                                }
                            }
                            TextField {
                                id: editFieldB
                                anchors.fill: parent
                                visible: false
                                horizontalAlignment: TextInput.AlignHCenter
                                verticalAlignment: TextInput.AlignVCenter
                                font.family: "Consolas"
                                font.pixelSize: 22
                                font.bold: true
                                color: "#ffb000"
                                background: Rectangle {
                                    color: "#000000"
                                    border.color: "#00d8ff"
                                    border.width: 2
                                    radius: 3
                                }
                                function commit() {
                                    if (!visible) return
                                    var hz = ledB.parseFreqInput(text)
                                    visible = false
                                    if (hz >= 0) Stream.setVfoBHz(hz)
                                }
                                onAccepted: commit()
                                onActiveFocusChanged: if (!activeFocus) commit()
                                Keys.onEscapePressed: visible = false
                            }
                        }

                        // VFO B's Step (mode is shared with VFO A).
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Item { Layout.fillWidth: true }
                            Label { text: qsTr("Step"); color: "#cccccc"; font.bold: true }
                            LyraComboBox {
                                id: stepComboB
                                Layout.preferredWidth: 78
                                property var stepVals: [1, 10, 100, 1000, 5000, 10000]
                                model: ["1 Hz", "10 Hz", "100 Hz", "1 kHz", "5 kHz", "10 kHz"]
                                currentIndex: 3
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        // ── centred action row (under the VFOs) ─────────────────────────
        // SPLIT + VFO copy/swap + CW Pitch, centred.  Reserved still for
        // RIT / XIT / CTCSS / offset — each added functional WITH its
        // feature (RIT, repeaters), never as a dead widget.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }   // centre the action controls

            // SPLIT — TX on VFO B, RX on VFO A.  Lit cyan when on.  Hidden
            // in FM, where the RPT button (below) is the friendlier split.
            Button {
                id: splitBtn
                visible: !root.fmMode
                checkable: true
                implicitHeight: 26
                implicitWidth: 64
                checked: Stream.splitEnabled
                onToggled: Stream.setSplitEnabled(checked)
                text: qsTr("SPLIT")
                font.bold: true
                font.pixelSize: 12
                background: Rectangle {
                    radius: 4
                    color: splitBtn.checked ? "#10323a" : "#161e28"
                    border.width: 2
                    border.color: splitBtn.checked ? "#00e5ff" : "#2a3a4a"
                }
                contentItem: Text {
                    text: splitBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: splitBtn.checked ? "#00e5ff" : "#cdd9e5"
                    font: splitBtn.font
                    // A custom contentItem drops the style's default eliding, so
                    // a squeezed button paints its label out over its neighbours.
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("SPLIT — receive on VFO A, transmit on VFO B "
                    + "(same band).  Set VFO B to your TX freq; key and the red "
                    + "TX border + panadapter marker move to B.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }

            // ── FM front group: Deviation (always) + RPT → Dir/Offset/CTCSS ──
            Label {
                text: qsTr("Dev"); color: "#cccccc"; font.bold: true
                visible: root.fmMode
            }
            LyraSpinBox {
                id: devSpin
                visible: root.fmMode
                Layout.preferredWidth: 94
                // Integer tenths-of-kHz internally (10..60 = 1.0..6.0 kHz,
                // 0.5 kHz step) so the display shows decimal kHz.  Two-way
                // with Stream.fmDeviationHz (Settings → TX → FM stays in sync).
                from: 10; to: 60; stepSize: 5
                value: Math.round(Stream.fmDeviationHz / 100)
                textFromValue: function(v, loc) { return (v / 10).toFixed(1) + " k" }
                valueFromText: function(t, loc) { return Math.round(parseFloat(t) * 10) }
                onValueModified: {
                    Stream.setFmDeviationHz(value * 100)
                    // Auto-size the RX filter to the new deviation's occupied
                    // width (Carson).  TX BW is already deviation-derived in FM;
                    // this makes RX follow too.  Overridable in the Filters panel.
                    Prefs.rxBandwidth = root.fmRxBwForDev(value * 100)
                }
                ToolTip.text: qsTr("FM peak deviation — 5.0 k = Wide (US), 2.5 k = "
                    + "Narrow.  Sets RX bandwidth to match.  Same control as "
                    + "Settings → TX → FM.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
            }

            // FM pre-emphasis quick chip — Comm (6 dB/oct voice) / Off (flat,
            // data + warm HF).  Mirrors Settings → TX → FM (two-way via the
            // shared Stream.fmEmphasisMode property).  0 = Off, 1 = Comm.
            Label {
                text: qsTr("Emph"); color: "#cccccc"; font.bold: true
                visible: root.fmMode
            }
            LyraComboBox {
                id: emphCombo
                visible: root.fmMode
                Layout.preferredWidth: 88
                model: [qsTr("Comm"), qsTr("Off")]
                currentIndex: Stream.fmEmphasisMode === 1 ? 0 : 1
                onActivated: Stream.setFmEmphasisMode(currentIndex === 0 ? 1 : 0)
                ToolTip.text: qsTr("FM pre-emphasis — Comm = 6 dB/oct voice curve, "
                    + "Off = flat (digital/data + warmer HF).  Same control as "
                    + "Settings → TX → FM.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
            }

            // RPT — FM repeater duplex.  In FM, split ⟺ RPT.
            Button {
                id: rptBtn
                visible: root.fmMode
                checkable: true
                implicitHeight: 26; implicitWidth: 52
                checked: Stream.splitEnabled
                text: qsTr("RPT")
                font.bold: true; font.pixelSize: 12
                onToggled: {
                    if (checked) {
                        // Fresh engage → pick this band's standard offset.
                        root.applyBandDefault()
                        Stream.setSplitEnabled(true)
                        root.rptApply()
                    } else {
                        Stream.setSplitEnabled(false)
                        Stream.setCtcssEnabled(false)
                    }
                }
                background: Rectangle {
                    radius: 4
                    color: rptBtn.checked ? "#10323a" : "#161e28"
                    border.width: 2
                    border.color: rptBtn.checked ? "#00e5ff" : "#2a3a4a"
                }
                contentItem: Text {
                    text: rptBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: rptBtn.checked ? "#00e5ff" : "#cdd9e5"
                    font: rptBtn.font
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("Repeater — TX on VFO B = VFO A ± offset, RX on "
                    + "A.  Pick the shift direction + offset and (if needed) the "
                    + "CTCSS access tone.  VFO B tracks A as you tune.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            Label {
                text: qsTr("Offset"); color: "#cccccc"; font.bold: true
                visible: root.fmMode && Stream.splitEnabled
            }
            // Shift direction (− input below output / + above).
            Button {
                visible: root.fmMode && Stream.splitEnabled
                implicitHeight: 26; implicitWidth: 30; font.pixelSize: 15
                text: root.rptDirSign < 0 ? "−" : "+"
                onClicked: { root.rptDirSign = -root.rptDirSign; root.rptApply() }
                ToolTip.text: qsTr("Repeater shift direction"); ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            // Common repeater offsets.
            LyraComboBox {
                id: rptOffsetCombo
                visible: root.fmMode && Stream.splitEnabled
                Layout.preferredWidth: 92
                model: ["100 kHz", "500 kHz", "600 kHz", "1 MHz", "5 MHz"]
                currentIndex: 0
                onActivated: root.rptApply()
                ToolTip.text: qsTr("Repeater offset (10 m = 100 kHz, 6 m = 1 MHz)")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            // CTCSS access tone — lit toggle button (clearer than a tickbox) +
            // tone combo, two-way with Settings → TX → FM.  Orange when on.
            Button {
                id: ctcssBtn
                visible: root.fmMode && Stream.splitEnabled
                checkable: true
                implicitHeight: 26; implicitWidth: 62
                checked: Stream.ctcssEnabled
                onToggled: Stream.setCtcssEnabled(checked)
                text: qsTr("CTCSS")
                font.bold: true; font.pixelSize: 12
                background: Rectangle {
                    radius: 4
                    color: ctcssBtn.checked ? "#3a2a14" : "#161e28"
                    border.width: 2
                    border.color: ctcssBtn.checked ? "#ff9a3c" : "#2a3a4a"
                }
                contentItem: Text {
                    text: ctcssBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: ctcssBtn.checked ? "#ff9a3c" : "#cdd9e5"
                    font: ctcssBtn.font
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("Send a CTCSS sub-audible access tone — required "
                    + "by tone-protected repeaters.  Pick the tone at right.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            Label {
                text: qsTr("Tone"); color: "#cccccc"; font.bold: true
                visible: root.fmMode && Stream.splitEnabled && ctcssBtn.checked
            }
            LyraComboBox {
                id: ctcssToneCombo
                visible: root.fmMode && Stream.splitEnabled && ctcssBtn.checked
                Layout.preferredWidth: 78
                model: root.ctcssTones.map(function(t) { return t.toFixed(1) })
                currentIndex: root.nearestCtcssIndex(Stream.ctcssToneHz)
                onActivated: Stream.setCtcssToneHz(root.ctcssTones[currentIndex])
                ToolTip.text: qsTr("CTCSS access tone (Hz)")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }

            // VFO copy / swap.  1→2 = A→B, 2→1 = B→A, ⇄ = swap.
            Button {
                implicitHeight: 26; implicitWidth: 40; font.pixelSize: 12
                text: qsTr("1→2")
                onClicked: Stream.setVfoBHz(Stream.rx1FreqHz)
                ToolTip.text: qsTr("Copy VFO A → VFO B"); ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            Button {
                implicitHeight: 26; implicitWidth: 40; font.pixelSize: 12
                text: qsTr("2→1")
                onClicked: Stream.setRx1FreqHz(Stream.vfoBHz)
                ToolTip.text: qsTr("Copy VFO B → VFO A"); ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            Button {
                implicitHeight: 26; implicitWidth: 36; font.pixelSize: 14
                text: qsTr("⇄")
                onClicked: {
                    var a = Stream.rx1FreqHz
                    var b = Stream.vfoBHz
                    Stream.setVfoBHz(a)
                    Stream.setRx1FreqHz(b)
                }
                ToolTip.text: qsTr("Swap VFO A ↔ VFO B"); ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }

            // ── RIT / XIT — RX / TX incremental tuning offsets ──────────────
            // Lit toggle each (cyan when on, the panel accent); the ∓ offset
            // spin reveals only when engaged so the row stays tight until
            // used.  RIT shifts only the RX NCO, XIT only the TX NCO — both
            // PS-safe (XIT folds into the single TX-freq path so PureSignal
            // tracks the shifted TX automatically).
            Button {
                id: ritBtn
                checkable: true
                implicitHeight: 26; implicitWidth: 46
                checked: Stream.ritEnabled
                onToggled: Stream.setRitEnabled(checked)
                text: qsTr("RIT")
                font.bold: true; font.pixelSize: 12
                background: Rectangle {
                    radius: 4
                    color: ritBtn.checked ? "#10323a" : "#161e28"
                    border.width: 2
                    border.color: ritBtn.checked ? "#00e5ff" : "#2a3a4a"
                }
                contentItem: Text {
                    text: ritBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: ritBtn.checked ? "#00e5ff" : "#cdd9e5"
                    font: ritBtn.font
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("RIT — Receiver Incremental Tuning.  Shifts "
                    + "only the RX by the offset; TX stays put.  Chase an "
                    + "off-frequency station without moving your VFO.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            LyraSpinBox {
                id: ritSpin
                wheelStepping: false   // has its own WheelHandler (10 Hz)
                visible: ritBtn.checked
                Layout.preferredWidth: 104
                from: -9999; to: 9999; stepSize: 1   // ± arrows = 1 Hz
                value: Stream.ritOffsetHz
                textFromValue: function(v, loc) { return (v > 0 ? "+" : "") + v + " Hz" }
                valueFromText: function(t, loc) { return Math.round(parseFloat(t)) }
                onValueModified: Stream.setRitOffsetHz(value)
                // Re-sync from the model after an arrow edit breaks the value
                // binding, so the "0" reset / persistence / external set still
                // show (the proven tuneDriveSlider pattern).
                Connections {
                    target: Stream
                    function onRitChanged() {
                        if (ritSpin.value !== Stream.ritOffsetHz)
                            ritSpin.value = Stream.ritOffsetHz
                    }
                }
                // Wheel = coarse: 10 Hz (Shift = 100 Hz).  Arrows stay 1 Hz.
                WheelHandler {
                    onWheel: (ev) => {
                        var step = (ev.modifiers & Qt.ShiftModifier) ? 100 : 10
                        var nv = ritSpin.value + (ev.angleDelta.y > 0 ? step : -step)
                        Stream.setRitOffsetHz(Math.max(-9999, Math.min(9999, nv)))
                    }
                }
                ToolTip.text: qsTr("RX offset, ±9.99 kHz.  Arrows = 1 Hz, "
                    + "wheel = 10 Hz (Shift = 100 Hz).  '0' clears it.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
            }
            // Quick-zero — a reliable button (double-click on the spin fought
            // the SpinBox's own input handling and killed the arrows).
            Button {
                visible: ritBtn.checked
                implicitHeight: 26; implicitWidth: 26
                text: qsTr("0"); font.pixelSize: 12
                onClicked: Stream.setRitOffsetHz(0)
                ToolTip.text: qsTr("Zero the RIT offset")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }

            Button {
                id: xitBtn
                checkable: true
                implicitHeight: 26; implicitWidth: 46
                checked: Stream.xitEnabled
                onToggled: Stream.setXitEnabled(checked)
                text: qsTr("XIT")
                font.bold: true; font.pixelSize: 12
                background: Rectangle {
                    radius: 4
                    color: xitBtn.checked ? "#10323a" : "#161e28"
                    border.width: 2
                    border.color: xitBtn.checked ? "#00e5ff" : "#2a3a4a"
                }
                contentItem: Text {
                    text: xitBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: xitBtn.checked ? "#00e5ff" : "#cdd9e5"
                    font: xitBtn.font
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("XIT — Transmitter Incremental Tuning.  Shifts "
                    + "only the TX by the offset; RX stays put.  PureSignal "
                    + "tracks the shifted TX automatically.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }
            LyraSpinBox {
                id: xitSpin
                wheelStepping: false   // has its own WheelHandler (10 Hz)
                visible: xitBtn.checked
                Layout.preferredWidth: 104
                from: -9999; to: 9999; stepSize: 1   // ± arrows = 1 Hz
                value: Stream.xitOffsetHz
                textFromValue: function(v, loc) { return (v > 0 ? "+" : "") + v + " Hz" }
                valueFromText: function(t, loc) { return Math.round(parseFloat(t)) }
                onValueModified: Stream.setXitOffsetHz(value)
                Connections {
                    target: Stream
                    function onXitChanged() {
                        if (xitSpin.value !== Stream.xitOffsetHz)
                            xitSpin.value = Stream.xitOffsetHz
                    }
                }
                // Wheel = coarse: 10 Hz (Shift = 100 Hz).  Arrows stay 1 Hz.
                WheelHandler {
                    onWheel: (ev) => {
                        var step = (ev.modifiers & Qt.ShiftModifier) ? 100 : 10
                        var nv = xitSpin.value + (ev.angleDelta.y > 0 ? step : -step)
                        Stream.setXitOffsetHz(Math.max(-9999, Math.min(9999, nv)))
                    }
                }
                ToolTip.text: qsTr("TX offset, ±9.99 kHz.  Arrows = 1 Hz, "
                    + "wheel = 10 Hz (Shift = 100 Hz).  '0' clears it.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 800
            }
            Button {
                visible: xitBtn.checked
                implicitHeight: 26; implicitWidth: 26
                text: qsTr("0"); font.pixelSize: 12
                onClicked: Stream.setXitOffsetHz(0)
                ToolTip.text: qsTr("Zero the XIT offset")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 600
            }

            Label {
                text: qsTr("CW Pitch")
                color: "#cccccc"; font.bold: true
                visible: root.cwMode
            }
            LyraSpinBox {
                id: pitchSpin
                visible: root.cwMode
                Layout.preferredWidth: 110
                from: 200; to: 1500; stepSize: 10
                value: WdspEngine.cwPitchHz
                onValueModified: WdspEngine.setCwPitchHz(value)
            }

            Item { Layout.fillWidth: true }
        }
    }
}
