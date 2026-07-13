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

    // DeepFist CTC-lattice callsign verdicts — deduplicated + ranked by how many
    // windows confirmed each call (the real call dominates; window-edge noise is
    // a lone hit).  callChips is the top-N for display.
    property var callCounts: ({})
    property var callChips: []

    function addCall(best, orig, margin) {
        var m = root.callCounts
        if (!m[best]) m[best] = { count: 0, corrected: false }
        m[best].count += 1
        if (best !== orig) m[best].corrected = true
        root.callCounts = m
        var arr = []
        for (var k in m) arr.push({ call: k, count: m[k].count, corrected: m[k].corrected })
        arr.sort(function(a, b) { return b.count - a.count })
        root.callChips = arr.slice(0, 8)
    }
    function clearCalls() { root.callCounts = ({}); root.callChips = [] }

    function appendDecoded(s) {
        var t = root.decodedText + s
        if (t.length > 6000) t = t.slice(-4500)
        root.decodedText = t
    }
    function clearDecoded() { root.decodedText = ""; root.clearCalls() }

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
        // Restore the persisted engine; if the neural model can't load,
        // WdspEngine stays on Classic (cwDecodeEngine reflects the truth).
        if (Prefs.cwDecodeEngine === 1)
            WdspEngine.cwDecodeEngine = 1
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
        // Neural engine: CTC-lattice callsign verdict (confident only).
        function onCwNeuralCall(best, orig, margin) { root.addCall(best, orig, margin) }
        // Clear the transcript when switching engines.
        function onCwDecodeEngineChanged() { root.clearDecoded() }
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
            Label {
                visible: root.cwEngine === 1
                text: WdspEngine.cwNeuralAvailable
                      ? qsTr("• neural (rolling 6 s window)")
                      : qsTr("• model not found — see models/")
                color: WdspEngine.cwNeuralAvailable ? "#3fb6a0" : "#e0a030"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        // ── DeepFist callsign verdicts (CTC-lattice rescorer) — click → His Call ──
        Flow {
            visible: !root.collapsed && root.cwEngine === 1 && root.callChips.length > 0
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Calls")
                color: root.cMuted; font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Repeater {
                model: root.callChips
                delegate: Rectangle {
                    radius: 4
                    implicitHeight: 24
                    implicitWidth: callTxt.implicitWidth + 16
                    color: "#12281e"
                    border.color: modelData.corrected ? "#e0a030" : "#2e9d6a"
                    Text {
                        id: callTxt
                        anchors.centerIn: parent
                        text: modelData.call +
                              (modelData.count > 1 ? "·" + modelData.count : "")
                        color: modelData.corrected ? "#e6c060" : "#5fe0a0"
                        font.family: "Consolas"; font.pixelSize: 12; font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: CwMacros.hisCall = modelData.call
                    }
                }
            }
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
                TextArea {
                    id: decodeOut
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    textFormat: TextArea.PlainText
                    text: root.decodedText
                    color: Prefs.cwDecodeColor
                    font.family: "Consolas"
                    font.pixelSize: Prefs.cwDecodeFontSize
                    background: null
                    onTextChanged: cursorPosition = length
                    onContentHeightChanged: Qt.callLater(decodeScroll.scrollToBottom)

                    // Double-click a word → His Call.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: (pt) => {
                            decodeOut.cursorPosition =
                                decodeOut.positionAt(pt.position.x, pt.position.y)
                            decodeOut.selectWord()
                            var w = decodeOut.selectedText.trim()
                            if (w.length > 0) CwMacros.hisCall = w.toUpperCase()
                        }
                    }
                    // Right-click a word → menu (His Call / Name).
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (pt) => {
                            if (decodeOut.selectedText.trim().length === 0) {
                                decodeOut.cursorPosition =
                                    decodeOut.positionAt(pt.position.x, pt.position.y)
                                decodeOut.selectWord()
                            }
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
            text: qsTr("→ His Call:  ") + decodeOut.selectedText.trim().toUpperCase()
            enabled: decodeOut.selectedText.trim().length > 0
            onTriggered: CwMacros.hisCall = decodeOut.selectedText.trim().toUpperCase()
        }
        MenuItem {
            text: qsTr("→ Name:  ") + decodeOut.selectedText.trim()
            enabled: decodeOut.selectedText.trim().length > 0
            onTriggered: CwMacros.opName = decodeOut.selectedText.trim()
        }
    }
}
