// Lyra — Band dock panel.
//
// Four rows, matching old Lyra's BandSelectorPanel plus transverter chips:
//   1. AMATEUR  — 160m … 6m amateur bands
//   2. BC       — shortwave broadcast meter bands (120m … 13m)
//   3. GEN      — GEN1/2/3 general-coverage slots (TIME + Mem join here)
//   4. XVTR     — transverter slots (RF on the dial; radio IF = RF − LO)
//   5. ANT      — Alex ANT 1/2/3 + RX jack (P2 radios with a front-end
//                 ladder only; hidden on HL2 and Hermes-class BrickSDR)
// All built from the shared C++ band tables (Bands context property),
// so the lists never drift from the protocol/DSP side.
//
// Active-band highlight is set IMPERATIVELY on the rx1FreqChanged signal
// (Bands.indexForFreq / broadcastIndexForFreq) — never a per-button
// reactive read of Stream.rx1FreqHz (that proved unreliable here).  Each
// chip's look derives from `chipActive` (RX1, red) and `chipSub` (SUB, green).

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
    property int activeBandRx2: -1  // amateur band containing SUB, or -1
    property int activeBcRx2: -1
    property int activeCbRx2: -1
    function refreshActive() {
        root.activeBand = Bands.indexForFreq(Stream.rx1FreqHz)
        root.activeBc   = Bands.broadcastIndexForFreq(Stream.rx1FreqHz)
        root.activeCb   = Bands.cbIndexForFreq(Stream.rx1FreqHz)
        var rx2 = Stream.rx2FreqHz
        root.activeBandRx2 = Bands.indexForFreq(rx2)
        root.activeBcRx2   = Bands.broadcastIndexForFreq(rx2)
        root.activeCbRx2   = Bands.cbIndexForFreq(rx2)
    }

    // Ham / BC / CB chips: click = focused RX (A, or SUB if TUNE B);
    // Shift+click or right-click = SUB (turns SUB on, keeps A focus).
    function tuneRx1(key, defaultHz) {
        Gen.deactivate()
        var f = BandMemory.recallFreq(key, Stream.rx1FreqHz, defaultHz, false)
        Stream.setRx1FreqHz(f > 0 ? f : defaultHz)
    }
    function tuneSub(key, defaultHz) {
        var f = BandMemory.recallFreq(key, Stream.rx2FreqHz, defaultHz, true)
        Stream.setRx2FreqHz(f > 0 ? f : defaultHz)
        Stream.setSubEnabled(true)
        BandMemory.applyRx2Band(key)
    }
    function tuneVfoB(key, defaultHz) {
        var f = BandMemory.recallFreq(key, Stream.vfoBHz, defaultHz, false)
        Stream.setVfoBHz(f > 0 ? f : defaultHz)
    }
    function goBand(key, defaultHz, forceSub) {
        if (forceSub) {
            tuneSub(key, defaultHz)
            return
        }
        if (Stream.focusedRx === 2 && Stream.subEnabled) {
            tuneSub(key, defaultHz)
            return
        }
        if (Stream.focusedRx === 2 && Stream.splitEnabled) {
            tuneVfoB(key, defaultHz)
            return
        }
        tuneRx1(key, defaultHz)
    }
    // Live memory-preset list for the Mem recall menu (kept current).
    property var memList: []
    function refreshMem() { root.memList = Memory.list() }

    Component.onCompleted: { refreshActive(); refreshMem() }
    Connections {
        target: Stream
        function onRx1FreqChanged() { root.refreshActive() }
        function onRx2FreqChanged() { root.refreshActive() }
        function onSubEnabledChanged() { root.refreshActive() }
    }
    Connections {
        target: Memory
        function onChanged() { root.refreshMem() }
    }

    // Shared flat-chip button (same look across all three rows).  Active
    // colours default to the amateur red-glow; GEN overrides them to cyan.
    readonly property string bandTip:
        qsTr("Click: VFO A (or SUB if TUNE B)\n"
           + "Click again on this band: next stored frequency (1 / 2 / 3)\n"
           + "Shift+click / right-click: SUB")

    component ChipButton : Button {
        id: cb
        property bool chipActive: false
        property bool chipSub: false     // SUB parked on this band (green)
        property string bandKey: ""
        // 1..3 on the chip that owns a stack.  Reading stackGeneration makes
        // the mark refresh when a second click advances the slot.
        readonly property int stackMark: {
            var g = BandMemory.stackGeneration
            if (!bandKey || g < 0)
                return 0
            if (chipActive)
                return BandMemory.stackSlot(bandKey)
            if (chipSub)
                return BandMemory.stackSlotRx2(bandKey)
            return 0
        }
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
                   : (cb.chipSub ? "#082610"
                      : (cb.down ? "#1d2b38" : "#b416202a"))
            border.width: (cb.chipActive || cb.chipSub) ? 2 : 1
            border.color: cb.chipActive
                          ? (cb.chipSub ? "#34c759" : cb.activeBorder)
                          : (cb.chipSub ? "#34c759"
                             : (cb.hovered ? "#8fdcff" : "#5ec8ff"))
            Text {
                visible: cb.stackMark > 0
                text: cb.stackMark
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 2
                anchors.bottomMargin: 0
                font.pixelSize: 8
                font.bold: true
                color: cb.chipActive ? "#ffcc88" : "#88ffcc"
            }
        }
        contentItem: Text {
            text: cb.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            font.bold: true
            color: cb.chipActive ? cb.activeText
                   : (cb.chipSub ? "#88ffcc" : "#5ec8ff")
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
                    bandKey: modelData.name
                    chipActive: index === root.activeBand
                    chipSub: Stream.subEnabled && index === root.activeBandRx2
                    ToolTip.visible: hovered && Prefs.tooltipsEnabled
                    ToolTip.text: root.bandTip
                    onClicked: root.goBand(modelData.name, modelData.hz, false)
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            mouse.accepted = (mouse.button === Qt.RightButton)
                                    || (mouse.modifiers & Qt.ShiftModifier)
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton)
                                hamSubMenu.popup()
                            else
                                root.goBand(modelData.name, modelData.hz, true)
                        }
                        Menu {
                            id: hamSubMenu
                            MenuItem {
                                text: qsTr("Tune SUB to %1").arg(modelData.name)
                                onTriggered: root.goBand(modelData.name,
                                                         modelData.hz, true)
                            }
                        }
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
                    bandKey: "cb_" + modelData.name
                    chipActive: index === root.activeCb
                    chipSub: Stream.subEnabled && index === root.activeCbRx2
                    ToolTip.visible: hovered && Prefs.tooltipsEnabled
                    ToolTip.text: root.bandTip
                    onClicked: root.goBand("cb_" + modelData.name,
                                           modelData.hz, false)
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            mouse.accepted = (mouse.button === Qt.RightButton)
                                    || (mouse.modifiers & Qt.ShiftModifier)
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton)
                                cbSubMenu.popup()
                            else
                                root.goBand("cb_" + modelData.name,
                                            modelData.hz, true)
                        }
                        Menu {
                            id: cbSubMenu
                            MenuItem {
                                text: qsTr("Tune SUB to %1").arg(modelData.name)
                                onTriggered: root.goBand("cb_" + modelData.name,
                                                         modelData.hz, true)
                            }
                        }
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
                    bandKey: "bc_" + modelData.name
                    chipActive: index === root.activeBc
                    chipSub: Stream.subEnabled && index === root.activeBcRx2
                    ToolTip.visible: hovered && Prefs.tooltipsEnabled
                    ToolTip.text: root.bandTip
                    onClicked: root.goBand("bc_" + modelData.name,
                                           modelData.hz, false)
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            mouse.accepted = (mouse.button === Qt.RightButton)
                                    || (mouse.modifiers & Qt.ShiftModifier)
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton)
                                bcSubMenu.popup()
                            else
                                root.goBand("bc_" + modelData.name,
                                            modelData.hz, true)
                        }
                        Menu {
                            id: bcSubMenu
                            MenuItem {
                                text: qsTr("Tune SUB to %1").arg(modelData.name)
                                onTriggered: root.goBand("bc_" + modelData.name,
                                                         modelData.hz, true)
                            }
                        }
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
                onClicked: memRecallPopup.open()
                MemoryRecallPopup {
                    id: memRecallPopup
                    memories: root.memList
                    onRecalled: function(index) { Gen.deactivate(); Memory.recall(index) }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: memManageMenu.popup()
                }
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

        // ── Row 4: transverter slots (RF dial, IF on the radio) ──────
        RowLayout {
            spacing: 4
            RowLabel { text: qsTr("Xvtr") }
            Repeater {
                model: 4
                delegate: ChipButton {
                    required property int index
                    text: {
                        Xvtr.activeSlot
                        Xvtr.activeSlotRx2
                        return Xvtr.slotName(index)
                    }
                    opacity: {
                        Xvtr.activeSlot
                        return Xvtr.slotEnabled(index) ? 1.0 : 0.45
                    }
                    chipActive: Xvtr.activeSlot === index
                    chipSub: Stream.subEnabled && Xvtr.activeSlotRx2 === index
                    activeFill: "#082026"
                    activeBorder: "#34d0ff"
                    activeText: "#b8f0ff"
                    ToolTip.visible: hovered && Prefs.tooltipsEnabled
                    ToolTip.text: qsTr("Click: tune RX1 to this transverter\n"
                                     + "Shift+click: SUB · right-click: edit slot")
                    onClicked: {
                        if (!Xvtr.slotEnabled(index)) {
                            xvtrEditor.openFor(index)
                            return
                        }
                        Gen.deactivate()
                        Xvtr.tune(index)
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            mouse.accepted = (mouse.button === Qt.RightButton)
                                    || (mouse.modifiers & Qt.ShiftModifier)
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                xvtrEditor.openFor(index)
                                return
                            }
                            if (Xvtr.slotEnabled(index)) {
                                Gen.deactivate()
                                Xvtr.tuneSub(index)
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Row 5: Alex antenna / RX jack (P2 only) ──────────────────
        // Remembered per band. TX dock is too tight for this; Band already
        // has chip rows and the selection is band-scoped.
        RowLayout {
            visible: P2Bridge.running && P2Bridge.hasAlexAntennaSelect
            spacing: 4
            RowLabel { text: qsTr("Ant") }
            ChipButton {
                visible: P2Bridge.trxAntennaCount >= 1
                text: "1"
                chipActive: P2Bridge.trxAntenna === 1
                onClicked: P2Bridge.trxAntenna = 1
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("ANT 1 — shared TX/RX jack 1 (per band)")
            }
            ChipButton {
                visible: P2Bridge.trxAntennaCount >= 2
                text: "2"
                chipActive: P2Bridge.trxAntenna === 2
                onClicked: P2Bridge.trxAntenna = 2
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("ANT 2 — shared TX/RX jack 2 (per band)")
            }
            ChipButton {
                visible: P2Bridge.trxAntennaCount >= 3
                text: "3"
                chipActive: P2Bridge.trxAntenna === 3
                onClicked: P2Bridge.trxAntenna = 3
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("ANT 3 — shared TX/RX jack 3 (per band)")
            }
            Item { Layout.preferredWidth: 10 }
            RowLabel { text: qsTr("RX") }
            ChipButton {
                visible: (P2Bridge.rxJackMask & 1) !== 0
                text: qsTr("TRX")
                Layout.preferredWidth: 44
                chipActive: P2Bridge.rxInput === 0
                onClicked: P2Bridge.rxInput = 0
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("Receive on the selected ANT jack")
            }
            ChipButton {
                visible: (P2Bridge.rxJackMask & 2) !== 0
                text: qsTr("BYPS")
                Layout.preferredWidth: 48
                chipActive: P2Bridge.rxInput === 1
                onClicked: P2Bridge.rxInput = 1
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("RX Bypass jack (RX-only, front-end bypassed)")
            }
            ChipButton {
                visible: (P2Bridge.rxJackMask & 4) !== 0
                text: P2Bridge.rxExt1Label
                Layout.preferredWidth: 48
                chipActive: P2Bridge.rxInput === 2
                onClicked: P2Bridge.rxInput = 2
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: P2Bridge.rxExt1Label === "RX2"
                        ? qsTr("RX2 RX-only jack (rear panel)")
                        : qsTr("EXT1 RX-only jack")
            }
            ChipButton {
                visible: (P2Bridge.rxJackMask & 8) !== 0
                text: qsTr("XVTR")
                Layout.preferredWidth: 48
                chipActive: P2Bridge.rxInput === 3
                onClicked: P2Bridge.rxInput = 3
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("Transverter RX jack")
            }
            ChipButton {
                text: qsTr("MAP")
                Layout.preferredWidth: 48
                chipActive: antMapPopup.visible
                onClicked: antMapPopup.open()
                ToolTip.visible: hovered && Prefs.tooltipsEnabled
                ToolTip.text: qsTr("Assign ANT and RX jack for every amateur band")
            }
            Item { Layout.fillWidth: true }
        }
    }

    Popup {
        id: antMapPopup
        popupType: Popup.Window
        modal: true
        dim: false
        focus: true
        width: 420
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((parent.width - width) / 2)
        y: 12
        property int feRev: 0
        Connections {
            target: P2Bridge
            function onFrontEndChanged() { antMapPopup.feRev++ }
        }
        background: Rectangle {
            color: "#0d141b"
            radius: 8
            border.color: "#2a4a5a"
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Antenna map")
                color: "#50d0ff"
                font.bold: true
                font.pixelSize: 13
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Same ANT jack for TX and RX. RX-only jacks are the extra column — not a second TX antenna.")
                color: "#8a9aac"
                font.pixelSize: 11
            }
            Repeater {
                model: P2Bridge.running ? P2Bridge.mapBandNames() : []
                delegate: RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: modelData
                        color: "#cdd9e5"
                        font.pixelSize: 12
                        font.bold: true
                        Layout.preferredWidth: 40
                    }
                    // Chips, not ComboBox: a LyraComboBox Window popup
                    // nested inside this MAP Window never paints on Win32.
                    Repeater {
                        model: P2Bridge.trxAntennaCount
                        ChipButton {
                            required property int index
                            text: (index + 1).toString()
                            Layout.preferredWidth: 28
                            chipActive: {
                                var _ = antMapPopup.feRev
                                return P2Bridge.antennaForBand(modelData)
                                       === (index + 1)
                            }
                            onClicked: P2Bridge.setAntennaForBand(
                                           modelData, index + 1)
                        }
                    }
                    Item { Layout.preferredWidth: 8 }
                    ChipButton {
                        visible: (P2Bridge.rxJackMask & 1) !== 0
                        text: qsTr("TRX")
                        Layout.preferredWidth: 40
                        chipActive: {
                            var _ = antMapPopup.feRev
                            return P2Bridge.rxInputForBand(modelData) === 0
                        }
                        onClicked: P2Bridge.setRxInputForBand(modelData, 0)
                    }
                    ChipButton {
                        visible: (P2Bridge.rxJackMask & 2) !== 0
                        text: qsTr("BYPS")
                        Layout.preferredWidth: 44
                        chipActive: {
                            var _ = antMapPopup.feRev
                            return P2Bridge.rxInputForBand(modelData) === 1
                        }
                        onClicked: P2Bridge.setRxInputForBand(modelData, 1)
                    }
                    ChipButton {
                        visible: (P2Bridge.rxJackMask & 4) !== 0
                        text: P2Bridge.rxExt1Label
                        Layout.preferredWidth: 44
                        chipActive: {
                            var _ = antMapPopup.feRev
                            return P2Bridge.rxInputForBand(modelData) === 2
                        }
                        onClicked: P2Bridge.setRxInputForBand(modelData, 2)
                    }
                    ChipButton {
                        visible: (P2Bridge.rxJackMask & 8) !== 0
                        text: qsTr("XVTR")
                        Layout.preferredWidth: 44
                        chipActive: {
                            var _ = antMapPopup.feRev
                            return P2Bridge.rxInputForBand(modelData) === 3
                        }
                        onClicked: P2Bridge.setRxInputForBand(modelData, 3)
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                implicitHeight: 26
                onClicked: antMapPopup.close()
                background: Rectangle {
                    radius: 4
                    color: parent.down ? "#1a3040"
                         : parent.hovered ? "#1f4655" : "#161e28"
                    border.color: "#50d0ff"
                }
                contentItem: Text {
                    text: parent.text
                    color: "#eaf2f7"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Own OS window — a Dialog/Popup.Item is clipped to this dock's
    // QQuickWidget (same lesson as AudioPanel out/tune popups).
    // Dark Lyra fields — stock Windows TextField/CheckBox read as a
    // pasted-in system dialog on this panel.
    Popup {
        id: xvtrEditor
        popupType: Popup.Window
        modal: true
        dim: false
        focus: true
        width: 360
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((parent.width - width) / 2)
        y: 12
        property int idx: 0
        readonly property color cAccent: "#50d0ff"
        readonly property color cText:   "#cdd9e5"
        readonly property color cMuted:  "#8a9aac"
        readonly property color cField:  "#161e28"
        readonly property color cBorder: "#2a3a4a"
        function mhz(field) {
            var v = parseFloat(field.text)
            return isNaN(v) ? 0 : v * 1e6
        }
        function save() {
            Xvtr.setSlot(idx, xvtrOn.checked, xvtrName.text,
                         mhz(xvtrRfLo), mhz(xvtrRfHi), mhz(xvtrLo),
                         parseFloat(xvtrErr.text) || 0,
                         xvtrPa.checked, xvtrRx.checked)
            close()
        }
        function openFor(i) {
            idx = i
            xvtrOn.checked = Xvtr.slotEnabled(i)
            xvtrName.text = Xvtr.slotName(i)
            xvtrRfLo.text = (Xvtr.slotRfLoHz(i) / 1e6).toFixed(6)
            xvtrRfHi.text = (Xvtr.slotRfHiHz(i) / 1e6).toFixed(6)
            xvtrLo.text = (Xvtr.slotLoHz(i) / 1e6).toFixed(6)
            xvtrErr.text = String(Math.round(Xvtr.slotErrorHz(i)))
            xvtrPa.checked = Xvtr.slotDisablePa(i)
            xvtrRx.checked = Xvtr.slotRxOnly(i)
            open()
        }

        component Field: ColumnLayout {
            property alias label: lab.text
            property alias unit: unitLab.text
            property alias text: fld.text
            property alias placeholderText: fld.placeholderText
            property bool mono: true
            spacing: 3
            Layout.fillWidth: true
            RowLayout {
                spacing: 6
                Label {
                    id: lab
                    color: xvtrEditor.cText
                    font.pixelSize: 12
                }
                Label {
                    id: unitLab
                    color: xvtrEditor.cMuted
                    font.pixelSize: 11
                    visible: text.length > 0
                }
            }
            TextField {
                id: fld
                Layout.fillWidth: true
                implicitHeight: 26
                selectByMouse: true
                color: "#f2f8fc"
                font.family: parent.mono ? "Consolas" : "Segoe UI"
                font.pixelSize: 13
                placeholderTextColor: xvtrEditor.cMuted
                background: Rectangle {
                    radius: 4
                    color: xvtrEditor.cField
                    border.color: fld.activeFocus ? xvtrEditor.cAccent
                                                  : xvtrEditor.cBorder
                }
            }
        }

        component Tick: CheckBox {
            id: cb
            font.pixelSize: 12
            implicitHeight: 22
            indicator: Rectangle {
                implicitWidth: 16
                implicitHeight: 16
                x: cb.leftPadding
                y: parent.height / 2 - height / 2
                radius: 3
                color: xvtrEditor.cField
                border.color: cb.checked ? xvtrEditor.cAccent : "#3a5a6a"
                Text {
                    anchors.centerIn: parent
                    visible: cb.checked
                    text: "✓"
                    color: xvtrEditor.cAccent
                    font.pixelSize: 11
                    font.bold: true
                }
            }
            contentItem: Text {
                text: cb.text
                color: xvtrEditor.cText
                font: cb.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: cb.indicator.width + 8
            }
        }

        component GhostButton: Button {
            implicitHeight: 26
            font.pixelSize: 12
            property bool accent: false
            background: Rectangle {
                radius: 4
                color: parent.down ? "#1a3040"
                     : parent.hovered ? "#1f4655" : xvtrEditor.cField
                border.color: parent.accent ? xvtrEditor.cAccent
                                            : xvtrEditor.cBorder
            }
            contentItem: Text {
                text: parent.text
                color: "#eaf2f7"
                font: parent.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        background: Rectangle {
            color: "#0d141b"
            radius: 8
            border.color: "#2a4a5a"
        }
        contentItem: ColumnLayout {
            id: xvtrForm
            spacing: 10
            Label {
                text: qsTr("Transverter slot %1").arg(xvtrEditor.idx + 1)
                color: xvtrEditor.cAccent
                font.bold: true
                font.pixelSize: 13
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("RF on the dial; radio IF = RF − LO − error.")
                color: xvtrEditor.cMuted
                font.pixelSize: 11
            }
            Tick { id: xvtrOn; text: qsTr("On") }
            Field {
                id: xvtrName
                label: qsTr("Name")
                mono: false
                placeholderText: qsTr("e.g. 2 m")
            }
            Field { id: xvtrRfLo; label: qsTr("RF low");  unit: qsTr("MHz") }
            Field { id: xvtrRfHi; label: qsTr("RF high"); unit: qsTr("MHz") }
            Field { id: xvtrLo;   label: qsTr("LO");      unit: qsTr("MHz") }
            Field { id: xvtrErr;  label: qsTr("Error");   unit: qsTr("Hz") }
            Tick { id: xvtrPa; text: qsTr("Disable radio PA") }
            Tick { id: xvtrRx; text: qsTr("RX only (block transmit)") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GhostButton { text: qsTr("Cancel"); onClicked: xvtrEditor.close() }
                GhostButton {
                    text: qsTr("OK")
                    accent: true
                    onClicked: xvtrEditor.save()
                }
            }
        }
    }
}
