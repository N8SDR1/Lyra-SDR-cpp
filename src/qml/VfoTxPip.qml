// Lyra — the always-on TX assignment pip on a VFO (locked pile-up model):
// red = will transmit on this VFO (bright while keyed), idle = click to
// assign TX here.  Red is fixed for every style so TX always reads as TX;
// the style themes only the idle look.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: pip
    required property bool active     // TX is assigned to this VFO
    required property bool keyed      // transmitting now
    required property string tipText
    property var theme
    signal assign()

    radius: 3
    color: active ? (keyed ? "#ff4136" : "#8a1f1a") : theme.pipIdle
    border.width: 1
    border.color: active ? "#ff6b63" : theme.pipIdleBorder

    Label {
        anchors.centerIn: parent
        text: qsTr("TX")
        color: pip.active ? "#ffffff" : pip.theme.pipIdleText
        font.pixelSize: 9
        font.bold: true
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: pip.assign()
        ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
        ToolTip.delay: 400
        ToolTip.text: pip.tipText
    }
}
