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

    // sid is the MeterModel style index (src/meter_styles.h — append-only,
    // retired faces are not listed).  SettingsDialog lists the same ids.
    readonly property var styleChoices: [
        { "sid": 0,  "name": "Arc" },
        { "sid": 1,  "name": "Bar" },
        { "sid": 2,  "name": "Ladder" },
        { "sid": 3,  "name": "Porthole" },
        { "sid": 5,  "name": "Nixie" },
        { "sid": 7,  "name": "VFD" },
        { "sid": 10, "name": "Drum" },
        { "sid": 11, "name": "Iris" },
        { "sid": 12, "name": "Sonar" },
        { "sid": 17, "name": "Cluster" },
        { "sid": 16, "name": "Phosphor" }
    ]

    function styleName(id) {
        for (var i = 0; i < styleChoices.length; ++i)
            if (styleChoices[i].sid === id) return styleChoices[i].name
        return "Arc"
    }

    function styleRow(id) {
        for (var i = 0; i < styleChoices.length; ++i)
            if (styleChoices[i].sid === id) return i
        return 0
    }

    // The overlay is this panel, which can be ~150 px tall, so the list
    // never grows past it: it is capped to the overlay (less the popup
    // margins) and scrolls inside.  It opens below the chip when it fits,
    // else above, else pinned to the bottom margin.
    function openStyleMenu() {
        var m = stylePop.margins
        var oh = Overlay.overlay.height
        var ow = Overlay.overlay.width
        var listH = root.styleChoices.length * 22 + stylePop.padding * 2
        stylePop.height = Math.max(22 + stylePop.padding * 2, Math.min(listH, oh - 2 * m))
        var anchor = styleBtn.mapToItem(Overlay.overlay, 0, 0)
        stylePop.x = Math.max(m, Math.min(anchor.x + styleBtn.width - stylePop.width,
                                          ow - stylePop.width - m))
        var below = anchor.y + styleBtn.height + 2
        var above = anchor.y - stylePop.height - 2
        if (below + stylePop.height <= oh - m)
            stylePop.y = below
        else if (above >= m)
            stylePop.y = above
        else
            stylePop.y = Math.max(m, oh - m - stylePop.height)
        stylePop.open()
    }

    // ── Style chip (top-right). Too many faces for a row of buttons. ──
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
            text: root.styleName(Meter.activeStyle) + "  ▾"
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
        margins: 4
        width: 168
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#0c1218"
            border.color: "#2a4a5a"
            radius: 2
        }
        // Bring the current face into view each time the list opens.
        onOpened: styleList.positionViewAtIndex(root.styleRow(Meter.activeStyle),
                                                ListView.Contain)
        contentItem: ListView {
            id: styleList
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: styleList.contentHeight > styleList.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            model: root.styleChoices
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 22
                color: Meter.activeStyle === modelData.sid ? "#15435a" : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    text: (Meter.activeStyle === modelData.sid ? "✓  " : "    ") + modelData.name
                    color: Meter.activeStyle === modelData.sid ? "#00e5ff" : "#cdd9e5"
                    font.pixelSize: 12
                    font.family: "Menlo"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Meter.setActiveStyle(modelData.sid)
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
            case 5:  return nixieFace
            case 7:  return vfdFace
            case 10: return drumFace
            case 11: return irisFace
            case 12: return sonarFace
            case 16: return phosphorFace
            case 17: return clusterFace
            default: return arcFace
            }
        }
    }

    Component { id: arcFace;      HorizonArc {} }
    Component { id: barFace;      PlasmaBar {} }
    Component { id: ladderFace;   VerticalLadder {} }
    Component { id: portholeFace; MeterPorthole {} }
    Component { id: nixieFace;    MeterNixie {} }
    Component { id: vfdFace;      MeterVfd {} }
    Component { id: drumFace;     MeterDrum {} }
    Component { id: irisFace;     MeterIris {} }
    Component { id: sonarFace;    MeterSonar {} }
    Component { id: clusterFace;  MeterCluster {} }
    Component { id: phosphorFace; MeterPhosphor {} }

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
    // land in follow-on commits.  The style chip in the top-right
    // corner sits ABOVE this MouseArea (higher z) so clicks there
    // don't fall through to the source cycle.
    MouseArea {
        anchors.fill: face
        z: 1                              // below the style chip (z:10)
        // Disabled on Ladder (2) and Cluster (17) — they show all the
        // ladder sources at once, so there's no primary to cycle.  Operator
        // picks per-state preference from Settings → Meter as usual.
        enabled: Meter.activeStyle !== 2 && Meter.activeStyle !== 17
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
