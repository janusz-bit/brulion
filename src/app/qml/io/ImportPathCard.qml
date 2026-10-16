pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ImportPathCard — the way around the system's picker: a file or folder typed as a path on this
// device ("/storage/emulated/0/Download/Obsidian"; file:// and content:// URLs work too) and read
// like a picked one. On Android a path outside Brulion's own storage needs "All files access"
// (the same permission a notebook on the phone's storage asks for): the card says so and opens
// the system setting; the field keeps its text while the user is away.
//
//   ImportPathCard { importer: importController }
IoCard {
    id: card

    required property ImportController importer
    readonly property string path: field.text.trim()
    /// Android: the path lies outside the app's storage and "All files access" is missing.
    readonly property bool needsAccess: card.path !== "" && card.path.indexOf("content:") !== 0
                                        && AppController.needsStoragePermission(card.path)
                                        && !AppController.permissions.allFilesAccess

    function submit() {
        if (card.path !== "")
            card.importer.addPaths([card.path])
    }

    iconName: "folder-open"
    title: qsTr("Or type a path")
    subtitle: AppController.isAndroid
              ? qsTr("A file or folder on this phone, e.g. /storage/emulated/0/Download/Obsidian.")
              : qsTr("A file or folder on this computer.")

    BTextField {
        id: field
        objectName: "importPathField"
        Layout.fillWidth: true
        placeholderText: AppController.isAndroid ? "/storage/emulated/0/Download/…" : qsTr("Path to a file or folder")
        accessibleName: qsTr("Path to import")
        leadingIcon: "folder"
        clearable: true
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
        onAccepted: card.submit()
    }

    BCard {
        Layout.fillWidth: true
        visible: card.needsAccess
        tone: "warning"
        padding: Theme.space3
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.space2
            BText {
                Layout.fillWidth: true
                text: qsTr("Reading files outside Brulion's own storage needs “All files access”. Allow it in the system settings and come back – or use “Choose files…” / “Choose a folder…” above, which need no permission.")
                wrapMode: Text.Wrap
            }
            BButton {
                text: qsTr("Open system settings")
                iconName: "settings"
                size: "small"
                onClicked: AppController.requestPermission("allFilesAccess")
            }
        }
    }

    BButton {
        objectName: "importPathButton"
        text: qsTr("Import from this path")
        iconName: "arrow-right"
        enabled: card.path !== ""
        onClicked: card.submit()
    }
}
