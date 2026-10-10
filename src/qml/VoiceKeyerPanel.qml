// Lyra — Voice Keyer dock panel (#89 Build 1, Stage B4).
//
// Chip-launched floating dock (the "Voice Keyer" header chip, like CW / CW
// Dec / Tuner).  Labelled clip rows — the CW-macro-bank idiom (#176) applied
// to voice messages: each clip fires ▶ OTA (transmit) or ▶ Review (local),
// and its F-key (F1..F12, handled globally in MainWindow for voice modes).
// Edit mode turns the rows into rename + F-key + gain + delete.
//
// Bindings: Clips (ClipBank) for the list + per-clip management;
// VoiceKeyer (the controller) for play/stop/record/gain/bypass + the import /
// storage-folder dialogs.  WdspEngine.mode gates OTA to voice modes.
//
// Until B1 (the HL2 bench step) wires the injector into the live mic funnel,
// VoiceKeyer.live is false: Transmit / Review / Record are disabled with a
// hint (the disabled-XIT / disabled-TUN precedent).  Building the clip bank —
// Import, rename, per-clip gain, F-key, delete, storage folder — is fully
// live now.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 470
    implicitHeight: collapsed ? 40 : (body.implicitHeight + 16)
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cTx:     "#ff7a5c"   // transmit (OTA) — caution warm
    readonly property color cReview: "#7ee081"   // review (local monitor)

    property bool collapsed: false
    property bool editMode: false
    property int  recSrc: 0        // REC source: 0 = Mic (voice message), 1 = RX audio

    readonly property bool voiceMode: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "USB" || m === "LSB" || m === "AM"
            || m === "SAM" || m === "DSB" || m === "FM"
    }
    readonly property var clipList: Clips.clips

    function fmtDur(ms) {
        var s = Math.round(ms / 1000)
        return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2)
    }

    // ── A reusable clip row (labelled, click-to-fire like a CW macro) ──
    Component {
        id: clipRow
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.editMode ? 98 : 50
            radius: 8
            property bool active: VoiceKeyer.playingId === modelData.id
            color: active ? "#10303a" : "#1c252b"
            border.color: active ? "#7fd6ef" : "#3a4750"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                // Fn badge (0 = unassigned)
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 30; implicitHeight: 20; radius: 5
                    color: "transparent"
                    border.color: active ? root.cAccent : "#5b6b76"
                    visible: modelData.fkey > 0
                    Text {
                        anchors.centerIn: parent
                        text: "F" + modelData.fkey
                        color: active ? root.cAccent : "#8fb0bc"
                        font.family: "Consolas"; font.pixelSize: 11
                    }
                }

                // ── Send view ──────────────────────────────────────────
                ColumnLayout {
                    visible: !root.editMode
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: 6
                        Text {
                            text: modelData.label; color: "#fff"
                            font.bold: true; font.pixelSize: 13
                            elide: Text.ElideRight; Layout.maximumWidth: 180
                        }
                        Rectangle {
                            visible: modelData.kind === 1   // Rx clip
                            implicitWidth: rxT.implicitWidth + 8; implicitHeight: 16; radius: 4
                            color: "#16202a"; border.color: "#4a5a64"
                            Text { id: rxT; anchors.centerIn: parent; text: "RX"
                                   color: root.cMuted; font.pixelSize: 9 }
                        }
                    }
                    Text {
                        text: root.fmtDur(modelData.durationMs)
                              + (active ? "  ▮ playing…" : "")
                        color: active ? root.cAccent : root.cMuted
                        font.family: "Consolas"; font.pixelSize: 11
                        SequentialAnimation on opacity {
                            running: active; loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.4; duration: 450 }
                            NumberAnimation { from: 0.4; to: 1.0; duration: 450 }
                        }
                    }
                }

                // ── Edit view ──────────────────────────────────────────
                ColumnLayout {
                    visible: root.editMode
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: nameEdit
                        Layout.fillWidth: true
                        text: modelData.label
                        placeholderText: qsTr("clip name")
                        color: root.cText; font.bold: true; font.pixelSize: 12
                        background: Rectangle { radius: 4; color: "#0b141b"
                            border.color: nameEdit.activeFocus ? root.cAccent : "#2a4a5a" }
                        onEditingFinished: Clips.setLabel(modelData.id, text)
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("F-key"); color: root.cMuted; font.pixelSize: 11 }
                        LyraSpinBox {
                            from: 0; to: 12; value: modelData.fkey
                            implicitHeight: 26; implicitWidth: 84
                            textFromValue: function (v) { return v === 0 ? "—" : "F" + v }
                            onValueModified: Clips.setFkey(modelData.id, value)
                        }
                        Label { text: qsTr("Gain"); color: root.cMuted; font.pixelSize: 11 }
                        LyraSpinBox {
                            from: -20; to: 20; value: Math.round(modelData.gainDb)
                            implicitHeight: 26; implicitWidth: 96
                            textFromValue: function (v) { return (v > 0 ? "+" : "") + v + " dB" }
                            onValueModified: Clips.setGainDb(modelData.id, value)
                        }
                        Item { Layout.fillWidth: true }
                    }
                }

                // ── Actions ────────────────────────────────────────────
                Button {
                    visible: !root.editMode
                    implicitHeight: 30; implicitWidth: 54
                    enabled: VoiceKeyer.live && root.voiceMode
                    text: qsTr("▶ OTA")
                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: !VoiceKeyer.live
                        ? qsTr("Transmit activates in the next build")
                        : qsTr("Switch to a voice mode (SSB/AM/FM) to transmit")
                    onClicked: VoiceKeyer.playOta(modelData.id)
                    background: Rectangle { radius: 5
                        color: parent.enabled ? "#3a1f18" : "#16202a"
                        border.color: parent.enabled ? root.cTx : "#2a3a44" }
                    contentItem: Text { text: parent.text
                        color: parent.enabled ? root.cTx : root.cMuted
                        font.bold: true; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Button {
                    visible: !root.editMode
                    implicitHeight: 30; implicitWidth: 66
                    enabled: VoiceKeyer.reviewReady
                    text: qsTr("▶ Review")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Play locally on this PC (default output) — no transmit")
                    onClicked: VoiceKeyer.playReview(modelData.id)
                    background: Rectangle { radius: 5
                        color: parent.enabled ? "#16301f" : "#16202a"
                        border.color: parent.enabled ? root.cReview : "#2a3a44" }
                    contentItem: Text { text: parent.text
                        color: parent.enabled ? root.cReview : root.cMuted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                TrashCan {
                    visible: root.editMode
                    Layout.alignment: Qt.AlignVCenter
                    tip: qsTr("Delete this clip")
                    onConfirmed: Clips.remove(modelData.id)
                }
            }

            // Playing underline
            Rectangle {
                visible: active
                anchors.left: parent.left; anchors.right: parent.right
                anchors.bottom: parent.bottom; anchors.margins: 2
                height: 3; radius: 2; color: root.cAccent
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

    ScrollView {
        id: bodyScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: root.collapsed ? ScrollBar.AlwaysOff
                                                  : ScrollBar.AsNeeded

    ColumnLayout {
        id: body
        width: bodyScroll.availableWidth
        spacing: 6

        // ── Header ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("Voice Keyer"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            // Operator opt-in for TRANSMIT (default OFF, persisted). Off = the
            // keyer builds/manages clips only; On = ▶ OTA / F-keys actually key.
            // Amber "armed" accent so an enabled keyer reads at a glance.
            Button {
                implicitHeight: 22
                visible: !root.collapsed
                checkable: true
                checked: VoiceKeyer.live
                text: VoiceKeyer.live ? qsTr("TX ● on") : qsTr("Enable TX")
                onToggled: VoiceKeyer.live = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Enable voice-keyer transmit. Verify on a dummy load "
                                   + "first. Off = build/manage clips only.")
                background: Rectangle { radius: 4
                    color: VoiceKeyer.live ? "#5a4410" : "#1f2a35"
                    border.color: VoiceKeyer.live ? "#e0b040" : "#3a5060" }
                contentItem: Text { text: parent.text
                    color: VoiceKeyer.live ? "#ffd070" : root.cMuted
                    font.pixelSize: 11; font.bold: VoiceKeyer.live
                    leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter }
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitWidth: 60; implicitHeight: 22
                visible: !root.collapsed
                text: root.editMode ? qsTr("Done") : qsTr("Edit")
                onClicked: root.editMode = !root.editMode
                background: Rectangle { radius: 4
                    color: root.editMode ? "#2e7d9a" : "#1f2a35"
                    border.color: root.editMode ? root.cAccent : "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cText; font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button {
                implicitWidth: 28; implicitHeight: 22
                text: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
                background: Rectangle { radius: 4; color: "#1f2a35"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Not-yet-live banner ─────────────────────────────────────────
        Rectangle {
            visible: !root.collapsed && !VoiceKeyer.live
            Layout.fillWidth: true
            Layout.preferredHeight: bannerTxt.implicitHeight + 12
            radius: 6; color: "#231a10"; border.color: "#7a5a20"
            Text {
                id: bannerTxt
                anchors.fill: parent; anchors.margins: 6
                wrapMode: Text.WordWrap
                text: qsTr("Transmit is off. Tick “Enable TX” above (verify on a dummy "
                           + "load first) to key clips. You can build the bank now — "
                           + "Import a WAV, rename, set F-keys and per-clip gain.")
                color: "#e0b060"; font.pixelSize: 11
            }
        }

        // ── Controls: REC · Bypass DSP · global Gain · STOP ─────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            // REC source: Mic (a voice message) or RX (record what you hear).
            // Locked while recording.
            Button {
                implicitHeight: 28; implicitWidth: 52
                enabled: !VoiceKeyer.recording && !VoiceKeyer.counting
                text: root.recSrc === 1 ? qsTr("RX") : qsTr("Mic")
                onClicked: root.recSrc = (root.recSrc === 1 ? 0 : 1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("What REC captures: Mic = a voice message; RX = the receive audio.")
                background: Rectangle { radius: 5; color: "#1c252b"
                    border.color: root.recSrc === 1 ? "#7ee081" : "#3a4750" }
                contentItem: Text { text: parent.text
                    color: parent.enabled ? (root.recSrc === 1 ? root.cReview : root.cText) : root.cMuted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            // REC records the selected source into a new clip (no transmit) —
            // gated on the stream being connected, NOT the TX opt-in. Click to
            // start, click again to stop + save (auto-named; rename in Edit).
            Button {
                implicitHeight: 28
                implicitWidth: (VoiceKeyer.recording || VoiceKeyer.counting) ? 100 : 64
                enabled: Stream.running || VoiceKeyer.recording || VoiceKeyer.counting
                text: VoiceKeyer.counting
                      ? ("⏱ " + VoiceKeyer.countdownSec + "…")   // get-set countdown (click to cancel)
                      : (VoiceKeyer.recording
                         ? ("■ " + root.fmtDur(VoiceKeyer.recordMs))
                         : qsTr("● REC"))
                ToolTip.visible: hovered && !enabled
                ToolTip.text: qsTr("Connect the radio to record")
                onClicked: {
                    if (VoiceKeyer.recording) VoiceKeyer.stopRecord("")
                    else if (VoiceKeyer.counting) VoiceKeyer.cancelCountdown()
                    else VoiceKeyer.startRecord(root.recSrc)
                }
                background: Rectangle { radius: 5
                    color: VoiceKeyer.recording ? "#5a1414"
                           : (VoiceKeyer.counting ? "#3a2a10"
                              : (parent.enabled ? "#2a1414" : "#16202a"))
                    border.color: VoiceKeyer.recording ? "#ff6a6a"
                           : (VoiceKeyer.counting ? "#e0b040"
                              : (parent.enabled ? "#c0504d" : "#2a3a44"))
                    SequentialAnimation on opacity {
                        running: VoiceKeyer.recording || VoiceKeyer.counting
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.55; duration: 500 }
                        NumberAnimation { from: 0.55; to: 1.0; duration: 500 }
                    }
                }
                contentItem: Text { text: parent.text
                    color: VoiceKeyer.counting ? "#ffd070"
                           : (parent.enabled ? "#ff8a80" : root.cMuted)
                    font.bold: true; font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button {
                implicitHeight: 28
                checkable: true
                checked: VoiceKeyer.bypassDsp
                text: qsTr("Bypass DSP")
                onToggled: VoiceKeyer.bypassDsp = checked
                background: Rectangle { radius: 5
                    color: VoiceKeyer.bypassDsp ? "#2e7d9a" : "#1c252b"
                    border.color: VoiceKeyer.bypassDsp ? root.cAccent : "#3a4750" }
                contentItem: Text { text: parent.text
                    color: VoiceKeyer.bypassDsp ? "#fff" : root.cMuted
                    font.pixelSize: 11; leftPadding: 8; rightPadding: 8
                    verticalAlignment: Text.AlignVCenter }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Play clips as recorded — skip Lyra's TX rack "
                                   + "(Speech / EQ / Combinator / Plating). WDSP stages "
                                   + "(Leveler, PHROT, ALC) and the bandpass stay on. "
                                   + "Off = run the full TX chain.")
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitHeight: 28
                text: qsTr("■ Stop")
                onClicked: VoiceKeyer.stop()
                background: Rectangle { radius: 5; color: "#3a1c1c"; border.color: "#c0504d" }
                contentItem: Text { text: parent.text; color: "#ff8a80"
                    font.bold: true; font.pixelSize: 11; leftPadding: 10; rightPadding: 10
                    verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Global playback gain ────────────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Gain"); color: root.cText; font.bold: true; font.pixelSize: 12 }
            LyraSlider {
                id: gain
                Layout.fillWidth: true
                from: -20; to: 10; stepSize: 1
                snapMode: Slider.SnapAlways
                value: VoiceKeyer.gainDb
                onMoved: VoiceKeyer.gainDb = value
            }
            Label {
                text: (VoiceKeyer.gainDb > 0 ? "+" : "") + Math.round(VoiceKeyer.gainDb) + qsTr(" dB")
                color: root.cText; font.family: "Consolas"; font.bold: true; font.pixelSize: 12
                Layout.preferredWidth: 56; horizontalAlignment: Text.AlignRight
            }
        }

        // ── Progress (while playing) ────────────────────────────────────
        ProgressBar {
            visible: !root.collapsed && VoiceKeyer.playingId !== ""
            Layout.fillWidth: true
            from: 0; to: 1; value: VoiceKeyer.progress
        }

        // ── Clip list ───────────────────────────────────────────────────
        ColumnLayout {
            visible: !root.collapsed && root.clipList.length > 0
            Layout.fillWidth: true
            spacing: 8
            Repeater { model: root.clipList; delegate: clipRow }
        }

        // Empty-state hint
        Text {
            visible: !root.collapsed && root.clipList.length === 0
            Layout.fillWidth: true
            text: qsTr("No clips yet — Import a WAV to start your bank.")
            color: root.cMuted; font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            topPadding: 10; bottomPadding: 10
        }

        // + Import
        Button {
            visible: !root.collapsed
            Layout.fillWidth: true
            implicitHeight: 34
            text: qsTr("+ Import WAV")
            onClicked: VoiceKeyer.importClipDialog()
            background: Rectangle { radius: 8; color: "#16202a"; border.color: "#4a5a64" }
            contentItem: Text { text: parent.text; color: root.cMuted; font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }

        // ── Storage folder footer ───────────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 6
            Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
        }
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Clips"); color: root.cMuted; font.pixelSize: 10 }
            Text {
                Layout.fillWidth: true
                text: Clips.clipsDir
                color: root.cMuted; font.family: "Consolas"; font.pixelSize: 10
                elide: Text.ElideMiddle
            }
            Button {
                implicitHeight: 22; text: qsTr("Open")
                onClicked: VoiceKeyer.openClipsFolder()
                background: Rectangle { radius: 4; color: "#1c252b"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent; font.pixelSize: 10
                    leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter }
            }
            Button {
                implicitHeight: 22; text: qsTr("Change…")
                onClicked: VoiceKeyer.changeClipsFolderDialog()
                background: Rectangle { radius: 4; color: "#1c252b"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent; font.pixelSize: 10
                    leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter }
            }
        }
    }   // body
    }   // bodyScroll
    }   // outer
}
