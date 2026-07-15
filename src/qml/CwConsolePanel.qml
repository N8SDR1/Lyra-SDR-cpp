// Lyra — CW console dock panel (#105 send/decode + #176 macro bank).
//
// Chip-launched floating dock.  Three stacked sections:
//   - Controls: WPM + Repeat + a prominent Stop (Esc).
//   - Macro bank (#176): named click-to-send macros — the common defaults,
//     then a "My macros" group the operator builds out.  Each macro also
//     fires on its F-key (F1..F12, handled globally in MainWindow).  Tokens
//     {MYCALL}/{CALL}/{RST}/{NAME}/{#} are filled from the contact row.
//     Edit mode (pencil) turns the chips into editable name+text fields.
//   - Keyboard send: type-and-send field (Enter sends, Esc/Stop aborts).
// (The RX decoder is a SEPARATE "CW Dec" panel — #173 CW-5b.)
//
// Bindings: CwMacros (CwMacroModel) for the bank + contact row + send/stop;
// Stream for cwKeyerSpeedWpm + sendCw/abortCw; WdspEngine.mode to gate the
// send controls outside CWU/CWL.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 460
    implicitHeight: collapsed ? 40 : (body.implicitHeight + sendBar.implicitHeight + 18)
    color: "#0d141b"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cToken:  "#ff9a3c"

    property bool collapsed: false
    property bool editMode: false
    property int  lastIdx: -1          // last macro fired (for Repeat)
    property bool repeatOn: false
    property int  repeatSec: 4
    // The editable field a token-palette click inserts into (a macro's text
    // field in edit mode, or the keyboard line).  A palette chip is a plain
    // MouseArea (no focus grab), so this field keeps its cursor position.
    property var  insertTarget: null

    // Click-to-insert palette: the 5 built-ins + the operator's personal tokens.
    readonly property var builtinTokens: ["{CALL}", "{RST}", "{#}", "{MYCALL}", "{NAME}"]
    readonly property var userTokenChips:
        CwMacros.tokens.map(function (t) { return "{" + t.name + "}" })
                       .filter(function (s) { return s !== "{}" })

    function insertToken(tok) {
        if (insertTarget) insertTarget.insert(insertTarget.cursorPosition, tok)
    }

    // CW send only does anything in CWU/CWL; dim + disable + hint otherwise.
    readonly property bool cwActive: {
        var m = WdspEngine.mode.toUpperCase()
        return m === "CWU" || m === "CWL"
    }

    // The bank, split into the seeded defaults and the operator's own.
    readonly property var allMacros: CwMacros.macros
    readonly property var defaultMacros: allMacros.filter(function (m) { return !m.user })
    readonly property var userMacros:    allMacros.filter(function (m) { return  m.user })

    Timer {
        running: root.repeatOn && root.lastIdx >= 0 && root.cwActive
        interval: Math.max(1, root.repeatSec) * 1000
        repeat: true
        onTriggered: if (root.lastIdx >= 0) CwMacros.sendIndex(root.lastIdx)
    }

    // ── A reusable macro chip (shared by the defaults + My-macros grids) ──
    Component {
        id: macroChip
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.editMode ? 66 : 46
            radius: 8
            property bool sending: CwMacros.sendingIndex === modelData.index
            color: modelData.user && root.editMode ? "#16202a"
                   : sending ? "#10303a" : "#1c252b"
            border.color: sending ? "#7fd6ef" : "#3a4750"

            MouseArea {
                anchors.fill: parent
                enabled: !root.editMode
                cursorShape: Qt.PointingHandCursor
                onClicked: { CwMacros.sendIndex(modelData.index); root.lastIdx = modelData.index }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10

                // Fn badge (modelData.fkey 0 = no key assigned)
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 30; implicitHeight: 20; radius: 5
                    color: "transparent"
                    border.color: sending ? root.cAccent : "#5b6b76"
                    visible: modelData.fkey > 0
                    Text {
                        anchors.centerIn: parent
                        text: "F" + modelData.fkey
                        color: sending ? root.cAccent : "#8fb0bc"
                        font.family: "Consolas"; font.pixelSize: 11
                    }
                }

                // ── Send view ──────────────────────────────────────────
                ColumnLayout {
                    visible: !root.editMode
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: 6
                        Text {
                            text: modelData.name; color: "#fff"
                            font.bold: true; font.pixelSize: 13
                            elide: Text.ElideRight; Layout.maximumWidth: 150
                        }
                        Text {
                            visible: sending
                            text: "▮ sending…"; color: root.cAccent; font.pixelSize: 11
                            SequentialAnimation on opacity {
                                running: sending; loops: Animation.Infinite
                                NumberAnimation { from: 1.0; to: 0.35; duration: 450 }
                                NumberAnimation { from: 0.35; to: 1.0; duration: 450 }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.preview && modelData.preview.length > 0
                              ? modelData.preview : modelData.text
                        color: sending ? "#9fbfca" : root.cMuted
                        font.family: "Consolas"; font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // ── Edit view ──────────────────────────────────────────
                ColumnLayout {
                    visible: root.editMode
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: nameEdit
                        Layout.fillWidth: true
                        text: modelData.name
                        placeholderText: qsTr("name")
                        color: root.cText; font.bold: true; font.pixelSize: 12
                        background: Rectangle { radius: 4; color: "#0b141b"
                            border.color: nameEdit.activeFocus ? root.cAccent : "#2a4a5a" }
                        onEditingFinished: CwMacros.setMacro(modelData.index, text, textEdit.text)
                    }
                    TextField {
                        id: textEdit
                        Layout.fillWidth: true
                        text: modelData.text
                        placeholderText: qsTr("CW text — use {CALL} {RST} {#} {MYCALL} {NAME}  · add {LOG} to log the QSO")
                        color: root.cMuted; font.family: "Consolas"; font.pixelSize: 12
                        background: Rectangle { radius: 4; color: "#0b141b"
                            border.color: textEdit.activeFocus ? root.cAccent : "#2a4a5a" }
                        onEditingFinished: CwMacros.setMacro(modelData.index, nameEdit.text, text)
                        onActiveFocusChanged: if (activeFocus) root.insertTarget = textEdit
                    }
                }

                // Delete (edit mode only) — trashcan + confirm, matches the
                // Tuner + Voice Keyer panels.
                TrashCan {
                    visible: root.editMode
                    Layout.alignment: Qt.AlignTop
                    tip: qsTr("Delete this macro")
                    onConfirmed: CwMacros.removeMacro(modelData.index)
                }
            }

            // Sending underline
            Rectangle {
                visible: sending
                anchors.left: parent.left; anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 2
                height: 3; radius: 2; color: root.cAccent
                SequentialAnimation on opacity {
                    running: sending; loops: Animation.Infinite
                    NumberAnimation { from: 0.9; to: 0.3; duration: 450 }
                    NumberAnimation { from: 0.3; to: 0.9; duration: 450 }
                }
            }
        }
    }

    // Scrollable so the macros / token tags / option rows stay reachable
    // even when the operator shrinks the console below its natural height.
    // Outer column: a scrollable body on top, and the keyboard-send bar
    // PINNED at the bottom.  The free-hand type-and-send line must stay
    // reachable no matter how short the (floating) dock is — the host uses
    // QQuickWidget::SizeRootObjectToView, which ignores QML implicitHeight,
    // so a scroll-buried field simply clips off the bottom and reads as
    // "the free-hand input is missing."
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

    ScrollView {
        id: bodyScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth          // no horizontal scroll
        ScrollBar.vertical.policy: root.collapsed ? ScrollBar.AlwaysOff
                                                  : ScrollBar.AsNeeded

    ColumnLayout {
        id: body
        width: bodyScroll.availableWidth
        spacing: 6

        // ── Header ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("CW Console"); color: root.cAccent
                    font.bold: true; font.pixelSize: 14 }
            Label {
                visible: !root.cwActive
                text: qsTr("• switch to CW to send")
                color: "#e0a030"; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitWidth: 60; implicitHeight: 22
                visible: !root.collapsed
                text: root.editMode ? qsTr("Done") : qsTr("Edit")
                onClicked: root.editMode = !root.editMode
                background: Rectangle { radius: 4
                    color: root.editMode ? "#2e7d9a" : "#1f2a35"
                    border.color: root.editMode ? root.cAccent : "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cText
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button {
                implicitWidth: 28; implicitHeight: 22
                text: root.collapsed ? "▲" : "▼"
                onClicked: root.collapsed = !root.collapsed
                background: Rectangle { radius: 4; color: "#1f2a35"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Controls: WPM + Stop ────────────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("WPM"); color: root.cText; font.bold: true; font.pixelSize: 12 }
            LyraSlider {
                id: wpm
                Layout.fillWidth: true
                from: 5; to: 60; stepSize: 1
                snapMode: Slider.SnapAlways
                value: Stream.cwKeyerSpeedWpm
                onMoved: Stream.cwKeyerSpeedWpm = value
            }
            Label {
                text: Math.round(Stream.cwKeyerSpeedWpm) + qsTr(" wpm")
                color: root.cText; font.family: "Consolas"; font.bold: true; font.pixelSize: 12
                Layout.preferredWidth: 56; horizontalAlignment: Text.AlignRight
            }
            Button {
                implicitHeight: 24
                text: qsTr("Stop · Esc")
                onClicked: { CwMacros.stop(); root.repeatOn = false }
                background: Rectangle { radius: 4; color: "#3a1c1c"; border.color: "#c0504d" }
                contentItem: Text { text: parent.text; color: "#ff8a80"
                    font.bold: true; font.pixelSize: 11; leftPadding: 8; rightPadding: 8
                    verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Repeat ──────────────────────────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Button {
                implicitHeight: 22
                text: root.repeatOn ? qsTr("Repeat ON") : qsTr("Repeat")
                onClicked: root.repeatOn = !root.repeatOn
                background: Rectangle { radius: 4
                    color: root.repeatOn ? "#2e7d9a" : "#1c252b"
                    border.color: root.repeatOn ? root.cAccent : "#3a4750" }
                contentItem: Text { text: parent.text
                    color: root.repeatOn ? "#fff" : root.cMuted
                    font.pixelSize: 11; leftPadding: 8; rightPadding: 8
                    verticalAlignment: Text.AlignVCenter }
            }
            Label { text: qsTr("every"); color: root.cMuted; font.pixelSize: 11 }
            LyraSpinBox {
                from: 1; to: 60; value: root.repeatSec
                onValueModified: root.repeatSec = value
                implicitHeight: 24
            }
            Label { text: qsTr("s — re-sends the last macro"); color: root.cMuted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
        }

        // ── Contact row (token source) ──────────────────────────────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("His call"); color: root.cMuted; font.pixelSize: 11 }
            TextField {
                id: hisCall
                Layout.preferredWidth: 92
                text: CwMacros.hisCall
                placeholderText: "—"
                color: root.cText; font.family: "Consolas"; font.pixelSize: 13
                background: Rectangle { radius: 4; color: "#0b141b"
                    border.color: hisCall.activeFocus ? root.cAccent : "#2a4a5a" }
                onTextEdited: CwMacros.hisCall = text
                inputMethodHints: Qt.ImhUppercaseOnly
            }
            Label { text: qsTr("Name"); color: root.cMuted; font.pixelSize: 11 }
            TextField {
                id: opName
                Layout.preferredWidth: 76
                text: CwMacros.opName
                placeholderText: "—"
                color: root.cText; font.family: "Consolas"; font.pixelSize: 13
                background: Rectangle { radius: 4; color: "#0b141b"
                    border.color: opName.activeFocus ? root.cAccent : "#2a4a5a" }
                onTextEdited: CwMacros.opName = text
            }
            Label { text: qsTr("RST"); color: root.cMuted; font.pixelSize: 11 }
            TextField {
                id: rst
                Layout.preferredWidth: 50
                text: CwMacros.rst
                color: root.cText; font.family: "Consolas"; font.pixelSize: 13
                background: Rectangle { radius: 4; color: "#0b141b"
                    border.color: rst.activeFocus ? root.cAccent : "#2a4a5a" }
                onTextEdited: CwMacros.rst = text
            }
            Label { text: qsTr("#"); color: root.cMuted; font.pixelSize: 11 }
            Button {
                implicitWidth: 24; implicitHeight: 24; text: "−"
                onClicked: CwMacros.bumpSerial(-1)
                background: Rectangle { radius: 4; color: "#1c252b"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Label {
                text: CwMacros.serial; color: root.cAccent
                font.family: "Consolas"; font.bold: true; font.pixelSize: 13
                Layout.preferredWidth: 34; horizontalAlignment: Text.AlignHCenter
            }
            Button {
                implicitWidth: 24; implicitHeight: 24; text: "+"
                onClicked: CwMacros.bumpSerial(1)
                background: Rectangle { radius: 4; color: "#1c252b"; border.color: "#3a5060" }
                contentItem: Text { text: parent.text; color: root.cAccent
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Item { Layout.fillWidth: true }
        }

        // ── Default macros ──────────────────────────────────────────────
        GridLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8; rowSpacing: 8
            Repeater { model: root.defaultMacros; delegate: macroChip }
        }

        // ── My macros divider (always shown so the operator's own bank
        //     is visible + addable without first toggling Edit) ───────────
        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 8
            Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
            Label { text: qsTr("My macros"); color: root.cMuted; font.pixelSize: 11 }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
        }

        GridLayout {
            visible: !root.collapsed && root.userMacros.length > 0
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8; rowSpacing: 8
            Repeater { model: root.userMacros; delegate: macroChip }
        }

        // + Add — always available; drops into edit mode on the new entry
        Button {
            visible: !root.collapsed
            Layout.fillWidth: true
            implicitHeight: 36
            text: qsTr("+ Add macro")
            onClicked: { CwMacros.addMacro(qsTr("New"), ""); root.editMode = true }
            background: Rectangle { radius: 8; color: "#16202a"; border.color: "#4a5a64" }
            contentItem: Text { text: parent.text; color: root.cMuted; font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }

        // ── My tokens editor (edit mode) — your reusable facts ──────────
        ColumnLayout {
            visible: !root.collapsed && root.editMode
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
                Label { text: qsTr("My tokens"); color: root.cMuted; font.pixelSize: 11 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#243845" }
            }
            Label {
                text: qsTr("Name a token (e.g. ME, PWR, QTH, RIG, ANT) + its value. "
                           + "Use it in any macro as {NAME}.")
                color: root.cMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Repeater {
                model: CwMacros.tokens
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "{"; color: root.cToken; font.family: "Consolas"; font.pixelSize: 13 }
                    TextField {
                        id: tokName
                        Layout.preferredWidth: 96
                        text: modelData.name
                        placeholderText: qsTr("NAME")
                        color: root.cToken; font.family: "Consolas"; font.pixelSize: 12
                        background: Rectangle { radius: 4; color: "#0b141b"
                            border.color: tokName.activeFocus ? root.cAccent : "#2a4a5a" }
                        onEditingFinished: CwMacros.setToken(modelData.index, text, tokVal.text)
                        inputMethodHints: Qt.ImhUppercaseOnly
                    }
                    Text { text: "}"; color: root.cToken; font.family: "Consolas"; font.pixelSize: 13 }
                    Text { text: "="; color: root.cMuted; font.pixelSize: 12 }
                    TextField {
                        id: tokVal
                        Layout.fillWidth: true
                        text: modelData.value
                        placeholderText: qsTr("value — e.g. Rick / 25 watts / Hamilton OH")
                        color: root.cText; font.pixelSize: 12
                        background: Rectangle { radius: 4; color: "#0b141b"
                            border.color: tokVal.activeFocus ? root.cAccent : "#2a4a5a" }
                        onEditingFinished: CwMacros.setToken(modelData.index, tokName.text, text)
                    }
                    TrashCan {
                        tip: qsTr("Delete this token")
                        onConfirmed: CwMacros.removeToken(modelData.index)
                    }
                }
            }
            Button {
                Layout.fillWidth: true; implicitHeight: 30
                text: qsTr("+ Add token")
                onClicked: CwMacros.addToken("", "")
                background: Rectangle { radius: 6; color: "#16202a"; border.color: "#4a5a64" }
                contentItem: Text { text: parent.text; color: root.cMuted; font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        // ── Token palette — click to insert at the cursor ───────────────
        ColumnLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: root.insertTarget
                      ? qsTr("Click a token to insert it at the cursor:")
                      : qsTr("Click into a macro's text or the send line, then click a token:")
                color: root.cMuted; font.pixelSize: 10
            }
            // A Flow is height-for-width, which Qt Quick Layouts does not
            // support: as a direct Layout child its height feeds back into the
            // width the layout gives it, and on a negotiated (docked) width that
            // cycle recurses until the stack blows.  The Item breaks the cycle.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: tokenFlow.implicitHeight
                Flow {
                id: tokenFlow
                width: parent.width
                spacing: 6
                Repeater {
                    model: root.builtinTokens.concat(root.userTokenChips)
                    delegate: Rectangle {
                        width: tokTxt.implicitWidth + 14; height: 22; radius: 5
                        property bool isUser: index >= root.builtinTokens.length
                        color: "#16202a"
                        border.color: isUser ? "#7fd6ef" : "#5b6b76"
                        Text {
                            id: tokTxt; anchors.centerIn: parent
                            text: modelData
                            color: isUser ? root.cAccent : root.cToken
                            font.family: "Consolas"; font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.insertToken(modelData)
                        }
                    }
                }
                // {LOG} — ACTION token (yellow), distinct from the cyan user
                // tokens and amber text tokens.  It is stripped from the keyed
                // CW and instead logs the QSO to a linked SDRLogger+ (Combo
                // Stage B).  Only meaningful inside a macro's text, so it dims +
                // no-ops when the live send line (or nothing) is focused.
                Rectangle {
                    property bool canLog: root.insertTarget && root.insertTarget !== sendField
                    width: logTxt.implicitWidth + 14; height: 22; radius: 5
                    color: "#1e2112"
                    border.color: "#c9a800"
                    opacity: canLog ? 1.0 : 0.4
                    Text {
                        id: logTxt; anchors.centerIn: parent
                        text: "{LOG}"
                        color: "#ffdd33"
                        font.family: "Consolas"; font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: parent.canLog ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (parent.canLog) root.insertToken("{LOG}")
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Action token — not keyed as CW. When this macro is sent, it logs the QSO to a linked SDRLogger+ (Combo). Click into a macro's text field first.")
                    }
                }
                }   // tokenFlow
            }
        }

        // (RX decoder lives in its own "CW Dec" panel — #173 CW-5b — so it can
        // run independently of this keyer/macro console.)
    }   // body ColumnLayout
    }   // bodyScroll ScrollView

        // ── Keyboard send — PINNED (always visible; never scroll-buried) ──
        // Lives OUTSIDE the ScrollView so the free-hand type-and-send line
        // stays on screen at any dock height (Enter sends, Esc/Stop aborts).
        ColumnLayout {
            id: sendBar
            visible: !root.collapsed
            Layout.fillWidth: true
            spacing: 3
            opacity: root.cwActive ? 1.0 : 0.5

            Label {
                text: qsTr("Free-hand text")
                color: "#b5f36a"
                font.pixelSize: 11; font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: sendField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    enabled: root.cwActive
                    placeholderText: qsTr("type, Enter sends — Esc stops")
                    color: root.cText
                    font.family: "Consolas"; font.pixelSize: 14
                    selectByMouse: true
                    background: Rectangle {
                        radius: 4; color: "#17262f"
                        border.color: sendField.activeFocus ? "#c9f59a" : "#8fdc5a"
                        border.width: 1
                    }
                    onAccepted: {
                        if (text.length > 0) { Stream.sendCw(text); text = "" }
                    }
                    onActiveFocusChanged: if (activeFocus) root.insertTarget = sendField
                    Keys.onEscapePressed: CwMacros.stop()
                }
                Button {
                    text: qsTr("Send")
                    Layout.preferredHeight: 36
                    enabled: root.cwActive && sendField.text.length > 0
                    onClicked: { Stream.sendCw(sendField.text); sendField.text = "" }
                    background: Rectangle { radius: 4
                        color: enabled ? "#1c3a44" : "#16202a"
                        border.color: enabled ? root.cAccent : "#2a3a44" }
                    contentItem: Text { text: parent.text
                        color: parent.enabled ? root.cAccent : root.cMuted
                        font.bold: true; font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }   // outer ColumnLayout
}
