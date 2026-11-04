pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentExportDialog — "Eksportuj stronę" (the page's menu, the palette's "Eksportuj stronę…" and
// "Drukuj stronę…"): one page in a format that makes sense for one page — Markdown (the file as
// it is), Markdown with its attachments (ZIP), one HTML file with the pictures inside, PDF, Word
// (DOCX: both made by Brulion itself, also on the phone) or Pandoc Markdown — copied to the
// clipboard (Markdown, HTML), saved (desktop and Android: the system's save dialog; Android also
// "Zapisz w Pobranych"; the browser: its save picker or a download) or printed (desktop: the
// system's print dialog; Android: the system's print service; the browser: its print dialog).
// The file is made by an ExportController of the page (scope "page": the attachments it links
// to come along) in the background; folders, the whole notebook and the other formats are in
// Import and export ("Więcej opcji eksportu…").
//
//   ContentExportDialog { id: exportDialog; tools: tools }
//   exportDialog.openFor(page.path)          // the formats
//   exportDialog.openFor(page.path, true)    // and print at once
BDialog {
    id: dialog

    property PageTools tools
    property string path
    property string format: "markdown-file"
    /// What happens once the file is ready: "save" | "downloads" | "browser".
    property string pending: ""

    readonly property var formats: [
        { id: "markdown-file", name: qsTr("Markdown"), icon: "file-text",
          description: qsTr("The note exactly as it is stored — readable by Obsidian, Logseq and any text editor.") },
        { id: "markdown-zip", name: qsTr("Markdown with attachments"), icon: "files",
          description: qsTr("A ZIP archive of the note with the pictures and files it links to.") },
        { id: "html-single", name: qsTr("HTML"), icon: "file-output",
          description: qsTr("A web page that opens in any browser and can be printed or sent, with the pictures included.") },
        { id: "pdf", name: qsTr("PDF"), icon: "printer",
          description: qsTr("Pages ready to print or send, the same on every device – no other program needed.") },
        { id: "docx", name: qsTr("Word (DOCX)"), icon: "type",
          description: qsTr("A document to edit in Word, LibreOffice Writer or Google Docs.") },
        { id: "pandoc", name: qsTr("Pandoc Markdown"), icon: "book-open",
          description: qsTr("A ZIP archive with one Markdown document for Pandoc, to turn into EPUB or other formats.") }
    ]
    readonly property var current: formats.find(f => f.id === format) || formats[0]
    readonly property bool copyable: format === "markdown-file" || format === "html-single"

    function openFor(pagePath, andPrint) {
        pending = ""
        path = pagePath
        exporter.discard()
        open()
        if (andPrint)
            dialog.startPrint()
    }
    function produce(action) {
        pending = action
        exporter.prepare()
    }
    function startPrint() {
        pending = ""
        exporter.print()
    }
    function finishPending() {
        const action = pending
        pending = ""
        if (action === "browser") {
            exporter.saveInBrowser()
        } else if (action === "downloads") {
            exporter.saveToExportFolder()
        } else if (action === "save") {
            // Assigned now: the dialog (Android: the system's "Save as") reads the name on open().
            saveDialog.selectedFile = exporter.suggestedSaveFile
            saveDialog.open()
        }
    }

    title: qsTr("Export page")
    subtitle: Workspace.pageTitle(path)
    iconName: "share"
    showFooter: false
    preferredWidth: Theme.dialogWidth

    onClosed: {
        exporter.cancel()
        pending = ""
    }

    ExportController {
        id: exporter
        objectName: "pageExportController"
        workspace: Workspace
        scope: "page"
        page: dialog.path
        format: dialog.format
        webPictures: AppSettings.exportWebPictures
        onStateChanged: {
            if (state === "ready" && dialog.pending !== "")
                dialog.finishPending()
            else if (state === "error")
                AppController.toast(errorText, "error")
        }
        onSaved: (message) => {
            AppController.toast(message, "success")
            dialog.close()
        }
        onPrinted: (message) => {
            if (message !== "")
                AppController.toast(message, "success")
            dialog.close()
        }
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Format")
        variant: "label"
    }
    GridLayout {
        id: grid
        objectName: "pageExportFormats"
        Layout.fillWidth: true
        columns: Theme.compact ? 2 : 3
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2
        Repeater {
            model: dialog.formats
            delegate: T.AbstractButton {
                id: tile
                required property var modelData
                readonly property bool chosen: dialog.format === modelData.id
                objectName: "pageExportFormat_" + modelData.id
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 0, implicitContentHeight + topPadding + bottomPadding)
                padding: Theme.space2
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                enabled: !exporter.busy
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.description: modelData.description
                Accessible.checked: chosen
                onClicked: dialog.format = modelData.id

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
                        name: tile.modelData.icon
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: tile.modelData.name
                        variant: "label"
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
    BText {
        Layout.fillWidth: true
        text: dialog.current.description
        variant: "muted"
        wrapMode: Text.Wrap
    }

    // Pictures from the internet: the HTML, PDF, Word and Pandoc formats can carry copies.
    ColumnLayout {
        Layout.fillWidth: true
        visible: exporter.webPicturesApply
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: qsTr("Pictures from the internet")
            variant: "label"
        }
        BSegmented {
            Layout.fillWidth: true
            fill: true
            enabled: !exporter.busy
            accessibleName: qsTr("Pictures from the internet")
            model: [
                { value: "link", text: qsTr("Keep addresses"), icon: Theme.compact ? "" : "link" },
                { value: "include", text: qsTr("Include copies"), icon: Theme.compact ? "" : "image" }
            ]
            currentValue: exporter.webPictures
            onActivated: (value) => {
                exporter.webPictures = value
                AppSettings.exportWebPictures = value
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: exporter.busy
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: exporter.progressText
            variant: "small"
            color: Theme.fgMuted
            elide: Text.ElideRight
        }
        BProgressBar {
            Layout.fillWidth: true
            indeterminate: exporter.progress < 0
            value: Math.max(0, exporter.progress)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        spacing: Theme.space2
        BButton {
            objectName: "pageExportSave"
            Layout.fillWidth: true
            variant: "primary"
            iconName: "download"
            text: AppController.isWasm ? qsTr("Download") : qsTr("Save as file…")
            busy: exporter.busy && dialog.pending !== ""
            enabled: !exporter.busy && !!exporter.summary.valid
            onClicked: dialog.produce(AppController.isWasm ? "browser" : "save")
        }
        BButton {
            visible: AppController.isAndroid
            Layout.fillWidth: true
            iconName: "download"
            text: qsTr("Save to Downloads")
            enabled: !exporter.busy && !!exporter.summary.valid
            onClicked: dialog.produce("downloads")
        }
        BButton {
            objectName: "pageExportPrint"
            visible: exporter.printAvailable
            Layout.fillWidth: true
            iconName: "printer"
            text: qsTr("Print…")
            busy: exporter.busy && dialog.pending === ""
            enabled: !exporter.busy && !!exporter.summary.valid
            onClicked: dialog.startPrint()
        }
        BButton {
            visible: dialog.copyable
            Layout.fillWidth: true
            iconName: "copy"
            text: qsTr("Copy to clipboard")
            enabled: !exporter.busy
            onClicked: {
                if (dialog.tools.copyExport(dialog.path, dialog.format === "html-single" ? "html" : "markdown")) {
                    AppController.toast(qsTr("Copied to the clipboard"), "success")
                    dialog.close()
                } else {
                    AppController.toast(qsTr("The page could not be read."), "error")
                }
            }
        }
        BButton {
            visible: exporter.busy
            Layout.fillWidth: true
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: {
                dialog.pending = ""
                exporter.cancel()
            }
        }
        BButton {
            Layout.fillWidth: true
            variant: "ghost"
            iconName: "import"
            text: qsTr("More export options…")
            enabled: !exporter.busy
            onClicked: {
                const preset = { view: "importExport", tab: "export", scope: "page", path: dialog.path }
                if (dialog.format !== "markdown-file")
                    preset.format = dialog.format
                dialog.close()
                AppController.navigate(preset)
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
        currentFolder: exporter.defaultSaveFolder
        // selectedFile: set by finishPending()
        nameFilters: [dialog.current.name + " (*" + dialog.extension(dialog.format) + ")", qsTr("All files (*)")]
        onAccepted: exporter.saveTo(selectedFile)
    }

    function extension(id) {
        switch (id) {
        case "markdown-file": return ".md"
        case "html-single": return ".html"
        case "pdf": return ".pdf"
        case "docx": return ".docx"
        default: return ".zip"
        }
    }
}
