// Lyra VFO style — Amber LED (the original look).
//
// A port of FreqDisplay's old QPainter readout: amber digits on near-black
// with a ghost "8" behind each (unlit-segment look), dimmed leading MHz
// zeros, cyan selected-digit underline and MHz/kHz/Hz labels, in the
// original 360×108 bordered box.  Digits are Consolas Bold where installed,
// else Courier New Bold, as the original readout rendered per platform.
// Style contract: see VfoBox.qml.
import QtQuick

Item {
    id: st
    // ── Contract inputs (set by VfoBox) ──
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"          // "rx" | "tx" | "armed"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true
    // ── Contract outputs ──
    readonly property real naturalWidth: 360
    readonly property real naturalHeight: 108
    readonly property rect tuneRect: Qt.rect(8, 4, 344, 64)
    readonly property rect stepModeRect: Qt.rect(8, 78, 344, 24)
    readonly property rect txPipRect: Qt.rect(327, 5, 28, 15)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#cccccc", field: "#161e28", fieldBorder: "#2a3a4a",
        text: "#f2f8fc", accent: "#50d0ff",
        pipIdle: "#2a323c", pipIdleBorder: "#5a6570", pipIdleText: "#7a8490",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    // First installed family, like the old QFont::setFamilies fallback list.
    function pickFamily(list) {
        var have = Qt.fontFamilies()
        for (var i = 0; i < list.length; ++i)
            if (have.indexOf(list[i]) >= 0) return list[i]
        return list[list.length - 1]
    }
    readonly property string digitFamily:
        pickFamily(["Consolas", "Courier New"])

    width: naturalWidth
    height: naturalHeight

    Rectangle {
        anchors.fill: parent
        color: "#0a0e12"
        radius: 6
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height

        Rectangle { anchors.fill: parent; color: "#040406" }

        VfoDigitRow {
            id: digits
            digitString: st.digitString
            digitFont: Qt.font({ family: st.digitFamily,
                                 pixelSize: Math.max(24, Math.floor(area.height * 0.64)),
                                 weight: Font.Bold })
            digitColor: "#ffab47"
            dimColor: "#181006"
            ghostText: "8"
            ghostColor: "#181006"
            sepColor: "#ffab47"
            selectedDigit: st.selectedDigit
            selectColor: "#00e5ff"
            renderType: Text.NativeRendering
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }

        FontMetrics {
            id: unitFm
            font.family: st.digitFamily
            font.pixelSize: Math.max(11, Math.floor(area.height * 0.19))
            font.bold: true
        }
        Repeater {
            model: ["MHz", "kHz", "Hz"]
            Text {
                required property int index
                required property string modelData
                text: modelData
                font: unitFm.font
                color: "#b9cadc"
                renderType: Text.NativeRendering
                x: digits.x + digits.groupCenters[index] - implicitWidth / 2
                y: area.height - 4 - unitFm.ascent
            }
        }
    }

    Text {   // amber role tag, upper-left — over the readout, as before
        x: 8
        y: 4
        text: st.roleText
        color: "#efb340"
        font.bold: true
        font.pixelSize: 12
    }
}
