// Tiny binder: when `hovered` the parked Help card shows title + text.
// Lives as a child of any control (slider, chip, button).  Width/height
// 0 so it never affects layout.  HelpHint gates on Prefs.tooltipsEnabled.
import QtQuick

Item {
    id: root
    width: 0
    height: 0
    property string title: ""
    property string text: ""
    property bool hovered: false

    function apply() {
        if (typeof HelpHint === "undefined" || HelpHint === null)
            return
        if (hovered && text.length > 0)
            HelpHint.setHoverHelp(title, text)
        else
            HelpHint.clearHoverHelp()
    }

    onHoveredChanged: apply()
    onTextChanged: if (hovered) apply()
    onTitleChanged: if (hovered) apply()
    Component.onDestruction: {
        if (hovered && typeof HelpHint !== "undefined" && HelpHint !== null)
            HelpHint.clearHoverHelp()
    }
}
