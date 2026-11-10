pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ImportSourcePicker — step 1 of an import: the apps Brulion imports from as tiles; choosing one
// shows how to get the notes out of it and what to pick (a file / a folder). Without a choice
// Brulion recognises the files by itself.
//
//   ImportSourcePicker { importer: importController; phone: true; onPickFiles: …; onPickFolder: … }
//
// `phone`: also the chosen app's steps on a phone (phoneHowTo) and how the system's picker works.
// Microsoft To Do adds its sign-in (MsTodoSignIn) above the file buttons.
ColumnLayout {
    id: picker

    required property ImportController importer
    /// An importer is chosen (importer.chosenImporter has its fields).
    readonly property bool hasCurrent: importer.chosenImporter.id !== undefined
    readonly property string pick: hasCurrent ? "" + importer.chosenImporter.pick : "both"
    readonly property bool collapsed: Theme.compact && hasCurrent
    readonly property string currentIcon: hasCurrent ? "" + importer.chosenImporter.icon : ""
    readonly property string currentName: hasCurrent ? "" + importer.chosenImporter.name : ""
    readonly property string currentPhoneHowTo: hasCurrent ? "" + importer.chosenImporter.phoneHowTo : ""
    /// The chosen importer's file types ("*.enex").
    readonly property list<string> fileTypes: {
        if (!picker.hasCurrent)
            return []
        return picker.importer.chosenImporter.fileTypes
    }
    property bool phone: false

    signal pickFiles()
    signal pickFolder()

    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    spacing: Theme.space4

    BText {
        Layout.fillWidth: true
        text: qsTr("Where are your notes now?")
        variant: "title"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }

    // Phones: once an app is chosen, the tiles step aside for its instructions.
    RowLayout {
        Layout.fillWidth: true
        visible: picker.collapsed
        spacing: Theme.space2
        BIcon { name: picker.currentIcon; size: Theme.iconSize; color: Theme.link }
        BText {
            Layout.fillWidth: true
            text: picker.currentName
            variant: "label"
            elide: Text.ElideRight
        }
        BButton {
            text: qsTr("Change")
            size: "small"
            onClicked: picker.importer.importerId = ""
        }
    }

    GridLayout {
        id: grid
        visible: !picker.collapsed
        Layout.fillWidth: true
        columns: width >= 760 ? 4 : width >= 470 ? 3 : 2
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2

        Repeater {
            model: picker.importer.importers
            delegate: T.AbstractButton {
                id: tile
                // (A row's "id" cannot be a property name, "icon" is the button's own.)
                required property var modelData
                required property string name
                readonly property string importerKey: AppController.routeText(modelData, "id")
                readonly property string iconName: AppController.routeText(modelData, "icon")
                readonly property bool chosen: picker.importer.importerId === tile.importerKey
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: Math.max(Theme.touchUi ? 52 : 44, implicitContentHeight + topPadding + bottomPadding)
                leftPadding: Theme.space3
                rightPadding: Theme.space2
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                checkable: true
                checked: tile.chosen
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.name
                Accessible.checked: tile.chosen
                onClicked: picker.importer.importerId = tile.chosen ? "" : tile.importerKey

                background: Rectangle {
                    radius: Theme.radius
                    color: tile.chosen ? Theme.bgSelected : tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : Theme.bgElevated
                    border.width: tile.chosen ? 1.5 : 1
                    border.color: tile.chosen ? Theme.focusRing : Theme.border
                    BFocusRing { visible: tile.visualFocus; targetRadius: Theme.radius }
                }
                contentItem: RowLayout {
                    spacing: Theme.space2
                    BIcon {
                        name: tile.iconName
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: tile.name
                        variant: tile.chosen ? "label" : "body"
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    IoCard {
        iconName: picker.hasCurrent ? picker.currentIcon : "sparkles"
        iconColor: picker.hasCurrent ? Theme.link : Theme.fgMuted
        title: picker.hasCurrent ? qsTr("From %1").arg(picker.currentName) : qsTr("Not sure where to start?")
        subtitle: picker.hasCurrent ? "" + picker.importer.chosenImporter.howTo
                  : qsTr("Choose the exported files or the folder – Brulion recognises Obsidian, Logseq, Notion, Evernote and the others by itself. Nothing is written before you see what will be imported.")

        // On the phone: where that app's notes are and what to pick.
        RowLayout {
            objectName: "importPhoneHowTo"
            Layout.fillWidth: true
            visible: picker.phone && picker.currentPhoneHowTo !== ""
            spacing: Theme.space2
            BIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                name: "smartphone"
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
            BText {
                Layout.fillWidth: true
                text: picker.currentPhoneHowTo
                wrapMode: Text.Wrap
            }
        }

        // Microsoft To Do: sign in and download, or a file from Graph Explorer.
        Loader {
            Layout.fillWidth: true
            active: picker.hasCurrent && picker.importer.chosenImporter.id === "mstodo"
            visible: active
            sourceComponent: MsTodoSignIn { importer: picker.importer }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "importChooseFiles"
                visible: picker.pick !== "folder"
                text: picker.hasCurrent && picker.pick === "files" && picker.fileTypes.length === 1
                      ? qsTr("Choose the %1 file…").arg(Js.replace(picker.fileTypes[0], "*", ""))
                      : qsTr("Choose files…")
                iconName: "file-input"
                variant: "primary"
                onClicked: picker.pickFiles()
            }
            BButton {
                objectName: "importChooseFolder"
                visible: picker.pick !== "files" && !AppController.isWasm
                text: qsTr("Choose a folder…")
                iconName: "folder-open"
                variant: picker.pick === "folder" ? "primary" : "secondary"
                onClicked: picker.pickFolder()
            }
        }
        Hint {
            visible: AppController.isWasm && picker.pick !== "files"
            text: qsTr("In the browser choose a ZIP of the folder.")
        }
        // The system's picker, briefly: where the places are, what Android refuses. (Its menu in
        // words: Android's fonts have no ☰, U+2630.)
        Hint {
            visible: picker.phone
            text: picker.pick === "files"
                  ? qsTr("The system's picker opens: its menu (three lines in its top left corner) switches between Downloads, the phone's storage, Google Drive and other apps; a long press selects several files.")
                  : qsTr("The system's picker opens: its menu (three lines in its top left corner) switches between Downloads, the phone's storage, Google Drive and other apps. Android does not let apps take the whole Downloads folder or the top of the storage – choose a folder inside it, or type its path below.")
        }
    }
}
