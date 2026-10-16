// Lyra VFO style — Nixie.
//
// Each digit in its own glass tube with a warm orange glow; unlit leading
// tubes stay dark.  Serif brass labels on a fine-mesh background; the rim
// carries the RX/TX/armed colour.  Style contract: see VfoBox.qml.
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

    readonly property real naturalWidth: 420
    readonly property real naturalHeight: 134
    readonly property rect tuneRect: Qt.rect(30, 26, 360, 70)
    readonly property rect stepModeRect: Qt.rect(14, 104, 392, 24)
    readonly property rect txPipRect: Qt.rect(378, 6, 28, 15)
    readonly property var theme: ({
        label: "#d9b48a", field: "#1b2a3a", fieldBorder: "#3a5a78",
        text: "#f0d6b4", accent: "#ff8a2b",
        pipIdle: "#22303e", pipIdleBorder: "#4a6078", pipIdleText: "#8aa0b4",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property real tubeW: 30
    readonly property real tubeH: 64
    readonly property real tubeY: 28
    readonly property real x0: Math.round((naturalWidth - (9 * 33 + 30 + 22)) / 2)
    function tubeX(i) { return x0 + i * 33 + (i > 3 ? 11 : 0) + (i > 6 ? 11 : 0) }
    function leadingZero(i) {
        if (9 - i < 6) return false
        for (var j = 0; j <= i; ++j)
            if (digitString.charAt(j) !== "0") return false
        return true
    }
    readonly property var digitRects: {
        var a = []
        for (var place = 0; place < 10; ++place)
            a.push(Qt.rect(tubeX(9 - place), tubeY, tubeW, tubeH))
        return a
    }

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // rim only — the panel color shows through
        anchors.fill: parent
        radius: 8
        color: "transparent"
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }

    Repeater {   // tube glass
        model: 10
        Rectangle {
            required property int index
            x: st.tubeX(index)
            y: st.tubeY
            width: st.tubeW
            height: st.tubeH
            topLeftRadius: 15
            topRightRadius: 15
            bottomLeftRadius: 6
            bottomRightRadius: 6
            color: "transparent"
            border.width: 1.5
            border.color: "#ff8a2b"
        }
    }

    Item {   // lit cathodes — drawn through the glow below
        id: glyphs
        anchors.fill: parent
        visible: st.softwareRender
        Repeater {
            model: 10
            Text {
                required property int index
                x: st.tubeX(index)
                y: st.tubeY
                width: st.tubeW
                height: st.tubeH
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: st.digitString.charAt(index)
                font.family: "Nixie One"
                font.pixelSize: 44
                color: st.leadingZero(index) ? Qt.rgba(1, 138 / 255, 43 / 255, 0.07) : "#ff8a2b"
            }
        }
        Repeater {
            model: [3, 6]
            Text {
                required property int modelData
                x: st.tubeX(modelData) + st.tubeW + 1
                y: st.tubeY + st.tubeH - 22
                text: "."
                font.family: "Nixie One"
                font.pixelSize: 22
                color: "#ff8a2b"
            }
        }
    }
    MultiEffect {
        visible: !st.softwareRender
        source: glyphs
        anchors.fill: glyphs
        shadowEnabled: true
        shadowColor: "#ff6a00"
        shadowOpacity: 0.9
        shadowBlur: 0.7
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        blurMax: 24
    }

    Rectangle {   // selected-digit underline
        visible: st.selectedDigit >= 0
        x: st.tubeX(9 - st.selectedDigit) + 3
        y: st.tubeY + st.tubeH + 2
        width: st.tubeW - 6
        height: 2
        color: "#ffb066"
    }

    Repeater {
        model: [{ t: "MHz", a: 0, b: 3 }, { t: "kHz", a: 4, b: 6 }, { t: "Hz", a: 7, b: 9 }]
        Text {
            required property var modelData
            text: modelData.t
            color: "#b08a63"
            font.family: "Georgia"
            font.pixelSize: 11
            font.letterSpacing: 1
            x: (st.tubeX(modelData.a) + st.tubeX(modelData.b) + st.tubeW) / 2 - implicitWidth / 2
            y: st.tubeY + st.tubeH + 6
        }
    }

    Text {
        x: 14; y: 6
        text: st.roleText
        color: st.vfoState === "tx" ? "#ff5a3a" : "#d9a066"
        font.family: "Georgia"
        font.pixelSize: 12
        font.bold: true
    }
}
