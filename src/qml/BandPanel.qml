// Lyra — Band dock panel.
//
// Three rows, matching old Lyra's BandSelectorPanel:
//   1. AMATEUR  — 160m … 6m amateur bands
//   2. BC       — shortwave broadcast meter bands (120m … 13m)
//   3. GEN      — GEN1/2/3 general-coverage slots (TIME + Mem join here
//                 as those features land)
// All built from the shared C++ band tables (Bands context property),
// so the lists never drift from the protocol/DSP side.
//
// Active-band highlight is set IMPERATIVELY on the rx1FreqChanged signal
// (Bands.indexForFreq / broadcastIndexForFreq) — never a per-button
// reactive read of Stream.rx1FreqHz (that proved unreliable here).  Each
// chip's look derives from `chipActive`, so exactly one lights per row.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: col.implicitHeight + 16   // grows/shrinks with the CB row
    implicitWidth: 720
    color: "#101820"
    border.color: "#2a4a5a"

    // Honest floor, measured from the content — see AudioPanel for the rationale.
    readonly property int lyraMinWidth:  col.implicitWidth + 16
    readonly property int lyraMinHeight: col.implicitHeight + 16

    property int activeBand: -1     // amateur band index containing RX1, or -1
    property int activeBc: -1       // broadcast band index, or -1
    property int activeCb: -1       // CB band index, or -1
    function refreshActive() {
        root.activeBand = Bands.indexForFreq(Stream.rx1FreqHz)
        root.activeBc   = Bands.broadcastIndexForFreq(Stream.rx1FreqHz)
        root.activeCb   = Bands.cbIndexForFreq(Stream.rx1FreqHz)
    }
    // Live memory-preset list for the Mem recall menu (kept current).
    property var memList: []
    function refreshMem() { root.memList = Memory.list() }

    Component.onCompleted: { refreshActive(); refreshMem() }
    Connections {
        target: Stream
        function onRx1FreqChanged() { root.refreshActive() }
    }
    Connections {
        target: Memory
        function onChanged() { root.refreshMem() }
    }

    // Shared flat-chip button (same look across all three rows).  Active
    // colours default to the amateur red-glow; GEN overrides them to cyan.
    component ChipButton : Button {
        id: cb
        property bool chipActive: false
        property color activeFill:   "#260808"
        property color activeBorder: "#ff3344"
        property color activeText:   "#ffcc88"
        Layout.preferredWidth: 44
        Layout.preferredHeight: 22
        padding: 0
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: 4
            color: cb.chipActive ? cb.activeFill
                   : (cb.down ? "#1d2b38" : "#b416202a")
            border.width: cb.chipActive ? 2 : 1
            border.color: cb.chipActive ? cb.activeBorder
                          : (cb.hovered ? "#8fdcff" : "#5ec8ff")
        }
        contentItem: Text {
            text: cb.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            font.bold: true
            color: cb.chipActive ? cb.activeText : "#5ec8ff"
            // Without this the band label paints outside its shrinking chip and
            // the whole row smears together at narrow widths.
            elide: Text.ElideRight
            clip: true
        }
    }

    component RowLabel : Label {
        color: "#8fa6ba"
        font.bold: true
        Layout.preferredWidth: 34
        Layout.alignment: Qt.AlignVCenter
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // ── Row 1: amateur bands ──────────────────────────────────────
        RowLayout {
            spacing: 4
            RowLabel { text: qsTr("Ham") }
            Repeater {
                model: Bands.amateur()
                delegate: ChipButton {
                    required property var modelData
                    required property int index
                    text: modelData.name
                    chipActive: index === root.activeBand
                    // Return to this band's last freq (per-band memory),
                    // else its default; and leave any active GEN slot.
                    onClicked: {
                        Gen.deactivate()
                        var f = BandMemory.freqFor(modelData.name)
                        Stream.setRx1FreqHz(f > 0 ? f : modelData.hz)
                    }
                }
            }
            // 11m / CB band — optional button right after 6m (toggle in
            // Settings → Hardware → Band panel); same Ham row, no new row.
            // Small gap + a "CB" label (same style as "Ham") precede it.
            Item {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 1
                visible: Prefs.cbBandEnabled
            }
            RowLabel {
                text: qsTr("CB")
                visible: Prefs.cbBandEnabled
                Layout.preferredWidth: 22
            }
            Repeater {
                model: Bands.cb()
                delegate: ChipButton {
                    required property var modelData
                    required property int index
                    visible: Prefs.cbBandEnabled
                    text: modelData.name
                    chipActive: index === root.activeCb
                    onClicked: {
                        Gen.deactivate()
                        var f = BandMemory.freqFor("cb_" + modelData.name)
                        Stream.setRx1FreqHz(f > 0 ? f : modelData.hz)
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Row 2: broadcast (SW) bands ───────────────────────────────
        RowLayout {
            spacing: 4
            RowLabel { text: qsTr("BC") }
            Repeater {
                model: Bands.broadcast()
                delegate: ChipButton {
                    required property var modelData
                    required property int index
                    text: modelData.name
                    chipActive: index === root.activeBc
                    // BC bands now recall last freq (per-band memory),
                    // else the band default; mode follows via BandMemory.
                    onClicked: {
                        Gen.deactivate()
                        var f = BandMemory.freqFor("bc_" + modelData.name)
                        Stream.setRx1FreqHz(f > 0 ? f : modelData.hz)
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Row 3: GEN general-coverage slots ─────────────────────────
        // Click recalls a slot's last freq+mode; while active, tuning
        // updates it.  Right-click to reset.  (TIME + Mem land here too
        // when those features are built.)
        RowLayout {
            spacing: 4
            RowLabel { text: qsTr("Gen") }
            Repeater {
                model: 3
                delegate: ChipButton {
                    required property int index
                    readonly property int slot: index + 1
                    text: "GEN" + slot
                    Layout.preferredWidth: 50
                    chipActive: Gen.activeSlot === slot
                    activeFill:   "#10303a"
                    activeBorder: "#7ff7ff"
                    activeText:   "#d8fbff"
                    onClicked: Gen.recall(slot)
                    ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                    ToolTip.text: "GEN" + slot + ":  "
                        + (Gen.slotFreq(slot) / 1.0e6).toFixed(3) + " MHz "
                        + Gen.slotMode(slot)
                        + (Gen.slotLabel(slot) !== ""
                           ? "  — " + Gen.slotLabel(slot) : "")
                        + qsTr("\n(right-click to reset)")
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: genMenu.popup()
                        Menu {
                            id: genMenu
                            MenuItem {
                                text: qsTr("Reset to default")
                                onTriggered: Gen.resetSlot(slot)
                            }
                        }
                    }
                }
            }

            // ── TIME — HF time-station cycle ─────────────────────────
            // Left-click: tune the next WWV/WWVH/CHU/… entry (cycles
            // through the whole table, advancing each click).  Right-
            // click: pick any station/frequency directly.  Mode follows
            // the station (AM, or USB for CHU).
            ChipButton {
                id: timeBtn
                text: qsTr("TIME")
                Layout.preferredWidth: 50
                chipActive: false
                onClicked: { Gen.deactivate(); Status.show(Time.cycleNext(), 2500) }
                ToolTip.visible: (hovered) && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("HF time stations\n"
                    + "Click to cycle · right-click for the full list")
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: timeMenu.popup()
                    Menu {
                        id: timeMenu
                        implicitWidth: 230
                        // Custom contentItem: cap the height so the long
                        // list (9 stations × several freqs) scrolls, and
                        // show an always-on cyan scrollbar so it's obvious
                        // there's more below — the default thin auto-hiding
                        // indicator left folks unaware the list continued.
                        contentItem: ListView {
                            id: timeList
                            implicitHeight: Math.min(contentHeight, 340)
                            implicitWidth: timeMenu.implicitWidth
                            model: timeMenu.contentModel
                            currentIndex: timeMenu.currentIndex
                            interactive: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                id: timeScroll
                                policy: ScrollBar.AlwaysOn
                                width: 11
                                contentItem: Rectangle {
                                    implicitWidth: 11
                                    radius: 5
                                    color: timeScroll.pressed ? "#8fdcff" : "#5ec8ff"
                                    opacity: 0.85
                                }
                                background: Rectangle {
                                    radius: 5
                                    color: "#14202a"
                                    border.color: "#2a4a5a"
                                    border.width: 1
                                }
                            }
                        }
                        // Flat list: bold station headers (disabled) + the
                        // tunable frequency rows beneath each.  A single
                        // Repeater of MenuItems is the reliable Qt6 pattern
                        // (nested Repeater-of-Menu submenus do not register).
                        Repeater {
                            model: Time.menuEntries()
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.text
                                enabled: !modelData.header
                                font.bold: modelData.header
                                onTriggered: {
                                    Gen.deactivate()
                                    Status.show(
                                        Time.tuneEntry(modelData.station,
                                                       modelData.freq), 2500)
                                }
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Reset cycle to first entry")
                            onTriggered: Time.resetCycle()
                        }
                    }
                }
            }

            // ── Mem — frequency memory bank ──────────────────────────
            // Left-click: recall a preset.  Right-click: save current /
            // manage.  (Full editing in Settings → Bands → Memory.)
            ChipButton {
                id: memBtn
                text: qsTr("Mem")
                Layout.preferredWidth: 50
                chipActive: false
                onClicked: memRecallMenu.popup()
                Menu {
                    id: memRecallMenu
                    Repeater {
                        model: root.memList
                        delegate: MenuItem {
                            required property var modelData
                            required property int index
                            text: (modelData.name.length > 0
                                   ? modelData.name : modelData.freqMHz)
                                  + "   " + modelData.freqMHz + " " + modelData.mode
                            onTriggered: { Gen.deactivate(); Memory.recall(index) }
                        }
                    }
                    MenuItem {
                        text: qsTr("(no presets — right-click to save)")
                        enabled: false
                        visible: root.memList.length === 0
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: memManageMenu.popup()
                    Menu {
                        id: memManageMenu
                        MenuItem {
                            text: qsTr("Save current…")
                            onTriggered: {
                                memNameDialog.suggested = Memory.currentAutoName()
                                memNameDialog.open()
                            }
                        }
                        MenuItem {
                            text: qsTr("Manage presets…")
                            onTriggered: Help.openSettings("memory")
                        }
                    }
                }

                // Name-on-save: prompt for an operator name, pre-filled with
                // the freq auto-name (so it stays optional) — gives saved
                // memories a real name to recall by instead of the frequency.
                Dialog {
                    id: memNameDialog
                    title: qsTr("Save memory")
                    modal: true
                    parent: Overlay.overlay
                    anchors.centerIn: Overlay.overlay
                    width: 340
                    standardButtons: Dialog.Ok | Dialog.Cancel
                    property string suggested: ""
                    onAboutToShow: {
                        nameField.text = memNameDialog.suggested
                        nameField.selectAll()
                        nameField.forceActiveFocus()
                    }
                    // Empty/whitespace falls back to the freq auto-name in
                    // MemoryStore::addCurrent, so OK-ing as-is still works.
                    onAccepted: Memory.addCurrent(nameField.text)
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        Label {
                            text: qsTr("Name this memory (e.g. \"W1AW 2 m\"):")
                            Layout.fillWidth: true
                        }
                        TextField {
                            id: nameField
                            Layout.fillWidth: true
                            selectByMouse: true
                            onAccepted: memNameDialog.accept()
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
