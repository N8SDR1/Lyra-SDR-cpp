// Lyra — TX Speech dock panel (#88).  Two pre-EQ stages bound to the
// Speech context property (lyra::ui::SpeechModel): Auto-AGC (input
// leveller) + De-esser (frequency-dynamic).  Dockable / collapsible /
// View-hideable like every panel; auto-bypassed in DIGU/DIGL with the rest
// of the rack.  Wire-INERT visuals: SpeechModel uses real Q_PROPERTY +
// NOTIFY, so bindings auto-update (no revision tick).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 460
    implicitHeight: collapsed ? 40 : (body.implicitHeight + 12)
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"

    property bool collapsed: false

    // The whole TX rack is bypassed in the digital data modes (DIGU/DIGL,
    // gated by SetTxRackBypass) and is moot in CW (no mic audio).  Dim the
    // stage lamps + controls so the operator sees at a glance the rack isn't
    // shaping audio in those modes.  Purely visual — the model/profile state
    // is untouched and re-lights the instant the mode returns to USB/LSB.
    readonly property bool rackInactive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "DIGU" || m === "DIGL" || m === "CWU" || m === "CWL"
    }

    // A labelled slider row: [label] [====slider====] [value].
    component CtlRow : RowLayout {
        property string label: ""
        property real from: 0
        property real to: 1
        property real step: 1
        property real value: 0
        property string suffix: ""
        signal moved(real v)
        Layout.fillWidth: true
        spacing: 8
        Label { text: parent.label; color: root.cText; font.bold: true
                font.pixelSize: 13; Layout.preferredWidth: 72 }
        LyraSlider {
            id: sl
            Layout.fillWidth: true
            from: parent.from; to: parent.to; stepSize: parent.step
            snapMode: Slider.SnapAlways
            value: parent.value
            onMoved: parent.moved(value)
        }
        Label {
            text: Math.round(parent.value) + parent.suffix
            color: root.cText; font.family: "Consolas"; font.bold: true
            font.pixelSize: 13; Layout.preferredWidth: 64
            horizontalAlignment: Text.AlignRight
        }
    }

    // A stage card: header (ON toggle + name + sublabel) + its controls.
    component Stage : Rectangle {
        id: stage
        default property alias content: body.data
        property string name: ""
        property string sub: ""
        property bool on: false
        property color accent: root.cAccent
        signal toggled(bool v)
        Layout.fillWidth: true
        radius: 4
        color: "#0b141b"
        border.color: (on && !root.rackInactive) ? accent : "#1c2a36"
        border.width: (on && !root.rackInactive) ? 2 : 1
        implicitHeight: col.implicitHeight + 16
        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Button {
                    id: en
                    checkable: true; checked: on
                    implicitWidth: 46; implicitHeight: 22
                    // #162: qualify with the Stage id — an unqualified
                    // `toggled` resolves to AbstractButton's built-in
                    // `toggled` signal (shadowing the Stage's custom one),
                    // so the model write never fired and Speech saved
                    // defaults.  stage.toggled() emits the right signal.
                    onClicked: stage.toggled(checked)
                    background: Rectangle {
                        radius: 11
                        color: (en.checked && !root.rackInactive) ? accent : "#1f2a35"
                        border.color: (en.checked && !root.rackInactive) ? accent : "#3a5060"
                    }
                    contentItem: Text {
                        text: en.checked ? "ON" : "OFF"
                        color: (en.checked && !root.rackInactive) ? "#0d141b" : root.cMuted
                        font.bold: true; font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                // #162 (lamp refresh): a checkable Button flips its own
                // `checked` on click, severing the inline `checked: on`
                // binding — so a later Profile recall wouldn't update the
                // lamp.  This Binding re-asserts `checked` from the model
                // whenever `on` changes (recall).  Safe for a toggle: it
                // only re-fires on a model change, not continuously, so it
                // can't fight the click (the model write happens after).
                Binding { target: en; property: "checked"; value: on }
                ColumnLayout {
                    spacing: 0
                    Label { text: name; color: root.cText; font.bold: true
                            font.pixelSize: 13 }
                    Label { text: sub; color: root.cMuted; font.pixelSize: 11 }
                }
                Item { Layout.fillWidth: true }
            }
            Item { id: body; Layout.fillWidth: true
                   implicitHeight: childrenRect.height
                   opacity: (on && !root.rackInactive) ? 1.0 : 0.5 }
        }
    }

    // Vertical scroll when the panel is shorter than its content (grouped
    // into a small container, floated small, or on a small screen) — fills
    // the width, scrolls only when needed.  Same idiom as the CW decoder.
    ScrollView {
        id: bodyScroll
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
        id: body
        width: bodyScroll.availableWidth
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("TX Speech"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label { text: qsTr("pre-EQ rack"); color: root.cMuted
                    font.pixelSize: 11 }
            Label {
                visible: root.rackInactive
                text: qsTr("• bypassed (") + WdspEngine.mode.toUpperCase() + ")"
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitWidth: 28; implicitHeight: 22
                text: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
                background: Rectangle { radius: 4; color: "#1f2a35"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Noise Gate (first in the chain) ────────────────────────────
        Stage {
            visible: !root.collapsed
            name: qsTr("Noise Gate")
            sub: qsTr("mutes shack noise between words")
            accent: "#9a6cff"
            on: Speech.gateEnabled
            onToggled: (v) => Speech.gateEnabled = v
            ColumnLayout {
                width: parent.width
                spacing: 4
                CtlRow {
                    label: qsTr("Thresh"); from: -80; to: 0; step: 1
                    value: Speech.gateThreshDb; suffix: " dB"
                    onMoved: (v) => Speech.gateThreshDb = v
                }
                CtlRow {
                    label: qsTr("Depth"); from: 0; to: 80; step: 1
                    value: Speech.gateRangeDb; suffix: " dB"
                    onMoved: (v) => Speech.gateRangeDb = v
                }
                CtlRow {
                    label: qsTr("Hold"); from: 0; to: 500; step: 10
                    value: Speech.gateHoldMs; suffix: " ms"
                    onMoved: (v) => Speech.gateHoldMs = v
                }
            }
        }

        // ── Auto-AGC ───────────────────────────────────────────────────
        Stage {
            visible: !root.collapsed
            name: qsTr("Auto-AGC")
            sub: qsTr("levels the mic before the rack")
            accent: "#378ADD"
            on: Speech.agcEnabled
            onToggled: (v) => Speech.agcEnabled = v
            ColumnLayout {
                width: parent.width
                spacing: 4
                CtlRow {
                    label: qsTr("Target"); from: -40; to: 0; step: 1
                    value: Speech.agcTargetDb; suffix: " dB"
                    onMoved: (v) => Speech.agcTargetDb = v
                }
                CtlRow {
                    label: qsTr("Max gain"); from: 0; to: 40; step: 1
                    value: Speech.agcMaxGainDb; suffix: " dB"
                    onMoved: (v) => Speech.agcMaxGainDb = v
                }
            }
        }

        // ── De-esser ───────────────────────────────────────────────────
        Stage {
            visible: !root.collapsed
            name: qsTr("De-esser")
            sub: qsTr("tames harsh sibilance")
            accent: "#BA7517"
            on: Speech.deessEnabled
            onToggled: (v) => Speech.deessEnabled = v
            ColumnLayout {
                width: parent.width
                spacing: 4
                CtlRow {
                    label: qsTr("Freq"); from: 2000; to: 12000; step: 100
                    value: Speech.deessFreqHz; suffix: " Hz"
                    onMoved: (v) => Speech.deessFreqHz = v
                }
                CtlRow {
                    label: qsTr("Thresh"); from: -60; to: 0; step: 1
                    value: Speech.deessThreshDb; suffix: " dB"
                    onMoved: (v) => Speech.deessThreshDb = v
                }
                CtlRow {
                    label: qsTr("Range"); from: 0; to: 24; step: 1
                    value: Speech.deessRangeDb; suffix: " dB"
                    onMoved: (v) => Speech.deessRangeDb = v
                }
            }
        }
        }
    }
}
