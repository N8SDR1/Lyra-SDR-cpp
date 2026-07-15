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
        }

        // ── TX Drive % — operator tunes between QSOs ────────────────
        // Help lives on the LABEL, not the slider — a ToolTip popup is
        // window-clamped, so "above the slider" got shoved back onto the
        // track when the panel sits high.  Hover the name instead; the
        // slider stays clear (operator idea 2026-06-20).
        Label {
            text: qsTr("Drive"); color: root.cMuted
            HoverHandler { id: driveLblHov }
            ToolTip.visible: (driveLblHov.hovered) && Prefs.tooltipsEnabled
            ToolTip.delay: 600
            ToolTip.text: qsTr("How hard the HL2 drives its PA.  0 % = no carrier "
                + "even with PA + MOX.  Start LOW (5–10 %) on a dummy load; raise "
                + "gradually while watching the watt-meter.  Wheel adjusts (Shift = 5 %).")
        }
        LyraSlider {
            id: driveSlider
            Layout.preferredWidth: 90    // shortened to reclaim panel width for the CAP chip + button cluster
            from: 0; to: 100; stepSize: 1; snapMode: Slider.SnapAlways
            value: root.rawToPct(Stream.txDriveLevel)
            onMoved: Stream.setTxDriveLevel(root.pctToRaw(value))
            WheelHandler {
                onWheel: (ev) => {
                    var step = (ev.modifiers & Qt.ShiftModifier) ? 5 : 1
                    var nv = driveSlider.value + (ev.angleDelta.y > 0 ? step : -step)
                    nv = Math.max(0, Math.min(100, nv))
                    Stream.setTxDriveLevel(root.pctToRaw(nv))
                }
            }
            // (Help moved to the "Drive" label above — slider stays clear.)
        }
        Label {
            // Live readback from the wire atomic — so an external
            // change (CAT, persistence reload) updates the readout.
            text: root.rawToPct(Stream.txDriveLevel) + qsTr(" %")
            color: cText
            font.family: "Consolas"
            font.bold: true
            Layout.preferredWidth: 44
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
            ToolTip.visible: (micLblHov.hovered) && Prefs.tooltipsEnabled
            ToolTip.delay: 600
            ToolTip.text: qsTr("Mic gain into the TX modulator (WDSP TXA "
                + "PanelGain1).  0 dB = unity.  Raise toward +10 to +20 dB for "
                + "typical SSB voice — watch the ALC meter (Settings → TX → "
                + "ALC ceiling) and back off when ALC engages hard.  Wheel "
                + "adjusts (Shift = 5 dB).  Settings → TX → Mic Gain offers "
                + "a typed spin-box for the same value — both surfaces tune "
                + "the same control bidirectionally.")
        }
        LyraSlider {
            id: micGainSlider
            Layout.preferredWidth: 90    // matches driveSlider — front-panel sliders kept short
            // Range matches the verified reference's Default TX profile
            // (Max +40, Min -90) so the operator's full reference-style
            // travel is preserved.  Live value bidirectionally binds
            // with the Settings → TX Mic Gain spin-box so either
            // surface tunes the same QSettings tx/micGainDb value.
            from: -90; to: 40; stepSize: 1; snapMode: Slider.SnapAlways
            value: Stream.micGainDb
            onMoved: Stream.setMicGainDb(value)
            WheelHandler {
                onWheel: (ev) => {
                    var step = (ev.modifiers & Qt.ShiftModifier) ? 5 : 1
                    var nv = micGainSlider.value + (ev.angleDelta.y > 0 ? step : -step)
                    nv = Math.max(-90, Math.min(40, nv))
                    Stream.setMicGainDb(nv)
                }
            }
            // (Help moved to the "Mic" label above — slider stays clear.)
        }
        Label {
            // Live readback from the persisted Q_PROPERTY — so an
            // external change (Settings dialog, persistence reload,
            // future profile-recall) refreshes the readout.
            text: (Stream.micGainDb >= 0 ? "+" : "") +
                  Math.round(Stream.micGainDb) + qsTr(" dB")
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
            ToolTip.text: qsTr("ATT-on-TX — forces the HL2 step attenuator (RX LNA "
                + "to minimum) while transmitting so TX coupling can't blind the "
                + "RX ADC.  Orange = armed, red = engaged (keyed).  Click to "
                + "toggle (also in Settings → TX); value set there too.  "
                + "Disabling removes RX-ADC protection on TX.")
            ToolTip.delay: 1000
            ToolTip.visible: (hovered && !pressed) && Prefs.tooltipsEnabled
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
            ToolTip.text: protBtn.tripped
                ? qsTr("TX protection acted: %1 (limit %2:1).  Re-key "
                       + "(MOX/TUN) to reset once the antenna/feedline is "
                       + "fixed — it won't recover on its own.")
                      .arg(Stream.swrProtectReason)
                      .arg(Stream.swrProtectLimit.toFixed(1))
                : qsTr("SWR protection — on sustained reflected power "
                       + "above %1:1 it cuts (or folds back) TX.  Limit, "
                       + "action + enable in Settings → TX.  Green = "
                       + "armed, red = acted.  Click to toggle.")
                      .arg(Stream.swrProtectLimit.toFixed(1))
            ToolTip.delay: 1000
            ToolTip.visible: (hovered && !pressed) && Prefs.tooltipsEnabled
        }

        // ── Amp-cap (Max Output) active indicator ───────────────────
        // Sibling of the ATT/PROT lamps, but VISIBLE ONLY when the watts
        // cap is actively holding TX power down on the current band — an
        // invisible Layout item takes zero space, so the panel stays clean
        // the rest of the time.  Amber "CAP ~30%" = the trap: cap on but
        // this band isn't TUN-calibrated, so Lyra clamps to a safe ~30 %
        // drive and power reads LOW (Pierre HS0ZRT's 6 W-cap-but-3 W-out).
        // Cyan "CAP nW" = cap holding a calibrated band at the set watts
        // (working as intended).  Purely informational (not a toggle) —
        // the cap lives in Settings → PA Gain.
        Rectangle {
            id: capChip
            visible: Stream.capStatus > 0
            readonly property bool uncal: Stream.capStatus === 2
            Layout.preferredWidth: 68    // sized to match the ATT/PROT lamps
            Layout.preferredHeight: 26
            radius: 4
            color: uncal ? "#3a2a10" : "#12252e"
            border.color: uncal ? "#ffb020" : root.cAccent
            border.width: 2
            Text {
                anchors.centerIn: parent
                text: capChip.uncal ? qsTr("CAP ~30%")
                                    : qsTr("CAP %1W").arg(Math.round(Stream.capLimitW))
                color: capChip.uncal ? "#ffcf6b" : root.cAccent
                font.bold: true
                font.pixelSize: 12
            }
            HoverHandler { id: capHov }
            ToolTip.visible: capHov.hovered && Prefs.tooltipsEnabled
            ToolTip.delay: 800
            ToolTip.text: capChip.uncal
                ? qsTr("Max Output cap is ON but this band isn't calibrated, "
                       + "so TX is limited to a safe ~30% drive — your power "
                       + "reads LOW.  Fix: Settings → PA Gain → measure Full "
                       + "Output + TUN each band, or turn the cap off there.")
                : qsTr("Max Output cap is holding this band at your set limit "
                       + "(%1 W).  Adjust or disable in Settings → PA Gain.")
                      .arg(Math.round(Stream.capLimitW))
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
            ToolTip.visible: (tuneLblHov.hovered) && Prefs.tooltipsEnabled
            ToolTip.delay: 600
            ToolTip.text: qsTr("Drive %% applied while TUN is armed. "
                + "Per-band remembered (tune-into-amp on 80 m vs tune-"
                + "into-tuner on 10 m get their own settings).  Lyra "
                + "swaps to this on tune-arm and restores your main "
                + "TX Drive %% on tune-release.  Wheel adjusts "
                + "(Shift = 5 %%); Settings → TX → Tune Drive offers "
                + "typed entry.")
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
            // (Help moved to the "Tune" label above — slider stays clear.)
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
            ToolTip.text: qsTr("VOX — voice-operated transmit.  Green = armed "
                + "(listening to your mic); red = keying now.  Keys TX when you "
                + "speak and drops after the hang time.  Voice modes only; never "
                + "overrides a manual / foot-switch key.  Threshold, hang time + "
                + "anti-VOX live in Settings → TX.  Click to arm / disarm.")
            ToolTip.delay: 1000
            ToolTip.visible: (hovered && !pressed) && Prefs.tooltipsEnabled
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
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.tuneEnabled (wire truth) so the auto-clear on MOX-drop
            // shows immediately.  Click = toggle command (arm / release).
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            text: qsTr("Tune")
            font.bold: true
            font.pixelSize: 12
            checked: Stream.tuneEnabled
            onClicked: {
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
                color: Stream.tuneEnabled ? "#3a2a14" : "#1f2a35"
                border.color: Stream.tuneEnabled ? root.cOn : "#3a5060"
                border.width: 2
            }
            contentItem: Text {
                text: tunBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Stream.tuneEnabled ? root.cOn : root.cText
                font: tunBtn.font
                elide: Text.ElideRight
                clip: true
            }
            ToolTip.text: qsTr("Tune carrier — emits a 1 kHz complex tone in the "
                + "TX I/Q stream and keys MOX.  Carrier power = TX Drive %.  "
                + "Auto-disarms when MOX clears for any reason (release, "
                + "safety timer, etc.).  Set Drive low for first session.")
            // Tooltip was intercepting clicks on the most-pressed button on
            // the panel — long delay + hide-on-press so a fast tune-arm
            // gesture isn't fighting a popup that's already up.
            ToolTip.delay: 1500
            ToolTip.visible: (hovered && !pressed) && Prefs.tooltipsEnabled
        }

        // ── MOX — the keying button, big + lit red on wire truth ────
        // Funnels through Stream.requestMox (the FSM single funnel —
        // same path as the space-bar momentary in MainWindow).  The
        // lit state tracks Stream.moxActive (wire truth, post-TR-delay
        // settle), NOT the click checked-state, so the visual stays
        // honest through the ~65 ms keydown window.
        Button {
            id: moxBtn
            // NOT checkable: `checked` stays a pure one-way reflection of
            // Stream.moxActive (wire truth); the lamp already reads moxActive
            // directly, so this just keeps the control from severing it.
            checkable: false
            implicitWidth: 64
            implicitHeight: 26
            text: qsTr("MOX")
            font.bold: true
            font.pixelSize: 12
            checked: Stream.moxActive
            onClicked: Stream.requestMox(!Stream.moxActive)
            background: Rectangle {
                // Three-way state: moxActive (wire MOX live) → red;
                // pressIntent (operator pressed within last ~220 ms,
                // wire MOX not yet settled) → tune-armed-style orange-
                // tinted dark fill; otherwise gray.  Matches the TUN
                // armed visual idiom so operators read both buttons
                // the same way.
                color: Stream.moxActive ? root.cMox
                     : root.pressIntent ? "#3a2a14"
                     : "#1f2a35"
                border.color: Stream.moxActive ? root.cMoxEdge
                            : root.pressIntent ? root.cOn
                            : "#3a5060"
                border.width: 2
                radius: 5
            }
            contentItem: Text {
                text: moxBtn.text
                color: Stream.moxActive ? "#ffffff"
                     : root.pressIntent ? root.cOn
                     : root.cText
                font: moxBtn.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                clip: true
            }
            ToolTip.text: qsTr("Key the radio (MOX).  Goes red the instant the "
                + "wire-MOX bit settles after the TR-delay.  Space-bar momentary "
                + "(when no text widget has focus) routes through the same FSM.")
            // Operator-reported 2026-05-29: a tooltip that pops the instant
            // the cursor lands on MOX intercepts the click — you end up
            // hunting for the bottom edge of the button to dodge it.  Long
            // delay + hide-on-press means hover-with-intent still gets the
            // hint, but key-the-radio gestures are instant.
            ToolTip.delay: 1500
            ToolTip.visible: (hovered && !pressed) && Prefs.tooltipsEnabled
        }
    }
}
