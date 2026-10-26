// Lyra — front-facing TX/RX Profile quick-recall dock (#55).
//
// A thin convenience surface: a dropdown to recall a saved profile, a
// Save button (overwrite the active profile), and a "● modified" dot
// that lights when the live chain differs from the active profile.
// Create / rename / delete / set-default + the per-mode auto-recall
// table live in Settings → Profiles (#49) — this dock is recall + Save.
//
// Context property Profiles (ProfileManager) is provided by the host.
// Manual select only; the only automatic recall is the per-mode
// binding configured in Settings.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 44
    implicitWidth: 360
    color: "#101820"
    border.color: "#2a4a5a"

    readonly property color cAccent: "#00e5ff"
    readonly property color cText:   "#cdd9e5"
    readonly property color cMuted:  "#8a9aac"
    readonly property color cOn:     "#ff9a3c"   // "modified" orange

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Label {
            text: qsTr("Profile"); color: root.cAccent; font.bold: true
            HoverHandler { id: profLblHov }
            LyraHoverHelp {
                title: qsTr("Profile")
                text: qsTr("Recall a saved TX/RX snapshot. Create / rename / delete and set the default in Settings → Profiles.")
                hovered: profLblHov.hovered
            }
        }

        LyraComboBox {
            id: profCombo
            helpTitle: qsTr("Profile")
            helpText: qsTr("Recall a saved TX/RX profile. Create / rename / delete and set the default in Settings → Profiles.")
            // Fixed, sensible width — a long profile name elides in the
            // field but shows in full in the open dropdown list, so we
            // don't stretch the combo across the whole dock.
            Layout.fillWidth: false
            Layout.preferredWidth: 170
            implicitHeight: 26
            font.pixelSize: 12
            model: Profiles.names
            displayText: count > 0 ? currentText : qsTr("(no profiles)")
            // loadByUser() == explicit operator pick → fires the companion-app
            // launch (#193).  Plain load() (auto-recall) deliberately does not.
            onActivated: (i) => Profiles.loadByUser(textAt(i))
            function syncToActive() {
                var i = Profiles.names.indexOf(Profiles.activeName)
                currentIndex = i >= 0 ? i
                               : (Profiles.names.length > 0 ? 0 : -1)
            }
            Component.onCompleted: syncToActive()
            Connections {
                target: Profiles
                function onActiveChanged() { profCombo.syncToActive() }
                function onNamesChanged()  { profCombo.syncToActive() }
            }
        }

        Item { Layout.fillWidth: true }   // absorb slack → dot + Save sit right

        // ● modified — orange when the live chain differs from the active
        // profile (i.e. there's something to Save).
        Rectangle {
            width: 12; height: 12; radius: 6
            visible: Profiles.activeName.length > 0
            color: Profiles.modified ? root.cOn : "#26323c"
            border.width: 1
            border.color: Profiles.modified ? "#ffb060" : "#33424e"
            MouseArea { id: dotMa; anchors.fill: parent; hoverEnabled: true }
            LyraHoverHelp {
                title: qsTr("Profile")
                text: Profiles.modified
                    ? qsTr("Modified — live settings differ from “%1”. Save to keep them.")
                        .arg(Profiles.activeName)
                    : qsTr("Saved — live settings match the active profile.")
                hovered: dotMa.containsMouse
            }
        }

        // Save — always available.  Opens the native "Save Profile"
        // dialog (ProfileUi): overwrite the active profile, or name a new
        // one + optionally bind it to a mode family — so a profile for a
        // just-set mode can be built here without opening Settings.  It's
        // a NATIVE dialog (not a QML popup) because a top-level QML popup
        // hosted from this QQuickWidget dock can't receive keyboard focus
        // for the name field (same reason AudioPanel uses native dialogs).
        Button {
            text: qsTr("Save")
            implicitHeight: 26; implicitWidth: 56
            font.pixelSize: 12
            onClicked: ProfileUi.openSaveDialog()
            HoverHandler { id: saveHov }
            LyraHoverHelp {
                title: qsTr("Save")
                text: qsTr("Save the current live settings — overwrite the active profile, or create a new one (and optionally set which mode family auto-recalls it).")
                hovered: saveHov.hovered
            }
        }
    }
}
