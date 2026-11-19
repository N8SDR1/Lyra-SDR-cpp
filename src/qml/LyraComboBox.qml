// Lyra — shared themed drop-down picker.
//
// Drop-in replacement for a stock Controls `ComboBox` that draws its own
// field, chevron and popup at FIXED metrics, so it renders identically on
// any Qt Quick Controls style and never balloons/clips a panel.  All the
// usual ComboBox properties (model / currentIndex / onActivated / textRole /
// displayText) pass through — callers only change `ComboBox {` → `LyraComboBox {`.
//
// Look: compact dark field (#161e28), light text, cyan chevron + cyan focus
// border, dark popup list with a cyan highlight.
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 24
    font.pixelSize: 13

    property string helpTitle: ""
    property string helpText: ""
    ToolTip.visible: false
    HoverHandler { id: helpHov }
    LyraHoverHelp {
        title: control.helpTitle
        text: control.helpText
        hovered: helpHov.hovered && control.helpText.length > 0
                 && !control.popup.visible
    }
    wheelEnabled: true      // mouse-wheel cycles the selection

    // Self-heal: a ComboBox ignores the wheel while it thinks its list is
    // open.  Field reports had a list stuck "open" but not on screen (Mode
    // went dead to clicks and the wheel until Alt-Tab).  A wheel over the
    // field while the list is open closes it, and the log notes it.  Only
    // active while the list is open, so normal wheel stepping is untouched.
    WheelHandler {
        enabled: control.popup.visible
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            console.warn("[ui] drop-down list was open under the wheel — closed"
                         + (control.helpTitle ? " (" + control.helpTitle + ")" : ""))
            control.popup.close()
            event.accepted = true
        }
    }
    // Tuning FM can tighten these; other call sites keep the roomy default.
    property int textLeftPad: 8
    property int textRightPad: 20

    // Selected-value text.  Leaves room on the right for the chevron.
    contentItem: Text {
        leftPadding: control.textLeftPad
        rightPadding: control.textRightPad
        text: control.displayText
        font: control.font
        color: control.enabled ? "#f2f8fc" : "#5a6670"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // Down chevron.
    indicator: Text {
        x: control.width - width - 6
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "▾"
        font.pixelSize: 11
        color: control.enabled ? "#50d0ff" : "#5a6670"
    }

    background: Rectangle {
        implicitWidth: 64
        color: control.enabled ? "#161e28" : "#12171d"
        border.color: control.activeFocus ? "#50d0ff"
                    : control.hovered      ? "#3a5a6a"
                    :                        "#2a3a4a"
        border.width: 1
        radius: 3
    }

    // Dropdown rows.  Full-width, left-aligned, bright text.
    delegate: ItemDelegate {
        width: ListView.view.width
        height: 26
        padding: 8
        contentItem: Text {
            text: control.textRole
                  ? (Array.isArray(control.model)
                        ? modelData[control.textRole]
                        : model[control.textRole])
                  : modelData
            font: control.font
            color: control.currentIndex === index ? "#8fe0ff" : "#eaf2f7"
            verticalAlignment: Text.AlignVCenter
        }
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            color: highlighted ? "#1f4655" : "transparent"
        }
    }

    // Popup grows to at least the field width (and a sane floor) so long
    // labels aren't clipped, sizes its height to the item count, and
    // scrolls when there are more items than fit.
    popup: Popup {
        // Render in a top-level window so the list is NOT clipped by the
        // dock panel's frame — otherwise items past the panel edge are
        // hidden + unselectable (Mode, RX BW, … in a narrow panel).
        popupType: (typeof Prefs !== "undefined" && Prefs.popupsInPanel) ? Popup.Item : Popup.Window
        y: control.height - 1
        width: Math.max(control.width, 130)
        implicitHeight: Math.min(control.count * 26 + 2, 320)
        padding: 1
        contentItem: ListView {
            clip: true
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { active: true }
        }
        background: Rectangle {
            color: "#12181f"
            border.color: "#2a3a4a"
            border.width: 1
            radius: 3
        }
    }
}
