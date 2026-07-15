// Lyra — DSP + AUDIO dock panel.
//
// Laid out to match old Lyra's DSP+AUDIO panel: THREE rows.
//   Row 1 — LEVELS:  LNA · Auto · AF · Vol · MUTE · Bal · Out
//   Row 2 — DSP:     [NB BIN NR ANF LMS SQ APF NF] · notch counter · AGC
//   Row 3 — NR:      NR Mode · AEPF · NPE · Cap · source · DSP Settings…
//
// Controls whose WDSP backend is wired ship live (Vol, MUTE, NR enable,
// NR Mode 1-4, AEPF, NPE, AGC-mode cycle).  Controls whose backend is
// still being ported render PRESENT-BUT-DISABLED with an "arrives in a
// later build" tooltip — the geometry matches old Lyra now so nothing
// reflows as each backend lands.  Context property WdspEngine is
// provided by the host.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 118
    implicitWidth: 860
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor, measured from the content — NOT the same thing as the
    // implicit size above, which is only a preferred size.  MainWindow pushes
    // these to Qt as the dock's real minimum, so a row can't come up too small
    // and quietly clip controls off the edge.  Derived from the layout rather
    // than hard-coded so it stays true as the panel changes.
    readonly property int lyraMinWidth:  body.implicitWidth + 16
    readonly property int lyraMinHeight: body.implicitHeight + 16

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cDim:    "#5a7080"
    readonly property color cOn:     "#ff9a3c"   // "engaged" orange (old-Lyra dsp_btn)

    // Small toggle button matching old Lyra's dsp_btn (orange when on).
    component DspToggle: Button {
        id: btn
        property string note: ""
        implicitWidth: 42
        implicitHeight: 24
        checkable: true
        font.pixelSize: 12
        ToolTip.text: note
        ToolTip.visible: (hovered && note.length > 0) && Prefs.tooltipsEnabled
        ToolTip.delay: 400
        background: Rectangle {
            radius: 3
            color: btn.checked ? "#3a2a14" : "#161e28"
            border.color: btn.checked ? "#ff9a3c"
                          : (btn.enabled ? "#2a3a4a" : "#1c2630")
        }
        contentItem: Text {
            text: btn.text
            color: !btn.enabled ? "#5a7080"
                   : (btn.checked ? "#ff9a3c" : "#cdd9e5")
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: btn.font
            // A custom contentItem drops the style's default eliding, so a
            // squeezed toggle paints its label out over its neighbours.
            elide: Text.ElideRight
            clip: true
        }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        // ── Row 1 — LEVELS ──────────────────────────────────────────
        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("LNA"); color: root.cMuted }
            LyraSlider {
                id: lnaSlider
                Layout.preferredWidth: 120
                from: -12; to: 48; stepSize: 1; snapMode: Slider.SnapAlways
                value: Stream.lnaGainDb
                onMoved: Stream.setLnaGainDb(value)
                ToolTip.text: qsTr("LNA — RF input gain on the HL2's AD9866 PGA, −12…+48 dB.\n"
                    + "Higher = more sensitivity; back off on strong bands to avoid ADC overload.\n"
                    + "The S-meter compensates automatically, so changing LNA won't shift the reading.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                WheelHandler {
                    onWheel: (ev) => Stream.setLnaGainDb(
                        Stream.lnaGainDb + (ev.angleDelta.y > 0 ? 1 : -1))
                }
            }
            Label {
                text: (Stream.lnaGainDb > 0 ? "+" : "") + Stream.lnaGainDb + qsTr(" dB")
                color: root.cText; font.family: "Consolas"; Layout.preferredWidth: 48
            }
            Button {
                id: autoBtn
                text: qsTr("Auto")
                checkable: true
                checked: Stream.autoLna
                onToggled: Stream.setAutoLna(checked)
                implicitWidth: 46; implicitHeight: 24
                background: Rectangle {
                    radius: 3
                    color: autoBtn.checked ? "#3a2a14" : "#161e28"
                    border.color: autoBtn.checked ? root.cOn : "#2a3a4a"
                    border.width: 1
                }
                contentItem: Text {
                    text: autoBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: autoBtn.checked ? root.cOn : root.cText
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("Auto-LNA — backs the LNA off on ADC overload and "
                    + "creeps it back when the band clears (rides the overload edge).\n"
                    + "Auto roams freely; your manual setting is restored when you turn it off.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            // ADC-overload lamp — red when the HL2 front end is clipping.
            Rectangle {
                width: 10; height: 10; radius: 5
                color: Stream.adcOverload ? "#ff4040" : "#26323c"
                border.width: 1
                border.color: Stream.adcOverload ? "#ff8080" : "#33424e"
                ToolTip.text: qsTr("ADC overload — the HL2 front end is clipping. "
                    + "Reduce LNA or enable Auto.")
                ToolTip.visible: (ovLampMa.containsMouse) && Prefs.tooltipsEnabled
                MouseArea { id: ovLampMa; anchors.fill: parent; hoverEnabled: true }
            }

            Item { width: 12 }

            Label { text: qsTr("AF"); color: root.cMuted }
            LyraSlider {
                id: afSlider
                Layout.preferredWidth: 84
                from: 0; to: 40; stepSize: 1; snapMode: Slider.SnapAlways
                value: WdspEngine.afGainDb
                onMoved: WdspEngine.setAfGainDb(value)
                ToolTip.text: qsTr("AF makeup gain (0…+40 dB) — pre-Volume output "
                    + "trim. Set a comfortable level here, then ride Vol on top.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { text: "+" + Math.round(WdspEngine.afGainDb) + qsTr(" dB")
                    color: root.cText; font.family: "Consolas"
                    Layout.preferredWidth: 44 }

            Item { width: 10 }

            // ── Volume + MUTE — LIVE (RX1) ──
            Label { text: qsTr("Vol"); color: root.cMuted }
            LyraSlider {
                id: volSlider
                Layout.preferredWidth: 150
                from: 0.0; to: 1.0
                value: WdspEngine.volume
                onMoved: WdspEngine.setVolume(value)
                WheelHandler {
                    onWheel: (ev) => {
                        var nv = WdspEngine.volume
                                 + (ev.angleDelta.y > 0 ? 0.02 : -0.02)
                        WdspEngine.setVolume(Math.max(0.0, Math.min(1.0, nv)))
                    }
                }
            }
            Label {
                text: WdspEngine.volume <= 0.0
                      ? qsTr("-∞ dB")
                      : Math.round(WdspEngine.volumeDb) + qsTr(" dB")
                color: root.cText; font.family: "Consolas"
                Layout.preferredWidth: 52
            }
            Button {
                text: WdspEngine.muted ? qsTr("MUTED") : qsTr("MUTE")
                checkable: true
                checked: WdspEngine.muted
                onToggled: WdspEngine.setMuted(checked)
                implicitWidth: 66; implicitHeight: 24
                ToolTip.text: qsTr("Silence output without changing the Volume slider.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }

            Item { width: 10 }

            Label { text: qsTr("Bal"); color: root.cMuted }
            LyraSlider {
                id: balSlider
                Layout.preferredWidth: 84
                from: -1.0; to: 1.0
                value: WdspEngine.balance
                // Snap to dead-centre near 0 so it's easy to recentre.
                onMoved: WdspEngine.setBalance(Math.abs(value) < 0.06 ? 0.0 : value)
                ToolTip.text: qsTr("Stereo balance — pan the audio left/right "
                    + "(centre = both channels equal; snaps to centre near the middle).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }

            Item { width: 10 }

            // ── TX monitor (#90) — MON toggle + Monitor level ──
            Button {
                id: monBtn
                text: qsTr("MON TX")
                checkable: true
                checked: WdspEngine.monEnabled
                onToggled: WdspEngine.setMonEnabled(checked)
                implicitWidth: 60; implicitHeight: 24
                background: Rectangle {
                    radius: 3
                    color: monBtn.checked ? "#3a2a14" : "#161e28"
                    border.color: monBtn.checked ? root.cOn : "#2a3a4a"
                    border.width: 1
                }
                contentItem: Text {
                    text: monBtn.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: monBtn.checked ? root.cOn : root.cText
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("Monitor — hear your own processed TX audio "
                    + "(post-rack: Speech / EQ / Combinator / Plating) on the "
                    + "headphone jack while you transmit. It's your voice through "
                    + "the DSP rack, before the radio's corrective ALC and TX "
                    + "bandpass — set the level with the Monitor slider. "
                    + "(HL2 jack now; a separate PC monitor device comes later.)")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            LyraSlider {
                id: monSlider
                Layout.preferredWidth: 96
                from: 0.0; to: 1.0
                enabled: WdspEngine.monEnabled
                value: WdspEngine.monVolume
                onMoved: WdspEngine.setMonVolume(value)
                WheelHandler {
                    onWheel: (ev) => {
                        var nv = WdspEngine.monVolume
                                 + (ev.angleDelta.y > 0 ? 0.02 : -0.02)
                        WdspEngine.setMonVolume(Math.max(0.0, Math.min(1.0, nv)))
                    }
                }
                ToolTip.text: qsTr("Monitor level — how loud your own TX audio is in the monitor.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label {
                text: WdspEngine.monVolume <= 0.0
                      ? qsTr("-∞ dB")
                      : Math.round(20 * Math.log(WdspEngine.monVolume) / Math.LN10) + qsTr(" dB")
                color: WdspEngine.monEnabled ? root.cText : root.cDim
                font.family: "Consolas"; Layout.preferredWidth: 48
            }

            Item { Layout.fillWidth: true }

            // #164 — inline output-device quick-switch (HL2 jack <-> PC
            // devices), one click, no Settings trip.  Reads the same list +
            // setter the Settings -> Audio combo uses (audioOutputDevices() /
            // setAudioOutputDevice / audioDeviceIndex; index 0 = HL2 jack).
            // Full setup (host API, exclusive, VAC) stays in Settings.
            Button {
                id: outBtn
                text: qsTr("Out")
                implicitWidth: 46; implicitHeight: 24
                onClicked: outPopup.open()
                background: Rectangle { radius: 3; color: "#161e28"
                    border.color: "#2a3a4a" }
                contentItem: Text { text: outBtn.text; color: root.cText
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    clip: true }
                ToolTip.text: {
                    var d = WdspEngine.audioOutputDevices()
                    var i = WdspEngine.audioDeviceIndex
                    return qsTr("Audio output: ")
                         + ((i >= 0 && i < d.length) ? d[i] : "?")
                         + qsTr(" — click to switch (full setup in Settings → Audio).")
                }
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                // #164 — styled list popup: a titled dark panel with one row
                // per device, the current one ▶-marked / accented, hover
                // highlight, and an as-needed scrollbar.  Reads the same
                // audioOutputDevices()/audioDeviceIndex/setAudioOutputDevice
                // the Settings → Audio combo uses.
                Popup {
                    id: outPopup
                    // Render as its own top-level window so the list ISN'T
                    // clipped to the short AudioPanel QQuickWidget (which cut
                    // the list off at the panel bottom with no way to scroll).
                    popupType: Popup.Window
                    x: outBtn.width - width
                    y: outBtn.height + 2
                    width: 248
                    padding: 1
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    onAboutToShow: outList.model = WdspEngine.audioOutputDevices()
                    background: Rectangle { color: "#0d141b"
                        border.color: root.cAccent; radius: 4 }
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Output device")
                            color: root.cAccent; font.bold: true; font.pixelSize: 11
                            leftPadding: 8; topPadding: 5; bottomPadding: 5
                            background: Rectangle { color: "#11202b" }
                        }
                        ListView {
                            id: outList
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(count * 26, 8 * 26)
                            clip: true
                            model: WdspEngine.audioOutputDevices()
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ItemDelegate {
                                id: devDel
                                width: outList.width
                                height: 26
                                onClicked: {
                                    WdspEngine.setAudioOutputDevice(index)
                                    outPopup.close()
                                }
                                contentItem: Text {
                                    text: (index === WdspEngine.audioDeviceIndex
                                           ? "▶  " : "      ") + modelData
                                    color: index === WdspEngine.audioDeviceIndex
                                           ? root.cAccent : root.cText
                                    font.pixelSize: 12
                                    font.bold: index === WdspEngine.audioDeviceIndex
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: index === WdspEngine.audioDeviceIndex ? "#13222c"
                                           : (devDel.hovered ? "#162230" : "transparent")
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Row 2 — DSP toggles + AGC readout ───────────────────────
        RowLayout {
            spacing: 4
            Layout.fillWidth: true

            Label { text: qsTr("DSP"); color: root.cAccent; font.bold: true }

            DspToggle {
                text: "NB"
                checked: WdspEngine.nbEnabled
                onToggled: WdspEngine.setNbEnabled(checked)
                note: qsTr("Noise Blanker — impulse-noise suppression (ignition / power-line / lightning) on the raw IQ. Strength slider on the row below.")
            }
            DspToggle {
                text: "BIN"
                checked: WdspEngine.binEnabled
                onToggled: WdspEngine.setBinEnabled(checked)
                note: qsTr("Binaural — pseudo-stereo soundstage widening for headphones (weak CW/SSB seems to lift out of the noise). Depth slider on the row below.")
            }
            DspToggle {
                text: "NR"
                checked: WdspEngine.nrEnabled
                onToggled: WdspEngine.setNrEnabled(checked)
                note: qsTr("Noise Reduction (EMNR). Mode / AEPF / NPE on the row below.")
            }
            DspToggle {
                text: "ANF"
                checked: WdspEngine.anfEnabled
                onToggled: WdspEngine.setAnfEnabled(checked)
                note: qsTr("Auto Notch — LMS predictor that nulls carriers / heterodynes.")
            }
            DspToggle {
                text: "LMS"
                checked: WdspEngine.lmsEnabled
                onToggled: WdspEngine.setLmsEnabled(checked)
                note: qsTr("LMS line enhancer — lifts CW / tones above broadband noise. Strength slider on the row below.")
            }
            DspToggle {
                text: "SQ"
                checked: WdspEngine.squelchEnabled
                onToggled: WdspEngine.setSquelchEnabled(checked)
                note: qsTr("All-mode squelch — mutes between transmissions (SSB/CW/DIG via SSQL, FM, AM). Threshold slider on the row below.")
            }
            DspToggle {
                text: "APF"
                checked: WdspEngine.apfEnabled
                onToggled: WdspEngine.setApfEnabled(checked)
                note: qsTr("Audio Peak Filter — a narrow peak on the CW pitch to lift a CW tone out of the noise. CW modes only (no effect in SSB/AM/FM).")
            }
            DspToggle {
                text: "NF"
                checked: WdspEngine.notchEnabled
                onToggled: WdspEngine.setNotchEnabled(checked)
                note: qsTr("Manual notches — right-click the spectrum to drop one; drag to move, wheel to widen, right-click it to remove.")
            }

            Label {
                text: WdspEngine.notches.length === 1
                      ? qsTr("1 notch")
                      : WdspEngine.notches.length + qsTr(" notches")
                color: WdspEngine.notches.length > 0 ? root.cText : root.cDim
                opacity: WdspEngine.notches.length > 0 ? 0.9 : 0.6
                font.family: "Consolas"; font.pixelSize: 10
                Layout.preferredWidth: 80
            }

            Item { width: 12 }

            // ── AGC readout — the WHOLE cell is one click target that
            // cycles Off → Fast → Med → Slow.  Hover highlights it so it
            // reads as a button; thr + gain are live (gain = WDSP
            // RXA_AGC_GAIN action, dB).
            Rectangle {
                id: agcCell
                Layout.preferredHeight: 26
                implicitWidth: agcRow.implicitWidth + 16
                // agcRow is anchors.centerIn, not filled, so it keeps its
                // implicit width when the layout squeezes this cell — and
                // without a clip the AGC readout paints straight out over the
                // DSP toggles beside it.  Clip so it degrades instead.
                clip: true
                radius: 4
                color: agcMa.containsMouse ? "#16242e" : "transparent"
                border.width: 1
                border.color: agcMa.containsMouse ? "#2a4a5a" : "transparent"
                Row {
                    id: agcRow
                    anchors.centerIn: parent
                    spacing: 6
                    Label { text: qsTr("AGC"); color: root.cAccent; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter }
                    Label { text: WdspEngine.agcMode.toUpperCase()
                            color: root.cAccent; font.family: "Consolas"; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            width: 38 }
                    Label { text: qsTr("thr"); color: root.cMuted; font.pixelSize: 9
                            anchors.verticalCenter: parent.verticalCenter }
                    Label { text: Math.round(WdspEngine.agcThreshDb) + qsTr(" dBFS")
                            color: root.cText; font.family: "Consolas"
                            anchors.verticalCenter: parent.verticalCenter }
                    Label { text: qsTr("gain"); color: root.cMuted; font.pixelSize: 9
                            anchors.verticalCenter: parent.verticalCenter }
                    Label { text: WdspEngine.agcMode === "off"
                                  ? "—" : Math.round(WdspEngine.agcGainDb) + qsTr(" dB")
                            color: "#50d0ff"; font.family: "Consolas"
                            anchors.verticalCenter: parent.verticalCenter
                            width: 48 }
                }
                MouseArea {
                    id: agcMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var order = ["off", "fast", "med", "slow"]
                        var i = order.indexOf(WdspEngine.agcMode)
                        WdspEngine.setAgcMode(order[(i + 1) % order.length])
                    }
                    ToolTip.text: qsTr("Click to cycle AGC: Off → Fast → Med → Slow.")
                    ToolTip.visible: (containsMouse) && Prefs.tooltipsEnabled
                    ToolTip.delay: 500
                }
            }

            Item { Layout.fillWidth: true }

            // ── CW sidetone monitor (#105) — gateware HW sidetone level,
            // visible only in CW.  This is "how loud you hear your own CW
            // while keying."  Deliberately SEPARATE from the MON TX monitor
            // (host post-rack tap) — it drives the radio's hardware sidetone
            // (Stream.cwSidetoneLevel, 0-127), not the MON circuit.  Sits on
            // row 2's right edge, under the MON TX slider on row 1.
            RowLayout {
                spacing: 4
                visible: WdspEngine.mode === "CWL" || WdspEngine.mode === "CWU"
                Label {
                    text: qsTr("CW MON")
                    color: root.cMuted; font.pixelSize: 11
                    Layout.alignment: Qt.AlignVCenter
                }
                LyraSlider {
                    id: cwMonSlider
                    Layout.preferredWidth: 96
                    from: 0; to: 127; stepSize: 1
                    value: Stream.cwSidetoneLevel
                    onMoved: Stream.cwSidetoneLevel = Math.round(value)
                    WheelHandler {
                        onWheel: (ev) => Stream.cwSidetoneLevel =
                            Math.max(0, Math.min(127, Stream.cwSidetoneLevel
                                     + (ev.angleDelta.y > 0 ? 2 : -2)))
                    }
                    ToolTip.text: qsTr("CW sidetone level — how loud you hear "
                        + "your own CW while keying (0-127). The radio's "
                        + "hardware sidetone; separate from the MON TX monitor.")
                    ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                }
                Label {
                    text: Stream.cwSidetoneLevel
                    color: root.cText; font.family: "Consolas"
                    Layout.preferredWidth: 28
                }
            }
        }

        // ── Row 3 — NR character + reference ────────────────────────
        RowLayout {
            spacing: 6
            Layout.fillWidth: true

            Label { text: qsTr("NR Mode:"); color: root.cText; font.pixelSize: 11 }
            LyraSlider {
                id: nrModeSlider
                from: 1; to: 4; stepSize: 1; snapMode: Slider.SnapAlways
                value: WdspEngine.nrMode
                Layout.preferredWidth: 110
                onMoved: WdspEngine.setNrMode(Math.round(value))
                ToolTip.text: qsTr("NR mode 1..4 (WDSP EMNR gain function):\n"
                    + "1 Wiener+SPP   2 Wiener   3 MMSE-LSA (default)   4 trained")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { text: WdspEngine.nrMode; color: "#50d0ff"
                    font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 16 }

            Item { width: 8 }
            CheckBox {
                text: qsTr("AEPF")
                checked: WdspEngine.aepfEnabled
                onToggled: WdspEngine.setAepfEnabled(checked)
                font.pixelSize: 11
                ToolTip.text: qsTr("Anti-musical-noise smoother — engages BOTH WDSP "
                    + "stages (artifact elimination + post-filter). On = "
                    + "noticeably less musical 'twinkle' with the voice kept "
                    + "natural (MMSE-LSA); off = rawer EMNR on quiet bands.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }

            Item { width: 6 }
            Label { text: qsTr("NPE:"); color: root.cText; font.pixelSize: 11 }
            LyraComboBox {
                id: npeCombo
                model: ["OSMS", "MCRA"]
                currentIndex: WdspEngine.npeMethod
                onActivated: WdspEngine.setNpeMethod(currentIndex)
                Layout.preferredWidth: 86
                font.pixelSize: 11
            }

            // LMS strength — appears only when LMS is engaged (old-Lyra idiom).
            Item { width: 8; visible: WdspEngine.lmsEnabled }
            Label { text: qsTr("LMS:"); color: root.cText; font.pixelSize: 11
                    visible: WdspEngine.lmsEnabled }
            LyraSlider {
                id: lmsSlider
                visible: WdspEngine.lmsEnabled
                from: 0; to: 100; stepSize: 1
                value: Math.round(WdspEngine.lmsStrength * 100)
                Layout.preferredWidth: 120
                onMoved: WdspEngine.setLmsStrength(value / 100.0)
                ToolTip.text: qsTr("LMS strength — more taps + harder prediction.\n"
                    + "0 subtle · 50 WDSP-class default · 100 full")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { visible: WdspEngine.lmsEnabled
                    text: Math.round(WdspEngine.lmsStrength * 100) + "%"
                    color: "#50d0ff"; font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 34 }

            // Squelch threshold — appears only when SQ is engaged.
            Item { width: 8; visible: WdspEngine.squelchEnabled }
            Label { text: qsTr("SQ:"); color: root.cText; font.pixelSize: 11
                    visible: WdspEngine.squelchEnabled }
            LyraSlider {
                id: sqSlider
                visible: WdspEngine.squelchEnabled
                from: 0; to: 100; stepSize: 1
                value: Math.round(WdspEngine.squelchThreshold * 100)
                Layout.preferredWidth: 120
                onMoved: WdspEngine.setSquelchThreshold(value / 100.0)
                ToolTip.text: qsTr("Squelch threshold — higher = tighter (only stronger signals open it).\n"
                    + "Typical sweet spot 10–30; routes to SSQL / FM-SQ / AM-SQ by mode.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { visible: WdspEngine.squelchEnabled
                    text: Math.round(WdspEngine.squelchThreshold * 100)
                    color: "#50d0ff"; font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 26 }

            // NB strength — appears only when NB is engaged.
            Item { width: 8; visible: WdspEngine.nbEnabled }
            Label { text: qsTr("NB:"); color: root.cText; font.pixelSize: 11
                    visible: WdspEngine.nbEnabled }
            LyraSlider {
                id: nbSlider
                visible: WdspEngine.nbEnabled
                from: 0; to: 100; stepSize: 1
                value: Math.round(WdspEngine.nbStrength * 100)
                Layout.preferredWidth: 120
                onMoved: WdspEngine.setNbStrength(value / 100.0)
                ToolTip.text: qsTr("Noise-blanker strength — higher = more aggressive impulse blanking.\n"
                    + "Back off if it starts chewing CW/SSB transients.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { visible: WdspEngine.nbEnabled
                    text: Math.round(WdspEngine.nbStrength * 100) + "%"
                    color: "#50d0ff"; font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 34 }

            // APF gain — appears only when APF is on; stepped in 3 dB
            // increments so it's a quick few-choice pick (3…18 dB).
            Item { width: 8; visible: WdspEngine.apfEnabled }
            Label { text: qsTr("APF:"); color: root.cText; font.pixelSize: 11
                    visible: WdspEngine.apfEnabled }
            LyraSlider {
                id: apfSlider
                visible: WdspEngine.apfEnabled
                from: 3; to: 18; stepSize: 3; snapMode: Slider.SnapAlways
                value: WdspEngine.apfGainDb
                Layout.preferredWidth: 110
                onMoved: WdspEngine.setApfGainDb(value)
                ToolTip.text: qsTr("APF peak gain — how hard the CW peak lifts the tone (3–18 dB).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { visible: WdspEngine.apfEnabled
                    text: Math.round(WdspEngine.apfGainDb) + qsTr(" dB")
                    color: "#50d0ff"; font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 40 }

            // BIN depth — appears only when BIN is on (0 = mono … 100 = full).
            Item { width: 8; visible: WdspEngine.binEnabled }
            Label { text: qsTr("BIN:"); color: root.cText; font.pixelSize: 11
                    visible: WdspEngine.binEnabled }
            LyraSlider {
                id: binSlider
                visible: WdspEngine.binEnabled
                from: 0; to: 100; stepSize: 5
                value: Math.round(WdspEngine.binDepth * 100)
                Layout.preferredWidth: 110
                onMoved: WdspEngine.setBinDepth(value / 100.0)
                ToolTip.text: qsTr("Binaural depth — soundstage width on headphones (0 = mono, 100 = full Hilbert pair).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
            }
            Label { visible: WdspEngine.binEnabled
                    text: Math.round(WdspEngine.binDepth * 100) + "%"
                    color: "#50d0ff"; font.family: "Consolas"; font.bold: true
                    Layout.preferredWidth: 34 }

            Item { width: 10 }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20
                        color: "#2a3a4a" }
            Item { width: 4 }

            // ── Captured noise profile — Lyra's IQ-domain spectral NR ──
            // 📷 Cap averages the band's noise spectrum (do it on a quiet
            // frequency); NR-C applies it (Wiener subtraction, pre-WDSP);
            // profiles are named + saved, tagged with rate + FFT size.
            Button {
                id: capBtn
                implicitHeight: 24; implicitWidth: 72; font.pixelSize: 12
                text: WdspEngine.noiseCapturing
                      ? (Math.round(WdspEngine.noiseCaptureProgress * 100) + "%")
                      : qsTr("📷 Cap")
                onClicked: WdspEngine.noiseCapturing
                           ? WdspEngine.cancelNoiseCapture()
                           : WdspEngine.startNoiseCaptureDefault()
                background: Rectangle {
                    radius: 3
                    color: WdspEngine.noiseCapturing ? "#3a2a14" : "#161e28"
                    border.color: WdspEngine.noiseCapturing ? "#ff9a3c" : "#2a3a4a"
                }
                contentItem: Text {
                    text: capBtn.text
                    color: WdspEngine.noiseCapturing ? "#ff9a3c" : "#cdd9e5"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: capBtn.font
                    elide: Text.ElideRight
                    clip: true
                }
                ToolTip.text: qsTr("Capture the band's noise spectrum over the chosen "
                    + "window — do it on a quiet, signal-free frequency. Click again to cancel.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }

            LyraComboBox {
                id: durCombo
                implicitHeight: 24; Layout.preferredWidth: 58; font.pixelSize: 11
                model: ["3 s", "5 s", "10 s"]
                property var secs: [3.0, 5.0, 10.0]
                currentIndex: secs.indexOf(WdspEngine.noiseCaptureSeconds)
                onActivated: (i) => WdspEngine.setNoiseCaptureSeconds(secs[i])
                ToolTip.text: qsTr("Capture window length.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }

            LyraComboBox {
                id: fftCombo
                implicitHeight: 24; Layout.preferredWidth: 74; font.pixelSize: 11
                model: ["2048", "4096", "8192"]
                property var sizes: [2048, 4096, 8192]
                currentIndex: sizes.indexOf(WdspEngine.noiseFftSize)
                onActivated: (i) => WdspEngine.setNoiseFftSize(sizes[i])
                ToolTip.text: qsTr("FFT resolution — 8192 resolves wide ESSB noise finest "
                    + "(more latency); 2048 = lowest latency. Changing it clears the "
                    + "current profile (size-specific).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }

            DspToggle {
                text: qsTr("NR-C")
                implicitWidth: 48
                checked: WdspEngine.noiseApplyEnabled
                onToggled: WdspEngine.setNoiseApply(checked)
                note: qsTr("Apply the captured noise profile to RX audio (IQ-domain "
                    + "spectral subtraction, before WDSP). Capture or load a profile first.")
            }

            // Tune button — Strength / Floor / Smoothing (visible when applying).
            Button {
                id: tuneBtn
                implicitWidth: 32; implicitHeight: 24
                visible: WdspEngine.noiseApplyEnabled
                onClicked: tunePop.open()
                ToolTip.text: qsTr("Tune the captured-profile noise reduction "
                    + "(Strength / Floor / Smoothing).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
                background: Rectangle {
                    radius: 3
                    color: tunePop.opened ? "#16242e" : "#161e28"
                    border.color: root.cAccent
                }
                contentItem: Text {
                    text: "⚙"          // ⚙
                    color: root.cAccent
                    font.pixelSize: 15; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                Popup {
                    id: tunePop
                    // Own top-level window so it isn't clipped to the panel's
                    // QQuickWidget rect (Qt 6.8+; the panel is a short dock).
                    popupType: Popup.Window
                    y: -implicitHeight - 6           // pop ABOVE (panel sits low)
                    x: tuneBtn.width - width         // right-align to the button
                    width: 300; padding: 12
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: "#0c141c"; radius: 6
                                            border.color: root.cAccent }
                    contentItem: ColumnLayout {
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Captured-profile NR"); color: root.cAccent
                                    font.bold: true; font.pixelSize: 12; Layout.fillWidth: true }
                            Button {
                                implicitWidth: 22; implicitHeight: 20
                                onClicked: tunePop.close()
                                background: Rectangle { radius: 3; color: "transparent"
                                                        border.color: "#2a4a5a" }
                                contentItem: Text { text: "✕"; color: root.cMuted
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter }
                            }
                        }
                        RowLayout { spacing: 8; Layout.fillWidth: true
                            Label { text: qsTr("Strength"); color: root.cText
                                    font.pixelSize: 11; Layout.preferredWidth: 68 }
                            LyraSlider { Layout.fillWidth: true
                                from: 1.0; to: 5.0; stepSize: 0.1
                                value: WdspEngine.noiseStrength
                                onMoved: WdspEngine.setNoiseStrength(value) }
                            Label { text: WdspEngine.noiseStrength.toFixed(1) + "×"
                                    color: "#50d0ff"; font.family: "Consolas"
                                    Layout.preferredWidth: 38 }
                        }
                        RowLayout { spacing: 8; Layout.fillWidth: true
                            Label { text: qsTr("Floor"); color: root.cText
                                    font.pixelSize: 11; Layout.preferredWidth: 68 }
                            LyraSlider { Layout.fillWidth: true
                                from: -30; to: -3; stepSize: 1
                                value: WdspEngine.noiseFloorDb
                                onMoved: WdspEngine.setNoiseFloorDb(value) }
                            Label { text: Math.round(WdspEngine.noiseFloorDb) + qsTr(" dB")
                                    color: "#50d0ff"; font.family: "Consolas"
                                    Layout.preferredWidth: 44 }
                        }
                        RowLayout { spacing: 8; Layout.fillWidth: true
                            Label { text: qsTr("Smoothing"); color: root.cText
                                    font.pixelSize: 11; Layout.preferredWidth: 68 }
                            LyraSlider { Layout.fillWidth: true
                                from: 0; to: 95; stepSize: 5
                                value: Math.round(WdspEngine.noiseSmoothing * 100)
                                onMoved: WdspEngine.setNoiseSmoothing(value / 100.0) }
                            Label { text: Math.round(WdspEngine.noiseSmoothing * 100) + "%"
                                    color: "#50d0ff"; font.family: "Consolas"
                                    Layout.preferredWidth: 38 }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Deeper Floor + higher Strength = more cut "
                                + "(watch for 'twinkle'); raise Smoothing to tame it.")
                            color: root.cText; font.pixelSize: 10; wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            LyraComboBox {
                id: profCombo
                implicitHeight: 24; Layout.preferredWidth: 150; font.pixelSize: 11
                model: WdspEngine.noiseProfiles
                displayText: count > 0 ? currentText : qsTr("(no profiles)")
                onActivated: (i) => {
                    WdspEngine.loadProfileOrWarn(textAt(i))
                    currentIndex = WdspEngine.noiseProfiles
                                   .indexOf(WdspEngine.noiseActiveProfile)
                }
                Component.onCompleted: currentIndex =
                    WdspEngine.noiseProfiles.indexOf(WdspEngine.noiseActiveProfile)
                Connections {
                    target: WdspEngine
                    function onNoiseProfilesChanged() {
                        profCombo.currentIndex = WdspEngine.noiseProfiles
                            .indexOf(WdspEngine.noiseActiveProfile)
                    }
                }
                ToolTip.text: qsTr("Saved noise profiles — select to load (tagged with "
                    + "rate + FFT size).")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }

            Button {
                text: qsTr("Save"); implicitHeight: 24; implicitWidth: 50
                font.pixelSize: 11
                enabled: WdspEngine.noiseProfileValid
                opacity: enabled ? 1.0 : 0.45
                onClicked: WdspEngine.promptSaveProfile()
                ToolTip.text: qsTr("Save the just-captured profile under a name.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }
            Button {
                text: qsTr("Del"); implicitHeight: 24; implicitWidth: 44
                font.pixelSize: 11
                enabled: WdspEngine.noiseActiveProfile.length > 0
                opacity: enabled ? 1.0 : 0.45
                onClicked: if (WdspEngine.noiseActiveProfile.length > 0)
                               WdspEngine.deleteNoiseProfile(WdspEngine.noiseActiveProfile)
                ToolTip.text: qsTr("Delete the selected profile.")
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled; ToolTip.delay: 400
            }

            Item { Layout.fillWidth: true }
        }
    }

    // Save-name prompt + rate-mismatch warning are NATIVE dialogs
    // (WdspEngine.promptSaveProfile / loadProfileOrWarn) — QML Popups are
    // clipped to this panel's QQuickWidget, so they can't be used here.
}
