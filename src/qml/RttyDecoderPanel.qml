// Lyra — RTTY decoder dock panel.
//
// RX-only Baudot copy from the fldigi RTTY receive port (WdspEngine
// rttyDecoder_).  Decoding runs ONLY in DIGU/DIGL.  Contest defaults:
// 45.45 baud / 170 Hz shift / 2210 Hz audio centre (2125/2295).
// Grab to His Call / Name uses the same CwMacros row as the CW decoder.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 470
    implicitHeight: collapsed ? 40 : (body.implicitHeight + 12)
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"

    property bool   collapsed: false
    property string decodedText: ""
    property string grabWord: ""
    property real   sqMetric: 0

    readonly property var colorChoices:
        ["#39ff14", "#ffbe5a", "#00e5ff", "#e6edf3", "#ff9a3c", "#ff6b6b"]

    readonly property bool rttyActive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "DIGU" || m === "DIGL"
    }

    function appendDecoded(s) {
        root.decodedText += s
        if (root.decodedText.length > 8000)
            root.decodedText = root.decodedText.substring(root.decodedText.length - 6000)
    }
    function clearDecoded() { root.decodedText = "" }

    function pushSquelch() {
        WdspEngine.setRttySquelch(Prefs.rttySquelchOn, Prefs.rttySquelchValue)
    }

    function applyKnobs() {
        WdspEngine.setRttyCenterHz(Prefs.rttyCenterHz)
        WdspEngine.setRttyShiftHz(Prefs.rttyShiftHz)
        WdspEngine.setRttyBaud(Prefs.rttyBaud)
        WdspEngine.setRttyReverse(Prefs.rttyReverse)
        pushSquelch()
    }

    function wordAt(pos) {
        var t = decodeOut.getText(0, decodeOut.length)
        if (pos < 0 || pos > t.length) return ""
        function isCh(c) {
            return (c >= "A" && c <= "Z") || (c >= "a" && c <= "z")
                || (c >= "0" && c <= "9") || c === "/"
        }
        var a = pos, b = pos
        while (a > 0 && isCh(t.charAt(a - 1))) a--
        while (b < t.length && isCh(t.charAt(b))) b++
        return t.substring(a, b).trim()
    }

    Component.onCompleted: applyKnobs()

    Connections {
        target: WdspEngine
        function onRttyDecodedChar(ch) { root.appendDecoded(ch) }
    }

    Timer {
        interval: 100; repeat: true
        running: !root.collapsed && root.rttyActive && WdspEngine.rttyDecodeEnabled
        onTriggered: root.sqMetric = WdspEngine.rttyDecodeMetric()
        onRunningChanged: if (!running) root.sqMetric = 0
    }

    component ChipButton: Rectangle {
        id: chip
        property string label: ""
        property bool   lit: false
        property bool   chipEnabled: true
        signal clicked()
        implicitHeight: 26
        implicitWidth: chipTxt.implicitWidth + 22
        radius: 4
        opacity: chipEnabled ? 1.0 : 0.4
        color: chip.lit ? "#2e7d9a" : "#1c252b"
        border.color: chip.lit ? root.cAccent : "#3a4750"
        Text {
            id: chipTxt
            anchors.centerIn: parent
            text: chip.label
            color: chip.lit ? "#ffffff" : root.cMuted
            font.pixelSize: 12; font.bold: chip.lit
        }
        MouseArea {
            anchors.fill: parent
            enabled: chip.chipEnabled
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    component Divider: RowLayout {
        id: div
        property string label: ""
        Layout.fillWidth: true
        spacing: 8
        Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
        Label { text: div.label; color: root.cMuted; font.pixelSize: 11 }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
    }

    // SizeRootObjectToView stretches this root to the (often short) floating
    // dock and clips anything past the copy pane.  Scroll + knobs-first so
    // Baud / Shift / SQL are on screen without hunting.
    ScrollView {
        id: bodyScroll
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        contentWidth: availableWidth
        contentHeight: body.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: root.collapsed ? ScrollBar.AlwaysOff
                                                  : ScrollBar.AsNeeded

    ColumnLayout {
        id: body
        width: bodyScroll.availableWidth
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("RTTY Decoder"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label {
                visible: WdspEngine.rttyDecodeEnabled && !root.rttyActive
                text: qsTr("• switch to DIGU/DIGL to decode")
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            ChipButton {
                label: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
            }
        }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            ChipButton {
                label: WdspEngine.rttyDecodeEnabled ? qsTr("Decoding") : qsTr("Decode OFF")
                lit: WdspEngine.rttyDecodeEnabled
                onClicked: {
                    WdspEngine.rttyDecodeEnabled = !WdspEngine.rttyDecodeEnabled
                    if (WdspEngine.rttyDecodeEnabled)
                        root.applyKnobs()
                }
            }
            Item { Layout.fillWidth: true }
            ChipButton {
                label: qsTr("Clear")
                Layout.rightMargin: 18
                onClicked: root.clearDecoded()
            }
        }

        Divider { visible: !root.collapsed; label: qsTr("Modem") }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Baud"); color: root.cText; font.pixelSize: 12 }
            ComboBox {
                id: baudBox
                model: ["45.45", "50", "75"]
                Component.onCompleted: {
                    var i = model.indexOf(String(Prefs.rttyBaud))
                    currentIndex = (i >= 0) ? i : 0
                }
                onActivated: {
                    Prefs.rttyBaud = parseFloat(currentText)
                    WdspEngine.setRttyBaud(Prefs.rttyBaud)
                }
                Layout.preferredWidth: 90
            }
            Label { text: qsTr("Shift"); color: root.cText; font.pixelSize: 12 }
            ComboBox {
                id: shiftBox
                model: ["170", "425", "850"]
                Component.onCompleted: {
                    var i = model.indexOf(String(Prefs.rttyShiftHz))
                    currentIndex = (i >= 0) ? i : 0
                }
                onActivated: {
                    Prefs.rttyShiftHz = parseInt(currentText)
                    WdspEngine.setRttyShiftHz(Prefs.rttyShiftHz)
                }
                Layout.preferredWidth: 80
            }
            ChipButton {
                label: qsTr("Reverse")
                lit: Prefs.rttyReverse
                onClicked: {
                    Prefs.rttyReverse = !Prefs.rttyReverse
                    WdspEngine.setRttyReverse(Prefs.rttyReverse)
                }
            }
        }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Centre"); color: root.cText; font.pixelSize: 12 }
            SpinBox {
                from: 500; to: 3000; stepSize: 10
                value: Prefs.rttyCenterHz
                onValueModified: {
                    Prefs.rttyCenterHz = value
                    WdspEngine.setRttyCenterHz(value)
                }
            }
            Label { text: qsTr("Hz"); color: root.cMuted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
        }

        Divider { visible: !root.collapsed; label: qsTr("Threshold") }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            ChipButton {
                label: qsTr("SQL")
                lit: Prefs.rttySquelchOn
                onClicked: {
                    Prefs.rttySquelchOn = !Prefs.rttySquelchOn
                    root.pushSquelch()
                }
            }
            Slider {
                from: 0; to: 100; stepSize: 1
                value: Prefs.rttySquelchValue
                Layout.fillWidth: true
                enabled: Prefs.rttySquelchOn
                onMoved: {
                    Prefs.rttySquelchValue = value
                    root.pushSquelch()
                }
            }
            Label {
                text: Prefs.rttySquelchValue.toFixed(0)
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 28
            }
        }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Signal")
                color: root.cText; font.pixelSize: 11
                Layout.preferredWidth: 44
            }
            Item {
                Layout.fillWidth: true
                implicitHeight: 10
                Rectangle {
                    anchors.fill: parent
                    radius: 2; color: "#0e1621"
                    border.color: "#1a2632"; border.width: 1
                }
                Rectangle {
                    anchors.left: parent.left; y: 1
                    height: parent.height - 2
                    width: parent.width * Math.max(0, Math.min(root.sqMetric, 100)) / 100
                    radius: 2; color: root.cAccent
                }
                Rectangle {
                    visible: Prefs.rttySquelchOn
                    width: 2; height: parent.height + 4
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.width
                        * Math.max(0, Math.min(Prefs.rttySquelchValue, 100)) / 100
                        - width / 2
                    color: "#e6a23f"
                }
            }
            Label {
                text: Math.round(Math.min(root.sqMetric, 99))
                color: root.cText; font.family: "Consolas"; font.pixelSize: 12
                Layout.preferredWidth: 28
            }
        }

        Rectangle {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            radius: 6
            color: "#070d12"
            border.color: "#1c2a36"
            ScrollView {
                id: decodeScroll
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                property bool followBottom: true
                function atBottom() {
                    var f = decodeScroll.contentItem
                    if (!f) return true
                    var ch = Math.max(f.contentHeight, decodeOut.implicitHeight)
                    if (ch <= f.height) return true
                    return f.contentY >= (ch - f.height) - decodeOut.font.pixelSize * 1.5
                }
                function scrollToBottom() {
                    var f = decodeScroll.contentItem
                    if (!f) return
                    var ch = Math.max(f.contentHeight, decodeOut.implicitHeight)
                    f.contentY = ch > f.height ? ch - f.height : 0
                }
                Connections {
                    target: decodeScroll.contentItem
                    function onContentYChanged() {
                        decodeScroll.followBottom = decodeScroll.atBottom()
                    }
                }
                TextArea {
                    id: decodeOut
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    text: root.decodedText
                    color: Prefs.cwDecodeColor
                    font.family: "Consolas"
                    font.pixelSize: Prefs.cwDecodeFontSize
                    background: null
                    bottomPadding: Math.round(Prefs.cwDecodeFontSize * 1.5)
                    onTextChanged: {
                        cursorPosition = length
                        if (decodeScroll.followBottom)
                            Qt.callLater(decodeScroll.scrollToBottom)
                    }
                    onContentHeightChanged:
                        if (decodeScroll.followBottom)
                            Qt.callLater(decodeScroll.scrollToBottom)
                    onCursorRectangleChanged:
                        if (decodeScroll.followBottom)
                            Qt.callLater(decodeScroll.scrollToBottom)
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: (pt) => {
                            var w = root.wordAt(
                                decodeOut.positionAt(pt.position.x, pt.position.y))
                            if (w.length > 0) CwMacros.hisCall = w.toUpperCase()
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (pt) => {
                            root.grabWord = root.wordAt(
                                decodeOut.positionAt(pt.position.x, pt.position.y))
                            grabMenu.popup()
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: qsTr("Double-click a call → His Call · right-click for menu")
                color: root.cMuted; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            visible: !root.collapsed
            Layout.fillWidth: true
            text: qsTr("Tune so the two RTTY tones sit around 2210 Hz in the "
                       + "passband (mark/space 2295/2125 with 170 Hz shift).  "
                       + "SQL uses fldigi’s 0–100 mark/space vs noise metric — "
                       + "raise it until the Signal bar sits below the amber "
                       + "tick on noise.  DIGU on all HF bands.")
            color: root.cMuted; font.pixelSize: 10; wrapMode: Text.WordWrap
        }
    }
    }

    Menu {
        id: grabMenu
        MenuItem {
            text: qsTr("→ His Call:  ") + root.grabWord.toUpperCase()
            enabled: root.grabWord.length > 0
            onTriggered: CwMacros.hisCall = root.grabWord.toUpperCase()
        }
        MenuItem {
            text: qsTr("→ Name:  ") + root.grabWord
            enabled: root.grabWord.length > 0
            onTriggered: CwMacros.opName = root.grabWord
        }
    }
}
