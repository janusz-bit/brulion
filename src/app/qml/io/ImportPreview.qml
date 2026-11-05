pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ImportPreview — steps 2 and 3 of an import (ImportController): what was picked and recognised
// (with "Importuj jako…" to override), where the pages go, what will be written (pages, daily notes
// — appended under "## Import (…)" when the day exists —, attachments, renamed files, warnings and
// the first files), progress while reading / checking / writing, and the report with "Otwórz
// zaimportowany folder". Shown once something was picked.
//
//   ImportPreview { importer: importController; onPickAgain: … }
ColumnLayout {
    id: preview

    required property ImportController importer
    readonly property string phase: importer.state
    property bool showFiles: false
    property bool showWarnings: false
    readonly property list<var> none: []

    // The preview's and the report's lists (importer.preview / importer.report).
    function previewWarnings(): list<string> {
        if (preview.importer.preview.warnings === undefined)
            return []
        return preview.importer.preview.warnings
    }
    function previewFiles(): list<var> {
        if (preview.importer.preview.files === undefined)
            return []
        return preview.importer.preview.files
    }
    function reportWarnings(): list<string> {
        if (preview.importer.report.warnings === undefined)
            return []
        return preview.importer.report.warnings
    }
    function reportFailed(): list<var> {
        if (preview.importer.report.failed === undefined)
            return []
        return preview.importer.report.failed
    }
    /// The counts of the preview's tiles.
    function tiles(): list<var> {
        const p = preview.importer.preview
        return [
            { label: qsTr("Pages"), value: p.pages || 0, icon: "file-text" },
            { label: qsTr("Daily notes"), value: p.journals || 0, icon: "calendar" },
            { label: qsTr("Attachments"), value: p.attachments || 0, icon: "image" }
        ]
    }
    /// What happens to existing files.
    function notes(): list<var> {
        const p = preview.importer.preview
        let out = preview.none
        if ((p.journalsAppended || 0) > 0)
            out = Js.appended(out, { icon: "calendar-plus", text: qsTr("%n daily note(s) exist already: the imported text is added at their end under “Import (%1)”.", "", p.journalsAppended).arg(preview.importer.effectiveName) })
        if ((p.renamed || 0) > 0)
            out = Js.appended(out, { icon: "copy", text: qsTr("%n file(s) get a new name (“… 2”) because the name is taken; links to them follow.", "", p.renamed) })
        if (p.folderExists)
            out = Js.appended(out, { icon: "folder", text: qsTr("The folder “%1” exists already; nothing in it is replaced.").arg(p.folder) })
        return out
    }
    /// The report's summary ("3 pages · 2 daily notes").
    function reportSummary(): string {
        const r = preview.importer.report
        let out = ""
        if (r.pages > 0)
            out = qsTr("%n page(s)", "", r.pages)
        if (r.journals > 0) {
            const journals = r.appended > 0 ? qsTr("%n daily note(s) (%1 added to existing ones)", "", r.journals).arg(r.appended)
                                            : qsTr("%n daily note(s)", "", r.journals)
            out = out === "" ? journals : out + " · " + journals
        }
        if (r.attachments > 0) {
            const attachments = qsTr("%n attachment(s)", "", r.attachments)
            out = out === "" ? attachments : out + " · " + attachments
        }
        return out !== "" ? out : qsTr("Nothing was written.")
    }

    signal pickAgain()

    spacing: Theme.space4

    // ---- What was picked ------------------------------------------------------------------------
    IoCard {
        iconName: "file-input"
        title: preview.importer.sourceName !== "" ? preview.importer.sourceName : qsTr("Reading…")
        subtitle: {
            if (preview.importer.inputCount === 0)
                return ""
            const files = qsTr("%n file(s)", "", preview.importer.inputCount)
            return files + " · " + preview.importer.inputSizeText
        }
        trailing: [
            BButton {
                visible: !preview.importer.busy && preview.phase !== "done"
                text: Theme.compact ? "" : qsTr("Choose other files")
                iconName: "x"
                size: "small"
                variant: "ghost"
                Accessible.name: qsTr("Choose other files")
                onClicked: preview.pickAgain()
            }
        ]

        // Recognised as … / import as …
        RowLayout {
            Layout.fillWidth: true
            visible: preview.phase !== "reading" && preview.phase !== "done"
            spacing: Theme.space2
            BIcon {
                name: preview.importer.effectiveId !== "" ? "circle-check" : "circle-help"
                size: Theme.iconSize
                color: preview.importer.effectiveId !== "" ? Theme.successText : Theme.warningText
            }
            BText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    if (preview.importer.importerId !== "")
                        return qsTr("Imported as %1").arg(preview.importer.effectiveName)
                    if (preview.importer.detectedId !== "")
                        return qsTr("Recognised: %1").arg(preview.importer.effectiveName)
                    return preview.phase === "analyzing" ? qsTr("Recognising the format…") : qsTr("Format not recognised")
                }
            }
            BButton {
                id: asButton
                text: qsTr("Import as…")
                iconName: "chevron-down"
                iconRight: true
                size: "small"
                enabled: !preview.importer.busy
                onClicked: asMenu.popup(asButton, 0, asButton.height)
            }
        }

        // Target folder.
        ColumnLayout {
            Layout.fillWidth: true
            visible: preview.phase === "ready" || preview.phase === "analyzing" || (preview.phase === "error" && preview.importer.effectiveId !== "")
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: qsTr("Folder for the pages")
                variant: "label"
            }
            BTextField {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.compact ? -1 : 420
                text: preview.importer.targetFolder
                placeholderText: qsTr("The notebook's top level")
                accessibleName: qsTr("Folder for the pages")
                leadingIcon: "folder"
                errorText: preview.importer.targetFolderError
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextEdited: preview.importer.targetFolder = text
            }
            BText {
                Layout.fillWidth: true
                text: qsTr("Daily notes always go to your journal; attachments to the attachments folder.")
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }
    }

    BMenu {
        id: asMenu
        BMenuItem {
            text: qsTr("Recognise automatically")
            checkable: true
            checked: preview.importer.importerId === ""
            onTriggered: preview.importer.importerId = ""
        }
        BMenuSeparator {}
    }
    Instantiator {
        model: preview.importer.importers
        delegate: BMenuItem {
            id: asItem
            // (A row's "id" cannot be a property name.)
            required property var modelData
            readonly property string importerKey: AppController.routeText(modelData, "id")
            text: AppController.routeText(modelData, "name")
            iconName: AppController.routeText(modelData, "icon")
            checkable: true
            checked: preview.importer.importerId === asItem.importerKey
            onTriggered: preview.importer.importerId = asItem.importerKey
        }
        onObjectAdded: (index, object) => asMenu.insertItem(index + 2, object as Item)
        onObjectRemoved: (index, object) => asMenu.removeItem(object as Item)
    }

    // ---- Busy -------------------------------------------------------------------------------------
    IoCard {
        visible: preview.importer.busy
        iconName: preview.phase === "importing" ? "download" : "hourglass"
        title: preview.phase === "importing" ? qsTr("Importing…")
             : preview.phase === "reading" ? qsTr("Reading the files…") : qsTr("Checking what will be imported…")
        subtitle: preview.importer.progressText
        BProgressBar {
            Layout.fillWidth: true
            indeterminate: preview.importer.progress < 0
            value: Math.max(0, preview.importer.progress)
        }
        BButton {
            visible: preview.phase === "reading" || preview.phase === "importing"
            text: preview.phase === "importing" ? qsTr("Stop") : qsTr("Cancel")
            iconName: "x"
            size: "small"
            onClicked: preview.importer.cancel()
        }
    }

    // ---- Error ------------------------------------------------------------------------------------
    IoCard {
        visible: preview.phase === "error"
        tone: "warning"
        iconName: "triangle-alert"
        iconColor: Theme.warningText
        title: qsTr("Nothing to import yet")
        subtitle: preview.importer.errorText
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                text: qsTr("Import as…")
                iconName: "chevron-down"
                iconRight: true
                onClicked: asMenu.popup()
            }
            BButton {
                text: qsTr("Choose other files")
                onClicked: preview.pickAgain()
            }
        }
    }

    // ---- Preview -----------------------------------------------------------------------------------
    IoCard {
        visible: preview.phase === "ready" && !!preview.importer.preview.fileCount
        iconName: "eye"
        title: qsTr("What will be imported")
        subtitle: qsTr("Nothing has been written yet.")

        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 2 : 3
            columnSpacing: Theme.space2
            rowSpacing: Theme.space2
            Repeater {
                model: preview.tiles()
                delegate: Rectangle {
                    id: tile
                    required property string label
                    required property int value
                    required property string icon
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: tileColumn.implicitHeight + 2 * Theme.space3
                    radius: Theme.radius
                    color: Theme.bgSunken
                    Accessible.role: Accessible.StaticText
                    Accessible.name: tile.label + ": " + tile.value
                    ColumnLayout {
                        id: tileColumn
                        anchors.fill: parent
                        anchors.margins: Theme.space3
                        spacing: 2
                        RowLayout {
                            spacing: Theme.space1
                            BIcon { name: tile.icon; size: Theme.iconSizeSm; color: Theme.fgMuted }
                            Text {
                                text: Qt.locale().toString(tile.value)
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXl
                                font.weight: Theme.weightSemiBold
                                color: Theme.fg
                            }
                        }
                        BText {
                            Layout.fillWidth: true
                            text: tile.label
                            variant: "small"
                            color: Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // What happens to existing files.
        Repeater {
            model: preview.notes()
            delegate: RowLayout {
                id: note
                required property string icon
                required property string text
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon { Layout.alignment: Qt.AlignTop; name: note.icon; size: Theme.iconSize; color: Theme.infoText }
                BText {
                    Layout.fillWidth: true
                    text: note.text
                    wrapMode: Text.Wrap
                }
            }
        }

        // Warnings of the importer (unsupported parts, skipped files).
        BButton {
            visible: preview.previewWarnings().length > 0
            text: qsTr("%n warning(s)", "", preview.previewWarnings().length)
            iconName: preview.showWarnings ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: preview.showWarnings = !preview.showWarnings
        }
        Repeater {
            model: preview.showWarnings ? preview.previewWarnings().slice(0, 100) : preview.none
            delegate: BText {
                required property string modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space4
                text: "• " + modelData
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }

        // The first files.
        BButton {
            text: preview.showFiles ? qsTr("Hide the files") : qsTr("Show the files")
            iconName: preview.showFiles ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: preview.showFiles = !preview.showFiles
        }
        Rectangle {
            Layout.fillWidth: true
            visible: preview.showFiles
            implicitHeight: Math.min(fileList.contentHeight, 10 * 32) + 2
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.border
            ListView {
                id: fileList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: preview.showFiles ? preview.previewFiles() : preview.none
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: BScrollBar {}
                footer: BText {
                    width: ListView.view ? ListView.view.width : 0
                    visible: AppController.routeInt(preview.importer.preview, "fileCount", 0) > preview.previewFiles().length
                    height: visible ? 32 : 0
                    leftPadding: Theme.space3
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("… and %n more", "", (preview.importer.preview.fileCount || 0) - preview.previewFiles().length)
                    variant: "small"
                    color: Theme.fgMuted
                }
                delegate: RowLayout {
                    id: fileRow
                    required property string path
                    required property string kind
                    required property bool append
                    width: ListView.view.width
                    height: 32
                    spacing: Theme.space2
                    BIcon {
                        Layout.leftMargin: Theme.space3
                        name: fileRow.kind === "journal" ? "calendar" : fileRow.kind === "page" ? "file-text" : "image"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: fileRow.path
                        elide: Text.ElideMiddle
                    }
                    BBadge {
                        Layout.rightMargin: Theme.space3
                        visible: fileRow.append
                        text: qsTr("added to")
                        variant: "info"
                    }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "importStartButton"
                text: qsTr("Import %n file(s)", "", preview.importer.preview.fileCount || 0)
                iconName: "download"
                variant: "primary"
                enabled: preview.importer.targetFolderError === ""
                onClicked: preview.importer.startImport()
            }
            BButton {
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: preview.importer.clear()
            }
        }
    }

    // ---- Report -----------------------------------------------------------------------------------
    IoCard {
        visible: preview.phase === "done"
        readonly property bool troubled: preview.reportFailed().length > 0 || !!preview.importer.report.cancelled
        tone: troubled ? "warning" : "success"
        iconName: troubled ? "triangle-alert" : "circle-check"
        iconColor: troubled ? Theme.warningText : Theme.successText
        title: preview.importer.report.cancelled ? qsTr("Import stopped") : qsTr("Imported from %1").arg(preview.importer.report.source || "")
        subtitle: preview.reportSummary()

        BText {
            Layout.fillWidth: true
            visible: (preview.importer.report.renamed || 0) > 0
            text: qsTr("%n file(s) got a new name because the name was taken.", "", preview.importer.report.renamed || 0)
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: preview.reportFailed().slice(0, 20)
            delegate: BText {
                required property string path
                required property string message
                Layout.fillWidth: true
                text: qsTr("Not saved: %1 (%2)").arg(path).arg(message)
                variant: "small"
                color: Theme.dangerText
                wrapMode: Text.Wrap
            }
        }
        BButton {
            visible: preview.reportWarnings().length > 0
            text: qsTr("%n warning(s)", "", preview.reportWarnings().length)
            iconName: preview.showWarnings ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: preview.showWarnings = !preview.showWarnings
        }
        Repeater {
            model: preview.showWarnings && preview.phase === "done" ? preview.reportWarnings().slice(0, 100) : preview.none
            delegate: BText {
                required property string modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space4
                text: "• " + modelData
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "importOpenFolder"
                visible: (preview.importer.report.folder || "") !== "" || (preview.importer.report.firstPage || "") !== ""
                //: Polish: "Otwórz zaimportowany folder"
                text: (preview.importer.report.folder || "") !== "" ? qsTr("Open the imported folder") : qsTr("Open the first page")
                iconName: "folder-open"
                variant: "primary"
                onClicked: preview.importer.openTarget()
            }
            BButton {
                text: qsTr("Import something else")
                iconName: "plus"
                onClicked: preview.importer.clear()
            }
        }
    }
}
