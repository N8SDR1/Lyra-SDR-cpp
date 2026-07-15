// Lyra — Frequency Calibration instrument (freq_calibration_design.md, Stage 4).
//
// Floating chip-launched panel.  Tap a time-station chip -> it auto-tunes USB
// ~1 kHz below the carrier and arms the measurement -> the A+B hero (carrier
// marker sliding across a centre-null scale + big Hz/ppm/error readout,
// colour-coded) -> Apply the suggested correction.  On close it disarms and
// restores the prior freq/mode.  Reset / manual entry live on Settings ->
// Calibration.
//
// Bindings: Stream (rx1FreqHz / setRx1FreqHz / setFreqCorrection /
// freqCorrection), WdspEngine (mode / setMode / setFreqCalMeasuring +
// freqCalUpdated), Time.calStations().  All exposed as context properties by
// MainWindow::makeQuick.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    // Fixed initial size (SizeRootObjectToView uses this as the dock's size
    // hint; the dock is user-resizable via the grip).  Deriving the height
    // from the ColumnLayout while it anchors.fill'd the root created a
    // binding loop that collapsed the panel to ~0 px — a 0-size floating
    // dock never visibly opens.
    implicitWidth: 560
    implicitHeight: 380
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cGreen:  "#78d28c"
    readonly property color cAmber:  "#f2b749"
    readonly property color cRed:    "#e67878"

    property bool   active: false
    property string stationLabel: ""
    property real   stationHz: 0
    property real   savedFreq: 0
    property string savedMode: "USB"
    property real   measuredHz: 0
    property real   snrDb: 0
    property int    windows: 0
    property real   expectedHz: 0
    property real   errorHz: 0
    property real   suggested: 1.0
    property bool   strong: false
    property bool   valid: false

    function colorForError(ae) {
        return ae < 2.0 ? cGreen : (ae < 20.0 ? cAmber : cRed)
    }

    function startStation(freqHz, label) {
        if (active) stopMeasure()
        savedFreq = Stream.rx1FreqHz
        savedMode = Prefs.mode              // operator-facing mode (drives display + demod)
        stationHz = freqHz
        stationLabel = label
        measuredHz = 0; snrDb = 0; windows = 0; valid = false; strong = false
        // Carrier lands ~+1 kHz in USB — USB on every band (WWV is AM; the
        // sign gate is calibrated for USB).  Freq first, then mode (so the
        // per-band restore the freq change triggers can't override USB).
        Stream.setRx1FreqHz(freqHz - 1000)
        Prefs.mode = "USB"                  // Q_PROPERTY WRITE → syncs demod AND the front-panel Mode
        WdspEngine.setFreqCalMeasuring(true)
        active = true
    }
    function stopMeasure() {
        if (!active) return
        WdspEngine.setFreqCalMeasuring(false)
        Stream.setRx1FreqHz(savedFreq)
        Prefs.mode = savedMode
        active = false
    }
    function applySuggested() {
        if (valid) Stream.setFreqCorrection(suggested)
    }

    Component.onDestruction: stopMeasure()
    onVisibleChanged: if (!visible) stopMeasure()

    Connections {
        target: WdspEngine
        function onFreqCalUpdated(m, snr, win) {
            if (!root.active) return
            root.measuredHz = m
            root.snrDb = snr
            root.windows = win
            var D = Stream.rx1FreqHz
            root.expectedHz = root.stationHz - D
            root.errorHz = m - root.expectedHz
            root.strong = snr >= 10.0 && m > 50.0
            var sane = root.expectedHz > 100 && root.expectedHz < 3000
                       && Math.abs(root.errorHz) < 500
            if (root.strong && sane && D > 1000000) {
                var eHw = (root.stationHz - D - m) / D   // kMeasureSign = +1
                // Compose onto the active correction (the tone already reflects
                // it) so re-calibrating from a non-1.0 factor is exact in one
                // shot — no Reset first.  From 1.0 this is unchanged.
                root.suggested = Stream.freqCorrection * (1.0 / (1.0 + eHw))
                root.valid = true
            } else {
                root.valid = false
            }
        }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Frequency calibration"; color: root.cText
                   font.pixelSize: 15; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { text: "correction " + Number(Stream.freqCorrection).toFixed(8)
                   color: root.cMuted; font.pixelSize: 12 }
        }

        Text { text: "Tap an open band's time station — it tunes USB and measures:"
               color: root.cMuted; font.pixelSize: 12 }

        // A Flow is height-for-width, which Qt Quick Layouts does not support:
        // as a direct Layout child its height feeds back into the width the
        // layout hands it, and when the panel is DOCKED (negotiated width) that
        // cycle recurses until the stack blows.  The Item breaks the cycle —
        // the Flow takes its width from the parent and only reports a height.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: stationFlow.implicitHeight
            Flow {
                id: stationFlow
                width: parent.width
                spacing: 6
                Repeater {
                    model: Time.calStations(Prefs.bandPlanRegion)
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool sel: root.active
                                                    && root.stationLabel === modelData.label
                        width: chipTxt.implicitWidth + 18; height: 26; radius: 13
                        color: sel ? root.cAccent : "#12202b"
                        border.color: sel ? root.cAccent : "#2a4a5a"
                        Text { id: chipTxt; anchors.centerIn: parent
                               text: modelData.label
                               color: parent.sel ? "#04121a" : root.cText
                               font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent
                            onClicked: root.startStation(modelData.freqHz, modelData.label) }
                    }
                }
            }
        }

        // ── A+B hero: carrier marker on a centre-null scale ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 128
            radius: 10
            color: "#0a1218"
            border.color: "#22323d"
            visible: root.active

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: root.valid
                              ? ((root.errorHz >= 0 ? "+" : "") + root.errorHz.toFixed(1))
                              : "—"
                        color: root.valid ? root.colorForError(Math.abs(root.errorHz))
                                          : root.cMuted
                        font.pixelSize: 30; font.bold: true
                    }
                    Text { text: "Hz off"; color: root.cMuted; font.pixelSize: 13
                           Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 5 }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.strong ? ("SNR " + root.snrDb.toFixed(0) + " dB  ✓")
                                          : "listening…"
                        color: root.strong ? root.cGreen : root.cMuted
                        font.pixelSize: 12
                    }
                }

                Item {
                    id: scale
                    Layout.fillWidth: true
                    implicitHeight: 44
                    readonly property real halfSpanHz: 50
                    function xForHz(hz) {
                        var c = Math.max(-halfSpanHz, Math.min(halfSpanHz, hz))
                        return width / 2 + (c / halfSpanHz) * (width / 2)
                    }
                    Rectangle { anchors.fill: parent; radius: 6
                                color: "#0d141b"; border.color: "#22323d" }
                    Rectangle {   // green in-tune band (±2 Hz)
                        y: 4; height: parent.height - 8
                        x: scale.xForHz(-2)
                        width: scale.xForHz(2) - scale.xForHz(-2)
                        color: root.cGreen; opacity: 0.18; radius: 3
                    }
                    Rectangle {   // centre target line
                        width: 2; height: parent.height
                        x: parent.width / 2 - 1; color: root.cGreen
                    }
                    Rectangle {   // the carrier marker (B), colour-coded (A)
                        visible: root.valid
                        y: 3; width: 4; height: parent.height - 6; radius: 2
                        x: scale.xForHz(root.errorHz) - 2
                        color: root.colorForError(Math.abs(root.errorHz))
                        Behavior on x { NumberAnimation { duration: 140
                                        easing.type: Easing.OutCubic } }
                    }
                    Text { text: "−50 Hz"; color: root.cMuted; font.pixelSize: 10
                           anchors.left: parent.left; anchors.leftMargin: 5
                           anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "+50 Hz"; color: root.cMuted; font.pixelSize: 10
                           anchors.right: parent.right; anchors.rightMargin: 5
                           anchors.verticalCenter: parent.verticalCenter }
                }

                Text {
                    Layout.fillWidth: true
                    text: "carrier " + root.measuredHz.toFixed(1) + " Hz  ·  target "
                          + root.expectedHz.toFixed(0) + " Hz  ·  " + root.windows + " win"
                    color: root.cMuted; font.pixelSize: 11
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.active
            Text {
                text: root.valid
                    ? ("→ " + Number(root.suggested).toFixed(8) + "  ("
                       + ((root.suggested - 1) * 1e6 >= 0 ? "+" : "")
                       + ((root.suggested - 1) * 1e6).toFixed(2) + " ppm)")
                    : "waiting for a strong carrier — is the band open?"
                color: root.valid ? root.cText : root.cMuted
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }
            Button { text: "Apply"; enabled: root.valid; onClicked: root.applySuggested() }
            Button { text: "Stop";  onClicked: root.stopMeasure() }
        }

        Text {
            visible: !root.active
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Pick a station above (10 / 15 MHz WWV are usually open by day). "
                  + "Lyra tunes USB ~1 kHz below the carrier, measures the offset, and "
                  + "suggests a correction — watch the marker slide to the green centre, "
                  + "then Apply so the dial reads true. Reset and manual entry are on "
                  + "Settings → Calibration."
            color: root.cMuted; font.pixelSize: 12
        }
    }
}
