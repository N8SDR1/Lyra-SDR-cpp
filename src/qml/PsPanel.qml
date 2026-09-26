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
            text: Stream.psArmed ? qsTr("ON") : qsTr("OFF")
            font.bold: true
            font.pixelSize: 12
            enabled: Stream.psAttestation
            onClicked: Stream.setPsArmed(!Stream.psArmed)
            background: Rectangle {
                radius: 4
                color: Stream.psArmed ? "#30200c" : "#161e28"
                border.color: Stream.psArmed ? root.cOn : "#2a3a4a"
                border.width: 2
            }
            contentItem: Text {
                text: onBtn.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Stream.psArmed ? root.cOn : root.cText
                font: onBtn.font
            }
            ToolTip.text: Stream.psAttestation
                ? qsTr("Arm PureSignal (puresignal_run). Mux cntrl1=4 only while MOX.")
                : qsTr("Check Settings → TX: I have the PureSignal hardware coupler.")
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
            enabled: Stream.psAttestation && Stream.psArmed
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

        Label {
            Layout.fillWidth: true
            text: {
                var d0s = Stream.psFeedSpr <= 0 ? "n/a"
                          : (Stream.psDdc0Dbfs <= -999 ? "n/a"
                             : (Stream.psDdc0Dbfs + " dB"))
                var d1s = Stream.psFeedSpr <= 0 ? "n/a"
                          : (Stream.psDdc1Dbfs <= -999 ? "n/a"
                             : (Stream.psDdc1Dbfs + " dB"))
                return qsTr("FB %1  st %2  cal %3  %4    D0 %5  D1 %6  in %7")
                    .arg(Stream.psFeedbackLevel)
                    .arg(Stream.psFsmState)
                    .arg(Stream.psCalCount)
                    .arg(Stream.psCorrecting ? qsTr("correcting") : qsTr("idle"))
                    .arg(d0s).arg(d1s).arg(Stream.psFeedSpr)
            }
            color: Stream.moxActive && Stream.psArmed ? root.cMox : root.cMuted
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
            enabled: Stream.psAttestation && Stream.psArmed
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
