pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Layouts
import Brulion

// ContentExportDialog — "Eksportuj stronę": one page as Markdown (the file as it is) or as a
// self-contained HTML document (pictures included), copied to the clipboard or saved to a file
// (desktop and Android: the system's save dialog; in the browser only the clipboard). Larger
// exports (folders, the whole notebook, other apps' formats) live in Import and export.
//
//   ContentExportDialog { id: exportDialog; tools: tools }
//   exportDialog.openFor(page.path)
BDialog {
    id: dialog

    property PageTools tools
    property string path
    property string format: "markdown"

    function openFor(pagePath) {
        path = pagePath
        open()
    }

    title: qsTr("Export page")
    subtitle: Workspace.pageTitle(path)
    iconName: "share"
    showFooter: false
    preferredWidth: Theme.dialogWidthSm

    BText {
        Layout.fillWidth: true
        text: qsTr("Format")
        variant: "label"
    }
    BSegmented {
        Layout.fillWidth: true
        fill: true
        model: [{ value: "markdown", text: qsTr("Markdown"), icon: "file-text" },
                { value: "html", text: qsTr("HTML"), icon: "file-output" }]
        currentValue: dialog.format
        accessibleName: qsTr("Format")
        onActivated: (value) => dialog.format = value
    }
    BText {
        Layout.fillWidth: true
        text: dialog.format === "html"
              ? qsTr("A web page that opens in any browser and can be printed or sent, with the pictures included.")
              : qsTr("The note exactly as it is stored — readable by Obsidian, Logseq and any text editor.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space3
        spacing: Theme.space2
        BButton {
            Layout.fillWidth: true
            variant: AppController.isWasm ? "primary" : "secondary"
            iconName: "copy"
            text: qsTr("Copy to clipboard")
            onClicked: {
                if (dialog.tools.copyExport(dialog.path, dialog.format)) {
                    AppController.toast(qsTr("Copied to the clipboard"), "success")
                    dialog.close()
                } else {
                    AppController.toast(qsTr("The page could not be read."), "error")
                }
            }
        }
        BButton {
            visible: !AppController.isWasm
            Layout.fillWidth: true
            variant: "primary"
            iconName: "download"
            text: qsTr("Save as file…")
            onClicked: saveDialog.open()
        }
        BButton {
            Layout.fillWidth: true
            variant: "ghost"
            iconName: "import"
            text: qsTr("More export options…")
            onClicked: {
                dialog.close()
                AppController.navigate({ view: "importExport" })
            }
        }
    }

    FilePicker {
        id: saveDialog
        objectName: "pageExportSaveDialog"
        fileMode: FilePicker.SaveFile
        title: qsTr("Save the page as")
        dark: Theme.dark
        colors: Theme.colors
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + dialog.tools.exportFileName(dialog.path, dialog.format)
        nameFilters: dialog.format === "html" ? [qsTr("HTML documents (*.html)")] : [qsTr("Markdown files (*.md)")]
        defaultSuffix: dialog.format === "html" ? "html" : "md"
        onAccepted: {
            if (dialog.tools.saveExport(dialog.path, dialog.format, selectedFile)) {
                AppController.toast(qsTr("Page exported"), "success")
                dialog.close()
            } else {
                AppController.toast(qsTr("The file could not be written."), "error")
            }
        }
    }
}
