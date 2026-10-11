// Nixie meter face. Tubes and Nixie One digits sit on the meter
// panel itself — no second background.  A fixed tube count per source;
// a separate red OVER lamp lights in the over zone (MeterInk.js).
import QtQuick
import QtQuick.Effects
import "MeterInk.js" as MeterInk

Item {
    id: root
    anchors.fill: parent

    readonly property bool softwareRender: GraphicsInfo.api === GraphicsInfo.Software
    readonly property string reading: {
        var t = Meter.text.length ? Meter.text : "-"
        return t.replace("—", "-").replace("–", "-")
    }

    // A fixed number of tubes per meter source (MeterInk.readoutCells), so
    // the tubes do not resize or shift as the reading changes length.
    // Unused tubes stay unlit.  The S-meter reads from the left; numeric
    // readings sit right-aligned.
    readonly property int cells: MeterInk.readoutCells(Meter.source, root.reading)
    readonly property string padded: MeterInk.padCells(root.reading, root.cells,
                                                       Meter.source === 0)
    readonly property real gapK: 0.12
    readonly property real tubeH: {
        var th = Math.min(height * 0.52, 78)
        var tw = th * (30 / 64)
        var total = cells * tw + Math.max(0, cells - 1) * Math.max(2, tw * gapK)
        var limit = width * 0.92
        return total > limit && total > 0 ? th * limit / total : th
    }
    // First installed family, like the old QFont::setFamilies fallback list.
    readonly property string monoFamily: {
        var have = Qt.fontFamilies(), list = ["Consolas", "Menlo", "Courier New"]
        for (var i = 0; i < list.length; ++i)
            if (have.indexOf(list[i]) >= 0) return list[i]
        return "monospace"
    }
    readonly property real tubeW: tubeH * (30 / 64)
    readonly property real tubeGap: Math.max(2, tubeW * gapK)
    readonly property real rowX: (width - (cells * tubeW + Math.max(0, cells - 1) * tubeGap)) / 2
    readonly property real rowY: height * 0.10

    function cellChar(i) { return root.padded.charAt(i) }
    function isDot(ch) { return ch === "." || ch === "," }

    Repeater {
        model: root.cells
        delegate: Rectangle {
            required property int index
            readonly property string ch: root.cellChar(index)
            x: root.rowX + index * (root.tubeW + root.tubeGap)
            y: root.rowY
            width: root.tubeW
            height: root.tubeH
            topLeftRadius: width / 2
            topRightRadius: width / 2
            bottomLeftRadius: width * 0.2
            bottomRightRadius: width * 0.2
            color: "transparent"
            border.width: 1.5
            border.color: "#ff8a2b"
            opacity: ch === " " ? 0.35 : 1
        }
    }

    Item {
        id: glyphs
        anchors.fill: parent
        visible: root.softwareRender
        Repeater {
            model: root.cells
            delegate: Text {
                required property int index
                readonly property string ch: root.cellChar(index)
                visible: ch !== " "
                x: root.rowX + index * (root.tubeW + root.tubeGap)
                y: root.rowY
                width: root.tubeW
                height: root.tubeH
                text: ch
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: root.isDot(ch) ? Text.AlignBottom : Text.AlignVCenter
                font.family: "Nixie One"
                font.pixelSize: Math.max(8, Math.round(root.tubeH * (root.isDot(ch) ? 22 : 44) / 64))
                color: "#ff8a2b"
            }
        }
    }
    MultiEffect {
        visible: !root.softwareRender
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

    Rectangle {
        x: parent.width * 0.12
        y: parent.height * 0.78
        width: parent.width * 0.76
        height: Math.max(3, parent.height * 0.028)
        radius: height / 2
        color: "#10141c"
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, Meter.level))
            height: parent.height
            radius: parent.radius
            color: "#00e5ff"
        }
        Rectangle {
            x: parent.width * Math.max(0, Math.min(1, Meter.peak)) - 1
            y: -3
            width: 1.5
            height: parent.height + 6
            color: "#cdd9e5"
        }
        Rectangle {
            visible: Meter.maxPeakEnabled && Meter.maxPeak > 0.01
            x: parent.width * Math.max(0, Math.min(1, Meter.maxPeak)) - 0.7
            y: -5
            width: 1.4
            height: parent.height + 10
            color: "#ff4136"
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.88
        text: {
            var sub = Meter.dbmText
            if (Meter.snrText.length)
                sub = (sub.length ? sub + "    " : "") + Meter.snrText
            if (Meter.secondary2Text.length)
                sub = (sub.length ? sub + "    " : "") + Meter.secondary2Text
            return sub
        }
        color: "#8a9aac"
        font.family: "Georgia"
        font.pixelSize: Math.max(11, Math.round(root.height * 0.065))
    }

    // OVER lamp (top left): the tubes keep their colour, this lights red
    // in the over zone (where the other faces turn red), like a HI-SWR LED.
    Row {
        id: overLamp
        readonly property color lampColor: MeterInk.lamp(Meter.level, Meter.normAtS9)
        readonly property bool lit: MeterInk.lampLit(Meter.level, Meter.normAtS9)
        x: 4
        y: 4
        spacing: 4
        Rectangle {
            readonly property int d: Math.max(6, Math.round(root.height * 0.05))
            anchors.verticalCenter: parent.verticalCenter
            width: d
            height: d
            radius: d / 2
            color: overLamp.lampColor
            border.width: overLamp.lit ? 0 : 1
            border.color: "#4a2020"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "OVER"
            color: overLamp.lit ? "#ff8a7a" : "#4a3030"
            font.family: root.monoFamily
            font.bold: true
            font.pixelSize: Math.max(8, Math.round(root.height * 0.05))
        }
    }
}
