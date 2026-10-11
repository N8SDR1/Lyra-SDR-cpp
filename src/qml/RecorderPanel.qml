// Lyra — Session Recorder panel (#201, RECORDER_DESIGN.md).
//
// Opt-in dockable tool window (View → Recorder; NOT in the default shipped
// layout).  Compact operate surface: a small Record/Stop button + live
// elapsed timer, a snapshot on/off toggle with its rate, and a Settings
// shortcut.  The heavy config (path, split, storage cap) and the Sessions
// list live on Settings → Recording.  The always-visible "● REC" status-bar
// chip (MainWindow) is the independent "don't forget it's running" light.
//
// Bindings: Recorder (recording / elapsedMs / snapshotsOn / snapshotsPerMin /
// toggle() / requestSettings()) — exposed as a context property by
// MainWindow::makeQuick.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitWidth: 300
    implicitHeight: 138
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cRed:    "#e67878"

    readonly property bool recording: Recorder ? Recorder.recording : false

    function hhmmss(ms) {
        var s = Math.floor((ms || 0) / 1000)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var ss = s % 60
        function p(n) { return (n < 10 ? "0" : "") + n }
        return p(h) + ":" + p(m) + ":" + p(ss)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ── Title + status dot + Settings shortcut ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Session recorder"; color: root.cText
                   font.pixelSize: 14; font.bold: true }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.recording ? root.cRed : "#33414d"
                SequentialAnimation on opacity {
                    running: root.recording; loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.25; duration: 700 }
                    NumberAnimation { from: 0.25; to: 1.0; duration: 700 }
                }
            }
            LyraButton {
                text: "⚙"
                font.pixelSize: 15
                implicitWidth: 26; implicitHeight: 26
                ToolTip.visible: hovered
                ToolTip.text: "Recording settings — folder, limits, sessions"
                onClicked: if (Recorder) Recorder.requestSettings()
            }
        }

        // ── Small Record/Stop button + live timer ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Button {
                id: recBtn
                implicitHeight: 28
                padding: 6
                text: root.recording ? "■ Stop" : "● Rec"
                enabled: Recorder !== null
                onClicked: if (Recorder) Recorder.toggle()
                contentItem: Text {
                    text: recBtn.text; color: "#04121a"
                    font.pixelSize: 13; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 66
                    radius: 6
                    color: root.recording ? root.cRed : root.cAccent
                    opacity: recBtn.down ? 0.8 : 1.0
                }
            }
            Text {
                text: root.hhmmss(Recorder ? Recorder.elapsedMs : 0)
                color: root.recording ? root.cRed : root.cMuted
                font.pixelSize: 22; font.bold: true
                font.family: "Consolas, Menlo, monospace"
            }
            Item { Layout.fillWidth: true }
        }

        // ── Snapshots: on/off + rate ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            CheckBox {
                id: snapChk
                text: "Snapshots"
                checked: Recorder ? Recorder.snapshotsOn : true
                onToggled: if (Recorder) Recorder.snapshotsOn = checked
                contentItem: Text {
                    text: snapChk.text; color: root.cText; font.pixelSize: 12
                    leftPadding: snapChk.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillWidth: true }
            SpinBox {
                id: rateSpin
                from: 1; to: 30
                value: Recorder ? Recorder.snapshotsPerMin : 5
                enabled: snapChk.checked
                implicitWidth: 58
                implicitHeight: 26
                font.pixelSize: 12
                onValueModified: if (Recorder) Recorder.snapshotsPerMin = value
            }
            Text { text: "/ min"; color: root.cMuted; font.pixelSize: 12 }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Records RX audio + snapshots to a session folder. ⚙ for path, "
                  + "auto-split, storage cap and the sessions list."
            color: root.cMuted; font.pixelSize: 11
        }

        Item { Layout.fillHeight: true }
    }
}
