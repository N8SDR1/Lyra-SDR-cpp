// Lyra — Meter dock panel (RX/TX meter).
//
// Hosts one of three GPU-flavoured renderers — Horizon Arc (default),
// Plasma Bar, or Vertical Ladder (Multi-source) — all fed by the C++
// `Meter` context property (MeterModel).  A tiny Arc|Bar|Ladder toggle
// in the corner flips the style live; the choice persists.
//
// Arc and Bar are single-source views (one needle / bar, peak pip,
// glow, secondary digital readouts).  Ladder is the Multi view — N
// source rows stacked, each with its own label / bar / value (TX =
// full telemetry stack of PWR/SWR/PA/VDD/T; RX = degraded 3-row view
// of S-meter / noise floor / SNR).  Click-to-cycle source picker
// stays wired for Arc/Bar but no-ops on Ladder (the Multi view shows
// all sources at once — no primary to cycle).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 300
    implicitHeight: 200
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor — see AudioPanel.  The meter face is a Loader, so there is no
    // content layout to measure: these are the smallest size at which the arc /
    // bar / ladder faces and the style toggle above them stay readable.  The
    // implicit size above is what the panel PREFERS; this is what it needs.
    readonly property int lyraMinWidth:  220
    readonly property int lyraMinHeight: 150

    // ── Style toggle (top-right): Arc | Bar | Ladder ──
    Row {
        id: styleToggle
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        anchors.rightMargin: 8
        spacing: 0
        z: 10

        Repeater {
            model: [
                { t: "Arc",    s: 0 },
                { t: "Bar",    s: 1 },
                { t: "Ladder", s: 2 }
            ]
            delegate: Rectangle {
                width: 44; height: 18
                readonly property bool sel: Meter.activeStyle === modelData.s
                color: sel ? "#15435a" : "#0c1218"
                border.color: sel ? "#00e5ff" : "#2a4a5a"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: modelData.t
                    color: sel ? "#00e5ff" : "#7790a0"
                    font.pixelSize: 10
                    font.bold: sel
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // Overrides the style of whatever state is showing now
                    // (TX style on TX when Separate RX/TX style is on).
                    onClicked: Meter.setActiveStyle(modelData.s)
                }
            }
        }
    }

    // ── The active renderer ──
    Loader {
        id: face
        anchors.fill: parent
        anchors.margins: 6
        sourceComponent: {
            switch (Meter.activeStyle) {
            case 1:  return barFace
            case 2:  return ladderFace
            default: return arcFace
            }
        }
    }

    Component { id: arcFace;    HorizonArc {} }
    Component { id: barFace;    PlasmaBar {} }
    Component { id: ladderFace; VerticalLadder {} }

    // ── Click-to-cycle source picker (task #35 / FEATURES.md §6) ──
    // Click on the meter face cycles the source through the wired
    // options.  Setting Meter.source routes through MeterModel::
    // setSource which writes to the appropriate preference slot
    // (rxSource or txSource based on the live MOX state) — so the
    // click persists into the operator's pref for that state, not
    // a transient mode.
    //
    // The cycle list is constant for now: only the wired sources
    // appear (RX S-meter / PWR / SWR).  PA_CURRENT / PA_VOLTS / TEMP /
    // ALC / MIC / COMP get added when their MeterModel computes
    // land in follow-on commits.  The Arc|Bar toggle in the top-
    // right corner sits ABOVE this MouseArea (higher z) so clicks
    // there don't fall through to the source cycle.
    MouseArea {
        anchors.fill: face
        z: 1                              // below the styleToggle (z:10)
        // Disabled on the Ladder style — Multi shows all sources at
        // once, so there's no primary to cycle.  Operator picks per-
        // state preference from Settings → Meter as usual.
        enabled: Meter.activeStyle !== 2
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        acceptedButtons: Qt.LeftButton
        property var sourceCycle: [0, 1, 2]  // RX_SMETER, PWR, SWR
        onClicked: function(mouse) {
            var i = sourceCycle.indexOf(Meter.source)
            var next = sourceCycle[(i + 1) % sourceCycle.length]
            Meter.source = next
        }
        ToolTip.delay: 1500
        ToolTip.visible: (containsMouse && !pressed) && Prefs.tooltipsEnabled
        ToolTip.text: qsTr("Click to cycle the meter source.  Sets your "
                           + "RX or TX preference (depending on whether "
                           + "the radio is keyed) — Settings → Meter "
                           + "also picks each one explicitly.")
        hoverEnabled: true
    }
}
