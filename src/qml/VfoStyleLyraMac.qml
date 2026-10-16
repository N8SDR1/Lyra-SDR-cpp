// Lyra VFO style — Lyra (Mac).  The default.
//
// A raised glass panel in the Lyra palette: blue-slate gradient body, soft
// top highlight and bottom shade for depth, a thin rim in the RX/TX/armed
// colour; thin system-font digits (SF on macOS, Inter elsewhere) with a
// faint cyan halo and cyan decimal points; the Lyra constellation (Vega and
// the parallelogram) etched top-right.  Style contract: see VfoBox.qml.
import QtQuick
import QtQuick.Effects

Item {
    id: st
    // Qt Quick's software renderer cannot run MultiEffect; show the source directly (no glow) there.
    readonly property bool softwareRender: GraphicsInfo.api === GraphicsInfo.Software
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 380
    readonly property real naturalHeight: 140
    readonly property rect tuneRect: Qt.rect(10, 26, 360, 64)
    readonly property rect stepModeRect: Qt.rect(10, 106, 322, 26)
    readonly property rect txPipRect: Qt.rect(342, 111, 28, 16)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#c5d6e6", field: "#1b2a3a", fieldBorder: "#3a5a78",
        text: "#eaf4ff", accent: "#00e5ff",
        pipIdle: "#22303e", pipIdleBorder: "#4a6078", pipIdleText: "#8aa0b4",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff4136"
                                      : vfoState === "armed" ? "#3a4250" : "#34c759"

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // body
        anchors.fill: parent
        radius: 14
        gradient: Gradient {
            GradientStop { position: 0.0;  color: "#1d3346" }
            GradientStop { position: 0.55; color: "#0f1a26" }
            GradientStop { position: 1.0;  color: "#0a1119" }
        }
        border.width: 2
        border.color: Qt.rgba(st.stateColor.r, st.stateColor.g, st.stateColor.b, 0.75)
    }
    Rectangle {   // top glass highlight
        x: 8; y: 5
        width: parent.width - 16
        height: 40
        radius: 10
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
    Rectangle {   // bottom inner shade — depth
        x: 2
        y: parent.height - 24
        width: parent.width - 4
        height: 22
        radius: 12
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.35) }
        }
    }

    Canvas {   // the Lyra constellation — static, painted once per size
        renderStrategy: Canvas.Cooperative
        x: parent.width - 66
        y: 4
        width: 56
        height: 53
        opacity: 0.5
        onPaint: {
            var c = getContext("2d")
            c.reset()
            c.scale(0.75, 0.75)
            // Vega, ε, ζ, δ, γ (Sulafat), β (Sheliak) …
            var stars = [[12, 8, 3.4], [22, 4, 1.3], [26, 18, 1.6], [36, 30, 1.6],
                         [58, 36, 1.5], [62, 62, 2.0], [40, 58, 2.0]]
            var lines = [[0, 2], [0, 1], [2, 3], [3, 4], [4, 5], [5, 6], [6, 3]]
            c.strokeStyle = "rgba(0,229,255,0.55)"
            c.lineWidth = 1
            c.beginPath()
            for (var i = 0; i < lines.length; ++i) {
                var a = stars[lines[i][0]], b = stars[lines[i][1]]
                c.moveTo(a[0], a[1])
                c.lineTo(b[0], b[1])
            }
            c.stroke()
            c.fillStyle = "#cfefff"
            for (var j = 0; j < stars.length; ++j) {
                c.beginPath()
                c.arc(stars[j][0], stars[j][1], stars[j][2], 0, 2 * Math.PI)
                c.fill()
            }
        }
    }

    Text {   // role tag
        x: 16; y: 8
        text: "● " + st.roleText
        color: st.vfoState === "tx" ? "#ff6b5e" : "#00e5ff"
        font.family: st.uiFamily
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height

        VfoDigitRow {
            id: digits
            visible: st.softwareRender   // else drawn through the halo effect below
            digitString: st.digitString
            digitFont: Qt.font({ family: st.uiFamily, pixelSize: 48, weight: Font.Light })
            digitColor: "#f2f8ff"
            dimColor: Qt.rgba(242 / 255, 248 / 255, 1, 0.13)
            sepColor: "#00e5ff"
            selectedDigit: st.selectedDigit
            selectColor: "#00e5ff"
            selectThickness: 2
            centerGlyphs: true
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
        MultiEffect {
            visible: !st.softwareRender
            source: digits
            anchors.fill: digits
            shadowEnabled: true
            shadowColor: "#00e5ff"
            shadowOpacity: 0.45
            shadowBlur: 0.6
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            blurMax: 16
        }
    }

    Repeater {   // MHz / kHz / Hz, under the readout
        model: ["MHz", "kHz", "Hz"]
        Text {
            required property int index
            required property string modelData
            text: modelData
            color: "#7fb3d0"
            font.family: st.uiFamily
            font.pixelSize: 11
            font.weight: Font.Medium
            font.letterSpacing: 1
            x: area.x + digits.x + digits.groupCenters[index] - implicitWidth / 2
            y: st.tuneRect.y + st.tuneRect.height
        }
    }
}
