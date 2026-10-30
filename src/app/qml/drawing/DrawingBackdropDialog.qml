pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// DrawingBackdropDialog — a PDF's pages as drawings' backdrops (docs/UI.md "Drawing editor"): the
// file's name and how many pages it has, the pages as thumbnails (DrawingBackdrops.pageUrl: the
// image provider "pdfpage", drawn when they scroll into view) — a tap chooses or drops one —,
// "Wszystkie" / "Żadna" and the field "Strony" ("1-3, 7"; kept in step with the thumbnails).
// Mode "insert" (a note's "Rysunek na obrazie lub PDF…"): any pages, at most
// DrawingBackdrops' 60, one drawing each — "Wstaw rysunki (N)"; mode "single" (the drawing
// editor's backdrop): one page, the current one chosen — "Ustaw podkład". While the pages are
// prepared ("Strona 3 z 12…", a progress bar) the dialog stays; "Anuluj" stops it. A PDF with a
// password, one that cannot be read or a build without PDF says so.
//
//   DrawingBackdropDialog { onPrepared: (items) => controller.insertDrawingsOn(row, from, to, items) }
//   dialog.openFor("Załączniki/wykład 3.pdf", "insert", 1)
BDialog {
    id: dialog
    objectName: "drawingBackdropDialog"

    /// The PDF's vault path.
    property string path: ""
    /// "insert" (several pages) | "single" (one page: the drawing editor's backdrop).
    property string mode: "insert"
    property int pageCount: 0
    /// Why the pages cannot be shown ("" none).
    property string error: ""
    /// The chosen pages, ascending.
    property var chosen: []
    /// More than the limit was asked for (the note under the field).
    property bool capped: false
    /// The preparing job (0 none) and its progress.
    property int job: 0
    property int done: 0
    property int total: 0

    readonly property bool single: mode === "single"
    readonly property string fileName: path.slice(path.lastIndexOf("/") + 1)
    readonly property int maxPages: 60

    /// The prepared backdrops ({ bytes, mimeType, width, height, path, page, name, error } each).
    signal prepared(var items)

    function openFor(vaultPath, openMode, page) {
        dialog.path = vaultPath
        dialog.mode = openMode
        const info = DrawingBackdrops.pdfInfo(vaultPath)
        dialog.pageCount = Math.max(0, info.pages)
        dialog.error = info.error
        dialog.capped = false
        dialog.job = 0
        dialog.done = 0
        dialog.total = 0
        const first = Math.min(Math.max(1, page || 1), Math.max(1, dialog.pageCount))
        dialog.setChosen(dialog.pageCount === 0 ? [] : dialog.single || dialog.pageCount === 1 ? [first] : [])
        dialog.open()
        if (dialog.single && dialog.pageCount > 0)
            Qt.callLater(() => grid.positionViewAtIndex(first - 1, GridView.Contain))
    }

    function setChosen(pages) {
        const sorted = pages.slice().sort((a, b) => a - b)
        dialog.capped = sorted.length > dialog.maxPages
        dialog.chosen = sorted.slice(0, dialog.maxPages)
        if (!pagesField.activeFocus)
            pagesField.text = DrawingBackdrops.formatPages(dialog.chosen)
    }

    function toggle(page) {
        if (dialog.job !== 0)
            return
        if (dialog.single) {
            dialog.setChosen([page])
            return
        }
        const i = dialog.chosen.indexOf(page)
        if (i >= 0) {
            const next = dialog.chosen.slice()
            next.splice(i, 1)
            dialog.setChosen(next)
        } else {
            dialog.setChosen(dialog.chosen.concat([page]))
        }
        pagesField.text = DrawingBackdrops.formatPages(dialog.chosen)
    }

    function chooseAll() {
        const all = []
        for (let p = 1; p <= dialog.pageCount; ++p)
            all.push(p)
        dialog.setChosen(all)
        pagesField.text = DrawingBackdrops.formatPages(dialog.chosen)
    }

    title: fileName
    subtitle: error !== "" ? "" : qsTr("%n page(s)", "", pageCount)
    iconName: "file-text"
    preferredWidth: Math.max(Theme.dialogWidth, 640)
    acceptText: single ? qsTr("Set backdrop") : qsTr("Insert drawings (%1)").arg(chosen.length)
    acceptEnabled: error === "" && chosen.length > 0
    busy: job !== 0
    onAcceptRequested: {
        // Prepared first (the dialog stays, with its progress); prepared() and the close follow.
        dialog.done = 0
        dialog.total = dialog.chosen.length
        dialog.job = DrawingBackdrops.prepare(dialog.path, dialog.chosen)
    }
    onRejected: {
        if (dialog.job !== 0)
            DrawingBackdrops.cancel(dialog.job)
        dialog.job = 0
    }

    Connections {
        target: DrawingBackdrops
        enabled: dialog.job !== 0
        function onProgress(job, done, total) {
            if (job === dialog.job) {
                dialog.done = done
                dialog.total = total
            }
        }
        function onFinished(job, items, cancelled) {
            if (job !== dialog.job)
                return
            dialog.job = 0
            if (cancelled)
                return
            dialog.close()
            dialog.prepared(items)
        }
    }

    BText {
        objectName: "drawingBackdropError"
        Layout.fillWidth: true
        visible: dialog.error !== ""
        text: dialog.error
        wrapMode: Text.Wrap
        color: Theme.danger
    }

    RowLayout {
        Layout.fillWidth: true
        visible: dialog.error === "" && !dialog.single
        spacing: Theme.space2
        BTextField {
            id: pagesField
            objectName: "drawingBackdropPages"
            Layout.fillWidth: true
            leadingIcon: "file-text"
            placeholderText: qsTr("Pages, e.g. 1-3, 7")
            accessibleName: qsTr("Pages")
            enabled: dialog.job === 0
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextEdited: dialog.setChosen(DrawingBackdrops.parsePages(text, dialog.pageCount))
            onEditingFinished: text = DrawingBackdrops.formatPages(dialog.chosen)
        }
        BButton {
            objectName: "drawingBackdropAll"
            text: qsTr("All")
            size: "small"
            enabled: dialog.job === 0
            onClicked: dialog.chooseAll()
        }
        BButton {
            objectName: "drawingBackdropNone"
            text: qsTr("None")
            size: "small"
            enabled: dialog.job === 0
            onClicked: {
                dialog.setChosen([])
                pagesField.text = ""
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.capped
        variant: "caption"
        wrapMode: Text.Wrap
        text: qsTr("At most %1 pages at once.").arg(dialog.maxPages)
    }

    GridView {
        id: grid
        objectName: "drawingBackdropGrid"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, Math.max(cellHeight, dialog.overlayHeight * 0.5))
        visible: dialog.error === "" && dialog.pageCount > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.error === "" ? dialog.pageCount : 0
        readonly property int columns: Math.max(2, Math.floor(width / (Theme.touchUi ? 132 : 120)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round(cellWidth * 1.3) + Theme.textSm + Theme.space2
        T.ScrollBar.vertical: BScrollBar {}

        delegate: Item {
            id: cell
            required property int index
            readonly property int page: index + 1
            readonly property bool checked: dialog.chosen.indexOf(page) >= 0
            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Page %1").arg(cell.page)
            Accessible.checkable: true
            Accessible.checked: cell.checked

            Rectangle {
                id: frame
                x: Theme.space1
                y: Theme.space1
                width: parent.width - 2 * Theme.space1
                height: parent.height - pageLabel.height - Theme.space2 - Theme.space1
                radius: Theme.radiusSm
                color: "#ffffff" // (a page is white, also in the dark theme)
                border.width: cell.checked ? 2 : 1
                border.color: cell.checked ? Theme.accent : Theme.borderSubtle

                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: DrawingBackdrops.pageUrl(dialog.path, cell.page)
                    sourceSize: Qt.size(Math.round(width * 1.5), Math.round(height * 1.5))
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                }
                Rectangle {
                    visible: cell.checked
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.space1
                    width: Theme.iconSize + 6
                    height: width
                    radius: width / 2
                    color: Theme.accentSolid
                    BIcon {
                        anchors.centerIn: parent
                        name: "check"
                        size: Theme.iconSizeSm
                        color: Theme.fgOnAccent
                    }
                }
            }
            BText {
                id: pageLabel
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space1
                text: cell.page
                variant: cell.checked ? "label" : "caption"
            }
            TapHandler {
                onTapped: dialog.toggle(cell.page)
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.job !== 0
        spacing: Theme.space1
        BText {
            objectName: "drawingBackdropProgress"
            Layout.fillWidth: true
            variant: "muted"
            text: qsTr("Page %1 of %2…").arg(Math.min(dialog.done + 1, dialog.total)).arg(dialog.total)
        }
        BProgressBar {
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, dialog.total)
            value: dialog.done
        }
    }
}
