// Lyra — PureSignal dock (P5).
// ON / 2-tone / live GetPSInfo / Reset. Attestation default OFF.
// Dummy load first. Stream context is set in MainWindow.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 72
    implicitWidth: 900
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property int lyraMinWidth:  body.implicitWidth + 24
    readonly property int lyraMinHeight: body.implicitHeight + 16

    readonly property color cText:    "#cdd9e5"
    readonly property color cMuted:   "#8a9aac"
    readonly property color cOn:      "#ff9a3c"
    readonly property color cMox:     "#d11515"
    readonly property color cMoxEdge: "#ff8080"
    readonly property color cFbOk:    "#3ecf6a"
    readonly property color cFbHot:   "#e23d3d"
    readonly property color cFbLow:   "#2ec4d4"

    // Mode combo (focused RX). TXA can still be USB while this reads DIGU.
    readonly property bool digitalMode: {
        var rx2 = Stream.subEnabled && Stream.focusedRx === 2
        var m = String(rx2 ? Prefs.modeRx2 : Prefs.mode).toUpperCase()
        return m === "DIGU" || m === "DIGL" || m === "DRM"
            || m === "CWL" || m === "CWU" || m === "FM"
    }
    readonly property bool liveOn: Stream.psLiveArmed && !root.digitalMode
    readonly property bool digitalLock: Stream.psDigitalLockout || root.digitalMode

    function fbZoneColor() {
        if (!(Stream.moxActive && root.liveOn))
            return root.cMuted
        var fb = Number(Stream.psFeedbackLevel)
        if (fb > 181)
            return root.cFbHot
        if (fb >= 129)
            return root.cFbOk
        return root.cFbLow
    }

    RowLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        Label {
            text: qsTr("PS")
            color: root.cOn
            font.bold: true
            font.pixelSize: 13
        }

        Button {
            id: onBtn
            focusPolicy: Qt.NoFocus
            implicitWidth: 56
            implicitHeight: 26
            text: root.liveOn ? qsTr("ON") : qsTr("OFF")
            font.bold: true
            font.pixelSize: 12
            enabled: Stream.psAttestation && !root.digitalLock
            onClicked: Stream.setPsArmed(!Stream.psArmed)
            background: Rectangle {
                radius: 4
                color: root.liveOn ? "#30200c" : "#161e28"
                border.color: root.liveOn ? root.cOn : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: onBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: root.liveOn ? root.cOn : root.cText
                font: onBtn.font
            }
            ToolTip.text: root.digitalLock
                ? qsTr("PureSignal is off in DIGU / DIGL / DRM / CWL / CWU / FM. Arm restores when you leave those modes.")
                : (Stream.psAttestation
                ? qsTr("Arm PureSignal. HL2: coupler mux while MOX. Brick: DDC0/DDC1 at TX + Alex PS bit; keyed PS uses one combined IQ stream.")
                : qsTr("Check Settings → TX: I have the PureSignal hardware coupler."))
            ToolTip.delay: 800
            ToolTip.visible: hovered && Prefs.tooltipsEnabled
        }

        Button {
            id: twoToneBtn
            focusPolicy: Qt.NoFocus
            implicitWidth: 72
            implicitHeight: 26
            text: qsTr("2-tone")
            font.bold: true
            font.pixelSize: 12
            enabled: Stream.psAttestation && root.liveOn
                     && (!Stream.tuneEnabled || Stream.twoToneEnabled)
            onClicked: {
                if (!Stream.twoToneEnabled) {
                    Stream.setTwoToneEnabled(true)
                    Stream.requestMox(true)
                } else {
                    Stream.requestMox(false)
                    Stream.setTwoToneEnabled(false)
                }
            }
            background: Rectangle {
                radius: 4
                color: Stream.twoToneEnabled ? "#302044" : "#161e28"
                border.color: Stream.twoToneEnabled ? "#d68cff" : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: twoToneBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Stream.twoToneEnabled ? "#d68cff" : root.cText
                font: twoToneBtn.font
            }
            ToolTip.text: qsTr("Two-tone into the TX chain (same as TX dock). Dummy load.")
            ToolTip.delay: 800
            ToolTip.visible: hovered && Prefs.tooltipsEnabled
        }

        Rectangle {
            id: attChip
            implicitHeight: 26
            implicitWidth: attTxt.implicitWidth + 16
            radius: 4
            color: Stream.moxActive && root.liveOn ? "#30200c" : "transparent"
            border.color: Stream.moxActive && root.liveOn ? root.cOn : "#2a3a4a"
            border.width: Stream.moxActive && root.liveOn ? 2 : 1
            HoverHandler { id: attHover }
            Label {
                id: attTxt
                anchors.centerIn: parent
                text: qsTr("ATT %1 dB").arg(Stream.psAutoAttDb)
                color: Stream.moxActive && root.liveOn ? root.cOn : root.cMuted
                font.bold: Stream.moxActive && root.liveOn
                font.pixelSize: 12
            }
            ToolTip.text: qsTr("Auto-att while PS is keyed. Brick/Hermes: ADC0 coupler pad 0…31 dB. HL2: TX step-att −28…+31 dB.")
            ToolTip.delay: 800
            ToolTip.visible: attHover.hovered && Prefs.tooltipsEnabled
        }

        Rectangle {
            id: fbChip
            implicitHeight: 26
            implicitWidth: fbTxt.implicitWidth + 16
            radius: 4
            color: "transparent"
            border.color: root.fbZoneColor()
            border.width: Stream.moxActive && root.liveOn ? 2 : 1
            HoverHandler { id: fbHover }
            Label {
                id: fbTxt
                anchors.centerIn: parent
                text: qsTr("FB %1").arg(Stream.psFeedbackLevel)
                color: root.fbZoneColor()
                font.bold: Stream.moxActive && root.liveOn
                font.pixelSize: 12
            }
            ToolTip.text: qsTr("Coupler FB. Teal below 129 (low), green 129–181, red above 181 (hot).")
            ToolTip.delay: 800
            ToolTip.visible: fbHover.hovered && Prefs.tooltipsEnabled
        }

        Label {
            Layout.fillWidth: true
            text: {
                var d0s = Stream.psFeedSpr <= 0 ? "n/a"
                          : (Stream.psDdc0Dbfs <= -999 ? "n/a"
                             : (Stream.psDdc0Dbfs + " dB"))
                var d1s = Stream.psFeedSpr <= 0 ? "n/a"
                          : (Stream.psDdc1Dbfs <= -999 ? "n/a"
                             : (Stream.psDdc1Dbfs + " dB"))
                return qsTr("st %1  cal %2  %3    D0 %4  D1 %5  in %6")
                    .arg(Stream.psFsmState)
                    .arg(Stream.psCalCount)
                    .arg(Stream.psCorrecting ? qsTr("correcting") : qsTr("idle"))
                    .arg(d0s).arg(d1s).arg(Stream.psFeedSpr)
            }
            color: Stream.moxActive && root.liveOn ? root.cText : root.cMuted
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Button {
            id: resetBtn
            focusPolicy: Qt.NoFocus
            implicitWidth: 64
            implicitHeight: 26
            text: qsTr("Reset")
            font.pixelSize: 12
            enabled: Stream.psAttestation && root.liveOn
            onClicked: Stream.resetPureSignal()
            background: Rectangle {
                radius: 4
                color: "#161e28"
                border.color: "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: resetBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: root.cText
                font: resetBtn.font
            }
            ToolTip.text: qsTr("Restart the WDSP calibrator (SetPSControl).")
            ToolTip.delay: 800
            ToolTip.visible: hovered && Prefs.tooltipsEnabled
        }
    }
}
