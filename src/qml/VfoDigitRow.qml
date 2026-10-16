// Lyra — one row of VFO digits (MMMM.kkk.hhh) for the VFO styles.
//
// Lays the ten digits and two separators out at EXPLICIT x positions (no Row
// positioner, so the geometry is known without a window — the style contract
// test relies on that) and reports each digit's cell as `rects` (index =
// place: 0 = Hz units … 9 = 1 GHz) for FreqDisplay hit-testing.  Draws an
// optional ghost glyph behind each digit, dims leading MHz zeros, and
// underlines the selected place.  Styles choose the font and colours.
import QtQuick

Item {
    id: row
    property string digitString: "0000000000"
    property font digitFont
    property color digitColor: "white"
    property color dimColor: "gray"          // leading MHz zeros
    property string ghostText: ""            // e.g. "8" for an unlit-segment ghost
    property color ghostColor: "transparent"
    property string sepText: "."
    property color sepColor: digitColor
    property int selectedDigit: -1
    property color selectColor: "#00e5ff"
    property real selectThickness: 3
    property real selectGap: 3               // underline centre, below the baseline
    property bool centerGlyphs: false        // centre proportional glyphs in their cell
    property int renderType: Text.QtRendering

    FontMetrics { id: fm; font: row.digitFont }
    // Widths come from hidden Texts that take the very same font (and render
    // type) as the drawn glyphs, so they track font changes.  FontMetrics.
    // advanceWidth() is a plain function: a binding on it never re-runs when the
    // font changes, and styles size the font from a height that is 0 at first.
    Text { id: digitProbe; visible: false; text: "0"; font: row.digitFont
           renderType: row.renderType }
    Text { id: sepProbe; visible: false; text: row.sepText; font: row.digitFont
           renderType: row.renderType }
    readonly property real digitW: digitProbe.implicitWidth
    readonly property real sepW: sepProbe.implicitWidth
    readonly property real cellH: fm.height
    readonly property real baselineY: fm.ascent

    // x of character cell i (0 = leftmost = place 9).
    function cellX(i) {
        return i * digitW + (i > 3 ? sepW : 0) + (i > 6 ? sepW : 0)
    }
    // Leading-zero suppression on the MHz digits (places 9..6).
    function leadingZero(i) {
        if (9 - i < 6) return false
        for (var j = 0; j <= i; ++j)
            if (digitString.charAt(j) !== "0") return false
        return true
    }

    readonly property var rects: {
        var a = []
        for (var place = 0; place < 10; ++place)
            a.push(Qt.rect(cellX(9 - place), 0, digitW, cellH))
        return a
    }
    // x centres of the MHz / kHz / Hz groups, for unit labels.
    readonly property var groupCenters: [cellX(0) + 2 * digitW,
                                         cellX(4) + 1.5 * digitW,
                                         cellX(7) + 1.5 * digitW]

    implicitWidth: 10 * digitW + 2 * sepW
    implicitHeight: cellH

    Repeater {
        model: 10
        Item {
            required property int index
            x: row.cellX(index)
            width: row.digitW
            height: row.cellH
            Text {
                visible: row.ghostText !== ""
                width: parent.width
                horizontalAlignment: row.centerGlyphs ? Text.AlignHCenter : Text.AlignLeft
                text: row.ghostText
                font: row.digitFont
                color: row.ghostColor
                renderType: row.renderType
            }
            Text {
                width: parent.width
                horizontalAlignment: row.centerGlyphs ? Text.AlignHCenter : Text.AlignLeft
                text: row.digitString.charAt(index)
                font: row.digitFont
                color: row.leadingZero(index) ? row.dimColor : row.digitColor
                renderType: row.renderType
            }
            Rectangle {
                visible: row.selectedDigit === 9 - index
                x: 1
                width: row.digitW - 2
                height: row.selectThickness
                y: row.baselineY + row.selectGap - height / 2
                color: row.selectColor
            }
        }
    }
    Repeater {
        model: [3, 6]
        Text {
            required property int modelData
            x: row.cellX(modelData) + row.digitW
            text: row.sepText
            font: row.digitFont
            color: row.sepColor
            renderType: row.renderType
        }
    }
}
