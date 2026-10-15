pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ImportPreview — steps 2 and 3 of an import (ImportController): what was picked and recognised
// (with "Importuj jako…" to override), where the pages go, what will be written (pages, daily notes
// — appended under "## Import (…)" when the day exists —, attachments, renamed files, warnings and
// the first files), progress while reading / checking / writing, and the report with "Otwórz
// folder". Shown once something was picked.
//
//   ImportPreview { importer: importController; onPickAgain: … }
ColumnLayout {
    id: preview

    required property ImportController importer
    readonly property string phase: importer.state
    readonly property var p: importer.preview
    readonly property var r: importer.report
    property bool showFiles: false
    property bool showWarnings: false

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
            required property var modelData
            text: modelData.name
            iconName: modelData.icon
            checkable: true
            checked: preview.importer.importerId === modelData.id
            onTriggered: preview.importer.importerId = modelData.id
        }
        onObjectAdded: (index, object) => asMenu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => asMenu.removeItem(object)
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
        visible: preview.phase === "ready" && !!preview.p.fileCount
        iconName: "eye"
        title: qsTr("What will be imported")
        subtitle: qsTr("Nothing has been written yet.")

        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 2 : 3
            columnSpacing: Theme.space2
            rowSpacing: Theme.space2
            Repeater {
                model: [
                    { label: qsTr("Pages"), value: preview.p.pages || 0, icon: "file-text" },
                    { label: qsTr("Daily notes"), value: preview.p.journals || 0, icon: "calendar" },
                    { label: qsTr("Attachments"), value: preview.p.attachments || 0, icon: "image" }
                ]
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: tileColumn.implicitHeight + 2 * Theme.space3
                    radius: Theme.radius
                    color: Theme.bgSunken
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.label + ": " + modelData.value
                    ColumnLayout {
                        id: tileColumn
                        anchors.fill: parent
                        anchors.margins: Theme.space3
                        spacing: 2
                        RowLayout {
                            spacing: Theme.space1
                            BIcon { name: tile.modelData.icon; size: Theme.iconSizeSm; color: Theme.fgMuted }
                            Text {
                                text: tile.modelData.value.toLocaleString(Qt.locale(), "f", 0)
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXl
                                font.weight: Theme.weightSemiBold
                                color: Theme.fg
                            }
                        }
                        BText {
                            Layout.fillWidth: true
                            text: tile.modelData.label
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
            model: {
                const notes = []
                const p = preview.p
                if ((p.journalsAppended || 0) > 0)
                    notes.push({ icon: "calendar-plus", text: qsTr("%n daily note(s) exist already: the imported text is added at their end under “Import (%1)”.", "", p.journalsAppended).arg(preview.importer.effectiveName) })
                if ((p.renamed || 0) > 0)
                    notes.push({ icon: "copy", text: qsTr("%n file(s) get a new name (“… 2”) because the name is taken; links to them follow.", "", p.renamed) })
                if (p.folderExists)
                    notes.push({ icon: "folder", text: qsTr("The folder “%1” exists already; nothing in it is replaced.").arg(p.folder) })
                return notes
            }
            delegate: RowLayout {
                id: note
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon { Layout.alignment: Qt.AlignTop; name: note.modelData.icon; size: Theme.iconSize; color: Theme.infoText }
                BText {
                    Layout.fillWidth: true
                    text: note.modelData.text
                    wrapMode: Text.Wrap
                }
            }
        }

        // Warnings of the importer (unsupported parts, skipped files).
        BButton {
            visible: (preview.p.warnings || []).length > 0
            text: qsTr("%n warning(s)", "", (preview.p.warnings || []).length)
            iconName: preview.showWarnings ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: preview.showWarnings = !preview.showWarnings
        }
        Repeater {
            model: preview.showWarnings ? (preview.p.warnings || []).slice(0, 100) : []
            delegate: BText {
                required property var modelData
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
                model: preview.showFiles ? (preview.p.files || []) : []
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: BScrollBar {}
                footer: BText {
                    width: ListView.view ? ListView.view.width : 0
                    visible: (preview.p.fileCount || 0) > (preview.p.files || []).length
                    height: visible ? 32 : 0
                    leftPadding: Theme.space3
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("… and %n more", "", (preview.p.fileCount || 0) - (preview.p.files || []).length)
                    variant: "small"
                    color: Theme.fgMuted
                }
                delegate: RowLayout {
                    id: fileRow
                    required property var modelData
                    width: ListView.view.width
                    height: 32
                    spacing: Theme.space2
                    BIcon {
                        Layout.leftMargin: Theme.space3
                        name: fileRow.modelData.kind === "journal" ? "calendar" : fileRow.modelData.kind === "page" ? "file-text" : "image"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: fileRow.modelData.path
                        elide: Text.ElideMiddle
                    }
                    BBadge {
                        Layout.rightMargin: Theme.space3
                        visible: fileRow.modelData.append
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
                text: qsTr("Import %n file(s)", "", preview.p.fileCount || 0)
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
        tone: (preview.r.failed || []).length > 0 || preview.r.cancelled ? "warning" : "success"
        iconName: (preview.r.failed || []).length > 0 || preview.r.cancelled ? "triangle-alert" : "circle-check"
        iconColor: (preview.r.failed || []).length > 0 || preview.r.cancelled ? Theme.warningText : Theme.successText
        title: preview.r.cancelled ? qsTr("Import stopped") : qsTr("Imported from %1").arg(preview.r.source || "")
        subtitle: {
            const r = preview.r
            const parts = []
            if (r.pages > 0)
                parts.push(qsTr("%n page(s)", "", r.pages))
            if (r.journals > 0)
                parts.push(r.appended > 0 ? qsTr("%n daily note(s) (%1 added to existing ones)", "", r.journals).arg(r.appended)
                                          : qsTr("%n daily note(s)", "", r.journals))
            if (r.attachments > 0)
                parts.push(qsTr("%n attachment(s)", "", r.attachments))
            return parts.length > 0 ? parts.join(" · ") : qsTr("Nothing was written.")
        }

        BText {
            Layout.fillWidth: true
            visible: (preview.r.renamed || 0) > 0
            text: qsTr("%n file(s) got a new name because the name was taken.", "", preview.r.renamed || 0)
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: (preview.r.failed || []).slice(0, 20)
            delegate: BText {
                required property var modelData
                Layout.fillWidth: true
                text: qsTr("Not saved: %1 (%2)").arg(modelData.path).arg(modelData.message)
                variant: "small"
                color: Theme.dangerText
                wrapMode: Text.Wrap
            }
        }
        BButton {
            visible: (preview.r.warnings || []).length > 0
            text: qsTr("%n warning(s)", "", (preview.r.warnings || []).length)
            iconName: preview.showWarnings ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: preview.showWarnings = !preview.showWarnings
        }
        Repeater {
            model: preview.showWarnings && preview.phase === "done" ? (preview.r.warnings || []).slice(0, 100) : []
            delegate: BText {
                required property var modelData
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
                visible: (preview.r.folder || "") !== "" || (preview.r.firstPage || "") !== ""
                //: Polish: "Otwórz folder"
                text: (preview.r.folder || "") !== "" ? qsTr("Open the folder") : qsTr("Open the first page")
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
