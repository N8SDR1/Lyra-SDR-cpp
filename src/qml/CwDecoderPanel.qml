// Lyra — CW decoder dock panel (#173).
//
// RX-only decoded text + receive WPM, driven by the faithful fldigi CW port
// (WdspEngine cwDecoder_).  Decoding runs ONLY in CWU/CWL (the tap is CW-mode-
// gated in WdspEngine).  The detection pitch == the unified CW pitch: tune the
// signal onto your pitch, exactly as in fldigi — there is no AFC.
//
// Controls are ONLY what fldigi's CW receiver exposes: Bandwidth (or matched-
// filter auto), Speed (WPM seed), adaptive Tracking, Squelch on/off + level.
// Everything else the old Lyra decoder had (auto-threshold, narrow, seek, NB,
// AFC, the Bayesian engine) is gone with the front-end it belonged to.
//
// Styled to match CwConsolePanel — hand-drawn chips + section dividers.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 470
    implicitHeight: collapsed ? 40 : (body.implicitHeight + 12)
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"

    property bool   collapsed: false
    property string decodedText: ""
    property int    rxWpm: 0
    property bool   matchTxSpeed: false

    // DeepFist — active engine (0=Classic fldigi, 1=Neural).  Both engines now
    // stream characters incrementally into the transcript: the classic engine
    // per decoded element, the neural engine via frame-timed commit (~1.3 s
    // latency).  So both just append to decodedText.
    readonly property int cwEngine: WdspEngine.cwDecodeEngine

    // Phase 3 "Learn calls" — the invariant is "if the operator SAW it green,
    // it is learned": displayHtml() notes each call in the same pass that
    // first renders it green (see there).  Every earlier scheme re-checked
    // spotted-ness at some OTHER moment and lost calls to timing on air:
    // tap-at-word-completion missed spots that arrived seconds later (W2DON),
    // a rescan window missed calls still visibly green further back (KT4K),
    // and rescan-on-spot-change missed spots evicted from the 200-cap bank
    // before the next bank event (KA1ULN).  Render time is the ONLY moment
    // green-ness is guaranteed observed.  Opt-in (Prefs.cwLearnCalls).
    property var greenSeen: ({})    // call -> ms epoch of the last learn note

    function appendDecoded(s) {
        var t = root.decodedText + s
        if (t.length > 6000) t = t.slice(-4500)
        root.decodedText = t
    }
    function clearDecoded() { root.decodedText = ""; root.greenSeen = {} }

    // Auto engine: fallback (Classic-during-fade) runs are wrapped in private
    // control-char markers (U+0002 / U+0003) so displayHtml() can dim them.
    // (Non-empty fallback text only — a bare gap space needs no marking.)
    function appendAuto(s, fallback) {
        if (fallback && s.trim().length > 0)
            appendDecoded("\u0002" + s + "\u0003")
        else
            appendDecoded(s)
    }

    // Bumped whenever the spot bank changes, to re-highlight the transcript.
    property int spotRev: 0

    // Word (call) under the last right-click, for the grab menu.
    property string grabWord: ""

    // The call-shaped word at plain-text position `pos` in the transcript.
    // Extracted straight from decodedText (not selectWord/selectedText) so it
    // works with the RichText pane and while text is streaming in.
    function wordAt(pos) {
        var t = root.decodedText
        if (pos < 0 || pos > t.length) return ""
        function isCh(c) {
            return (c >= "A" && c <= "Z") || (c >= "a" && c <= "z")
                || (c >= "0" && c <= "9") || c === "/"
        }
        var a = pos, b = pos
        while (a > 0 && isCh(t.charAt(a - 1))) a--
        while (b < t.length && isCh(t.charAt(b))) b++
        return t.substring(a, b).trim()
    }

    // True when a token has amateur-callsign shape (prefix, area digit, 1-3
    // letter suffix, optional /P-style tail).  FALLBACK ONLY — used when the
    // MASTER.SCP database isn't loaded yet (cwCallKnown is the real test);
    // shape alone ambers run-together garbles like AA0TTDE ("AA0TT DE …").
    function isCallShaped(w) {
        return /^(?:[A-Z][A-Z0-9]?|[0-9][A-Z])[0-9][A-Z]{1,3}(?:\/[A-Z0-9]{1,4})?$/.test(w)
    }

    // Render the transcript as HTML: any callsign-shaped word is coloured amber
    // so calls always pop out of the copy; one that is currently spotted on
    // RBN/cluster near the tuned frequency (Spots.isSpottedHere) upgrades to
    // bright green + bold — a real, active, verified station.  The green pass
    // doubles as the Learn tap (greenSeen memo above).
    function displayHtml(t) {
        var rev = root.spotRev   // create a binding dependency on spot updates
        var esc = t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        var html = esc.replace(/[A-Z0-9\/]{3,}/g, function(w) {
            // only bother the lookups with call-shaped tokens (contain a digit)
            if (/[0-9]/.test(w)) {
                if (Spots.isSpottedHere(w)) { // RBN/cluster-verified: live HERE
                    // Learn what renders green, AS it renders (see greenSeen).
                    // Memo keeps this one C++ call per call per 10 min — the
                    // same window as the harvester's per-call gold dedupe.
                    if (Prefs.cwLearnCalls) {
                        var tn = Date.now()
                        if (!root.greenSeen[w] || tn - root.greenSeen[w] > 600000) {
                            root.greenSeen[w] = tn
                            WdspEngine.cwNoteConfirmedCall(w)
                        }
                    }
                    return '<span style="color:#5fffa8; font-weight:bold">' + w + '</span>'
                }
                if (WdspEngine.cwCallKnown(w) // known-real (MASTER.SCP) —
                        || (!WdspEngine.cwNeuralAvailable && root.isCallShaped(w)))
                    return '<span style="color:#ffbe5a">' + w + '</span>'
            }
            return w
        })
        // Auto-engine fallback markers -> subtle dim (lower-confidence cue).
        // Qt RichText's CSS subset has NO `opacity` (it parses, silently drops
        // it) — dim by colour instead: a darker shade of the operator's decode
        // colour, so the cue follows the colour preset.
        var dim = Qt.darker(Prefs.cwDecodeColor, 1.5).toString()
        html = html.replace(/\u0002/g, '<span style="color:' + dim + '">')
                   .replace(/\u0003/g, '</span>')
        return html
    }

    // fldigi CW-receiver knob mirrors (persisted via Prefs; fldigi defaults:
    // BW 150 Hz, speed 18 wpm, tracking on, matched filter off, squelch off).
    property int  bandwidthHz:   Prefs.cwDecodeBandwidth
    property int  speedWpm:      Prefs.cwDecodeSpeed
    property bool trackingOn:    Prefs.cwDecodeTracking
    property bool matchedFilter: Prefs.cwDecodeMatchedFilter
    property bool squelchOn:     Prefs.cwDecodeSquelchOn
    property real squelchValue:  Prefs.cwDecodeSquelchValue

    // Live squelch signal metric (fldigi SNR reading, 0..100) — polled for the
    // meter bar so you can set the manual threshold just under the signal peaks.
    property real sqMetric: 0

    // Decoded-text colour presets.
    readonly property var colorChoices:
        ["#39ff14", "#ffbe5a", "#00e5ff", "#e6edf3", "#ff9a3c", "#ff6b6b"]

    readonly property bool cwActive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "CWU" || m === "CWL"
    }

    function pushSquelch() {
        WdspEngine.setCwDecodeSquelch(root.squelchOn, root.squelchValue)
    }

    Component.onCompleted: {
        WdspEngine.setCwDecodeBandwidthHz(root.bandwidthHz)
        WdspEngine.setCwDecodeSpeedWpm(root.speedWpm)
        WdspEngine.setCwDecodeTracking(root.trackingOn)
        WdspEngine.setCwDecodeMatchedFilter(root.matchedFilter)
        pushSquelch()
        WdspEngine.setCwBlankPenalty(Prefs.cwBlankPenalty)   // DeepFist blank penalty
        // Restore the persisted engine (DeepFist or Auto); if the neural model
        // can't load, WdspEngine stays on Classic (cwDecodeEngine reflects the
        // truth, and the chips light accordingly).
        if (Prefs.cwDecodeEngine === 1 || Prefs.cwDecodeEngine === 2)
            WdspEngine.cwDecodeEngine = Prefs.cwDecodeEngine
        // Phase 2: resume harvest capture if the operator left it on.
        if (Prefs.cwCaptureEnabled)
            WdspEngine.setCwCaptureEnabled(true)
    }

    function applyWpmToKeyer(w) {
        var c = Math.max(5, Math.min(60, Math.round(w)))
        if (Math.abs(c - Stream.cwKeyerSpeedWpm) < 2) return
        Stream.cwKeyerSpeedWpm = c
    }

    Connections {
        target: WdspEngine
        function onCwDecodedChar(ch, conf) { root.appendDecoded(ch) }
        function onCwRxWpmChanged(w) {
            root.rxWpm = w
            if (root.matchTxSpeed) root.applyWpmToKeyer(w)
        }
        // Neural engine: append the newly-committed characters (frame-timed).
        function onCwNeuralText(newText) { root.appendDecoded(newText) }
        // Auto engine: arbiter output; fallback runs are source-marked (dimmed).
        function onCwAutoText(text, fallback) { root.appendAuto(text, fallback) }
        // Engine switch: KEEP the transcript (the operator A/Bs engines on the
        // same signal and tunes each; the Clear chip empties it manually) —
        // just drop a seam glyph so the eye can see where the engine changed.
        function onCwDecodeEngineChanged() {
            if (root.decodedText.length > 0) root.appendDecoded(" • ")
        }
        // MASTER.SCP arrives with the neural model (first switch to
        // DeepFist/Auto) — re-render so already-copied calls gain their amber.
        function onCwNeuralAvailableChanged() { root.spotRev++ }
    }

    // Re-highlight the transcript when the spot bank changes.  The bumped
    // spotRev re-runs displayHtml, whose green pass is also the learner — so a
    // late-arriving spot both greens AND learns the call in the same render.
    Connections {
        target: Spots
        function onChanged() { root.spotRev++ }
    }

    // Poll the live squelch metric (~10 Hz) only while the decoder is running
    // in a CW mode and the panel is expanded — cheap, no cost otherwise.
    Timer {
        interval: 100; repeat: true
        running: !root.collapsed && root.cwActive && WdspEngine.cwDecodeEnabled
        onTriggered: root.sqMetric = WdspEngine.cwDecodeMetric()
        onRunningChanged: if (!running) root.sqMetric = 0
    }

    // ── Reusable hand-styled chip (lit = active) ──
    component ChipButton: Rectangle {
        id: chip
        property string label: ""
        property bool   lit: false
        property bool   chipEnabled: true
        signal clicked()
        implicitHeight: 26
        implicitWidth: chipTxt.implicitWidth + 22
        radius: 4
        opacity: chipEnabled ? 1.0 : 0.4
        color: chip.lit ? "#2e7d9a" : "#1c252b"
        border.color: chip.lit ? root.cAccent : "#3a4750"
        Text {
            id: chipTxt
            anchors.centerIn: parent
            text: chip.label
            color: chip.lit ? "#ffffff" : root.cMuted
            font.pixelSize: 12; font.bold: chip.lit
        }
        MouseArea {
            anchors.fill: parent
            enabled: chip.chipEnabled
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    // ── Reusable labelled divider ──
    component Divider: RowLayout {
        id: div
        property string label: ""
        Layout.fillWidth: true
        spacing: 8
        Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
        Label { text: div.label; color: root.cMuted; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ── Header ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("CW Decoder"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label {
                visible: WdspEngine.cwDecodeEnabled && !root.cwActive
                text: qsTr("• switch to CW to decode")
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Label {
                text: (root.rxWpm > 0 ? root.rxWpm : "—") + qsTr(" wpm")
                color: root.cText; font.family: "Consolas"; font.bold: true
                font.pixelSize: 12
            }
            ChipButton {
                label: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
            }
        }

        // ── Decode on/off + Clear ──
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            ChipButton {
                label: WdspEngine.cwDecodeEnabled ? qsTr("Decoding") : qsTr("Decode OFF")
                lit: WdspEngine.cwDecodeEnabled
                onClicked: WdspEngine.cwDecodeEnabled = !WdspEngine.cwDecodeEnabled
            }
            Item { Layout.fillWidth: true }
            ChipButton {
                label: qsTr("Clear")
                onClicked: root.clearDecoded()
            }
            ChipButton {
                label: qsTr("Learn")
                lit: Prefs.cwLearnCalls
                onClicked: Prefs.cwLearnCalls = !Prefs.cwLearnCalls
            }
            ChipButton {
                label: qsTr("Harvest")
                lit: Prefs.cwCaptureEnabled
                onClicked: {
                    Prefs.cwCaptureEnabled = !Prefs.cwCaptureEnabled
                    WdspEngine.setCwCaptureEnabled(Prefs.cwCaptureEnabled)
                }
            }
        }

        // ── Engine selector: Classic (fldigi) vs DeepFist (neural) ──
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Engine"); color: root.cText; font.pixelSize: 12 }
            ChipButton {
                label: qsTr("Classic")
                lit: root.cwEngine === 0
                onClicked: { WdspEngine.cwDecodeEngine = 0
                             Prefs.cwDecodeEngine = 0 }
            }
            ChipButton {
                label: qsTr("DeepFist")
                lit: root.cwEngine === 1
                onClicked: { WdspEngine.cwDecodeEngine = 1
                             // Persist only if it actually engaged (model loaded).
                             if (WdspEngine.cwDecodeEngine === 1)
                                 Prefs.cwDecodeEngine = 1 }
            }
            ChipButton {
                label: qsTr("Auto")
                lit: root.cwEngine === 2
                onClicked: { WdspEngine.cwDecodeEngine = 2
                             // Persist only if it actually engaged (model loaded).
                             if (WdspEngine.cwDecodeEngine === 2)
                                 Prefs.cwDecodeEngine = 2 }
            }
            Label {
                visible: (root.cwEngine === 1 || root.cwEngine === 2) && !WdspEngine.cwNeuralAvailable
                text: qsTr("• model not found — see models/")
                color: "#e0a030"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        // ── DeepFist: live "Sensitivity" (CTC blank-penalty) slider ──
        RowLayout {
            visible: !root.collapsed && root.cwEngine === 1
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("Sensitivity"); color: root.cText; font.pixelSize: 12 }
            LyraSlider {
                id: penSlider
                Layout.fillWidth: true
                // Bipolar: negative = "cleaner" (suppress stray/doubled chars on
                // strong signals), 0 = neutral, positive = recover weak code.
                from: -1; to: 1; stepSize: 0.5; snapMode: Slider.SnapAlways
                showTicks: true
                showTickNumbers: true
                value: Prefs.cwBlankPenalty
                onMoved: {
                    Prefs.cwBlankPenalty = value
                    WdspEngine.setCwBlankPenalty(value)
                }
            }
            Label {
                // Signed value, e.g. "-0.5", "0", "+2".
                text: (WdspEngine.cwBlankPenalty > 0 ? "+" : "")
                      + WdspEngine.cwBlankPenalty.toFixed(1)
                color: root.cText; font.pixelSize: 11
                Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight
            }
        }
        Label {
            visible: !root.collapsed && root.cwEngine === 1
            Layout.fillWidth: true
            text: qsTr("0 = neutral.  Negative = cleaner copy on strong signals "
                       + "(fewer stray/doubled characters).  Positive = pull weak/"
                       + "fainter code out of the noise (but more stray characters).")
            color: root.cMuted; font.pixelSize: 10; wrapMode: Text.WordWrap
        }

        // ── Decoded-text pane (operator font + colour) ──
        Rectangle {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            radius: 6
            color: "#070d12"
            border.color: "#1c2a36"
            ScrollView {
                id: decodeScroll
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                function scrollToBottom() {
                    var f = decodeScroll.contentItem
                    if (!f) return
                    var ch = Math.max(f.contentHeight, decodeOut.implicitHeight)
                    f.contentY = ch > f.height ? ch - f.height : 0
                }
                // Auto-scroll off the VIEWPORT's content height, not the TextArea's:
                // the Flickable's contentHeight updates only after ScrollView has
                // taken in the new (possibly just-wrapped) line, so scrolling here
                // always includes it — fixes the newest line lagging one update.
                Connections {
                    target: decodeScroll.contentItem
                    function onContentHeightChanged() {
                        Qt.callLater(decodeScroll.scrollToBottom)
                    }
                }
                TextArea {
                    id: decodeOut
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    textFormat: TextArea.RichText   // spotted calls highlighted
                    text: root.displayHtml(root.decodedText)
                    color: Prefs.cwDecodeColor
                    font.family: "Consolas"
                    font.pixelSize: Prefs.cwDecodeFontSize
                    background: null
                    onTextChanged: cursorPosition = length

                    // Double-click a word → His Call (robust word extraction).
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: (pt) => {
                            var w = root.wordAt(
                                decodeOut.positionAt(pt.position.x, pt.position.y))
                            if (w.length > 0) CwMacros.hisCall = w.toUpperCase()
                        }
                    }
                    // Right-click a word → menu (His Call / Name).
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (pt) => {
                            root.grabWord = root.wordAt(
                                decodeOut.positionAt(pt.position.x, pt.position.y))
                            grabMenu.popup()
                        }
                    }
                }
            }
        }

        // ── Grab a decoded selection into the CW console contact row (#181) ──
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Double-click a call → His Call · right-click for menu:")
                color: root.cMuted; font.pixelSize: 11
            }
            ChipButton {
                label: qsTr("→ His Call")
                chipEnabled: decodeOut.selectedText.length > 0
                onClicked: CwMacros.hisCall = decodeOut.selectedText.trim().toUpperCase()
            }
            ChipButton {
                label: qsTr("→ Name")
                chipEnabled: decodeOut.selectedText.length > 0
                onClicked: CwMacros.opName = decodeOut.selectedText.trim()
            }
            Item { Layout.fillWidth: true }
        }

        // ── Decoder (fldigi) controls — classic engine only; the neural engine
        //    is self-tuning (no BW/speed/squelch knobs). ──
        Divider {
            label: qsTr("Decoder")
            visible: !root.collapsed && root.cwEngine === 0
        }

        // Speed (WPM seed) + adaptive Tracking + Matched-filter.
        RowLayout {
            visible: !root.collapsed && root.cwEngine === 0
            Layout.fillWidth: true
            spacing: 8
            opacity: root.cwActive ? 1.0 : 0.6
            Label { text: qsTr("Speed"); color: root.cText; font.pixelSize: 12 }
            ChipButton {
                label: "−"
                onClicked: {
                    root.speedWpm = Math.max(5, root.speedWpm - 1)
                    Prefs.cwDecodeSpeed = root.speedWpm
                    WdspEngine.setCwDecodeSpeedWpm(root.speedWpm)
                }
            }
            Label {
                text: root.speedWpm + " wpm"
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 60; horizontalAlignment: Text.AlignHCenter
            }
            ChipButton {
                label: "+"
                onClicked: {
                    root.speedWpm = Math.min(50, root.speedWpm + 1)
                    Prefs.cwDecodeSpeed = root.speedWpm
                    WdspEngine.setCwDecodeSpeedWpm(root.speedWpm)
                }
            }
            Item { Layout.fillWidth: true }
            ChipButton {
                label: qsTr("Tracking")
                lit: root.trackingOn
                onClicked: {
                    root.trackingOn = !root.trackingOn
                    Prefs.cwDecodeTracking = root.trackingOn
                    WdspEngine.setCwDecodeTracking(root.trackingOn)
                }
            }
            ChipButton {
                label: qsTr("Matched filter")
                lit: root.matchedFilter
                onClicked: {
                    root.matchedFilter = !root.matchedFilter
                    Prefs.cwDecodeMatchedFilter = root.matchedFilter
                    WdspEngine.setCwDecodeMatchedFilter(root.matchedFilter)
                }
            }
        }

        // Bandwidth (disabled when matched-filter is on — fldigi auto-sets it).
        RowLayout {
            visible: !root.collapsed && root.cwEngine === 0
            Layout.fillWidth: true
            spacing: 10
            opacity: root.cwActive ? 1.0 : 0.6
            Label { text: qsTr("Bandwidth"); color: root.cText; font.pixelSize: 12 }
            LyraSlider {
                id: bwSlider
                Layout.fillWidth: true
                enabled: !root.matchedFilter
                from: 50; to: 1000; stepSize: 10; value: Prefs.cwDecodeBandwidth
                onMoved: {
                    root.bandwidthHz = Math.round(value)
                    Prefs.cwDecodeBandwidth = root.bandwidthHz
                    WdspEngine.setCwDecodeBandwidthHz(root.bandwidthHz)
                }
            }
            Label {
                text: root.matchedFilter ? qsTr("auto") : (root.bandwidthHz + " Hz")
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 64; horizontalAlignment: Text.AlignRight
            }
        }

        // Squelch on/off + metric level.
        RowLayout {
            visible: !root.collapsed && root.cwEngine === 0
            Layout.fillWidth: true
            spacing: 10
            opacity: root.cwActive ? 1.0 : 0.6
            ChipButton {
                label: qsTr("Squelch")
                lit: root.squelchOn
                onClicked: {
                    root.squelchOn = !root.squelchOn
                    Prefs.cwDecodeSquelchOn = root.squelchOn
                    root.pushSquelch()
                }
            }
            LyraSlider {
                id: sqSlider
                Layout.fillWidth: true
                enabled: root.squelchOn
                from: 0; to: 50; stepSize: 1; value: Prefs.cwDecodeSquelchValue
                onMoved: {
                    root.squelchValue = value
                    Prefs.cwDecodeSquelchValue = value
                    root.pushSquelch()
                }
            }
            Label {
                text: Math.round(root.squelchValue)
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight
            }
        }

        // Live signal-metric bar (fldigi SNR, own 0..50 scale).  The cyan fill
        // is the current signal; the amber tick is your Squelch threshold when
        // Squelch is on — set the slider just under the signal peaks.
        RowLayout {
            visible: !root.collapsed && root.cwEngine === 0
            Layout.fillWidth: true
            spacing: 10
            opacity: root.cwActive ? 1.0 : 0.4
            Label {
                text: qsTr("Signal"); color: root.cText; font.pixelSize: 11
                Layout.preferredWidth: 44
            }
            Item {
                Layout.fillWidth: true
                implicitHeight: 8
                Rectangle {                       // track
                    anchors.fill: parent
                    radius: 3; color: "#0e1621"
                    border.color: "#1a2632"; border.width: 1
                }
                Rectangle {                       // signal fill
                    anchors.left: parent.left; y: 1
                    height: parent.height - 2
                    width: parent.width * Math.max(0, Math.min(root.sqMetric, 50)) / 50
                    radius: 3; color: "#3fb6e6"
                }
                Rectangle {                       // squelch threshold tick
                    visible: root.squelchOn
                    width: 2; height: parent.height + 4
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.width * Math.max(0, Math.min(root.squelchValue, 50)) / 50 - width / 2
                    color: "#e6a23f"
                }
            }
            Label {
                text: Math.round(Math.min(root.sqMetric, 99))
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight
            }
        }

        // ── Display (font size + decoded-text colour) ──
        Divider { label: qsTr("Display") }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("Font"); color: root.cText; font.pixelSize: 12 }
            ChipButton {
                label: "−"
                onClicked: Prefs.cwDecodeFontSize = Prefs.cwDecodeFontSize - 1
            }
            Label {
                text: Prefs.cwDecodeFontSize + " px"
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter
            }
            ChipButton {
                label: "+"
                onClicked: Prefs.cwDecodeFontSize = Prefs.cwDecodeFontSize + 1
            }
            Item { Layout.preferredWidth: 12 }
            Label { text: qsTr("Colour"); color: root.cText; font.pixelSize: 12 }
            Repeater {
                model: root.colorChoices
                delegate: Rectangle {
                    width: 22; height: 22; radius: 4
                    color: modelData
                    border.width: Prefs.cwDecodeColor === modelData ? 2 : 1
                    border.color: Prefs.cwDecodeColor === modelData
                                  ? root.cAccent : "#3a4750"
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Prefs.cwDecodeColor = modelData
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Keyer coupling ──
        Divider { label: qsTr("Keyer") }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 10
            ChipButton {
                label: qsTr("Match TX speed to RX WPM")
                lit: root.matchTxSpeed
                onClicked: {
                    root.matchTxSpeed = !root.matchTxSpeed
                    if (root.matchTxSpeed && root.rxWpm > 0)
                        root.applyWpmToKeyer(root.rxWpm)
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: root.matchTxSpeed
                text: qsTr("keyer → ") + Math.round(Stream.cwKeyerSpeedWpm) + qsTr(" wpm")
                color: root.cMuted; font.family: "Consolas"; font.pixelSize: 11
            }
        }

        Label {
            visible: !root.collapsed
            Layout.fillWidth: true
            text: qsTr("Tune the signal to your CW pitch on the panadapter — the "
                       + "decoder copies at that pitch (fldigi-style; no AFC).")
            color: root.cMuted; font.pixelSize: 10; wrapMode: Text.WordWrap
        }
    }

    // Right-click grab menu.
    Menu {
        id: grabMenu
        MenuItem {
            text: qsTr("→ His Call:  ") + root.grabWord.toUpperCase()
            enabled: root.grabWord.length > 0
            onTriggered: CwMacros.hisCall = root.grabWord.toUpperCase()
        }
        MenuItem {
            text: qsTr("→ Name:  ") + root.grabWord
            enabled: root.grabWord.length > 0
            onTriggered: CwMacros.opName = root.grabWord
        }
    }
}
