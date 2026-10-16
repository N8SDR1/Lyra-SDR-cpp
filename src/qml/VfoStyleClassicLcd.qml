// Lyra VFO style — Classic LCD.
//
// Backlit grey-green glass set in a dark bezel, black 7-segment digits
// (DSEG7 Classic) with faint unlit segments — the older Yaesu/Kenwood look.
// The bezel rim carries the RX/TX/armed colour.  Style contract: VfoBox.qml.
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

    readonly property real naturalWidth: 360
    readonly property real naturalHeight: 118
    readonly property rect tuneRect: Qt.rect(14, 28, 332, 40)
    readonly property rect stepModeRect: Qt.rect(8, 88, 344, 24)
    readonly property rect txPipRect: Qt.rect(316, 12, 28, 14)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#c9ccd0", field: "#3a3e43", fieldBorder: "#555a60",
        text: "#e8eaec", accent: "#b9c79f",
        pipIdle: "#3a3e43", pipIdleBorder: "#5a5e63", pipIdleText: "#9a9ea3",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property color ink: "#1f2618"
    readonly property color unlit: Qt.rgba(31 / 255, 38 / 255, 24 / 255, 0.04)

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // bezel
        anchors.fill: parent
        radius: 8
        color: "#2a2d30"
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }
    Rectangle {   // backlit glass
        x: 8; y: 8
        width: parent.width - 16
        height: 74
        radius: 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#b9c79f" }
            GradientStop { position: 0.6; color: "#a3b387" }
            GradientStop { position: 1.0; color: "#95a679" }
        }
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.4)
    }
    Rectangle {   // glass inner shadow (top)
        x: 9; y: 9
        width: parent.width - 18
        height: 8
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.25) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    Text {
        x: 16; y: 12
        text: "VFO-" + st.vfoLetter + "   " + st.roleText
        color: "#2c3622"
        font.family: "Arial"
        font.pixelSize: 10
        font.bold: true
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
            digitFont: Qt.font({ family: "DSEG7 Classic", pixelSize: 30, weight: Font.Bold })
            digitColor: st.ink
            dimColor: st.unlit
            ghostText: "8"
            ghostColor: st.unlit
            sepColor: "transparent"   // DSEG "." does not draw here; dots below
            selectedDigit: st.selectedDigit
            selectColor: "#2c3622"
            selectThickness: 2
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
    }

    Repeater {   // decimal points, baseline-aligned in the separator gaps
        model: [4, 7]
        Rectangle {
            required property int modelData
            width: 4; height: 4
            color: st.ink
            x: area.x + digits.x + digits.cellX(modelData) - digits.sepW / 2 - width / 2
            y: area.y + digits.y + digits.baselineY - height
        }
    }

    Repeater {
        model: ["MHz", "kHz", "Hz"]
        Text {
            required property int index
            required property string modelData
            text: modelData
            color: "#2c3622"
            font.family: "Arial"
            font.pixelSize: 9
            x: area.x + digits.x + digits.groupCenters[index] - implicitWidth / 2
            y: st.tuneRect.y + st.tuneRect.height
        }
    }
}
