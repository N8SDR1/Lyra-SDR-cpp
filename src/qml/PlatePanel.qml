// Lyra — TX Plate dock panel (#52).  Schroeder-Moorer plate reverb bound to
// the Plate context property (lyra::ui::PlateModel).  Last native rack stage
// (after the Combinator); auto-bypasses in DIGU/DIGL with the rest of the
// rack.  Dockable / collapsible / View-hideable like every panel.  Default
// stage OFF.  PlateModel uses real Q_PROPERTY + NOTIFY, so bindings auto-
// update (no revision tick).

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
    // gated by SetTxRackBypass) and is moot in CW.  Dim the ON lamp +
    // controls so the operator sees the rack isn't shaping audio there.
    // Purely visual — model/profile state untouched, re-lights on USB/LSB.
    readonly property bool rackInactive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "DIGU" || m === "DIGL" || m === "CWU" || m === "CWL"
    }

    component CtlRow : RowLayout {
        property string label: ""
        property real from: 0
        property real to: 1
        property real step: 1
        property real value: 0
        property string suffix: ""
        property int decimals: 0
        property bool enab: true
        signal moved(real v)
        Layout.fillWidth: true
        spacing: 8
        Label { text: parent.label; color: root.cText; font.bold: true
                font.pixelSize: 13; Layout.preferredWidth: 78 }
        LyraSlider {
            Layout.fillWidth: true
            enabled: parent.enab
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
        contentWidth: Math.max(428, availableWidth)
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
            Label { text: qsTr("Plating"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label { text: qsTr("ESSB air"); color: root.cMuted
                    font.pixelSize: 11 }
            Label {
                visible: root.rackInactive
                text: qsTr("• bypassed (") + WdspEngine.mode.toUpperCase() + ")"
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                id: onBtn
                checkable: true; checked: !Plate.bypass
                implicitWidth: 50; implicitHeight: 22
                onClicked: Plate.bypass = !checked
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

        // ── Preset picker (loads the 8 reverb params; leaves MIX) ──────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Preset"); color: root.cMuted; font.pixelSize: 12 }
            Repeater {
                model: 2
                delegate: Button {
                    text: Plate.presetName(index)
                    implicitHeight: 22; Layout.preferredWidth: 70
                    onClicked: Plate.loadPreset(index)
                    background: Rectangle { radius: 4; color: "#1f2a35"
                        border.color: "#3a5060" }
                    contentItem: Text { text: parent.text; color: root.cText
                        font.pixelSize: 11; font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Reverb controls ────────────────────────────────────────────
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 4
            opacity: (Plate.bypass || root.rackInactive) ? 0.5 : 1.0
            CtlRow { label: qsTr("Pre-Delay"); from: 0; to: 100; step: 1; suffix: " ms"
                value: Plate.preDelayMs; onMoved: (v) => Plate.preDelayMs = v }
            CtlRow { label: qsTr("Decay"); from: 0.1; to: 5.0; step: 0.05; decimals: 2; suffix: " s"
                value: Plate.decayS; onMoved: (v) => Plate.decayS = v }
            CtlRow { label: qsTr("Damp"); from: 1; to: 100; step: 1
                value: Plate.damp; onMoved: (v) => Plate.damp = v }
            CtlRow { label: qsTr("Size"); from: 1; to: 100; step: 1
                value: Plate.size; onMoved: (v) => Plate.size = v }
            CtlRow { label: qsTr("Density"); from: 1; to: 100; step: 1
                value: Plate.density; onMoved: (v) => Plate.density = v }
            CtlRow { label: qsTr("Diffusion"); from: 1; to: 100; step: 1
                value: Plate.diff; onMoved: (v) => Plate.diff = v }
            CtlRow { label: qsTr("Bass"); from: -18; to: 18; step: 1; suffix: " dB"
                value: Plate.bassDb; onMoved: (v) => Plate.bassDb = v }
            CtlRow { label: qsTr("Treble"); from: -18; to: 18; step: 1; suffix: " dB"
                value: Plate.trebDb; onMoved: (v) => Plate.trebDb = v }
            // MIX caps at 15 % (ESSB useful range); operator default 7 %.
            CtlRow { label: qsTr("Mix"); from: 0; to: 15; step: 1; suffix: " %"
                value: Plate.mix * 100
                onMoved: (v) => Plate.mix = v / 100 }
        }
        }
    }
}
