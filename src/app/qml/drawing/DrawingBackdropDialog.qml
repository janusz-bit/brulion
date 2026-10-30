pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// DrawingBackdropDialog — a PDF's pages as drawings' backdrops (docs/UI.md "Drawing editor"): the
// file's name and how many pages it has, the pages as thumbnails (DrawingBackdrops.pageUrl: the
// image provider "pdfpage", drawn when they scroll into view; the cells shaped as the first page —
// wide for slides) — a tap chooses or drops one; with the keyboard the arrows move between them
// and Space chooses (in "single" the arrows choose, as in a group of radio buttons) —,
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
    /// The first page's height / width (the thumbnails' cells; clamped): portrait A4 1.41, a slide
    /// 0.56.
    property real pageAspect: 1.3
    /// The preparing job (0 none) and its progress.
    property int job: 0
    property int done: 0
    property int total: 0

    readonly property bool single: mode === "single"
    readonly property string fileName: path.slice(path.lastIndexOf("/") + 1)
    readonly property int maxPages: 60

    /// The prepared backdrops ({ bytes, mimeType, width, height, path, page, name, error } each;
    /// "insert": the drawings' files in `svg` instead of `bytes`, DrawingBackdrops.prepare()).
    signal prepared(var items)

    function openFor(vaultPath, openMode, page) {
        dialog.path = vaultPath
        dialog.mode = openMode
        const info = DrawingBackdrops.pdfInfo(vaultPath)
        dialog.pageCount = Math.max(0, info.pages)
        dialog.error = info.error
        dialog.pageAspect = info.width > 0 && info.height > 0 ? Math.min(2, Math.max(0.35, info.height / info.width)) : 1.3
        dialog.capped = false
        dialog.job = 0
        dialog.done = 0
        dialog.total = 0
        const first = Math.min(Math.max(1, page || 1), Math.max(1, dialog.pageCount))
        dialog.setChosen(dialog.pageCount === 0 ? [] : dialog.single || dialog.pageCount === 1 ? [first] : [])
        grid.keyboardUsed = false
        grid.currentIndex = dialog.pageCount > 0 ? first - 1 : -1
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
    // One page to choose: the arrows at once (insert: the field "Strony" first, Tab to the pages).
    initialFocusItem: single && error === "" && pageCount > 0 ? grid : null
    onAcceptRequested: {
        // Prepared first (the dialog stays, with its progress); prepared() and the close follow.
        dialog.done = 0
        dialog.total = dialog.chosen.length
        // (Inserting: the drawings' files made by the job too, off the GUI thread.)
        dialog.job = DrawingBackdrops.prepare(dialog.path, dialog.chosen, !dialog.single)
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
        // (From the rows, not contentHeight: GridView changes that while it lays out — a loop.)
        Layout.preferredHeight: Math.min(Math.ceil(count / columns) * cellHeight,
                                         Math.max(cellHeight, dialog.overlayHeight * 0.5))
        visible: dialog.error === "" && dialog.pageCount > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.error === "" ? dialog.pageCount : 0
        // Wide pages (slides) get wider cells, every cell the first page's shape.
        readonly property int columns: Math.max(2, Math.floor(width / ((Theme.touchUi ? 132 : 120) * (dialog.pageAspect < 1 ? 1.5 : 1))))
        readonly property int frameHeight: Math.round((cellWidth - 2 * Theme.space1 - 6) * dialog.pageAspect) + 6
        /// A key was used since the dialog opened (the focus ring shows; a tap hides it).
        property bool keyboardUsed: false
        cellWidth: Math.floor(width / columns)
        cellHeight: frameHeight + Theme.space1 + Theme.space2 + Math.ceil(Theme.textSm * 1.5)
        T.ScrollBar.vertical: BScrollBar {}
        activeFocusOnTab: visible
        keyNavigationEnabled: true
        currentIndex: -1
        onCurrentIndexChanged: {
            if (currentIndex < 0)
                return
            positionViewAtIndex(currentIndex, GridView.Contain)
            // One page: the arrows choose it (a group of radio buttons).
            if (dialog.single && activeFocus && dialog.job === 0)
                dialog.setChosen([currentIndex + 1])
        }
        Keys.onPressed: (event) => {
            grid.keyboardUsed = true
            if ((event.key === Qt.Key_Space || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                                 && event.modifiers === Qt.NoModifier)) && currentIndex >= 0) {
                dialog.toggle(currentIndex + 1)
                event.accepted = true
            }
        }

        delegate: Item {
            id: cell
            required property int index
            readonly property int page: index + 1
            readonly property bool checked: dialog.chosen.indexOf(page) >= 0
            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: dialog.single ? Accessible.RadioButton : Accessible.CheckBox
            Accessible.name: qsTr("Page %1").arg(cell.page)
            Accessible.checkable: true
            Accessible.checked: cell.checked

            Rectangle {
                id: frame
                x: Theme.space1
                y: Theme.space1
                width: parent.width - 2 * Theme.space1
                height: grid.frameHeight
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
                BFocusRing {
                    visible: grid.activeFocus && cell.GridView.isCurrentItem && (grid.keyboardUsed || !Theme.touchUi)
                    targetRadius: Theme.radiusSm
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
                onTapped: {
                    grid.keyboardUsed = false
                    grid.currentIndex = cell.index
                    dialog.toggle(cell.page)
                }
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
