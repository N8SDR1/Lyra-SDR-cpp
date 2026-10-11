// Lyra — Meter dock panel (RX/TX meter).
//
// Hosts the meter faces — Horizon Arc (default), Plasma Bar, Vertical
// Ladder, and the later single-source faces — all fed by the C++
// `Meter` context property (MeterModel).  A header chip opens the
// style list; the choice persists.
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

    // Short names, same order as MeterModel style indices.
    readonly property var styleNames: [
        "Arc", "Bar", "Ladder",
        "Porthole", "Fader", "Nixie", "Numitron", "VFD", "Dekatron", "Panaplex",
        "Drum", "Iris", "Sonar", "Ingot"
    ]

    function openStyleMenu() {
        var p = styleBtn.mapToItem(Overlay.overlay, 0, styleBtn.height + 2)
        stylePop.x = Math.max(4, p.x + styleBtn.width - stylePop.width)
        stylePop.y = p.y
        stylePop.open()
    }

    // ── Style chip (top-right). The list would not fit as Arc|Bar|Ladder. ──
    Rectangle {
        id: styleBtn
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        anchors.rightMargin: 8
        z: 10
        width: Math.min(112, Math.max(72, styleLabel.implicitWidth + 16))
        height: 18
        color: "#0c1218"
        border.color: "#00e5ff"
        border.width: 1
        Text {
            id: styleLabel
            anchors.centerIn: parent
            text: (root.styleNames[Meter.activeStyle] || "Arc") + "  ▾"
            color: "#00e5ff"
            font.pixelSize: 10
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openStyleMenu()
        }
    }

    Popup {
        id: stylePop
        parent: Overlay.overlay
        modal: true
        focus: true
        padding: 4
        width: 148
        height: Math.min(300, root.styleNames.length * 22 + 8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#0c1218"
            border.color: "#2a4a5a"
            radius: 2
        }
        contentItem: ListView {
            clip: true
            model: root.styleNames
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: ListView.view.width
                height: 22
                color: Meter.activeStyle === index ? "#15435a" : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    text: (Meter.activeStyle === index ? "✓  " : "    ") + modelData
                    color: Meter.activeStyle === index ? "#00e5ff" : "#cdd9e5"
                    font.pixelSize: 12
                    font.family: "Menlo"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Meter.setActiveStyle(index)
                        stylePop.close()
                    }
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
            case 3:  return portholeFace
            case 4:  return faderFace
            case 5:  return nixieFace
            case 6:  return numitronFace
            case 7:  return vfdFace
            case 8:  return dekatronFace
            case 9:  return panaplexFace
            case 10: return drumFace
            case 11: return irisFace
            case 12: return sonarFace
            case 13: return ingotFace
            default: return arcFace
            }
        }
    }

    Component { id: arcFace;      HorizonArc {} }
    Component { id: barFace;      PlasmaBar {} }
    Component { id: ladderFace;   VerticalLadder {} }
    Component { id: portholeFace; MeterPorthole {} }
    Component { id: faderFace;    MeterFader {} }
    Component { id: nixieFace;    MeterNixie {} }
    Component { id: numitronFace; MeterNumitron {} }
    Component { id: vfdFace;      MeterVfd {} }
    Component { id: dekatronFace; MeterDekatron {} }
    Component { id: panaplexFace; MeterPanaplex {} }
    Component { id: drumFace;     MeterDrum {} }
    Component { id: irisFace;     MeterIris {} }
    Component { id: sonarFace;    MeterSonar {} }
    Component { id: ingotFace;    MeterIngot {} }

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
        z: 1                              // below the style chip (z:10)
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

    // Right-click anywhere on the face opens the style list, including
    // on the Ladder where left-click does not cycle the source.
    MouseArea {
        anchors.fill: face
        z: 2
        acceptedButtons: Qt.RightButton
        onClicked: root.openStyleMenu()
    }
}
