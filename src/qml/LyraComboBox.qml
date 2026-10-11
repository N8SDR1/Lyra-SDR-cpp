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
    wheelEnabled: true      // mouse-wheel cycles the selection
    // Tuning FM can tighten these; other call sites keep the roomy default.
    property int textLeftPad: 8
    property int textRightPad: 20
    // The text pads above are the whole horizontal inset.  Zero the style's
    // own control padding (5 px a side on macOS) so it isn't stacked on top
    // of them — that double inset elided short labels ("ADC1" → "A…").
    leftPadding: 0
    rightPadding: 0

    // Optional theme (VFO styles colour their Step/Mode pickers).  Defaults
    // are the standard Lyra look, so every other combo is unchanged.
    property color fieldColor: "#161e28"
    property color fieldBorderColor: "#2a3a4a"
    property color textColor: "#f2f8fc"
    property color accentColor: "#50d0ff"

    // Selected-value text.  Leaves room on the right for the chevron.
    contentItem: Text {
        leftPadding: control.textLeftPad
        rightPadding: control.textRightPad
        text: control.displayText
        font: control.font
        color: control.enabled ? control.textColor : "#5a6670"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // Down chevron.
    indicator: Text {
        x: control.width - width - 6
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "▾"
        font.pixelSize: 11
        color: control.enabled ? control.accentColor : "#5a6670"
    }

    background: Rectangle {
        implicitWidth: 64
        color: control.enabled ? control.fieldColor : "#12171d"
        border.color: control.activeFocus ? control.accentColor
                    : control.hovered      ? "#3a5a6a"
                    :                        control.fieldBorderColor
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

    // Widest dropdown row: item text + the delegate's 8 px side padding.
    // Measured each time the popup opens (models change at runtime).
    property real _popupRowWidth: 0
    TextMetrics { id: rowMetrics; font: control.font }
    function _measureRows() {
        var w = 0
        for (var i = 0; i < control.count; ++i) {
            rowMetrics.text = control.textAt(i)
            w = Math.max(w, rowMetrics.advanceWidth)
        }
        return Math.ceil(w) + 16
    }

    // Popup is as wide as the field, a sane floor, or the widest row
    // (plus the scroll bar when the list scrolls), whichever is largest —
    // the rows don't elide, so a narrower list would cut labels off.  It
    // sizes its height to the item count and scrolls past 320 px.
    popup: Popup {
        // Render in a top-level window so the list is NOT clipped by the
        // dock panel's frame — otherwise items past the panel edge are
        // hidden + unselectable (Mode, RX BW, … in a narrow panel).
        popupType: Popup.Window
        y: control.height - 1
        width: Math.max(control.width, 130,
                        control._popupRowWidth + leftPadding + rightPadding
                        + (control.count * 26 + 2 > 320 ? popupBar.width : 0))
        implicitHeight: Math.min(control.count * 26 + 2, 320)
        padding: 1
        onAboutToShow: control._popupRowWidth = control._measureRows()
        contentItem: ListView {
            clip: true
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { id: popupBar; active: true }
        }
        background: Rectangle {
            color: "#12181f"
            border.color: "#2a3a4a"
            border.width: 1
            radius: 3
        }
    }
}
