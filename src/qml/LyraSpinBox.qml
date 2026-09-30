// Lyra — shared themed numeric spinner.
//
// Drop-in replacement for a stock Controls `SpinBox`, drawn at FIXED metrics
// so it renders identically on any Qt Quick Controls style and can't balloon.
// Editable by default: click the value to type an exact number, use the
// stacked chevrons, or mouse-wheel.  All the usual SpinBox properties
// (from / to / stepSize / value / textFromValue / valueFromText /
// onValueModified) pass through — callers only change `SpinBox {` →
// `LyraSpinBox {`.
//
// Look: compact dark field (#161e28), light centred value, cyan stacked
// chevrons on the right, cyan focus border.
import QtQuick
import QtQuick.Controls

SpinBox {
    id: control
    editable: true
    implicitHeight: 24
    font.pixelSize: 13
    // Reserve the right-hand column for the stacked chevrons.
    rightPadding: 18
    // Default is centred; Tuning FM Dev left-aligns so the field can shrink.
    property int valueHAlign: Qt.AlignHCenter
    property int textLeftPad: 6

    // A few call sites carry their OWN WheelHandler with custom stepping
    // (e.g. RIT/XIT = 10 Hz wheel / 1 Hz arrows); those set this false so the
    // component's default stepSize wheel below doesn't fight theirs.
    property bool wheelStepping: true

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? "#f2f8fc" : "#5a6670"
        selectionColor: "#50d0ff"
        selectedTextColor: "#0a0e12"
        horizontalAlignment: control.valueHAlign
        verticalAlignment: Qt.AlignVCenter
        leftPadding: control.textLeftPad
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    // Mouse-wheel: scroll up/down to step.  Uses the same plain WheelHandler
    // idiom the panels' sliders use (which work), stepping value directly and
    // emitting valueModified so each caller's onValueModified fires.
    WheelHandler {
        enabled: control.wheelStepping
        onWheel: (ev) => {
            var d = ev.angleDelta.y > 0 ? control.stepSize : -control.stepSize
            var v = Math.max(control.from, Math.min(control.to, control.value + d))
            if (v !== control.value) {
                control.value = v
                control.valueModified()
            }
        }
    }

    up.indicator: Rectangle {
        x: control.width - width
        y: 0
        implicitWidth: 16
        height: control.height / 2
        color: control.up.pressed ? "#1f4655" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "▴"; font.pixelSize: 10
            color: control.enabled ? "#50d0ff" : "#5a6670"
        }
    }
    down.indicator: Rectangle {
        x: control.width - width
        y: control.height / 2
        implicitWidth: 16
        height: control.height / 2
        color: control.down.pressed ? "#1f4655" : "transparent"
        Text {
            anchors.centerIn: parent
            text: "▾"; font.pixelSize: 10
            color: control.enabled ? "#50d0ff" : "#5a6670"
        }
    }

    background: Rectangle {
        implicitWidth: 60
        color: control.enabled ? "#161e28" : "#12171d"
        border.color: control.activeFocus ? "#50d0ff"
                    : control.hovered      ? "#3a5a6a"
                    :                        "#2a3a4a"
        border.width: 1
        radius: 3
    }
}
