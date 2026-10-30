pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ImportPanel — the "Import" tab of ImportExportView: choose the app (ImportSourcePicker), pick
// files or a folder, check the preview and import (ImportPreview), all through one
// ImportController.
//
// Picking: desktop and Android use the system's file picker (FilePicker, asynchronous; on Android
// the system's document pickers: several files at once, or a folder as a document tree —
// content:// URLs the controller reads through Android's document provider); the browser uses its
// own file picker (ImportController.pickInBrowser(), several files at once as well). On Android the file picker
// gets no type filter: providers name types differently (a Notion ZIP in Drive, a .md file in
// Downloads), and a filtered picker greys such files out — the files are recognised by their
// content instead. ImportPathCard reads a typed path (all platforms but the browser). `phone`
// (Android, or the phone emulation of --touch) adds the phone's steps per app and picker tips.
// `preset` (route parameters) can choose an importer and files up front: { importer, files: "a|b" }
// (paths or file:// / content:// URLs).
ColumnLayout {
    id: panel

    property var preset: ({})
    readonly property alias importer: importerObject
    readonly property bool phone: AppController.isAndroid || DevTools.touch

    function pickFiles() {
        if (AppController.isWasm)
            importerObject.pickInBrowser()
        else
            fileDialog.open()
    }
    function pickFolder() {
        if (!AppController.isWasm)
            folderDialog.open()
    }
    function applyPreset() {
        if (!preset)
            return
        if (preset.importer)
            importerObject.importerId = preset.importer
        if (preset.files)
            importerObject.addPaths(String(preset.files).split("|"))
    }

    onPresetChanged: applyPreset()
    Component.onCompleted: applyPreset()

    spacing: Theme.space5

    ImportController {
        id: importerObject
        objectName: "importController"
        workspace: Workspace
        onImported: (message) => AppController.toast(message, "success")
    }

    BText {
        Layout.fillWidth: true
        visible: importerObject.state === "empty"
        text: qsTr("Bring your notes from another app. Brulion converts them to plain Markdown pages in this notebook; nothing in the other app changes.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    ImportSourcePicker {
        Layout.fillWidth: true
        visible: importerObject.state === "empty"
        importer: importerObject
        phone: panel.phone
        onPickFiles: panel.pickFiles()
        onPickFolder: panel.pickFolder()
    }

    ImportPathCard {
        Layout.fillWidth: true
        visible: importerObject.state === "empty" && !AppController.isWasm
        importer: importerObject
    }

    ImportPreview {
        Layout.fillWidth: true
        visible: importerObject.state !== "empty"
        importer: importerObject
        onPickAgain: {
            const chosen = importerObject.importerId
            importerObject.clear()
            importerObject.importerId = chosen
        }
    }

    FilePicker {
        id: fileDialog
        objectName: "importFilesDialog"
        title: qsTr("Choose the files to import")
        fileMode: FilePicker.OpenFiles
        nameFilters: AppController.isAndroid ? [] : importerObject.nameFilters
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: importerObject.addUrls(selectedFiles)
    }
    FilePicker {
        id: folderDialog
        objectName: "importFolderDialog"
        title: qsTr("Choose the folder to import")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: importerObject.addFolder(selectedFolder)
    }
}
