// Lyra — one VFO (A, or B = SPLIT TX / SUB RX2) in the Tuning panel, drawn
// by the selected VFO style.
//
// The style (VfoStyle*.qml, picked by Prefs.vfoStyle through VfoStyles.js)
// only DRAWS.  Everything that must behave the same in every style lives
// here: the FreqDisplay tuning core over the style's tuneRect, the typed-
// entry field, the shared Step/Mode row and TX pip at the rects the style
// reports, the focus click, and the right-click "VFO style" menu.
//
// Style contract — a style's root item provides:
//   inputs : freqHz, selectedDigit, vfoState ("rx"|"tx"|"armed"), roleText,
//            vfoLetter, modeText, stepHz, showMode
//   outputs: naturalWidth/Height (design size), tuneRect, digitRects (10,
//            index = place, empty = hidden), stepModeRect, txPipRect — all in
//            the style's own coordinates — and theme { label, field,
//            fieldBorder, text, accent, pipIdle, pipIdleBorder, pipIdleText,
//            compact }.
// Styles must not touch Stream / Prefs.  scratch/test_vfo_styles.cpp checks
// every registered style against this contract.
//
// The style is drawn at its natural size and scaled uniformly — never up —
// to fit this box, so a large style on a small panel shrinks, never clips.
import QtQuick
import QtQuick.Controls
import QtQml
import LyraUI
import "VfoStyles.js" as VfoStyles

Item {
    id: box
    required property var panel      // TuningPanel root
    required property bool isB       // false = VFO A, true = VFO B

    readonly property var styleEntry: VfoStyles.resolve(Prefs.vfoStyle)
    readonly property var style: loader.item
    readonly property real naturalWidth: style ? style.naturalWidth : 360
    readonly property real naturalHeight: style ? style.naturalHeight : 108
    readonly property real fitScale:
        Math.min(1, width / naturalWidth, height / naturalHeight)

    readonly property rect noRect: Qt.rect(0, 0, 0, 0)
    readonly property rect tuneRect: style ? style.tuneRect : noRect
    readonly property rect stepModeRect: style ? style.stepModeRect : noRect
    readonly property rect txPipRect: style ? style.txPipRect : noRect
    readonly property var theme: style ? style.theme : ({
        label: "#cccccc", field: "#161e28", fieldBorder: "#2a3a4a",
        text: "#f2f8fc", accent: "#50d0ff",
        pipIdle: "#2a323c", pipIdleBorder: "#5a6570", pipIdleText: "#7a8490",
        compact: false })

    // VFO A shows the RX1 carrier (DDS + CW marker offset); VFO B shows
    // TuningPanel.displayBHz (SPLIT carrier / SUB RX2 / parked B).
    readonly property int freqHz: isB ? panel.displayBHz
                                      : panel.centerHz + WdspEngine.markerOffsetHz
    // TX is assigned here: A in simplex, B in SPLIT.
    readonly property bool txHere: isB ? Stream.splitEnabled : !Stream.splitEnabled
    readonly property string vfoState: isB
        ? ((Stream.splitEnabled && Stream.txDisplayActive) ? "tx"
           : (Stream.subEnabled && Stream.focusedRx === 2) ? "rx" : "armed")
        : ((Stream.txDisplayActive && !Stream.splitEnabled) ? "tx"
           : (Stream.subEnabled && Stream.focusedRx !== 1) ? "armed" : "rx")
    readonly property string roleText: isB
        ? (Stream.splitEnabled ? qsTr("TX") : qsTr("RX"))
        : ((Stream.txDisplayActive && !Stream.splitEnabled) ? qsTr("TX") : qsTr("RX"))

    function commitHz(hz) {
        if (isB) panel.commitBCarrier(hz)
        else if (!Stream.txBlocksBandJump(hz - WdspEngine.markerOffsetHz))
            Stream.setRx1FreqHz(hz - WdspEngine.markerOffsetHz)
    }

    implicitWidth: naturalWidth
    implicitHeight: naturalHeight

    Item {
        id: stage
        width: box.naturalWidth
        height: box.naturalHeight
        transformOrigin: Item.TopLeft
        scale: box.fitScale
        x: Math.round((box.width - width * scale) / 2)
        y: Math.round((box.height - height * scale) / 2)

        Loader {
            id: loader
            source: Qt.resolvedUrl(box.styleEntry.file)
            onLoaded: {
                item.freqHz        = Qt.binding(function () { return box.freqHz })
                item.selectedDigit = Qt.binding(function () { return fd.selectedDigit })
                item.vfoState      = Qt.binding(function () { return box.vfoState })
                item.roleText      = Qt.binding(function () { return box.roleText })
                item.vfoLetter     = box.isB ? "B" : "A"
                item.modeText      = Qt.binding(function () {
                    return (box.isB && Stream.subEnabled) ? Prefs.modeRx2 : Prefs.mode })
                item.stepHz        = Qt.binding(function () { return stepMode.stepHz })
                item.showMode      = Qt.binding(function () { return stepMode.showMode })
            }
        }

        FreqDisplay {
            id: fd
            x: box.tuneRect.x
            y: box.tuneRect.y
            width: box.tuneRect.width
            height: box.tuneRect.height
            freqHz: box.freqHz
            externalStepHz: stepMode.stepHz
            digitRects: box.style
                ? box.style.digitRects.map(function (d) {
                      return Qt.rect(d.x - box.tuneRect.x, d.y - box.tuneRect.y,
                                     d.width, d.height) })
                : []
            onFreqEdited: (hz) => box.commitHz(hz)
            onEditRequested: {
                editField.text = (fd.freqHz / 1.0e6).toFixed(6)
                editField.visible = true
                editField.selectAll()
                editField.forceActiveFocus()
            }
        }

        TextField {
            id: editField
            x: fd.x
            y: fd.y
            width: fd.width
            height: fd.height
            visible: false
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            font.family: "Consolas"
            font.pixelSize: 22
            font.bold: true
            color: "#ffb000"
            background: Rectangle {
                color: "#000000"
                border.color: "#00d8ff"
                border.width: 2
                radius: 3
            }
            function commit() {
                if (!visible) return
                var hz = fd.parseFreqInput(text)   // entered carrier
                visible = false
                if (hz >= 0) box.commitHz(hz)
            }
            onAccepted: commit()
            onActiveFocusChanged: if (!activeFocus) commit()
            Keys.onEscapePressed: visible = false
        }

        VfoStepMode {
            id: stepMode
            panel: box.panel
            isB: box.isB
            showMode: box.isB ? Stream.subEnabled : true
            theme: box.theme
            x: box.stepModeRect.x
            y: box.stepModeRect.y
            width: box.stepModeRect.width
            height: box.stepModeRect.height
        }

        VfoTxPip {
            active: box.txHere
            keyed: Stream.txDisplayActive
            theme: box.theme
            tipText: box.txHere
                ? (box.isB ? qsTr("Transmit on VFO B") : qsTr("Transmit on VFO A"))
                : (box.isB ? qsTr("Click to transmit on VFO B (enter SPLIT)")
                           : qsTr("Click to transmit on VFO A (leave SPLIT)"))
            onAssign: box.panel.assignTx(box.isB)
            x: box.txPipRect.x
            y: box.txPipRect.y
            width: box.txPipRect.width
            height: box.txPipRect.height
        }
    }

    // Any left press on the VFO focuses its receiver, then passes through.
    MouseArea {
        anchors.fill: parent
        z: 3
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => {
            Stream.setFocusedRx(box.isB ? 2 : 1)
            mouse.accepted = false
        }
    }

    // Right-click → VFO style menu (both VFOs share the choice).
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: styleMenu.popup()
    }
    Menu {
        id: styleMenu
        MenuItem { text: qsTr("VFO style"); enabled: false }
        MenuSeparator {}
    }
    Instantiator {
        model: VfoStyles.styles
        delegate: MenuItem {
            required property var modelData
            text: modelData.name
            checkable: true
            checked: box.styleEntry.id === modelData.id
            onTriggered: {
                Prefs.vfoStyle = modelData.id
                // Qt toggled `checked` on click, breaking the binding; restore
                // it so re-picking the current style leaves it ticked.
                checked = Qt.binding(function () { return box.styleEntry.id === modelData.id })
            }
        }
        onObjectAdded: (index, object) => styleMenu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => styleMenu.removeItem(object)
    }
}
