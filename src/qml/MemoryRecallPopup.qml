// Scrollable memory-recall list.  Stock Menu clips once the list is
// taller than the remaining screen (Settings overlay + Band chip).
import QtQuick
import QtQuick.Controls

Popup {
    id: root
    property var memories: []
    signal recalled(int index)

    y: parent ? parent.height : 0
    width: Math.max(280, parent ? parent.width : 280)
    padding: 4
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                 | Popup.CloseOnPressOutsideParent
    popupType: Popup.Window

    implicitHeight: Math.min(list.contentHeight + padding * 2, 380)

    background: Rectangle {
        color: "#12181f"
        border.color: "#2a3a4a"
        border.width: 1
        radius: 4
    }

    contentItem: ListView {
        id: list
        clip: true
        implicitHeight: Math.min(contentHeight, 360)
        model: root.memories.length > 0 ? root.memories : [null]
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            active: true
        }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 28
            enabled: root.memories.length > 0
            text: root.memories.length === 0
                  ? qsTr("(no presets — right-click to save)")
                  : ((modelData.name.length > 0
                      ? modelData.name : modelData.freqMHz)
                     + "   " + modelData.freqMHz + " " + modelData.mode)
            font.pixelSize: 13
            contentItem: Text {
                text: parent.text
                font: parent.font
                color: parent.enabled ? "#eaf2f7" : "#5a6670"
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            background: Rectangle {
                color: parent.hovered && parent.enabled ? "#1f4655" : "transparent"
                radius: 2
            }
            onClicked: {
                if (root.memories.length === 0) return
                root.recalled(index)
                root.close()
            }
        }
    }
}
