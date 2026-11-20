pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ImportCsvMapping — the spreadsheet step of an import (CSV, Airtable): what Brulion read (file,
// rows, encoding, separator, header row) and how the rows become pages — a page per row or one
// page with the table; the column naming the pages or a title template ("{{Name}} – {{Date}}"),
// subfolders from columns, the column that becomes the page's text, and per column its property
// name and type (detected: numbers, dates, check boxes, lists, tags, links to the pages of other
// rows, attachments; or leave it out). Below, the first rows with the titles their pages get and
// the first page as it will be written. Attachment cells ("photo.jpg (https://…)") can be
// downloaded so the pages embed them. Every change is checked again before anything is written
// (ImportController.csvOptions → csvMapping).
//
//   ImportCsvMapping { importer: importController }
IoCard {
    id: card

    required property ImportController importer
    // (Maps held in `var` are read through AppController's route helpers: compiled QML.)
    readonly property var file: importer.csvMapping.file
    readonly property bool tableMode: AppController.routeText(card.importer.csvOptions, "mode") === "table"
    readonly property list<var> none: []
    readonly property list<var> files: AppController.routeList(card.importer.csvMapping, "files")
    readonly property list<var> columns: AppController.routeList(card.file, "columns")
    readonly property list<var> rows: AppController.routeList(card.file, "rows")
    readonly property int attachments: AppController.routeInt(card.importer.csvMapping, "attachments", 0)
    readonly property int downloadedAttachments: AppController.routeInt(card.importer.csvMapping, "downloadedAttachments", 0)
    property bool showReading: false
    property bool showPreview: true

    function option(key: string): string {
        return AppController.routeText(card.importer.csvOptions, key)
    }
    function separatorText(value: string): string {
        if (value === "tab")
            return qsTr("tab")
        return value
    }
    function readingSummary(): string {
        const rowsText = qsTr("%n row(s)", "", AppController.routeInt(card.file, "rowCount", 0))
        const encoding = Js.toUpperCase(AppController.routeText(card.file, "encoding"))
        const separator = qsTr("separator %1").arg(card.separatorText(AppController.routeText(card.file, "separator")))
        const header = AppController.routeFlag(card.file, "header") ? qsTr("first row: column names") : qsTr("no header row")
        return rowsText + " · " + encoding + " · " + separator + " · " + header
    }
    function headerChoices(): list<var> {
        return [
            { value: "", text: qsTr("Detect") },
            { value: "yes", text: qsTr("The first row names the columns") },
            { value: "no", text: qsTr("The first row is data") }
        ]
    }
    function modeChoices(): list<var> {
        return [
            { value: "pages", text: qsTr("A page per row") },
            { value: "table", text: qsTr("One page with the table") }
        ]
    }

    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    component FieldLabel: BText {
        Layout.fillWidth: Theme.compact
        Layout.preferredWidth: Theme.compact ? -1 : 180
        variant: "label"
        wrapMode: Text.Wrap
    }

    objectName: "importCsvMapping"
    iconName: "table"
    title: qsTr("Columns and properties")
    subtitle: AppController.routeText(card.file, "name") + " · " + card.readingSummary()

    ImportAttachments {
        id: attachmentsObject
        objectName: "importAttachments"
        importer: card.importer
        onDownloaded: (count) => AppController.toast(qsTr("%n attachment(s) downloaded.", "", count), "success")
    }

    // Several CSV files: which one the card shows (each becomes a folder of its own).
    RowLayout {
        Layout.fillWidth: true
        visible: card.files.length > 1
        spacing: Theme.space2
        BText { text: qsTr("File"); variant: "label" }
        IoChoiceButton {
            objectName: "importCsvFile"
            model: card.files
            currentValue: "" + AppController.routeInt(card.importer.csvMapping, "fileIndex", 0)
            accessibleName: qsTr("File")
            onActivated: (value) => card.importer.setCsvFileIndex(Js.parseInt(value))
        }
        Hint { text: qsTr("Each file becomes a folder of its own; the mapping applies to columns of the same name in all of them.") }
    }

    BSegmented {
        objectName: "importCsvMode"
        Layout.fillWidth: Theme.compact
        fill: Theme.compact
        accessibleName: qsTr("How the rows are imported")
        model: card.modeChoices()
        currentValue: card.tableMode ? "table" : "pages"
        onActivated: (value) => card.importer.setCsvOption("mode", value === "table" ? "table" : "")
    }
    Hint {
        text: card.tableMode
              ? qsTr("The whole spreadsheet becomes one page with a Markdown table (the first 5000 rows).")
              : qsTr("Every row becomes a page; its cells become properties in the page's header, which bases, queries and the page's properties show.")
    }

    // ---- Titles, folders, text ---------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        visible: !card.tableMode
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space2

        FieldLabel { text: qsTr("Page titles from") }
        IoChoiceButton {
            objectName: "importCsvTitle"
            enabled: card.option("titleTemplate") === ""
            model: card.importer.csvTitleChoices
            currentValue: card.option("titleColumn")
            accessibleName: qsTr("Page titles from")
            onActivated: (value) => card.importer.setCsvOption("titleColumn", value)
        }
        FieldLabel { text: qsTr("Or a title template") }
        BTextField {
            objectName: "importCsvTitleTemplate"
            Layout.fillWidth: true
            size: "small"
            text: card.option("titleTemplate")
            placeholderText: qsTr("e.g. {{Name}} – {{Date}}")
            accessibleName: qsTr("Title template")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: card.importer.setCsvOption("titleTemplate", text)
        }
        FieldLabel { text: qsTr("Subfolders") }
        BTextField {
            objectName: "importCsvFolderTemplate"
            Layout.fillWidth: true
            size: "small"
            text: card.option("folderTemplate")
            placeholderText: qsTr("none, or e.g. {{Category}}")
            accessibleName: qsTr("Subfolders")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: card.importer.setCsvOption("folderTemplate", text)
        }
        FieldLabel { text: qsTr("Page text from") }
        IoChoiceButton {
            objectName: "importCsvBody"
            model: card.importer.csvBodyChoices
            currentValue: card.option("bodyColumn")
            accessibleName: qsTr("Page text from")
            onActivated: (value) => card.importer.setCsvOption("bodyColumn", value)
        }
    }
    Hint {
        visible: !card.tableMode
        text: qsTr("{{Column}} is a cell of the row (a column's name in any letter case), {{row}} its number. Rows without a title are named after the file and their row; a title that repeats gets a number.")
    }

    // ---- Columns -------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        text: card.tableMode ? qsTr("Columns of the table") : qsTr("Properties")
        variant: "label"
    }
    Repeater {
        model: card.columns
        delegate: ColumnLayout {
            id: column
            required property int index
            required property string name
            required property string property
            required property string chosenProperty
            required property string type
            required property string typeText
            required property bool isTitle
            required property bool isBody
            required property string sample
            readonly property bool editable: !card.tableMode && !column.isTitle && !column.isBody
            Layout.fillWidth: true
            spacing: Theme.space1

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    spacing: 0
                    BText {
                        Layout.fillWidth: true
                        text: column.name
                        variant: "label"
                        elide: Text.ElideRight
                    }
                    BText {
                        Layout.fillWidth: true
                        visible: column.sample !== ""
                        text: column.sample
                        variant: "small"
                        color: Theme.fgMuted
                        elide: Text.ElideRight
                    }
                }
                BBadge {
                    visible: !card.tableMode && (column.isTitle || column.isBody)
                    text: column.isTitle ? qsTr("title") : qsTr("page text")
                    variant: "info"
                }
                BTextField {
                    objectName: "importCsvProperty_" + column.index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    visible: column.editable && !Theme.compact
                    size: "small"
                    text: column.chosenProperty
                    placeholderText: column.property
                    accessibleName: qsTr("Property name of “%1”").arg(column.name)
                    onEditingFinished: card.importer.setCsvColumn(column.name, text, column.type)
                }
                IoChoiceButton {
                    objectName: "importCsvType_" + column.index
                    Layout.preferredWidth: Theme.compact ? -1 : 190
                    visible: !column.isTitle && !column.isBody
                    model: card.importer.csvTypeChoices
                    currentValue: column.type
                    fallbackText: column.typeText
                    // "Detect" shows what was detected.
                    text: column.type === "auto" ? qsTr("Detect: %1").arg(column.typeText) : currentText()
                    accessibleName: qsTr("Type of “%1”").arg(column.name)
                    onActivated: (value) => card.importer.setCsvColumn(column.name, column.chosenProperty, value)
                }
            }
            // Phones: the property's name on a line of its own.
            BTextField {
                Layout.fillWidth: true
                visible: column.editable && Theme.compact && column.type !== "skip"
                size: "small"
                text: column.chosenProperty
                placeholderText: qsTr("Property: %1").arg(column.property)
                accessibleName: qsTr("Property name of “%1”").arg(column.name)
                onEditingFinished: card.importer.setCsvColumn(column.name, text, column.type)
            }
        }
    }

    // ---- Attachments on the web ------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        visible: card.attachments > 0 && !card.tableMode && !AppController.isWasm
        implicitHeight: attachmentRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken
        ColumnLayout {
            id: attachmentRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: card.downloadedAttachments >= card.attachments
                      ? qsTr("All %n attachment(s) are downloaded: the pages embed them.", "", card.attachments)
                      : qsTr("The cells name %n attachment file(s) on the web. Download them to keep them in the notebook; otherwise the pages link to their addresses (Airtable's links expire after a few hours).", "", card.attachments)
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BButton {
                    objectName: "importDownloadAttachments"
                    visible: card.downloadedAttachments < card.attachments
                    text: qsTr("Download the attachments")
                    iconName: "download"
                    size: "small"
                    busy: attachmentsObject.busy
                    enabled: !attachmentsObject.busy && !card.importer.busy
                    onClicked: attachmentsObject.download()
                }
                BButton {
                    visible: attachmentsObject.busy
                    text: qsTr("Cancel")
                    size: "small"
                    variant: "ghost"
                    onClicked: attachmentsObject.cancel()
                }
                Hint { text: attachmentsObject.progressText }
            }
        }
    }

    // ---- Reading the file --------------------------------------------------------------------
    BButton {
        text: card.showReading ? qsTr("Less") : qsTr("Encoding, separator, header row…")
        iconName: card.showReading ? "chevron-down" : "chevron-right"
        size: "small"
        variant: "ghost"
        onClicked: card.showReading = !card.showReading
    }
    GridLayout {
        Layout.fillWidth: true
        visible: card.showReading
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space2
        FieldLabel { text: qsTr("Encoding") }
        IoChoiceButton {
            objectName: "importCsvEncoding"
            model: card.importer.csvEncodingChoices
            currentValue: card.option("encoding")
            accessibleName: qsTr("Encoding")
            onActivated: (value) => card.importer.setCsvOption("encoding", value)
        }
        FieldLabel { text: qsTr("Separator") }
        IoChoiceButton {
            model: card.importer.csvSeparatorChoices
            currentValue: card.option("separator")
            accessibleName: qsTr("Separator")
            onActivated: (value) => card.importer.setCsvOption("separator", value)
        }
        FieldLabel { text: qsTr("Header row") }
        IoChoiceButton {
            model: card.headerChoices()
            currentValue: card.option("header")
            accessibleName: qsTr("Header row")
            onActivated: (value) => card.importer.setCsvOption("header", value)
        }
        FieldLabel { text: qsTr("List items separated by") }
        BTextField {
            Layout.maximumWidth: 160
            size: "small"
            text: card.option("listSeparator")
            placeholderText: qsTr("detected")
            accessibleName: qsTr("List items separated by")
            onTextEdited: card.importer.setCsvOption("listSeparator", text)
        }
    }
    BSwitch {
        Layout.fillWidth: true
        visible: !card.tableMode
        text: qsTr("A base with all the pages")
        description: qsTr("An Obsidian base file next to the pages shows them as a table with these properties.")
        checked: card.option("base") !== "false"
        onToggled: card.importer.setCsvOption("base", checked)
    }

    // ---- Preview -----------------------------------------------------------------------------------
    BButton {
        text: card.showPreview ? qsTr("Hide the first rows") : qsTr("Show the first rows")
        iconName: card.showPreview ? "chevron-down" : "chevron-right"
        size: "small"
        variant: "ghost"
        onClicked: card.showPreview = !card.showPreview
    }
    Repeater {
        model: card.showPreview && !card.tableMode ? card.rows : card.none
        delegate: RowLayout {
            id: row
            required property string title
            required property list<string> cells
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon { name: "file-text"; size: Theme.iconSizeSm; color: Theme.fgMuted }
            BText {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: row.title
                variant: "label"
                elide: Text.ElideRight
            }
            BText {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                visible: !Theme.compact
                text: row.cells.join(" · ")
                variant: "small"
                color: Theme.fgMuted
                elide: Text.ElideRight
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        visible: card.showPreview && AppController.routeText(card.file, "samplePage") !== ""
        implicitHeight: sample.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken
        BText {
            id: sample
            objectName: "importCsvSamplePage"
            anchors.fill: parent
            anchors.margins: Theme.space3
            text: AppController.routeText(card.file, "samplePage")
            textFormat: Text.PlainText
            variant: "mono"
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 24
            elide: Text.ElideRight
        }
    }
    Hint {
        visible: card.showPreview && AppController.routeText(card.file, "samplePage") !== ""
        text: card.tableMode ? qsTr("The table page as it will be written.") : qsTr("The first row's page as it will be written.")
    }
    BButton {
        objectName: "importCsvReset"
        visible: AppController.routeFlag(card.importer.csvMapping, "customized")
        text: qsTr("Back to what Brulion detected")
        iconName: "rotate-ccw"
        size: "small"
        onClicked: card.importer.resetCsvMapping()
    }
}
