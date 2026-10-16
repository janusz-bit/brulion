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
ColumnLayout {
    id: picker

    required property ImportController importer
    readonly property var current: {
        const list = importer.importers
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === importer.importerId)
                return list[i]
        return null
    }
    readonly property string pick: current ? current.pick : "both"
    readonly property bool collapsed: Theme.compact && current !== null
    property bool phone: false

    signal pickFiles()
    signal pickFolder()

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
        BIcon { name: picker.current ? picker.current.icon : ""; size: Theme.iconSize; color: Theme.link }
        BText {
            Layout.fillWidth: true
            text: picker.current ? picker.current.name : ""
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
                required property var modelData
                readonly property bool chosen: picker.importer.importerId === modelData.id
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: Math.max(Theme.touchUi ? 52 : 44, implicitContentHeight + topPadding + bottomPadding)
                leftPadding: Theme.space3
                rightPadding: Theme.space2
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                checkable: true
                checked: chosen
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.checked: chosen
                onClicked: picker.importer.importerId = chosen ? "" : modelData.id

                background: Rectangle {
                    radius: Theme.radius
                    color: tile.chosen ? Theme.bgSelected : tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : Theme.bgElevated
                    border.width: tile.chosen ? 1.5 : 1
                    border.color: tile.chosen ? Theme.focusRing : Theme.border
                    BFocusRing { visible: tile.visualFocus; targetRadius: Theme.radius }
                }
                contentItem: RowLayout {
                    id: row
                    spacing: Theme.space2
                    BIcon {
                        name: tile.modelData.icon
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: tile.modelData.name
                        variant: tile.chosen ? "label" : "body"
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    IoCard {
        iconName: picker.current ? picker.current.icon : "sparkles"
        iconColor: picker.current ? Theme.link : Theme.fgMuted
        title: picker.current ? qsTr("From %1").arg(picker.current.name) : qsTr("Not sure where to start?")
        subtitle: picker.current ? picker.current.howTo
                  : qsTr("Choose the exported files or the folder – Brulion recognises Obsidian, Logseq, Notion, Evernote and the others by itself. Nothing is written before you see what will be imported.")

        // On the phone: where that app's notes are and what to pick.
        RowLayout {
            objectName: "importPhoneHowTo"
            Layout.fillWidth: true
            visible: picker.phone && picker.current !== null && picker.current.phoneHowTo !== ""
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
                text: picker.current ? picker.current.phoneHowTo : ""
                wrapMode: Text.Wrap
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "importChooseFiles"
                visible: picker.pick !== "folder"
                text: picker.current && picker.pick === "files" && picker.current.fileTypes.length === 1
                      ? qsTr("Choose the %1 file…").arg(picker.current.fileTypes[0].replace("*", ""))
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
        BText {
            Layout.fillWidth: true
            visible: AppController.isWasm && picker.pick !== "files"
            text: qsTr("In the browser choose a ZIP of the folder.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        // The system's picker, briefly: where the places are, what Android refuses.
        BText {
            Layout.fillWidth: true
            visible: picker.phone
            text: picker.pick === "files"
                  ? qsTr("The system's picker opens: ☰ switches between Downloads, the phone's storage, Google Drive and other apps; a long press selects several files.")
                  : qsTr("The system's picker opens: ☰ switches between Downloads, the phone's storage, Google Drive and other apps. Android does not let apps take the whole Downloads folder or the top of the storage – choose a folder inside it, or type its path below.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }
}
