// Lyra VFO style — Widget.
//
// A macOS/iOS-widget tile: frosted rounded square with the frequency
// stacked — MHz small (no leading zeros), kHz huge, Hz in cyan.  Hidden MHz
// places report empty rects (can't be clicked).  Compact Step/Mode.
// Style contract: see VfoBox.qml.
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

    readonly property real naturalWidth: 230
    readonly property real naturalHeight: 230
    readonly property rect tuneRect: Qt.rect(10, 36, 210, 148)
    readonly property rect stepModeRect: Qt.rect(14, 192, 202, 26)
    readonly property rect txPipRect: Qt.rect(188, 13, 28, 15)
    readonly property var theme: ({
        label: "#cfe6ff", field: "#1d3248", fieldBorder: "#3f6a90",
        text: "#eaf4ff", accent: "#00e5ff",
        pipIdle: "#1d3248", pipIdleBorder: "#4a7090", pipIdleText: "#8fb0cc",
        compact: true
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff4136"
                                      : vfoState === "armed" ? "#3a4250" : "#34c759"

    readonly property string mhzText: String(Math.floor(freqHz / 1000000))  // "0" below 1 MHz

    // Cell widths come from hidden probe Texts using the drawn fonts, so they
    // re-evaluate when fonts load/change (a binding on a FontMetrics method
    // would not).
    Text { id: mProbe; visible: false; text: "0"; font: mhz.font }
    Text { id: kProbe; visible: false; text: "0"; font: khz.font }
    Text { id: hProbe; visible: false; text: "0"; font: hzDigits.font }
    Text { id: dotProbe; visible: false; text: "."; font: hzDigits.font }
    FontMetrics { id: fmM; font: mhz.font }
    FontMetrics { id: fmK; font: khz.font }
    FontMetrics { id: fmH; font: hzDigits.font }
    readonly property real mW: mProbe.implicitWidth
    readonly property real kW: kProbe.implicitWidth
    readonly property real hW: hProbe.implicitWidth

    // kHz rect covers the glyph (cap height), not the whole line box, so it
    // stays clear of the Hz row tucked under the big digits.
    readonly property real kCap: Math.max(fmK.capitalHeight, 0.7 * khz.font.pixelSize)
    readonly property real kTop: khz.y + fmK.ascent - kCap
    readonly property var digitRects: {
        var a = []
        for (var p = 0; p < 3; ++p)        // Hz: places 0..2, left→right = 2,1,0
            a.push(Qt.rect(hzDigits.x + (2 - p) * hW, hzDigits.y, hW, fmH.height))
        for (var q = 0; q < 3; ++q)        // kHz: places 3..5
            a.push(Qt.rect(khz.x + (2 - q) * kW, kTop, kW, kCap + 2))
        for (var k = 0; k < 4; ++k) {      // MHz: places 6..9, only shown digits
            if (k < mhzText.length)
                a.push(Qt.rect(mhz.x + (mhzText.length - 1 - k) * mW, mhz.y, mW, fmM.height))
            else
                a.push(Qt.rect(0, 0, 0, 0))
        }
        return a
    }

    width: naturalWidth
    height: naturalHeight

    Rectangle {
        anchors.fill: parent
        radius: 28
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#284664" }
            GradientStop { position: 1.0; color: "#0e1824" }
        }
        border.width: 2
        border.color: Qt.rgba(st.stateColor.r, st.stateColor.g, st.stateColor.b, 0.8)
    }
    Rectangle {   // top sheen
        x: 10; y: 4
        width: parent.width - 20
        height: 50
        radius: 22
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.12) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    Text {
        x: 20; y: 14
        text: qsTr("VFO ") + st.vfoLetter
        color: "#8fd0ff"
        font.family: st.uiFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
    Text {
        x: st.txPipRect.x - 8 - implicitWidth
        y: 14
        text: "● " + st.roleText
        color: st.vfoState === "tx" ? "#ff6b5e" : "#8fd0ff"
        font.family: st.uiFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Row {    // MHz — one centred cell per digit, then the point
        id: mhz
        property font font: Qt.font({ family: st.uiFamily, pixelSize: 30 })
        x: 20; y: 40
        Repeater {
            model: st.mhzText.length
            Text {
                required property int index
                width: st.mW
                horizontalAlignment: Text.AlignHCenter
                text: st.mhzText.charAt(index)
                color: "#cfe6ff"
                font: mhz.font
            }
        }
        Text { text: "."; color: "#cfe6ff"; font: mhz.font }
    }
    Row {    // kHz — one centred cell per digit (proportional figures)
        id: khz
        property font font: Qt.font({ family: st.uiFamily, pixelSize: 86,
                                      weight: Font.ExtraLight })
        x: 16; y: 66
        Repeater {
            model: 3
            Text {
                required property int index
                width: st.kW
                horizontalAlignment: Text.AlignHCenter
                text: st.digitString.charAt(4 + index)
                color: "#ffffff"
                font: khz.font
            }
        }
    }
    Text {
        x: 20; y: 154
        text: "."
        color: "#00e5ff"
        font: hzDigits.font
    }
    Row {    // Hz digits
        id: hzDigits
        property font font: Qt.font({ family: st.uiFamily, pixelSize: 18 })
        x: 20 + dotProbe.implicitWidth
        y: 154
        Repeater {
            model: 3
            Text {
                required property int index
                width: st.hW
                horizontalAlignment: Text.AlignHCenter
                text: st.digitString.charAt(7 + index)
                color: "#00e5ff"
                font: hzDigits.font
            }
        }
    }
    Text {
        x: hzDigits.x + 3 * st.hW + 4
        y: 154
        text: "Hz"
        color: "#00e5ff"
        font: hzDigits.font
    }

    Rectangle {   // selected-digit underline (on whichever row holds it)
        readonly property rect r: st.selectedDigit >= 0
                                  ? st.digitRects[st.selectedDigit] : Qt.rect(0, 0, 0, 0)
        visible: r.width > 0
        x: r.x + 2
        y: r.y + r.height - 2
        width: Math.max(0, r.width - 4)
        height: 2
        color: "#00e5ff"
    }
}
