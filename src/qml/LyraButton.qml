// Lyra — shared themed push button.
//
// A drop-in replacement for a stock Controls `Button` that draws its OWN
// background + label, so it renders identically regardless of the global
// Qt Quick Controls style.  The macOS native style draws an unstyled Button
// with dark text on Lyra's dark panels, which is nearly invisible.  All the
// usual Button properties (text / checkable / checked / onClicked /
// onToggled / font / enabled / ToolTip) pass straight through — callers
// only change `Button {` → `LyraButton {`.
//
// Look matches the hand-styled SPLIT/SUB buttons: dark fill, slate border,
// light label; checked = cyan border + label (Lyra's accent #00e5ff).
import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitWidth: Math.max(40, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 24
    leftPadding: 6; rightPadding: 6
    topPadding: 2; bottomPadding: 2
    hoverEnabled: true

    background: Rectangle {
        radius: 4
        color: control.down ? "#0c2a32"
             : control.checked ? "#10323a"
             : control.hovered ? "#1e2a38"
             : "#161e28"
        border.width: control.checked ? 2 : 1
        border.color: control.checked ? "#00e5ff"
                    : control.hovered ? "#6a8aa8"
                    : "#4a6078"
        opacity: control.enabled ? 1.0 : 0.45
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? "#00e5ff" : "#cdd9e5"
        opacity: control.enabled ? 1.0 : 0.45
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
