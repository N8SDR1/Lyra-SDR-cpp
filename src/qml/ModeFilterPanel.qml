// Lyra — Filters dock panel.
//
// RX + TX filter bandwidth (per-mode presets, remembered per mode via the
// shared Prefs object) + IQ sample Rate + the 🔗 RX↔TX BW lock.  The
// bandwidth combos drive WdspEngine.bandwidth / Prefs.txBandwidth using
// the sideband-correct passband convention.
//
// NOTE: the Mode picker MOVED to the Tuning dock (under the VFO) in the
// VFO-cluster layout — so this panel is filters-only.  The
// Prefs.mode → WdspEngine.mode binding moved there with it.  Dock title
// is "Filters" (set in mainwindow.cpp addQuickDock); the file keeps its
// ModeFilterPanel.qml name to avoid churning the qml_module + qrc paths.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 50
    implicitWidth: 580
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor, measured from the content — see AudioPanel for the rationale.
    readonly property int lyraMinWidth:  body.implicitWidth + 24
    readonly property int lyraMinHeight: body.implicitHeight + 12

    // Mode list + per-mode bandwidth presets (Hz), from old Lyra.
    readonly property var modes: ["LSB", "USB", "CWL", "CWU",
                                  "DSB", "AM", "SAM", "FM", "DIGU", "DIGL"]
    readonly property var bwSSB: [1500, 1800, 2100, 2400, 2700, 3000,
                                  3600, 4000, 6000, 8000, 10000]
    readonly property var bwCW:  [50, 100, 150, 250, 400, 500, 750, 1000]
    readonly property var bwDSB: [3000, 4000, 5000, 6000, 8000, 10000]
    readonly property var bwAM:  [3000, 4000, 6000, 8000, 10000, 12000, 16000, 20000]
    // FM RX presets are the FM CHANNEL widths the IF must pass, not SSB
    // widths: narrow FM (±2.5 kHz dev) ≈ 12 k, wide FM (±5 kHz dev) ≈ 16 k
    // (Carson: 2·(dev + 3 kHz audio)).  8/10 cover data/very-narrow.
    readonly property var bwFM:  [8000, 10000, 12000, 16000]
    // FM derives its occupied TX width from deviation (audio is brick-walled
    // to 3 kHz ahead of the modulator), so the TX-BW selector + RX↔TX lock
    // are meaningless in FM and are disabled there.
    readonly property bool isFm: Prefs.mode === "FM"
    readonly property var bwDIG: [1500, 2400, 3000, 3600, 4000, 6000]

    function presetsFor(mode) {
        if (mode === "LSB" || mode === "USB") return bwSSB
        if (mode === "CWL" || mode === "CWU") return bwCW
        if (mode === "DSB") return bwDSB
        if (mode === "AM" || mode === "SAM") return bwAM
        if (mode === "FM")  return bwFM
        if (mode === "DIGL" || mode === "DIGU") return bwDIG
        return bwSSB
    }
    function fmtBw(hz) {
        return hz >= 1000 ? (hz / 1000).toFixed(hz % 1000 ? 1 : 0) + " k"
                          : hz + " Hz"
    }
    // Nearest preset index for the current bandwidth (handles a stored
    // value that isn't an exact preset).
    function bwIndex(mode, bw) {
        var p = presetsFor(mode), best = 0
        for (var i = 1; i < p.length; ++i)
            if (Math.abs(p[i] - bw) < Math.abs(p[best] - bw)) best = i
        return best
    }
    // The RX BW combo shows the ACTUAL bandwidth — including non-preset
    // values from dragging the panadapter passband edge (old-Lyra parity):
    // a value that isn't a preset is shown as a "(custom)" entry at the
    // top of the list and selected.  Picking a real preset drops it.
    function bwIsPreset(mode, bw) {
        var p = presetsFor(mode)
        for (var i = 0; i < p.length; ++i) if (p[i] === bw) return true
        return false
    }
    function bwModel(mode, bw) {
        var labels = presetsFor(mode).map(fmtBw)
        if (bwIsPreset(mode, bw)) return labels
        return [fmtBw(bw) + qsTr(" (custom)")].concat(labels)
    }
    function bwCurrentIndex(mode, bw) {
        var p = presetsFor(mode)
        if (!bwIsPreset(mode, bw)) return 0          // custom entry at front
        for (var i = 0; i < p.length; ++i) if (p[i] === bw) return i
        return 0
    }
    // Map the picked combo index back to a bandwidth (Hz).  `bw` is the
    // CURRENT bandwidth the model was built from, so the index offset for
    // the optional leading "(custom)" entry is consistent.
    function bwValueAt(mode, bw, idx) {
        var p = presetsFor(mode)
        if (bwIsPreset(mode, bw)) return p[idx]
        return idx === 0 ? bw : p[idx - 1]           // idx 0 = the custom entry
    }

    // IQ sample rates (96/192/384 k — 48 k excluded, EP2 cadence).
    readonly property var rateVals: [96000, 192000, 384000]
    function rateIndex(r) {
        var i = rateVals.indexOf(r)
        return i < 0 ? 1 : i
    }
    // A rate switch touches three places: Prefs (persist), the HL2 wire
    // speed bits (Stream), and the WDSP channel + analyzer (Engine).
    function applyRate(r) {
        Prefs.sampleRate = r
        Stream.setSampleRate(r)
        WdspEngine.setSampleRate(r)
    }
    // Push the persisted rate to the wire + DSP at startup (before the
    // channel opens on Start, so it comes up at the right rate).
    Component.onCompleted: applyRate(Prefs.sampleRate)

    // RX bandwidth is the single source of truth via Prefs; the engine
    // follows.  (Engine pushes RXASetPassband on change.)  The
    // Prefs.mode → WdspEngine.mode binding lives on the Tuning dock now
    // (it owns the Mode picker).
    Binding { target: WdspEngine; property: "bandwidth"; value: Prefs.rxBandwidth }
    // TX Component 8c — TX BW push lives in C++ main.cpp (a
    // QObject::connect on Prefs::txBandwidthChanged → Stream.setTxBwHz)
    // so it fires regardless of whether this QML panel is loaded.
    // No QML binding needed here.

    RowLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        Label { text: qsTr("Rate"); color: "#cccccc"; font.bold: true }
        LyraComboBox {
            id: rateCombo
            Layout.preferredWidth: 76
            model: ["96 k", "192 k", "384 k"]
            currentIndex: root.rateIndex(Prefs.sampleRate)
            onActivated: root.applyRate(root.rateVals[currentIndex])
        }

        Rectangle {   // divider
            Layout.preferredWidth: 1
            Layout.topMargin: 10; Layout.bottomMargin: 10
            Layout.fillHeight: true
            color: "#2a4a5a"
        }

        Label { text: qsTr("RX BW"); color: "#cccccc"; font.bold: true }
        LyraComboBox {
            id: bwCombo
            Layout.preferredWidth: 120
            // Rebuilds on mode change (per-mode presets) AND on bandwidth
            // change, so a dragged non-preset BW shows as "(custom)".
            model: root.bwModel(Prefs.mode, Prefs.rxBandwidth)
            currentIndex: root.bwCurrentIndex(Prefs.mode, Prefs.rxBandwidth)
            onActivated: Prefs.rxBandwidth =
                root.bwValueAt(Prefs.mode, Prefs.rxBandwidth, currentIndex)
        }

        // TX Component 8c — 🔗 RX↔TX BW lock.  Sits BETWEEN the RX and
        // TX combos (the glyph alone carries the meaning; no label).
        // Checkable toggle wired to Prefs.bwLocked: when ON, every BW
        // change on either side mirrors to the other (handled in
        // Prefs::setRxBandwidth / setTxBandwidth).  Toggling ON pulls
        // RX into TX so the operator's audible RX BW becomes the TX BW
        // — matches old-Lyra's lock-on direction.
        Button {
            id: bwLockBtn
            Layout.preferredWidth: 32
            checkable: true
            checked: Prefs.bwLocked
            enabled: !root.isFm           // no TX-BW to lock to in FM
            text: "🔗"
            ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            ToolTip.text: root.isFm
                ? qsTr("N/A in FM — TX bandwidth is set from deviation")
                : qsTr("Lock TX BW to RX BW (mirrors both directions when ON)")
            onToggled: Prefs.bwLocked = checked
        }

        Label {
            text: qsTr("TX BW"); font.bold: true
            color: root.isFm ? "#666666" : "#cccccc"
        }
        LyraComboBox {
            id: txBwCombo
            Layout.preferredWidth: 120
            enabled: !root.isFm           // FM TX width is auto (deviation-derived)
            // In FM, show the auto-derived occupied channel (Carson:
            // 2·(deviation + 3 kHz audio)) instead of an editable preset —
            // it's a readout, not a control.
            model: root.isFm
                ? [root.fmtBw(2 * (Stream.fmDeviationHz + 3000)) + qsTr(" (auto)")]
                : root.bwModel(Prefs.mode, Prefs.txBandwidth)
            currentIndex: root.isFm ? 0
                : root.bwCurrentIndex(Prefs.mode, Prefs.txBandwidth)
            onActivated: if (!root.isFm) Prefs.txBandwidth =
                root.bwValueAt(Prefs.mode, Prefs.txBandwidth, currentIndex)
            ToolTip.visible: (root.isFm && hovered) && Prefs.tooltipsEnabled
            ToolTip.text: qsTr("FM occupies ±(deviation + 3 kHz audio); set it with the Dev control")
        }

        Item { Layout.fillWidth: true }
    }
}
