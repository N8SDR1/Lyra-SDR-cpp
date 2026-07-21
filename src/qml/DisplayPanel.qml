// Lyra — Display dock panel (front-facing display controls).
//
// Mirrors old Lyra's DISPLAY (ViewPanel): the few spectrum/waterfall
// controls the operator reaches for mid-QSO live in this thin panel,
// one click away instead of buried in Settings.  Everything here ALSO
// lives in Settings → Visuals and binds the SAME Prefs object, so the
// two stay in sync (single source of truth) — exactly the old-Lyra
// arrangement.
//
// SCOPE (operator-locked 2026-05-22): only controls that were on old
// Lyra's FRONT Display panel AND exist in the C++ build today belong
// here.  Currently Zoom + Spec FPS + Waterfall rate + Peak Hold,
// arranged in old Lyra's 3-row grid (see the GridLayout comment below).
// Palette, smoothing, glow, gridline etc. stay in Settings → Visuals
// (that's where they were in old Lyra — don't promote them here).
// Panafall Step + Exact/100 Hz slot into the row-1 left cells when those
// features land in the C++ build.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 100
    implicitWidth: 700
    color: "#101820"
    border.color: "#2a4a5a"

    // Panadapter zoom presets (old-Lyra ZOOM_LEVELS).  Mirrors old Lyra's
    // Zoom row: a preset COMBO for fast jumps + a FINE slider for
    // in-between values + a live "N.Nx" readout.  All drive Prefs.zoom.
    readonly property var zoomLevels: [1, 2, 4, 8, 16]
    function zoomIndex(z) {
        var best = 0
        for (var i = 1; i < zoomLevels.length; ++i)
            if (Math.abs(zoomLevels[i] - z) < Math.abs(zoomLevels[best] - z))
                best = i
        return best
    }

    // Peak-hold combo values (old-Lyra encoding): 0 Off, -2 Live, N timed
    // seconds, -1 Hold (infinite).  Index-aligned with the combo model.
    readonly property var peakHoldVals: [0, -2, 1, 2, 5, 10, 30, -1]
    function peakHoldIndex(s) {
        var i = peakHoldVals.indexOf(s)
        return i < 0 ? 0 : i
    }
    // Decay rate presets (dB/s): Fast / Med / Slow.
    readonly property var peakDecayVals: [45, 10, 3]
    function peakDecayIndex(d) {
        var best = 0
        for (var i = 1; i < peakDecayVals.length; ++i)
            if (Math.abs(peakDecayVals[i] - d) < Math.abs(peakDecayVals[best] - d))
                best = i
        return best
    }

    // Old-Lyra DISPLAY layout is a 3-row grid (ViewPanel zoom_grid):
    //   Row 0 : Zoom (combo + fine slider + live readout)
    //   Row 1 : [Panafall Step + Exact — not built yet] | Spec FPS (right)
    //   Row 2 : Peak Hold + Decay + Clear (left)         | WF rate (right)
    // We mirror those exact row/column slots; the row-1 left cells stay
    // empty until Panafall Step / Exact-100 Hz land in the C++ build.
    // FILL, don't anchor-left.  Anchored to the left edge only, the grid keeps
    // its implicit width no matter how narrow the panel gets — it doesn't shrink,
    // it just runs off the right edge and the QQuickWidget viewport clips it.
    // The Spec-FPS and WF-rate controls (columns 4-6) then aren't small, they're
    // GONE: no scrollbar, no cue, unreachable.  Filled, the layout can give way,
    // and the three sliders below yield first (fillWidth with a floor).
    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        columns: 7
        rowSpacing: 7
        columnSpacing: 6

        // ── Row 0: Panadapter zoom (preset combo + fine slider + N.Nx) ──
        Label {
            Layout.row: 0; Layout.column: 0
            text: qsTr("Zoom"); color: "#cccccc"; font.bold: true
        }
        LyraComboBox {
            id: zoomCombo
            Layout.row: 0; Layout.column: 1
            Layout.preferredWidth: 64
            model: ["1x", "2x", "4x", "8x", "16x"]
            currentIndex: root.zoomIndex(Prefs.zoom)
            onActivated: Prefs.zoom = root.zoomLevels[currentIndex]
        }
        LyraSlider {
            id: zoomSlider
            Layout.row: 0; Layout.column: 2
            Layout.preferredWidth: 110
            Layout.fillWidth: true          // the slider yields, not the panel
            Layout.minimumWidth: 60
            from: 10; to: 160; stepSize: 1     // slider int = zoom × 10
            value: Prefs.zoom * 10
            onMoved: Prefs.zoom = Math.round(value) / 10
            WheelHandler {
                onWheel: (ev) => {
                    Prefs.zoom = Math.max(1.0, Math.min(16.0,
                        Math.round(Prefs.zoom * 10
                            + (ev.angleDelta.y > 0 ? 1 : -1)) / 10))
                }
            }
        }
        Label {
            Layout.row: 0; Layout.column: 3
            text: Prefs.zoom.toFixed(1) + qsTr("x")
            color: "#cdd9e5"; font.family: "Consolas"; font.bold: true
            Layout.preferredWidth: 40
        }

        // ── Row 1 (left group): Panafall scroll step + Exact/100 Hz ──
        // Panafall step = how far each mouse-wheel tick tunes over the
        // panadapter/waterfall (coarse band skim — distinct from the fine
        // VFO step on the Tuning panel).  Exact/100 Hz quantizes the
        // result of click/drag/wheel tuning to the 100 Hz grid when on.
        Label {
            Layout.row: 1; Layout.column: 0
            text: qsTr("Panafall"); color: "#cccccc"; font.bold: true
        }
        LyraComboBox {
            id: stepCombo
            Layout.row: 1; Layout.column: 1
            Layout.preferredWidth: 84
            readonly property var steps: [1, 10, 50, 500, 1000, 5000, 10000, 25000, 100000]
            model: ["1 Hz", "10 Hz", "50 Hz", "500 Hz", "1 kHz", "5 kHz",
                    "10 kHz", "25 kHz", "100 kHz"]
            currentIndex: Math.max(0, steps.indexOf(Prefs.panScrollStepHz))
            onActivated: Prefs.panScrollStepHz = steps[currentIndex]
            // No hover tooltip — the popup covered the choice list / the
            // control and swallowed clicks.  "Panafall" label + values are
            // self-explanatory; details are in the User Guide.
        }
        Button {
            Layout.row: 1; Layout.column: 2
            Layout.preferredWidth: 72
            checkable: true
            checked: Prefs.panRound100
            // Label is self-explanatory (Exact ⇄ 100 Hz); no hover tooltip
            // here — it popped over the button and swallowed the click.
            text: Prefs.panRound100 ? qsTr("100 Hz") : qsTr("Exact")
            onToggled: Prefs.panRound100 = checked
        }

        // ── Row 1 (right group): Spectrum frame rate ──
        Label {
            Layout.row: 1; Layout.column: 4
            text: qsTr("Spec"); color: "#cccccc"; font.bold: true
        }
        LyraSlider {
            id: fpsSlider
            Layout.row: 1; Layout.column: 5
            Layout.preferredWidth: 140
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            from: 5; to: 120; stepSize: 1
            value: Prefs.targetFps
            onMoved: Prefs.targetFps = Math.round(value)
            // Mouse-wheel nudges ±1 (sets the bound Prefs source so the
            // value binding stays intact — never assign slider.value).
            WheelHandler {
                onWheel: (ev) => {
                    Prefs.targetFps = Math.max(fpsSlider.from,
                        Math.min(fpsSlider.to,
                            Prefs.targetFps + (ev.angleDelta.y > 0 ? 1 : -1)))
                }
            }
        }
        Label {
            Layout.row: 1; Layout.column: 6
            text: Prefs.targetFps + qsTr(" fps")
            color: "#cdd9e5"; font.family: "Consolas"; font.bold: true
            Layout.preferredWidth: 56
        }

        // ── Row 2 (left group): Peak-hold markers ──
        Label {
            Layout.row: 2; Layout.column: 0
            text: qsTr("Peak Hold"); color: "#cccccc"; font.bold: true
        }
        LyraComboBox {
            id: peakCombo
            Layout.row: 2; Layout.column: 1
            Layout.preferredWidth: 80
            model: ["Off", "Live", "1 s", "2 s", "5 s", "10 s", "30 s", "Hold"]
            currentIndex: root.peakHoldIndex(Prefs.peakHoldSecs)
            onActivated: {
                Prefs.peakHoldSecs = root.peakHoldVals[currentIndex]
                Prefs.peakEnabled = (Prefs.peakHoldSecs !== 0)
            }
        }
        RowLayout {
            Layout.row: 2; Layout.column: 2
            spacing: 6
            Label { text: qsTr("Decay"); color: "#cccccc" }
            LyraComboBox {
                id: decayCombo
                Layout.preferredWidth: 72
                model: ["Fast", "Med", "Slow"]
                currentIndex: root.peakDecayIndex(Prefs.peakDecayDbps)
                // Decay only applies to the timed hold modes.
                enabled: Prefs.peakHoldSecs > 0
                onActivated: Prefs.peakDecayDbps = root.peakDecayVals[currentIndex]
            }
            Button {
                text: qsTr("Clear")
                Layout.preferredWidth: 60
                // Off has nothing to clear; Live re-seeds itself every tick.
                enabled: Prefs.peakEnabled && Prefs.peakHoldSecs !== -2
                onClicked: Prefs.requestClearPeaks()
            }
        }

        // ── Row 2 (right group): Waterfall scroll rate ──
        Label {
            Layout.row: 2; Layout.column: 4
            text: qsTr("WF"); color: "#cccccc"; font.bold: true
        }
        LyraSlider {
            id: wfSlider
            Layout.row: 2; Layout.column: 5
            Layout.preferredWidth: 140
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            from: 1; to: 120; stepSize: 1
            value: Prefs.waterfallSpeed
            onMoved: Prefs.waterfallSpeed = Math.round(value)
            WheelHandler {
                onWheel: (ev) => {
                    Prefs.waterfallSpeed = Math.max(wfSlider.from,
                        Math.min(wfSlider.to,
                            Prefs.waterfallSpeed + (ev.angleDelta.y > 0 ? 1 : -1)))
                }
            }
        }
        Label {
            Layout.row: 2; Layout.column: 6
            text: Prefs.waterfallSpeed + qsTr(" rows/s")
            color: "#cdd9e5"; font.family: "Consolas"; font.bold: true
            Layout.preferredWidth: 64
        }
    }
}
