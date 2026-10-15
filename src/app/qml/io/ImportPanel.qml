pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Brulion

// ImportPanel — the "Import" tab of ImportExportView: choose the app (ImportSourcePicker), pick
// files or a folder, check the preview and import (ImportPreview), all through one
// ImportController.
//
// Picking: desktop and Android use QtQuick.Dialogs (asynchronous FileDialog / FolderDialog; on
// Android these are the system's document pickers, giving content:// URLs Qt reads directly); the
// browser uses its own file picker (ImportController.pickInBrowser()). `preset` (route
// parameters) can choose an importer and local files up front: { importer, files: "a|b" }.
ColumnLayout {
    id: panel

    property var preset: ({})
    readonly property alias importer: importerObject

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
        onPickFiles: panel.pickFiles()
        onPickFolder: panel.pickFolder()
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

    FileDialog {
        id: fileDialog
        title: qsTr("Choose the files to import")
        fileMode: FileDialog.OpenFiles
        nameFilters: importerObject.nameFilters
        onAccepted: importerObject.addUrls(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Choose the folder to import")
        onAccepted: importerObject.addFolder(selectedFolder)
    }
}
