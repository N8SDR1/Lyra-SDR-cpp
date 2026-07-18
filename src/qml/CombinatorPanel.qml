// Lyra — TX Combinator dock panel (#51).  5-band multiband compressor +
// optional SBC, bound to the Combinator context property
// (lyra::ui::CombinatorModel).  Dockable / collapsible / View-hideable like
// every panel; sits after the EQ in the mic rack and auto-bypasses in
// DIGU/DIGL with the rest of the rack.  Mirrors the operator's X-Air panel:
// global control row + SBC + per-band selector with per-band THRESH/GAIN +
// per-band gain-reduction meters.  Default stage OFF (it materially
// compresses).  CombinatorModel uses real Q_PROPERTY + NOTIFY for the
// globals; per-band invokable getters re-eval off the `rev` tick.

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
    // Per-band colours (LOW -> HIGH), matching the reference band strip.
    readonly property var bandCol: ["#ff5a6e", "#ffa642", "#e070d0", "#8a7cff", "#22d3ee"]

    property bool collapsed: false
    property int  rev: 0        // bumped on paramsChanged -> re-eval per-band getters
    property int  mtick: 0      // bumped on metersChanged -> re-eval meter getters

    // The whole TX rack is bypassed in the digital data modes (DIGU/DIGL,
    // gated by SetTxRackBypass) and is moot in CW.  Dim the ON lamp +
    // controls so the operator sees the rack isn't shaping audio there.
    // Purely visual — model/profile state untouched, re-lights on USB/LSB.
    readonly property bool rackInactive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "DIGU" || m === "DIGL" || m === "CWU" || m === "CWL"
    }

    Connections { target: Combinator
        function onParamsChanged()  { root.rev++ }
        function onMetersChanged()  { root.mtick++ }
        function onSelectedBandChanged() { root.rev++ }
    }

    // A labelled slider row: [label] [====slider====] [value].
    component CtlRow : RowLayout {
        property string label: ""
        property real from: 0
        property real to: 1
        property real step: 1
        property real value: 0
        property string suffix: ""
        property int decimals: 0
        signal moved(real v)
        Layout.fillWidth: true
        spacing: 8
        Label { text: parent.label; color: root.cText; font.bold: true
                font.pixelSize: 13; Layout.preferredWidth: 72 }
        LyraSlider {
            Layout.fillWidth: true
            from: parent.from; to: parent.to; stepSize: parent.step
            snapMode: Slider.SnapAlways
            value: parent.value
            onMoved: parent.moved(value)
        }
        Label {
            text: parent.value.toFixed(parent.decimals) + parent.suffix
            color: root.cText; font.family: "Consolas"; font.bold: true
            font.pixelSize: 13; Layout.preferredWidth: 68
            horizontalAlignment: Text.AlignRight
        }
    }

    // Scroll when the panel is compressed below its natural content size
    // — both axes, AsNeeded — so a floated-small or racked panel stays
    // fully usable instead of silently clipping (SizeRootObjectToView
    // gives no scrollbar on its own).  Same idiom as SpeechPanel / the CW
    // decoder.  Above the natural size the content fills; below it scrolls.
    ScrollView {
        id: bodyScroll
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        contentWidth: Math.max(438, availableWidth)
        contentHeight: body.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded
        ScrollBar.vertical.policy:   ScrollBar.AsNeeded

        ColumnLayout {
            id: body
            width: bodyScroll.contentWidth
            spacing: 6

        // ── Header: title + ON + collapse ─────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("Combinator"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label { text: qsTr("5-band multiband comp"); color: root.cMuted
                    font.pixelSize: 11 }
            Label {
                visible: root.rackInactive
                text: qsTr("• bypassed (") + WdspEngine.mode.toUpperCase() + ")"
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                id: onBtn
                checkable: true; checked: !Combinator.bypass
                implicitWidth: 50; implicitHeight: 22
                onClicked: Combinator.bypass = !checked
                background: Rectangle { radius: 4
                    color: (onBtn.checked && !root.rackInactive) ? "#0b3a44" : "#1f2a35"
                    border.color: (onBtn.checked && !root.rackInactive) ? root.cAccent : "#3a5060"
                    border.width: 2 }
                contentItem: Text { text: onBtn.checked ? "ON" : "OFF"
                    color: (onBtn.checked && !root.rackInactive) ? root.cAccent : root.cMuted
                    font.bold: true; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }
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

        // ── Global controls ───────────────────────────────────────────
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 4
            opacity: (Combinator.bypass || root.rackInactive) ? 0.5 : 1.0
            CtlRow { label: qsTr("Mix"); from: 0; to: 100; step: 1; suffix: " %"
                value: Combinator.mix * 100
                onMoved: (v) => Combinator.mix = v / 100 }
            CtlRow { label: qsTr("Attack"); from: 0; to: 120; step: 1; suffix: " ms"
                value: Combinator.attackMs
                onMoved: (v) => Combinator.attackMs = v }
            CtlRow { label: qsTr("Release"); from: 20; to: 2000; step: 1; suffix: " ms"
                value: Combinator.releaseMs
                onMoved: (v) => Combinator.releaseMs = v }
            CtlRow { label: qsTr("Ratio"); from: 1; to: 10; step: 0.1; decimals: 1; suffix: ":1"
                value: Combinator.ratio
                onMoved: (v) => Combinator.ratio = v }
            CtlRow { label: qsTr("Thresh"); from: -40; to: 0; step: 0.5; decimals: 1; suffix: " dB"
                value: Combinator.threshDb
                onMoved: (v) => Combinator.threshDb = v }
            CtlRow { label: qsTr("Makeup"); from: -10; to: 10; step: 0.5; decimals: 1; suffix: " dB"
                value: Combinator.makeupDb
                onMoved: (v) => Combinator.makeupDb = v }
            CtlRow { label: qsTr("X-Over"); from: -30; to: 30; step: 1
                value: Combinator.xover
                onMoved: (v) => Combinator.xover = v }
        }

        // ── SBC (automatic spectral balance) ───────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            opacity: (Combinator.bypass || root.rackInactive) ? 0.5 : 1.0
            Button {
                id: sbcBtn
                checkable: true; checked: Combinator.sbcEnabled
                implicitWidth: 56; implicitHeight: 22
                onClicked: Combinator.sbcEnabled = checked
                background: Rectangle { radius: 4
                    color: sbcBtn.checked ? "#0b3a44" : "#1f2a35"
                    border.color: sbcBtn.checked ? root.cAccent : "#3a5060" }
                contentItem: Text { text: "SBC"
                    color: sbcBtn.checked ? root.cAccent : root.cMuted
                    font.bold: true; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter }
            }
            CtlRow { label: qsTr("Speed"); from: 0; to: 10; step: 1
                enabled: Combinator.sbcEnabled
                value: Combinator.sbcSpeed
                onMoved: (v) => Combinator.sbcSpeed = v }
        }

        // ── Per-band gain-reduction meters ─────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            spacing: 6
            Repeater {
                model: Combinator.numBands
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Rectangle {   // GR meter: fills downward from the top
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        color: "#0b141b"; border.color: "#1c2a36"; radius: 2
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; top: parent.top
                                      margins: 2 }
                            // GR 0..18 dB -> 0..(h-4) px
                            height: { root.mtick
                                var gr = Combinator.bandReductionDb(index)
                                return Math.max(0, Math.min(1, gr / 18.0)) * (parent.height - 4) }
                            color: root.bandCol[index]
                            radius: 1
                        }
                    }
                    Label { text: Combinator.bandName(index)
                        color: index === Combinator.selectedBand ? root.bandCol[index] : root.cText
                        font.pixelSize: 11; font.bold: index === Combinator.selectedBand
                        Layout.alignment: Qt.AlignHCenter }
                }
            }
        }

        // ── Band selector ──────────────────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Combinator.numBands
                delegate: Button {
                    Layout.fillWidth: true
                    implicitHeight: 24
                    checkable: true
                    checked: index === Combinator.selectedBand
                    onClicked: Combinator.selectedBand = index
                    background: Rectangle { radius: 4
                        color: checked ? "#0b3a44" : "#1f2a35"
                        border.color: checked ? root.bandCol[index] : "#3a5060"
                        border.width: checked ? 2 : 1 }
                    contentItem: Text { text: Combinator.bandName(index)
                        color: checked ? root.bandCol[index] : root.cText
                        font.pixelSize: 11; font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        // ── Selected band: THRESH / GAIN / enable ──────────────────────
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 4
            opacity: (Combinator.bypass || root.rackInactive) ? 0.5 : 1.0
            RowLayout {
                Layout.fillWidth: true
                Label { text: { root.rev; qsTr("Band: ") + Combinator.bandName(Combinator.selectedBand) }
                    color: root.cText; font.bold: true; font.pixelSize: 12
                    Layout.fillWidth: true }
                Button {
                    id: bandEn
                    checkable: true
                    checked: { root.rev; Combinator.bandEnabled(Combinator.selectedBand) }
                    implicitWidth: 72; implicitHeight: 22
                    onClicked: Combinator.setBandEnabled(Combinator.selectedBand, checked)
                    background: Rectangle { radius: 4
                        color: bandEn.checked ? "#0b3a44" : "#1f2a35"
                        border.color: bandEn.checked ? root.cAccent : "#3a5060" }
                    contentItem: Text { text: bandEn.checked ? qsTr("BAND ON") : qsTr("BAND OFF")
                        color: bandEn.checked ? root.cAccent : root.cMuted
                        font.pixelSize: 11; font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter }
                }
            }
            CtlRow { label: qsTr("Thresh"); from: -10; to: 10; step: 0.5; decimals: 1; suffix: " dB"
                value: { root.rev; Combinator.bandThreshDb(Combinator.selectedBand) }
                onMoved: (v) => Combinator.setBandThreshDb(Combinator.selectedBand, v) }
            CtlRow { label: qsTr("Gain"); from: -10; to: 10; step: 0.5; decimals: 1; suffix: " dB"
                value: { root.rev; Combinator.bandGainDb(Combinator.selectedBand) }
                onMoved: (v) => Combinator.setBandGainDb(Combinator.selectedBand, v) }
        }
        }
    }
}
