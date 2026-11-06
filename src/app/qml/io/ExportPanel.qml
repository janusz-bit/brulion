pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ExportPanel — the "Export" tab of ImportExportView (ExportController): format (Markdown ZIP,
// Obsidian, Logseq, HTML website, one HTML file, PDF, Word, Pandoc Markdown, JSON, iCalendar, OPML,
// tasks CSV), what to export (the whole notebook / a folder / a page, with the attachments they use),
// then "Export…": the file is produced in the background and saved where the user says.
//
// Pictures from the internet (`![](https://…)`): the HTML, PDF, Word and Pandoc formats offer "Keep
// addresses" (the export shows them only online) or "Include copies" (copies from the device's cache,
// the missing ones fetched first); the choice is remembered on the device (AppSettings).
//
// Saving: desktop and Android — the system's file picker in save mode (FilePicker; Android: the
// system's "Save as" document picker; "Save to Downloads" writes into the Download folder
// instead); browser — the browser's save picker or a download (platform/wasm/wasmfiles.h).
// `preset` (route parameters): { format, scope, folder, path (the page) }.
ColumnLayout {
    id: panel

    property var preset: ({})
    readonly property alias exporter: exporterObject
    // Phones: no icons, so "Whole notebook" ("Cały notatnik" in Polish) fits.
    readonly property list<var> scopes: [
        { value: "vault", text: qsTr("Whole notebook"), icon: Theme.compact ? "" : "notebook-pen" },
        { value: "folder", text: qsTr("Folder"), icon: Theme.compact ? "" : "folder" },
        { value: "page", text: qsTr("Page"), icon: Theme.compact ? "" : "file-text" }
    ]
    readonly property list<var> webPictureChoices: [
        { value: "link", text: qsTr("Keep addresses"), icon: Theme.compact ? "" : "link" },
        { value: "include", text: qsTr("Include copies"), icon: Theme.compact ? "" : "image" }
    ]

    function applyPreset(): void {
        if (!preset)
            return
        if (AppController.routeFlag(preset, "format"))
            exporterObject.format = AppController.routeText(preset, "format")
        if (AppController.routeFlag(preset, "folder"))
            exporterObject.folder = AppController.routeText(preset, "folder")
        if (AppController.routeFlag(preset, "path"))
            exporterObject.page = AppController.routeText(preset, "path")
        if (AppController.routeFlag(preset, "scope"))
            exporterObject.scope = AppController.routeText(preset, "scope")
        else if (AppController.routeFlag(preset, "path"))
            exporterObject.scope = "page"
    }
    function defaultPage(): void {
        if (exporterObject.page !== "")
            return
        const recent = Workspace.recentPages
        if (recent.length > 0)
            exporterObject.page = AppController.routeText(recent[0], "path")
    }
    function save(): void {
        if (AppController.isWasm) {
            exporterObject.saveInBrowser()
            return
        }
        // Assigned now, not bound: save() also runs from the controller's stateChanged, before
        // a binding on its file name has followed — the dialog (Android: the system's "Save as",
        // the name passed as its EXTRA_TITLE) read the name on open() and came up empty.
        saveDialog.selectedFile = exporterObject.suggestedSaveFile
        saveDialog.open()
    }

    onPresetChanged: applyPreset()
    Component.onCompleted: {
        applyPreset()
        defaultPage()
    }

    spacing: Theme.space5

    ExportController {
        id: exporterObject
        objectName: "exportController"
        workspace: Workspace
        webPictures: AppSettings.exportWebPictures
        onSaved: (message) => AppController.toast(message, "success")
        // The file is ready: ask where to put it right away.
        onStateChanged: if (exporterObject.state === "ready") panel.save()
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Your notes are plain Markdown files already. Export them for another app, a website or a backup – the notebook itself does not change.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    // ---- Format ----------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        text: qsTr("Format")
        variant: "title"
        Accessible.role: Accessible.Heading
    }
    GridLayout {
        id: formats
        Layout.fillWidth: true
        // Phones: names only in two columns, the chosen format explained below.
        readonly property bool brief: Theme.compact
        columns: brief ? 2 : width >= 700 ? 3 : 2
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2

        Repeater {
            model: exporterObject.formats
            delegate: T.AbstractButton {
                id: tile
                // (A row's "id" cannot be a property name, "icon" is the button's own.)
                required property var modelData
                required property string name
                required property string description
                readonly property string formatId: AppController.routeText(modelData, "id")
                readonly property string iconName: AppController.routeText(modelData, "icon")
                readonly property bool chosen: exporterObject.format === formatId
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                padding: Theme.space3
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                enabled: !exporterObject.busy
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.name
                Accessible.description: tile.description
                Accessible.checked: chosen
                onClicked: exporterObject.format = tile.formatId

                background: Rectangle {
                    radius: Theme.radius
                    color: tile.chosen ? Theme.bgSelected : tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : Theme.bgElevated
                    border.width: tile.chosen ? 1.5 : 1
                    border.color: tile.chosen ? Theme.focusRing : Theme.border
                    BFocusRing { visible: tile.visualFocus; targetRadius: Theme.radius }
                }
                contentItem: RowLayout {
                    id: tileRow
                    spacing: Theme.space3
                    BIcon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: Math.round((Theme.lineHeightPx(Theme.textBase, Theme.lineHeight) - Theme.iconSize) / 2)
                        name: tile.iconName
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 2
                        BText {
                            Layout.fillWidth: true
                            text: tile.name
                            variant: "label"
                            // Phones: two lines rather than "HTML website (…" (Polish names are longer).
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        BText {
                            Layout.fillWidth: true
                            visible: !formats.brief
                            text: tile.description
                            variant: "small"
                            color: Theme.fgMuted
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        visible: formats.brief
        text: exporterObject.chosenFormat.description !== undefined ? "" + exporterObject.chosenFormat.description : ""
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    // ---- What ---------------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        text: qsTr("What to export")
        variant: "title"
        Accessible.role: Accessible.Heading
    }
    BSegmented {
        Layout.fillWidth: Theme.compact
        fill: Theme.compact
        enabled: !exporterObject.busy
        accessibleName: qsTr("What to export")
        // Phones: no icons, so "Whole notebook" ("Cały notatnik" in Polish) fits.
        model: panel.scopes
        currentValue: exporterObject.scope
        onActivated: (value) => exporterObject.scope = value
    }
    RowLayout {
        Layout.fillWidth: true
        visible: exporterObject.scope !== "vault"
        spacing: Theme.space2
        BIcon {
            name: exporterObject.scope === "folder" ? "folder" : "file-text"
            size: Theme.iconSize
            color: Theme.fgMuted
        }
        BText {
            Layout.fillWidth: true
            text: exporterObject.scope === "folder"
                  ? (exporterObject.folder !== "" ? exporterObject.folder : qsTr("No folder chosen"))
                  : (exporterObject.page !== "" ? exporterObject.pageTitle : qsTr("No page chosen"))
            variant: (exporterObject.scope === "folder" ? exporterObject.folder : exporterObject.page) !== "" ? "label" : "muted"
            elide: Text.ElideMiddle
        }
        BButton {
            text: qsTr("Choose…")
            size: "small"
            enabled: !exporterObject.busy
            onClicked: {
                pathPicker.mode = exporterObject.scope
                pathPicker.open()
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon {
            Layout.alignment: Qt.AlignTop
            name: exporterObject.summary.valid ? "files" : "info"
            size: Theme.iconSize
            color: exporterObject.summary.valid ? Theme.fgMuted : Theme.warningText
        }
        BText {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                const s = exporterObject.summary
                if (!s.valid)
                    return s.error || ""
                const pages = qsTr("%n page(s)", "", s.pages)
                return (s.attachments > 0 ? pages + ", " + qsTr("%n attachment(s)", "", s.attachments) : pages) + " · " + s.sizeText
            }
        }
    }

    // ---- Pictures from the internet (HTML, PDF, Word, Pandoc) --------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: exporterObject.webPicturesApply
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: qsTr("Pictures from the internet")
            variant: "label"
        }
        BSegmented {
            objectName: "exportWebPictures"
            Layout.fillWidth: Theme.compact
            fill: Theme.compact
            enabled: !exporterObject.busy
            accessibleName: qsTr("Pictures from the internet")
            model: panel.webPictureChoices
            currentValue: exporterObject.webPictures
            onActivated: (value) => {
                exporterObject.webPictures = value
                AppSettings.exportWebPictures = value
            }
        }
        BText {
            Layout.fillWidth: true
            text: exporterObject.webPictures === "include"
                  ? qsTr("The export carries the pictures. Brulion fetches the ones this device does not have yet.")
                  : qsTr("The export shows such pictures only with an internet connection.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // ---- Export ------------------------------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: Theme.space2
        BButton {
            objectName: "exportStartButton"
            text: qsTr("Export…")
            iconName: "upload"
            variant: "primary"
            busy: exporterObject.busy
            enabled: !!exporterObject.summary.valid
            onClicked: exporterObject.prepare()
        }
        BButton {
            visible: exporterObject.state === "preparing"
            text: qsTr("Cancel")
            variant: "ghost"
            onClicked: exporterObject.cancel()
        }
    }

    IoCard {
        visible: exporterObject.state === "preparing"
        iconName: "hourglass"
        title: qsTr("Preparing the export…")
        subtitle: exporterObject.progressText
        BProgressBar {
            Layout.fillWidth: true
            indeterminate: exporterObject.progress < 0
            value: Math.max(0, exporterObject.progress)
        }
    }

    IoCard {
        visible: exporterObject.state === "ready"
        iconName: "file-output"
        title: qsTr("“%1” is ready").arg(exporterObject.fileName)
        subtitle: exporterObject.fileSizeText
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                text: AppController.isWasm ? qsTr("Download") : qsTr("Save…")
                iconName: "download"
                variant: "primary"
                onClicked: panel.save()
            }
            BButton {
                visible: AppController.isAndroid
                text: qsTr("Save to Downloads")
                iconName: "download"
                onClicked: exporterObject.saveToExportFolder()
            }
            BButton {
                text: qsTr("Discard")
                variant: "ghost"
                onClicked: exporterObject.discard()
            }
        }
    }

    IoCard {
        visible: exporterObject.state === "saved"
        tone: "success"
        iconName: "circle-check"
        iconColor: Theme.successText
        title: qsTr("Exported “%1”").arg(exporterObject.fileName)
        subtitle: AppController.isWasm ? qsTr("Your browser saves it in its downloads.") : exporterObject.savedTo
        Repeater {
            model: exporterObject.warnings.slice(0, 50)
            delegate: BText {
                required property var modelData
                Layout.fillWidth: true
                text: "• " + modelData
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }
        BButton {
            text: qsTr("Done")
            onClicked: exporterObject.discard()
        }
    }

    IoCard {
        visible: exporterObject.state === "error"
        tone: "danger"
        iconName: "circle-alert"
        iconColor: Theme.dangerText
        title: qsTr("The export did not work")
        subtitle: exporterObject.errorText
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                visible: exporterObject.fileName !== ""
                text: qsTr("Try saving again")
                onClicked: panel.save()
            }
            BButton {
                text: qsTr("Close")
                variant: "ghost"
                onClicked: exporterObject.discard()
            }
        }
    }

    IoPathPickerDialog {
        id: pathPicker
        folders: exporterObject.folders
        onChosen: (path) => {
            if (mode === "folder")
                exporterObject.folder = path
            else
                exporterObject.page = path
        }
    }

    FilePicker {
        id: saveDialog
        objectName: "exportSaveDialog"
        title: qsTr("Save the export")
        fileMode: FilePicker.SaveFile
        dark: Theme.dark
        colors: Theme.colors
        currentFolder: exporterObject.defaultSaveFolder
        // selectedFile: set by save()
        nameFilters: exporterObject.chosenFormat.id !== undefined
                     ? [exporterObject.chosenFormat.name + " (*" + exporterObject.chosenFormat.ext + ")", qsTr("All files (*)")]
                     : [qsTr("All files (*)")]
        onAccepted: exporterObject.saveTo(selectedFile)
    }
}
