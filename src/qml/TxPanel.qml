// Lyra — TX dock panel (front-facing TX controls).
//
// Operator-facing surface for the TX controls that get touched during
// a session: TX Drive % (tuned between QSOs) and MOX (the keying action
// itself).  PA Enable + safety timeout stay in Settings → Hardware →
// Transmit — PA Enable is the deliberate-arm safety gate, and the
// timeout is a set-once config; neither belongs on the operating-time
// front panel.
//
// Visual hierarchy:
//   left                centre                              right
//   [TX label]  [Drive ──●── 25 %]                              [ MOX ]
//
// Antenna jacks (ANT 1/2/3 + RX TRX/BYPS/EXT1/XVTR) live on the Band
// dock — this strip is already packed.
//
// The MOX button is the largest, sits on the right (away from the LED
// on the Tuning dock if they're docked side-by-side), and is lit red
// whenever the wire-MOX bit is high (Stream.moxActive — the post-TR-
// delay truth, NOT the click state — so it stays honest through the
// ~65 ms keydown TR window and through the space-bar momentary).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 64
    implicitWidth: 720
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor, measured from the content — see AudioPanel for the rationale.
    readonly property int lyraMinWidth:  body.implicitWidth + 24
    readonly property int lyraMinHeight: body.implicitHeight + 16

    readonly property color cAccent:   "#00e5ff"
    readonly property color cText:     "#cdd9e5"
    readonly property color cMuted:    "#8a9aac"
    // Desk: this is the laptop microphone into MIC1. Local radio: WDSP TXA.
    readonly property real micGainShown: DeskLink.deskSide
                                         ? DeskLink.micGainDb : Stream.micGainDb
    function commitMicGain(db) {
        var nv = Math.round(db)
        if (nv < -90) nv = -90
        if (nv > 40) nv = 40
        if (DeskLink.deskSide)
            DeskLink.setMicGainDb(nv)
        else
            Stream.setMicGainDb(nv)
    }
    readonly property color cOn:       "#ff9a3c"   // tune-armed orange
    readonly property color cMox:      "#d11515"   // wire-MOX red
    readonly property color cMoxEdge:  "#ff8080"

    // ── Press-intent indicator (task #18) ────────────────────────────
    // Operator-flagged UX gap: a short MOX press (space-bar tap shorter
    // than the ~65 ms TR delay) never lit the button red because the
    // wire-MOX FSM cancelled mid-mox_delay and moxActive never went
    // true — operator wondered "did it key?".  HL2Stream now fires
    // moxIntentPulse() on EVERY requestMox(true) call (click, TUN, or
    // space-bar press, regardless of FSM outcome).  We latch
    // pressIntent for ~220 ms so even a 5 ms tap shows an orange flash,
    // giving the operator instant feedback that their press registered.
    // Wire-MOX-red overrides pressIntent-orange the moment moxActive
    // goes true — the FSM outcome takes visual priority.
    property bool pressIntent: false

    // ── Wire-truth atomics from HL2Stream ────────────────────────────
    // txDriveLevel is raw 0..255; UI works in 0..100 %.  Math:
    //   pct = round(raw * 100 / 255)
    //   raw = round(pct * 255 / 100)
    // The setter clamps + persists + emits txDriveLevelChanged, so the
    // round-trip stays stable (a 25 % click → raw 64 → reads back 25 %).
    function rawToPct(raw) { return Math.round(raw * 100 / 255) }
    function pctToRaw(pct) { return Math.round(pct * 255 / 100) }

    // Press-intent decay timer (#18 — see pressIntent property above).
    Timer {
        id: pressIntentTimer
        interval: 220
        repeat: false
        onTriggered: root.pressIntent = false
    }
    Connections {
        target: Stream
        function onMoxIntentPulse() {
            // restart() (not start()) so back-to-back re-keys extend
            // the visible orange rather than truncating to whatever
            // remained from the previous pulse.
            root.pressIntent = true
            pressIntentTimer.restart()
        }
    }

    RowLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 12

        Label {
            text: qsTr("TX")
            color: root.cAccent
            font.bold: true
            font.pixelSize: 14
            HoverHandler { id: txLblHov }
            LyraHoverHelp {
                title: qsTr("TX")
                text: qsTr("TUN — dummy-load carrier at Drive %% (or the Tune slider if that mode is on). MOX — key. Space-bar uses the same path. VOX / ATT / PROT — lamps; knobs in Settings → TX.")
                hovered: txLblHov.hovered
            }
        }

        Rectangle {
            id: p2StateChip
            visible: P2Bridge.running
            Layout.preferredWidth: 92
            Layout.preferredHeight: 26
            radius: 4
            color: P2Bridge.txFaultLatched ? root.cMox
                 : P2Bridge.txTransmitting ? root.cMox
                 : P2Bridge.txBenchArmed ? "#3a2a14"
                 : "#12252e"
            border.color: P2Bridge.txFaultLatched ? root.cMoxEdge
                        : P2Bridge.txTransmitting ? root.cMoxEdge
                        : P2Bridge.txBenchArmed ? root.cOn
                        : root.cAccent
            border.width: 2
            Text {
                anchors.fill: parent
                anchors.margins: 3
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                clip: true
                text: P2Bridge.txFaultLatched ? qsTr("P2 FAULT")
                    : P2Bridge.txTransmitting ? qsTr("P2 TX")
                    : !P2Bridge.txHardwareSupported ? qsTr("P2 RX ONLY")
                    : P2Bridge.txBenchArmed ? qsTr("P2 ARMED")
                    : P2Bridge.txTransportReady ? qsTr("P2 SAFE")
                    : qsTr("P2 PRIME")
                color: (P2Bridge.txFaultLatched || P2Bridge.txTransmitting)
                    ? "#ffffff"
                    : P2Bridge.txBenchArmed ? root.cOn : root.cAccent
                font.bold: true
                font.pixelSize: 11
            }
            HoverHandler { id: p2StateHover }
            LyraHoverHelp {
                title: qsTr("P2")
                text: P2Bridge.txStatus
                      + qsTr("\nDUC FIFO: %1 samples. Bench drive ceiling: %2%.")
                          .arg(P2Bridge.ducFifoSamples)
                          .arg(P2Bridge.txDriveLimitPercent)
                hovered: p2StateHover.hovered
            }
        }

        Label {
            text: qsTr("Drive"); color: root.cMuted
            HoverHandler { id: driveLblHov }
            LyraHoverHelp {
                title: qsTr("Drive")
                text: driveSlider.helpText
                hovered: driveLblHov.hovered
            }
        }
        LyraSlider {
            id: driveSlider
            Layout.preferredWidth: 90    // shortened to reclaim panel width for the CAP chip + button cluster
            from: 0; to: 100; stepSize: 1; snapMode: Slider.SnapAlways
            value: root.rawToPct(Stream.txDriveLevel)
            onMoved: Stream.setTxDriveLevel(root.pctToRaw(value))
            helpTitle: qsTr("Drive")
            helpText: {
                var t = qsTr("How hard the radio drives its PA. 0 % = no carrier even with PA + MOX. Start LOW (5–10 %) on a dummy load; raise while watching the watt-meter. Wheel ±1 (Shift = 5 %).")
                if (P2Bridge.running && !P2Bridge.txOnAirValidated)
                    t += qsTr(" P2 bench ceiling is %1% (Settings → TX).")
                         .arg(P2Bridge.txDriveLimitPercent)
                return t
            }
            WheelHandler {
                onWheel: (ev) => {
                    var step = (ev.modifiers & Qt.ShiftModifier) ? 5 : 1
                    var nv = driveSlider.value + (ev.angleDelta.y > 0 ? step : -step)
                    nv = Math.max(0, Math.min(100, nv))
                    Stream.setTxDriveLevel(root.pctToRaw(nv))
                }
            }
        }
        Label {
            // Live readback from the wire atomic — so an external
            // change (CAT, persistence reload) updates the readout.
            // Unvalidated P2 clamps analog drive to the bench ceiling
            // (Settings → TX; default 50%): show that, not the slider,
            // when the slider is above the cap.
            readonly property int sliderPct: root.rawToPct(Stream.txDriveLevel)
            readonly property bool p2Capped: P2Bridge.running
                && !P2Bridge.txOnAirValidated
                && P2Bridge.txDriveLimitPercent < sliderPct
            text: (p2Capped ? P2Bridge.txDriveLimitPercent : sliderPct)
                  + qsTr(" %")
            color: p2Capped ? root.cOn : cText
            font.family: "Consolas"
            font.bold: true
            Layout.preferredWidth: 44
            HoverHandler { id: drivePctHov }
            LyraHoverHelp {
                title: qsTr("Drive")
                text: qsTr("Slider %1% — P2 bench ceiling %2% (Settings → TX).")
                    .arg(sliderPct).arg(P2Bridge.txDriveLimitPercent)
                hovered: drivePctHov.hovered && p2Capped
            }
        }

        // ── Mic Gain (dB) — operator tunes between QSOs ────────────
        // TX-1 component 8a — direct operator surface for
        // SetTXAPanelGain1 (the only operator-tunable software gain
        // stage in the WDSP TXA chain, before the bandpass / ALC /
        // everything downstream).  Default 0 dB = WDSP unity = no
        // change vs the lyra-cpp ship-no-setters posture at channel
        // open; typical ESSB headroom dials up via this slider.  ALC
        // ceiling lives in Settings → TX (operator sets the value;
        // it's a safety knob, not a per-QSO tweak).
        Label {
            text: qsTr("Mic"); color: root.cMuted
            HoverHandler { id: micLblHov }
            LyraHoverHelp {
                title: qsTr("Mic")
                text: micGainSlider.helpText
                hovered: micLblHov.hovered
            }
        }
        LyraSlider {
            id: micGainSlider
            Layout.preferredWidth: 90    // matches driveSlider — front-panel sliders kept short
            // Range matches the verified reference's Default TX profile
            // (Max +40, Min -90). On the local radio this is tx/micGainDb.
            // While attached as the desk it is the laptop mic into MIC1
            // (deskRemote/micGainDb) and does not write the host rack.
            from: -90; to: 40; stepSize: 1; snapMode: Slider.SnapAlways
            value: root.micGainShown
            onMoved: root.commitMicGain(value)
            helpTitle: qsTr("Mic")
            helpText: DeskLink.deskSide
                ? qsTr("Laptop microphone level sent to the shack. Changes while you transmit. 0 dB leaves that input unchanged.")
                : qsTr("Mic gain into the TX modulator. 0 dB = unity. Typical SSB +10 to +20 dB — watch ALC (Settings → TX) and back off when it hits hard. Wheel ±1 (Shift = 5 dB).")
            WheelHandler {
                onWheel: (ev) => {
                    var step = (ev.modifiers & Qt.ShiftModifier) ? 5 : 1
                    var nv = micGainSlider.value + (ev.angleDelta.y > 0 ? step : -step)
                    nv = Math.max(-90, Math.min(40, nv))
                    root.commitMicGain(nv)
                }
            }
        }
        Label {
            // Live readback. Desk shows the laptop input; the local
            // radio shows the WDSP TXA mic gain.
            text: (root.micGainShown >= 0 ? "+" : "") +
                  Math.round(root.micGainShown) + qsTr(" dB")
            color: cText
            font.family: "Consolas"
            font.bold: true
            Layout.preferredWidth: 52
        }

        // (No left spacer: the ATT/PROT/CAP lamp cluster sits tight after
        // the Mic readout; the single fill spacer below keeps TUN + MOX
        // pinned to the right edge.)

        // ── ATT-on-TX lamp (§15.31) ─────────────────────────────────
        // Operator-flagged: no visible confirmation that the RX front
        // end is being protected on key-down (Thetis shows the ATT jump
        // to 31).  This lit button sits in the gap between the Mic and
        // Tune sliders; same orange-armed / red-live idiom as the LNA
        // "Auto" lamp + the TUN/MOX buttons.  Gray = disabled; orange =
        // enabled + armed (on RX); red = engaged NOW (wire MOX live, RX
        // front end attenuated).  Click toggles enable (bidirectional
        // with Settings → TX).  Disabling drops RX-ADC protection during
        // TX — default ON.
        Button {
            id: attBtn
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.attOnTxEnabled (never severed by the control), so the
            // lamp always agrees with Settings → TX.  Click = toggle command.
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            checked: Stream.attOnTxEnabled
            onClicked: Stream.setAttOnTxEnabled(!Stream.attOnTxEnabled)
            // engaged = protection actually live on the wire right now
            readonly property bool engaged: Stream.attOnTxEnabled && Stream.moxActive
            // Disabled → "ATT off"; armed (RX) → "ATT 31" (the setpoint
            // it will apply on key-down); engaged (TX) → "ATT -31" (the
            // attenuation now applied to the RX front end — negative to
            // read as a gain reduction, matching the operator's mental
            // model of the LNA dropping to -31 on TX).
            text: !Stream.attOnTxEnabled
                      ? qsTr("ATT off")
                  : attBtn.engaged
                      ? qsTr("ATT -%1").arg(Stream.attOnTxDb)
                      : qsTr("ATT %1").arg(Stream.attOnTxDb)
            font.bold: true
            font.pixelSize: 12
            background: Rectangle {
                // Engaged = solid saturated red with white text (the MOX-
                // live idiom) — NOT a light-salmon-on-dark-maroon blend,
                // which read pink.  Armed = orange (TUN/Auto idiom);
                // disabled = gray.
                radius: 4
                color: attBtn.engaged ? root.cMox
                     : attBtn.checked ? "#3a2a14"
                     : "#161e28"
                border.color: attBtn.engaged ? root.cMoxEdge
                            : attBtn.checked ? root.cOn
                            : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: attBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: attBtn.engaged ? "#ffffff"
                     : attBtn.checked ? root.cOn
                     : root.cText
                font: attBtn.font
                // A custom contentItem drops the style's default eliding, so a
                // squeezed button paints its label straight out over whatever is
                // next to it.  protBtn below already guards against this; the
                // safety-critical controls in this row must too.
                elide: Text.ElideRight
                clip: true
            }
            HoverHandler { id: attHov }
            LyraHoverHelp {
                title: qsTr("ATT on TX")
                text: qsTr("RX front-end pad on key-down. Gray = off; orange = armed; red = engaged now. Click toggles (same as Settings → TX). Default ON.")
                hovered: attHov.hovered
            }
        }

        // ── SWR-protection lamp (#169) ──────────────────────────────
        // Sibling of the ATT lamp.  Gray = disabled; green = armed
        // (enabled, watching reflected power on RX/TX); red = TRIPPED
        // (TX was auto-cut on sustained high SWR — latched until the
        // operator re-keys).  Click toggles enable (bidirectional with
        // Settings → TX).  Unlike the ATT lamp (red on every key-down =
        // normal), PROT is red ONLY on a fault, so red here = "something
        // is wrong with the antenna / feedline."
        Button {
            id: protBtn
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.swrProtectEnabled (never severed by the control), so the
            // lamp always agrees with Settings → TX.  Click = toggle command.
            checkable: false
            implicitWidth: 84
            implicitHeight: 26
            checked: Stream.swrProtectEnabled
            onClicked: Stream.setSwrProtectEnabled(!Stream.swrProtectEnabled)
            readonly property bool tripped: Stream.swrProtectTripped
            text: !Stream.swrProtectEnabled ? qsTr("PROT off")
                : protBtn.tripped            ? Stream.swrProtectReason
                                             : qsTr("PROT")
            font.bold: true
            font.pixelSize: 12
            background: Rectangle {
                radius: 4
                // tripped = solid red alarm (MOX idiom); armed = calm
                // green ("protecting, all good"); disabled = gray.
                color: protBtn.tripped ? root.cMox
                     : protBtn.checked ? "#13301f"
                     : "#161e28"
                border.color: protBtn.tripped ? root.cMoxEdge
                            : protBtn.checked ? "#2e8b57"
                            : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: protBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // Clamp + elide so a long latched reason can NEVER spill
                // out of the lamp and overlap the ATT lamp to its left.
                elide: Text.ElideRight
                clip: true
                color: protBtn.tripped ? "#ffffff"
                     : protBtn.checked ? "#3fd07f"
                     : root.cText
                font: protBtn.font
            }
            HoverHandler { id: protHov }
            LyraHoverHelp {
                title: qsTr("PROT")
                text: qsTr("SWR protection. Gray = off; green = armed; red = tripped (TX cut until you re-key). Click toggles. Knobs in Settings → TX.")
                hovered: protHov.hovered
            }
        }

        // ── Amp-cap (Max Output) indicator ──────────────────────────
        // Visible whenever Max cap is armed — never hide it when Drive is
        // low (that was the "lamp gone" bug). Amber "CAP learn" = this
        // band hasn't finished TUN learning. Cyan "CAP nW" = locked for
        // the set watts. Opacity stays full so the chip doesn't vanish
        // on the dark TX panel when Drive is under the lock.
        Rectangle {
            id: capChip
            visible: Stream.capArmed && Stream.capLimitW > 0
            readonly property bool uncal: Stream.capStatus !== 1
            readonly property bool limiting: Stream.capLimiting
            Layout.preferredWidth: 76    // "CAP learn" / "CAP nW"
            Layout.preferredHeight: 26
            radius: 4
            opacity: 1.0
            color: uncal ? "#3a2a10" : "#12252e"
            border.color: uncal ? "#ffb020" : root.cAccent
            border.width: 2
            Text {
                anchors.centerIn: parent
                text: capChip.uncal ? qsTr("CAP learn")
                                    : qsTr("CAP %1W").arg(Math.round(Stream.capLimitW))
                color: capChip.uncal ? "#ffcf6b" : root.cAccent
                font.bold: true
                font.pixelSize: 12
            }
            HoverHandler { id: capHov }
            LyraHoverHelp {
                title: qsTr("CAP")
                text: {
                    const armed = capChip.uncal
                        ? qsTr("Max Output is armed but this band has not finished TUN learning. Keep Tune keyed into a dummy load until this chip turns cyan. Or turn the cap off in Settings → PA Gain.")
                        : qsTr("Max Output cap locked for this band at %1 W. Raising Drive above that lock does not raise RF.")
                              .arg(Math.round(Stream.capLimitW))
                    return capChip.limiting
                        ? armed
                        : armed + " " + qsTr("Drive/Tune is under the locked ceiling — RF may drop, the cap stays put.")
                }
                hovered: capHov.hovered
            }
        }

        Item { Layout.fillWidth: true }   // right half of the gap → TUN + MOX stay right

        // ── Tune-drive % (Task #74 / #95) — inline operator-tuned drive
        //    level applied only while TUN is armed; visible only in
        //    Settings → TX → Tune drive = "Use Tune slider" so the panel
        //    stays uncluttered for operators who tune at their main TX
        //    drive or a fixed value.  Persists via Prefs.tuneDrivePct.
        Label {
            text: qsTr("Tune")
            color: root.cMuted
            visible: Prefs.tuneDriveMode === 1   // TuneDriveTune (live per-band tune slider)
            HoverHandler { id: tuneLblHov }
            LyraHoverHelp {
                title: qsTr("Tune drive")
                text: qsTr("Drive % applied only while TUN is armed. Per-band. Lyra swaps to this on tune-arm and restores TX Drive % on release.")
                hovered: tuneLblHov.hovered
            }
        }
        // Slider matches the Drive/Mic idiom — drag for fast set,
        // wheel-tunes (Shift = 5 %) for fine, Settings → TX provides
        // typed entry.  Per-band persistence per #74 follow-up.
        LyraSlider {
            id: tuneDriveSlider
            visible: Prefs.tuneDriveMode === 1   // TuneDriveTune (live per-band tune slider)
            Layout.preferredWidth: 110
            from: 0; to: 100; stepSize: 1; snapMode: Slider.SnapAlways
            value: Prefs.tuneDrivePct
            onMoved: Prefs.tuneDrivePct = value
            helpTitle: qsTr("Tune drive")
            helpText: qsTr("Drive % applied only while TUN is armed. Per-band. Watch drv=/iq= on the status line while you drag.")
            WheelHandler {
                onWheel: (ev) => {
                    var step = (ev.modifiers & Qt.ShiftModifier) ? 5 : 1
                    var nv = tuneDriveSlider.value + (ev.angleDelta.y > 0 ? step : -step)
                    nv = Math.max(0, Math.min(100, nv))
                    Prefs.tuneDrivePct = nv
                }
            }
            // External-update mirror (Settings dialog spinbox,
            // persistence reload, per-band recall on band crossing).
            Connections {
                target: Prefs
                function onTuneDrivePctChanged() {
                    if (tuneDriveSlider.value !== Prefs.tuneDrivePct)
                        tuneDriveSlider.value = Prefs.tuneDrivePct
                }
            }
        }
        // Live percent readback next to the slider (matches the
        // Drive/Mic readback idiom).
        Label {
            visible: Prefs.tuneDriveMode === 1   // TuneDriveTune (live per-band tune slider)
            text: Prefs.tuneDrivePct + qsTr(" %")
            color: cText
            font.family: "Consolas"
            font.bold: true
            Layout.preferredWidth: 36
        }

        // ── VOX — voice-operated TX arm/keying lamp (#91) ───────────
        // Sits left of TUN in the right-hand button cluster.  Same
        // green-armed / red-live idiom as the PROT lamp: gray = off;
        // green = armed (the mic gate is listening); red = keying NOW
        // (VOX is holding the wire key on your voice).  Click arms/
        // disarms (bidirectional with Settings → TX, where the
        // threshold / hang / anti-VOX knobs live).  VOX keys voice
        // modes only and never overrides a manual / foot-switch key.
        Button {
            id: voxBtn
            // NOT checkable: `checked` stays a PURE one-way reflection of
            // Stream.voxEnabled and is never written by the control, so the
            // binding can't break (the old checkable+onToggled form broke it
            // on first click → the front-panel lamp desynced from the real
            // VOX state / from the Settings toggle).  Click = a toggle
            // command; the model change flows back through the binding.
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            checked: Stream.voxEnabled
            onClicked: Stream.setVoxEnabled(!Stream.voxEnabled)
            // keying = this gate currently holds the wire key (voice up)
            readonly property bool keying: Stream.voxKeying
            text: !Stream.voxEnabled ? qsTr("VOX off")
                : voxBtn.keying        ? qsTr("VOX ●")   // ● = keyed now
                                       : qsTr("VOX")
            font.bold: true
            font.pixelSize: 12
            background: Rectangle {
                radius: 4
                // keying = solid red alarm (MOX idiom); armed = calm
                // green ("listening"); disabled = gray.
                color: voxBtn.keying  ? root.cMox
                     : voxBtn.checked ? "#13301f"
                     : "#161e28"
                border.color: voxBtn.keying  ? root.cMoxEdge
                            : voxBtn.checked ? "#2e8b57"
                            : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: voxBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: voxBtn.keying  ? "#ffffff"
                     : voxBtn.checked ? "#3fd07f"
                     : root.cText
                font: voxBtn.font
                elide: Text.ElideRight
                clip: true
            }
            HoverHandler { id: voxHov }
            LyraHoverHelp {
                title: qsTr("VOX")
                text: qsTr("Voice-operated TX. Click arms/disarms. Gray = off; green = listening; red = keying on your voice. Threshold / hang in Settings → TX.")
                hovered: voxHov.hovered
            }
        }

        // ── TUN — armed-tune button (carrier @ TX freq + 1 kHz) ─────
        // Clicking arms the host-streamed 1 kHz tone AND requests MOX
        // in a single gesture (the operator's "press to tune" pattern).
        // Clicking again releases MOX through the normal FSM keyup —
        // the moxActiveChanged(false) edge auto-disarms the tune (via
        // HL2Stream's self-wired safety) so the visual goes back
        // gray-orange→gray.  The button visual tracks Stream.tuneEnabled
        // (wire truth) so the auto-clear is reflected immediately.
        //
        // Useful only when PA is enabled (Settings → Hardware → Transmit)
        // AND TX Drive > 0 % — otherwise the carrier exists in the EP2
        // I/Q stream but the gateware DAC scales it to inaudible.
        Button {
            id: tunBtn
            // Qt.NoFocus: a Button with keyboard focus activates on Space
            // (emits clicked) — which would key TX via the spacebar even with
            // "Space bar keys PTT" UNCHECKED (that feature lives wholly in
            // MainWindow::eventFilter).  Keep this off so only the real gated
            // feature can key from Space.  Mouse-click unaffected.  (Precedent:
            // BandPanel.qml.)
            focusPolicy: Qt.NoFocus
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.tuneEnabled (wire truth) so the auto-clear on MOX-drop
            // shows immediately.  Click = toggle command (arm / release).
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            text: qsTr("Tune")
            font.bold: true
            font.pixelSize: 12
            checked: DeskLink.deskSide ? DeskLink.tuneLit : Stream.tuneEnabled
            enabled: !P2Bridge.running
                     || (P2Bridge.txBenchArmed && !P2Bridge.txFaultLatched)
            onClicked: {
                if (DeskLink.deskSide) {
                    DeskLink.requestRemoteTune(!DeskLink.tuneLit)
                    return
                }
                if (!Stream.tuneEnabled) {
                    // Arming: set tone first so the very first EP2 frame
                    // after wire-MOX rises already carries the carrier.
                    Stream.setTuneEnabled(true)
                    Stream.requestMox(true)
                } else {
                    // Disarming: release MOX through the FSM; the
                    // moxActiveChanged(false) edge auto-clears tune.
                    Stream.requestMox(false)
                }
            }
            background: Rectangle {
                radius: 4
                color: (DeskLink.deskSide ? DeskLink.tuneLit : Stream.tuneEnabled) ? "#3a2a14" : "#1f2a35"
                border.color: (DeskLink.deskSide ? DeskLink.tuneLit : Stream.tuneEnabled) ? root.cOn : "#3a5060"
                border.width: 2
            }
            contentItem: Text {
                text: tunBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: (DeskLink.deskSide ? DeskLink.tuneLit : Stream.tuneEnabled) ? root.cOn : root.cText
                font: tunBtn.font
                elide: Text.ElideRight
                clip: true
            }
            HoverHandler { id: tunHov }
            LyraHoverHelp {
                title: qsTr("Tune")
                text: qsTr("Arms a dummy-load carrier at TX frequency and keys MOX. Click again to release. Useful when PA is on and Drive > 0%.")
                hovered: tunHov.hovered
            }
        }

        // ── MOX — the keying button, big + lit red on wire truth ────
        // Funnels through Stream.requestMox (the FSM single funnel —
        // same path as the space-bar momentary in MainWindow).  The
        // lit state tracks Stream.moxActive (wire truth, post-TR-delay
        // settle), NOT the click checked-state, so the visual stays
        // honest through the ~65 ms keydown window.
        Button {
            id: twoToneBtn
            focusPolicy: Qt.NoFocus   // see tunBtn: stop Space-activation keying TX
            checkable: false
            implicitWidth: 72
            implicitHeight: 26
            text: qsTr('2-Tone')
            font.bold: true
            font.pixelSize: 12
            checked: DeskLink.deskSide ? DeskLink.twoToneLit : Stream.twoToneEnabled
            enabled: DeskLink.deskSide
                     ? !(DeskLink.tuneLit && !DeskLink.twoToneLit)
                     : ((!P2Bridge.running
                         || (P2Bridge.txBenchArmed && !P2Bridge.txFaultLatched))
                        && (!Stream.tuneEnabled || Stream.twoToneEnabled))
            onClicked: {
                if (DeskLink.deskSide) {
                    DeskLink.requestRemoteTwoTone(!DeskLink.twoToneLit)
                } else if (!Stream.twoToneEnabled) {
                    Stream.setTwoToneEnabled(true)
                    Stream.requestMox(true)
                } else {
                    Stream.requestMox(false)
                    Stream.setTwoToneEnabled(false)
                }
            }
            background: Rectangle {
                radius: 4
                color: (DeskLink.deskSide ? DeskLink.twoToneLit : Stream.twoToneEnabled) ? '#302044' : '#1f2a35'
                border.color: (DeskLink.deskSide ? DeskLink.twoToneLit : Stream.twoToneEnabled) ? '#d68cff' : '#3a5060'
                border.width: 2
            }
            contentItem: Text {
                text: twoToneBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: (DeskLink.deskSide ? DeskLink.twoToneLit : Stream.twoToneEnabled) ? '#d68cff' : root.cText
                font: twoToneBtn.font
                elide: Text.ElideRight
                clip: true
            }
            HoverHandler { id: twoToneHov }
            LyraHoverHelp {
                title: qsTr("2-Tone")
                text: qsTr("Two-tone test. Keys MOX with a two-tone source for linearity checks. Click again to release.")
                hovered: twoToneHov.hovered
            }
        }

        Button {
            id: moxBtn
            focusPolicy: Qt.NoFocus   // see tunBtn: stop Space-activation keying TX
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.moxActive (wire truth); the lamp already reads moxActive
            // directly, so this just keeps the control from severing it.
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            text: qsTr("MOX")
            font.bold: true
            font.pixelSize: 12
            checked: DeskLink.deskSide
                      ? (DeskLink.moxLit && DeskLink.deskOwnsKey)
                      : Stream.moxActive
            enabled: !P2Bridge.running
                     || (P2Bridge.txBenchArmed && !P2Bridge.txFaultLatched)
            onClicked: {
                if (DeskLink.deskSide)
                    DeskLink.requestRemoteMox(!(DeskLink.moxLit && DeskLink.deskOwnsKey))
                else
                    Stream.requestMox(!Stream.moxActive)
            }
            background: Rectangle {
                // Three-way state: moxActive (wire MOX live) → red;
                // pressIntent (operator pressed within last ~220 ms,
                // wire MOX not yet settled) → tune-armed-style orange-
                // tinted dark fill; otherwise gray.  Matches the TUN
                // armed visual idiom so operators read both buttons
                // the same way.
                color: (DeskLink.deskSide ? (DeskLink.moxLit && DeskLink.deskOwnsKey)
                                      : Stream.moxActive) ? root.cMox
                     : root.pressIntent ? "#3a2a14"
                     : "#1f2a35"
                border.color: (DeskLink.deskSide ? (DeskLink.moxLit && DeskLink.deskOwnsKey)
                                                 : Stream.moxActive) ? root.cMoxEdge
                            : root.pressIntent ? root.cOn
                            : "#3a5060"
                border.width: 2
                radius: 5
            }
            contentItem: Text {
                text: moxBtn.text
                color: (DeskLink.deskSide ? (DeskLink.moxLit && DeskLink.deskOwnsKey)
                                      : Stream.moxActive) ? "#ffffff"
                     : root.pressIntent ? root.cOn
                     : root.cText
                font: moxBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                clip: true
            }
            HoverHandler { id: moxHov }
            LyraHoverHelp {
                title: qsTr("MOX")
                text: qsTr("Manual key. Space-bar uses the same path. Lights red when the wire is actually transmitting.")
                hovered: moxHov.hovered
            }
        }
    }
}
