// Lyra — Tuner panel: native manual-ATU memory (the "Tuner Reminder" idea,
// built in).  Tracks the live dial against the active antenna's stored
// Input / Output / Inductor settings and shows the matching — or nearest —
// point, with live colour-coded SWR embedded.  Collapsed = basics only;
// expanded = the full editable table.
//
// Context properties: Tuner (TunerMemory), Stream (fwd/rev power for SWR),
// Prefs (tooltips toggle).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 360
    implicitHeight: col.implicitHeight + 16
    // Honest FLOOR (below the preferred implicitHeight) so the host dock can be
    // shrunk under the content — the internal ScrollView then scrolls instead of
    // clipping.  Tuner collapses via the Tuner model (not a root `collapsed`
    // prop), so makeQuick treats it as non-collapsible and would otherwise pin
    // the dock to the full content height, defeating the vertical scroll.
    readonly property int lyraMinHeight: 170
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cDim:    "#5f7186"
    readonly property color cGreen:  "#34c759"
    readonly property color cAmber:  "#ff9a3c"
    readonly property color cRed:    "#ff4136"

    // Live SWR from forward / reflected power (only meaningful while keyed).
    // Re-evaluates as Stream's power readings change.  −1 = no reading.
    readonly property real swr: {
        var pf = Stream.fwdPowerW, pr = Stream.revPowerW
        if (pf <= 0.05) return -1
        var r = Math.sqrt(Math.max(0, pr) / pf)
        return r >= 0.999 ? 99.9 : (1 + r) / (1 - r)
    }
    function swrColor(s) {
        if (s < 0) return cMuted
        if (s <= 1.5) return cGreen
        if (s <= 2.4) return cAmber
        return cRed
    }

    // Match-state colour: green when on a stored point, amber when showing a
    // nearest, dim when the antenna has nothing stored yet.
    readonly property color matchCol: !Tuner.matchValid ? cDim
                                      : (Tuner.matchExact ? cGreen : cAmber)

    // Store-a-new-point editor state.
    property bool editingNew: false
    // Edit-existing toggle (✎): clicking a row makes ONLY that row editable.
    property bool editRows: false
    // Original index (into Tuner.points) of the single row being edited; -1 = none.
    property int editRowIdx: -1
    // Original index of the row awaiting a delete confirm; -1 = none.
    property int confirmDelIdx: -1
    onEditRowsChanged: { editRowIdx = -1; confirmDelIdx = -1 }

    // Reset edit/confirm selection when the antenna changes (indices are per-antenna).
    Connections {
        target: Tuner
        function onActiveAntennaChanged() { root.editRowIdx = -1; root.confirmDelIdx = -1 }
    }

    // How many stored points to show on each side of the current dial freq.
    // 1 = just the nearest below + nearest above (the bracketing pair); when
    // you're sitting on a stored point it shows that exact row + one each side
    // (3 rows).  Deliberately NOT a full scrollable list — see Settings → Tuner
    // for the complete table.
    property int windowEach: 1

    // Nearest-window view: instead of listing every stored point (which would
    // grow the panel unbounded and force scrolling through hundreds of combos),
    // show only the points that BRACKET the current frequency — up to
    // `windowEach` just below + the exact one if the current freq is stored +
    // up to `windowEach` just above.  Gives a ready starting position for the
    // manual tuner even on an unsaved frequency (the setting one notch down and
    // one notch up).  Each item carries its original index into Tuner.points so
    // edit/delete + match-highlight still address the right point.
    readonly property var nearbyPoints: {
        var pts = Tuner.points            // sorted ascending by freqHz
        var cur = Tuner.currentFreqHz
        var below = [], above = [], exact = null
        for (var i = 0; i < pts.length; ++i) {
            var item = { p: pts[i], idx: i }
            if (i === Tuner.matchIndex && Tuner.matchExact) { exact = item; continue }
            if (pts[i].freqHz <= cur) below.push(item)
            else above.push(item)
        }
        // closest below = tail of `below`; closest above = head of `above`
        var out = below.slice(Math.max(0, below.length - windowEach))
        if (exact) out = out.concat([exact])
        return out.concat(above.slice(0, windowEach))
    }

    function fmtDelta(hz) {
        var k = hz / 1000
        return (k >= 0 ? "+" : "") + k.toFixed(1) + " kHz"
    }

    // Scroll when the panel is compressed below its natural content size
    // — both axes, AsNeeded — so a floated-small or racked panel stays
    // fully usable instead of silently clipping (SizeRootObjectToView
    // gives no scrollbar on its own).  Same idiom as SpeechPanel / the CW
    // decoder.  Above the natural size the content fills; below it scrolls.
    ScrollView {
        id: bodyScroll
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        contentWidth: Math.max(324, availableWidth)
        // The memory list sizes itself with Layout.fillHeight, so the column
        // needs a definite height to grow into: fill the viewport when the panel
        // is tall enough, else hold at the ~380px natural height and let the
        // ScrollView scroll.  Bind to bodyScroll.height (stable) rather than
        // availableHeight so the fill can't feed a scrollbar-visibility loop.
        contentHeight: Tuner.collapsed ? col.implicitHeight
                                       : Math.max(380, bodyScroll.height)
        ScrollBar.horizontal.policy: ScrollBar.AsNeeded
        ScrollBar.vertical.policy:   ScrollBar.AsNeeded

        ColumnLayout {
            id: col
            width: bodyScroll.contentWidth
            height: bodyScroll.contentHeight
            spacing: 8

        // ── Header ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("TUNER"); color: root.cAccent; font.bold: true
                    font.letterSpacing: 1 }
            Label { text: qsTr("manual ATU memory"); color: root.cMuted
                    font.pixelSize: 11; Layout.fillWidth: true }
            // live SWR pill
            Rectangle {
                implicitHeight: 26; implicitWidth: swrLbl.implicitWidth + 20
                radius: 6; color: "#0d141b"
                border.color: root.swrColor(root.swr)
                Label {
                    id: swrLbl; anchors.centerIn: parent
                    text: root.swr < 0 ? qsTr("SWR —")
                          : qsTr("SWR ") + root.swr.toFixed(1) + ":1"
                    color: root.swrColor(root.swr); font.pixelSize: 14; font.bold: true
                }
            }
            // edit-existing toggle (hidden when collapsed)
            ToolButton {
                id: editBtn
                visible: !Tuner.collapsed
                checkable: true; checked: root.editRows
                onToggled: root.editRows = checked
                implicitWidth: 26; implicitHeight: 22
                background: Rectangle {
                    radius: 5
                    color: editBtn.checked ? "#10323a"
                           : (editBtn.hovered ? "#16242e" : "transparent")
                }
                contentItem: Label { text: "✎"; anchors.centerIn: parent
                    font.pixelSize: 15; font.bold: true
                    color: root.editRows ? root.cAccent : root.cText }
                ToolTip.text: qsTr("Edit / delete stored points")
                ToolTip.visible: hovered && Prefs.tooltipsEnabled; ToolTip.delay: 500
            }
            // collapse / expand
            ToolButton {
                id: collBtn
                onClicked: Tuner.collapsed = !Tuner.collapsed
                implicitWidth: 26; implicitHeight: 22
                background: Rectangle {
                    radius: 5
                    color: collBtn.hovered ? "#16242e" : "transparent"
                }
                contentItem: Label {
                    text: Tuner.collapsed ? "▸" : "▾"
                    anchors.centerIn: parent; font.pixelSize: 15; font.bold: true
                    color: root.cText }
                ToolTip.text: Tuner.collapsed ? qsTr("Expand") : qsTr("Collapse to basics")
                ToolTip.visible: hovered && Prefs.tooltipsEnabled; ToolTip.delay: 500
            }
        }

        // ── Antenna chips (expanded only) ──
        RowLayout {
            Layout.fillWidth: true
            visible: !Tuner.collapsed
            spacing: 6
            Label { text: qsTr("antenna"); color: root.cMuted; font.pixelSize: 11 }
            Repeater {
                model: Tuner.antennaNames
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    readonly property bool sel: Tuner.activeAntenna === index
                    radius: 7; implicitHeight: 24
                    implicitWidth: antLbl.implicitWidth + 22
                    color: sel ? "#10323a" : "#161e28"
                    border.color: sel ? root.cAccent : "#2a3a4a"
                    Label { id: antLbl; anchors.centerIn: parent; text: parent.modelData
                            color: parent.sel ? root.cAccent : root.cText; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent
                        onClicked: Tuner.activeAntenna = parent.index }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Context line: band + freq + match badge ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: Tuner.currentBand !== "" ? Tuner.currentBand : qsTr("—")
                    color: "#efb340"; font.bold: true; font.pixelSize: 13 }
            Label { text: Tuner.currentFreqText; color: root.cText
                    font.family: "Consolas"; font.pixelSize: 18 }
            Item { Layout.fillWidth: true }
            RowLayout {
                spacing: 4
                visible: Tuner.matchValid
                Rectangle { width: 8; height: 8; radius: 4; color: root.matchCol }
                Label {
                    text: Tuner.matchExact ? qsTr("exact match")
                          : qsTr("nearest ") + root.fmtDelta(Tuner.matchDeltaHz)
                    color: root.matchCol; font.pixelSize: 11 }
            }
        }

        // ── Match tiles: Input / Output / Inductor ──
        GridLayout {
            Layout.fillWidth: true
            columns: 3; columnSpacing: 8; rowSpacing: 0
            Repeater {
                model: [
                    { k: qsTr("INPUT"),    v: Tuner.matchInput },
                    { k: qsTr("OUTPUT"),   v: Tuner.matchOutput },
                    { k: qsTr("INDUCTOR"), v: Tuner.matchInductor }
                ]
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 52; radius: 9
                    color: "#0d141b"
                    border.color: Qt.rgba(root.matchCol.r, root.matchCol.g, root.matchCol.b, 0.55)
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 2
                        Label { text: tile.modelData.k; color: root.cMuted
                                font.pixelSize: 10; font.letterSpacing: 1 }
                        Label {
                            text: (tile.modelData.v && tile.modelData.v.length > 0)
                                  ? tile.modelData.v : "—"
                            color: Tuner.matchValid ? root.cText : root.cDim
                            font.family: "Consolas"; font.pixelSize: 22 }
                    }
                }
            }
        }

        // ── Nearest / no-match banner (amber) ──
        Rectangle {
            Layout.fillWidth: true
            visible: !root.editingNew && (!Tuner.matchValid || !Tuner.matchExact)
            implicitHeight: nrCol.implicitHeight + 14
            radius: 9; color: "#241a0c"; border.color: "#6e4f1c"
            RowLayout {
                id: nrCol
                anchors.fill: parent; anchors.margins: 7; spacing: 8
                Label { text: "⚠"; color: root.cAmber; font.pixelSize: 16 }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11
                    text: !Tuner.matchValid
                          ? qsTr("Nothing stored for this antenna yet — store this frequency.")
                          : qsTr("No exact match — showing nearest (") +
                            Tuner.fmtFreq(Tuner.matchFreqHz) + qsTr(", ") +
                            root.fmtDelta(Tuner.matchDeltaHz) + qsTr(")")
                    color: root.cText }
                Button {
                    text: qsTr("Store this freq")
                    implicitHeight: 24; font.pixelSize: 11
                    onClicked: {
                        inF.text = ""; outF.text = ""; indF.text = ""; noteF.text = ""
                        root.editingNew = true
                    }
                }
            }
        }

        // ── New-point editor (Store → fill → Save) ──
        Rectangle {
            Layout.fillWidth: true
            visible: root.editingNew
            implicitHeight: edCol.implicitHeight + 16
            radius: 9; color: "#0d141b"; border.color: root.cAccent
            ColumnLayout {
                id: edCol
                anchors.fill: parent; anchors.margins: 8; spacing: 6
                Label {
                    text: qsTr("New point · ") + Tuner.currentBand + " · " +
                          Tuner.currentFreqText
                    color: root.cAccent; font.pixelSize: 12 }
                GridLayout {
                    Layout.fillWidth: true; columns: 3; columnSpacing: 6; rowSpacing: 4
                    TextField { id: inF;  placeholderText: qsTr("Input");    Layout.fillWidth: true }
                    TextField { id: outF; placeholderText: qsTr("Output");   Layout.fillWidth: true }
                    TextField { id: indF; placeholderText: qsTr("Inductor"); Layout.fillWidth: true }
                }
                TextField { id: noteF; placeholderText: qsTr("Note (optional)"); Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Cancel"); implicitHeight: 24; font.pixelSize: 11
                             onClicked: root.editingNew = false }
                    Button {
                        text: qsTr("Save"); implicitHeight: 24; font.pixelSize: 11
                        highlighted: true
                        onClicked: {
                            Tuner.storePoint(Tuner.currentFreqHz, inF.text, outF.text,
                                             indF.text, noteF.text)
                            root.editingNew = false
                        }
                    }
                }
            }
        }

        // ── Stored-points table (expanded only) ──
        ColumnLayout {
            Layout.fillWidth: true
            visible: !Tuner.collapsed
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: qsTr("Band"); color: root.cMuted; font.pixelSize: 11; Layout.preferredWidth: 46 }
                Label { text: qsTr("Freq");  color: root.cMuted; font.pixelSize: 11; Layout.fillWidth: true }
                Label { text: qsTr("In");    color: root.cMuted; font.pixelSize: 11; Layout.preferredWidth: 48 }
                Label { text: qsTr("Out");   color: root.cMuted; font.pixelSize: 11; Layout.preferredWidth: 48 }
                Label { text: qsTr("Ind");   color: root.cMuted; font.pixelSize: 11; Layout.preferredWidth: 48 }
                Item { Layout.preferredWidth: root.editRows ? 48 : 0 }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#22323e" }

            // Fixed nearest-window list (no scroll): only the points bracketing
            // the current dial freq, so the panel stays a locked height.
            Repeater {
                model: root.nearbyPoints
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var modelData
                    readonly property int pIdx: modelData.idx
                    readonly property var p: modelData.p
                    readonly property bool here: Tuner.matchIndex === pIdx
                    // Only the single clicked row is editable (prevents accidental
                    // edits to a neighbour); confirming = this row's delete prompt.
                    readonly property bool editing: root.editRows && root.editRowIdx === pIdx
                    readonly property bool confirming: root.confirmDelIdx === pIdx
                    Layout.fillWidth: true
                    implicitHeight: 26
                    radius: editing ? 5 : 0
                    color: editing ? "#0e2a34" : (here ? "#10323a" : "transparent")
                    border.color: editing ? root.cAccent : "transparent"
                    border.width: editing ? 1 : 0

                    // In edit mode, click a (non-editing) row to select it for edit.
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.editRows && !row.editing
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.confirmDelIdx = -1; root.editRowIdx = row.pIdx }
                    }

                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 0; spacing: 0
                        Label { text: row.p.band; color: row.here ? root.cAccent : root.cMuted
                                font.pixelSize: 12; Layout.preferredWidth: 46 }
                        Label { text: row.p.freqText
                                color: row.here ? root.cAccent : root.cText
                                font.family: "Consolas"; font.pixelSize: 12; Layout.fillWidth: true }
                        // read-only labels (every row except the one being edited)
                        Label { visible: !row.editing; text: row.p.input || "—"
                                color: root.cText; font.family: "Consolas"; font.pixelSize: 12; Layout.preferredWidth: 48 }
                        Label { visible: !row.editing; text: row.p.output || "—"
                                color: root.cText; font.family: "Consolas"; font.pixelSize: 12; Layout.preferredWidth: 48 }
                        Label { visible: !row.editing; text: row.p.inductor || "—"
                                color: root.cText; font.family: "Consolas"; font.pixelSize: 12; Layout.preferredWidth: 48 }

                        // edit fields (only the selected row)
                        TextField { id: reIn;  visible: row.editing; text: row.p.input
                                    Layout.preferredWidth: 48; font.pixelSize: 12
                                    onEditingFinished: Tuner.updatePoint(row.pIdx, reIn.text, reOut.text, reInd.text, row.p.note) }
                        TextField { id: reOut; visible: row.editing; text: row.p.output
                                    Layout.preferredWidth: 48; font.pixelSize: 12
                                    onEditingFinished: Tuner.updatePoint(row.pIdx, reIn.text, reOut.text, reInd.text, row.p.note) }
                        TextField { id: reInd; visible: row.editing; text: row.p.inductor
                                    Layout.preferredWidth: 48; font.pixelSize: 12
                                    onEditingFinished: Tuner.updatePoint(row.pIdx, reIn.text, reOut.text, reInd.text, row.p.note) }

                        // trailing control: delete-with-confirm, only on the active row
                        Item {
                            Layout.preferredWidth: root.editRows ? 48 : 0
                            Layout.fillHeight: true
                            RowLayout {
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                spacing: 2; visible: row.editing
                                ToolButton {
                                    visible: !row.confirming; implicitWidth: 24; implicitHeight: 24
                                    background: Item {}
                                    // Trashcan glyph (drawn — the flat dark UI uses
                                    // mono line-art, not a colour emoji).
                                    contentItem: Canvas {
                                        anchors.centerIn: parent
                                        width: 18; height: 20
                                        readonly property color col: root.cRed
                                        onColChanged: requestPaint()
                                        Component.onCompleted: requestPaint()
                                        onPaint: {
                                            var ctx = getContext("2d"); ctx.reset();
                                            ctx.strokeStyle = col; ctx.lineWidth = 1.6;
                                            ctx.lineCap = "round"; ctx.lineJoin = "round";
                                            var w = width, h = height;
                                            ctx.beginPath();              // lid
                                            ctx.moveTo(1.5, 3.5); ctx.lineTo(w - 1.5, 3.5); ctx.stroke();
                                            ctx.beginPath();              // handle
                                            ctx.moveTo(w * 0.34, 3.5); ctx.lineTo(w * 0.38, 1.4);
                                            ctx.lineTo(w * 0.62, 1.4); ctx.lineTo(w * 0.66, 3.5); ctx.stroke();
                                            ctx.beginPath();              // body (tapered)
                                            ctx.moveTo(2.6, 4.6); ctx.lineTo(w - 2.6, 4.6);
                                            ctx.lineTo(w - 3.6, h - 1.2); ctx.lineTo(3.6, h - 1.2);
                                            ctx.closePath(); ctx.stroke();
                                            ctx.beginPath();              // ribs
                                            ctx.moveTo(w * 0.37, 6.2); ctx.lineTo(w * 0.37, h - 3.0);
                                            ctx.moveTo(w * 0.50, 6.2); ctx.lineTo(w * 0.50, h - 3.0);
                                            ctx.moveTo(w * 0.63, 6.2); ctx.lineTo(w * 0.63, h - 3.0);
                                            ctx.stroke();
                                        }
                                    }
                                    onClicked: root.confirmDelIdx = row.pIdx
                                    ToolTip.text: qsTr("Delete this point")
                                    ToolTip.visible: hovered && Prefs.tooltipsEnabled; ToolTip.delay: 500
                                }
                                ToolButton {
                                    visible: row.confirming; implicitWidth: 20; implicitHeight: 22
                                    background: Item {}
                                    contentItem: Label { text: "✓"; anchors.centerIn: parent; color: root.cRed
                                        font.pixelSize: 14; font.bold: true }
                                    onClicked: { var i = row.pIdx; root.confirmDelIdx = -1
                                                 root.editRowIdx = -1; Tuner.deletePoint(i) }
                                    ToolTip.text: qsTr("Confirm delete")
                                    ToolTip.visible: hovered && Prefs.tooltipsEnabled; ToolTip.delay: 300
                                }
                                ToolButton {
                                    visible: row.confirming; implicitWidth: 20; implicitHeight: 22
                                    background: Item {}
                                    contentItem: Label { text: "✗"; anchors.centerIn: parent; color: root.cMuted
                                        font.pixelSize: 14; font.bold: true }
                                    onClicked: root.confirmDelIdx = -1
                                    ToolTip.text: qsTr("Cancel")
                                    ToolTip.visible: hovered && Prefs.tooltipsEnabled; ToolTip.delay: 300
                                }
                            }
                        }
                    }
                }
            }
            // Empty-window hint + "more in Settings" note.
            Label {
                Layout.fillWidth: true; Layout.topMargin: 2
                visible: root.nearbyPoints.length === 0 || Tuner.points.length > root.nearbyPoints.length
                color: root.cDim; font.pixelSize: 10; wrapMode: Text.WordWrap
                text: root.nearbyPoints.length === 0
                      ? qsTr("No stored points near this frequency.")
                      : qsTr("Showing nearest stored points · full list in Settings → Tuner")
            }

            // Add-current-freq affordance.
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
                Button {
                    text: qsTr("Add current freq"); implicitHeight: 24; font.pixelSize: 11
                    onClicked: {
                        inF.text = ""; outF.text = ""; indF.text = ""; noteF.text = ""
                        root.editingNew = true
                    }
                }
                Label {
                    visible: root.editRows; color: root.cDim; font.pixelSize: 10
                    text: qsTr("click a row to edit · click away saves · − asks before deleting")
                    Layout.fillWidth: true }
            }
        }
        }
    }
}
