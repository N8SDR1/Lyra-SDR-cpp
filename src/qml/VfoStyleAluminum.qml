// Lyra VFO style — Aluminum.
//
// The light-mode partner of Lyra (Mac): brushed-silver body, white inset
// well, dark thin digits, blue decimal points and selection, an RX/TX pill
// in the state colour, compact pill pickers.  Style contract: VfoBox.qml.
import QtQuick

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 380
    readonly property real naturalHeight: 128
    readonly property rect tuneRect: Qt.rect(14, 12, 352, 64)
    readonly property rect stepModeRect: Qt.rect(64, 87, 264, 26)
    readonly property rect txPipRect: Qt.rect(338, 92, 28, 16)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#3a3a3c", field: "#ffffff", fieldBorder: "#c4c8ce",
        text: "#1d1d1f", accent: "#0a84ff",
        pipIdle: "#ffffff", pipIdleBorder: "#c4c8ce", pipIdleText: "#8e8e93",
        compact: true
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff3b30"
                                      : vfoState === "armed" ? "#8e8e93" : "#34c759"

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // body
        anchors.fill: parent
        radius: 18
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f4f5f7" }
            GradientStop { position: 1.0; color: "#d9dce1" }
        }
        border.width: 2
        border.color: st.stateColor
    }
    Rectangle {   // top edge highlight
        x: 14; y: 2
        width: parent.width - 28
        height: 1
        color: Qt.rgba(1, 1, 1, 0.9)
    }
    Rectangle {   // inset well
        x: st.tuneRect.x; y: st.tuneRect.y
        width: st.tuneRect.width; height: st.tuneRect.height
        radius: 12
        color: "#fbfbfc"
        border.width: 1
        border.color: "#c9cdd3"
    }
    Rectangle {   // well inner shadow
        x: st.tuneRect.x + 1; y: st.tuneRect.y + 1
        width: st.tuneRect.width - 2
        height: 7
        radius: 11
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.10) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height
        VfoDigitRow {
            id: digits
            digitString: st.digitString
            digitFont: Qt.font({ family: st.uiFamily, pixelSize: 44, weight: Font.Light })
            digitColor: "#1d1d1f"
            dimColor: "#c4c7cc"
            sepColor: "#0a84ff"
            selectedDigit: st.selectedDigit
            selectColor: "#0a84ff"
            selectThickness: 2
            centerGlyphs: true
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
    }

    Rectangle {   // RX / TX pill
        x: 14; y: 88
        width: 44; height: 24
        radius: 12
        color: st.stateColor
        Text {
            anchors.centerIn: parent
            text: st.roleText
            color: "#ffffff"
            font.family: st.uiFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }
}
