pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SearchView — route "search" ({query?, folder?}): full-text search over every note and the
// notebook's PDFs, documents (DOCX, PPTX, ODT, EPUB) and pictures (SearchResultModel: folded words — "zolw" finds "Żółw" —, prefix of
// the last word, fuzzy, titles first; attachments by their text layer, their XML and OCR, docs/UI.md
// "Searching attachments") with the matched words highlighted in titles and excerpts, filters (All /
// Pages / Journals / Attachments, the query's own in:pdf, in:pictures…, a folder: the route's — the
// sidebar's "Search in folder", any depth — or a top-level one from the menu) and the recent
// searches when the field is empty. A note's result opens its page at the matching line; a PDF's
// opens the PDF viewer at its page with the hit highlighted (route open "search"), a picture's the
// picture with the places of the words framed, a document's its file (read-only, "Open in another
// app"; its row says the slide or part the words are in). While attachments are being read, a line under the
// filters says so, with Pause / Resume (Workspace.attachments). With search by meaning on
// (Workspace.semantic, docs/UI.md "Search by meaning") the notes closest in meaning come in too —
// those without the words typed marked "Close in meaning" —, and while the notes are read by
// meaning a line says how far ("Reading notes by meaning: 120 of 900"). The query and the folder
// are kept in the route (back from a page returns to the results; a folder renamed or moved is
// followed).
//
// Keys: the field has the focus whenever the view is shown, its query selected (on phones the
// keyboard comes up); Up/Down move through the results, Enter opens (Ctrl+Enter in
// the side panel), Escape clears.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    property list<string> recent: []
    /// No highlighted matches (a daily note's title).
    readonly property list<var> noMatches: []

    readonly property string query: Js.trim(field.text)

    function loadRecent(): void {
        recent = AppSettings.recentSearches()
    }
    function remember(q: string): void {
        if (Js.trim(q) === "")
            return
        recent = AppSettings.rememberSearch(q)
    }
    function clearRecent(): void {
        recent = []
        AppSettings.clearRecentSearches()
    }
    MapHolder { id: resultInfo }
    /// Open result `row`: here, in the side panel (`newPane`) or in `target` ("tab": a new tab). A
    /// PDF opens at its page with the search typed (the hit highlighted), a picture with its words
    /// framed.
    function openResult(row: int, newPane: bool, target: string): void {
        // { path, line, kind, page, find, … }
        resultInfo.map = results.get(row)
        const path = "" + resultInfo.map.path
        if (resultInfo.map.path === undefined || path === "")
            return
        remember(field.text)
        const pane = newPane && !Theme.narrow
        const kind = AppController.routeText(resultInfo.map, "kind")
        const find = AppController.routeText(resultInfo.map, "find")
        OpenIn.begin(target)
        if (kind === "pdf")
            AppController.navigate({ view: "page", path: path, pdfPage: AppController.routeInt(resultInfo.map, "page", 1),
                                     open: "search", search: find }, pane)
        else if (kind === "picture" || kind === "document" || kind === "presentation")
            // (A document has no viewer of its own: its file, read-only, with "Open in another app".)
            AppController.navigate({ view: "page", path: path, search: find }, pane)
        else
            AppController.openPage(path, resultInfo.map.line, pane)
        OpenIn.end()
    }
    /// What the attachments' reading is doing, for the line under the filters ("" when nothing).
    readonly property AttachmentIndexer attachments: Workspace.attachments
    readonly property string attachmentsStatus: {
        if (!root.attachments.enabled)
            return ""
        switch (root.attachments.state) {
        case "reading":
            return root.attachments.filesTotal > 1
                   ? qsTr("Reading attachments… %1 of %2").arg(root.attachments.filesDone).arg(root.attachments.filesTotal)
                   : qsTr("Reading attachments…")
        case "recognizing":
            return root.attachments.currentPages > 1
                   ? qsTr("Recognizing text: page %1 of %2 in “%3”").arg(root.attachments.currentPage)
                         .arg(root.attachments.currentPages).arg(root.baseName(root.attachments.currentFile))
                   : qsTr("Recognizing text in “%1”").arg(root.baseName(root.attachments.currentFile))
        case "waiting":
            return qsTr("Reading attachments waits for the sync to end.")
        case "paused":
            return qsTr("Reading attachments is paused: they are found only as far as they were read.")
        default:
            return ""
        }
    }
    /// Search by meaning: the notes being embedded, for the line under the filters ("" when nothing).
    readonly property SemanticSearch semantic: Workspace.semantic
    readonly property string semanticStatus: {
        switch (root.semantic.state) {
        case "indexing":
            return qsTr("Reading notes by meaning: %1 of %2").arg(root.semantic.notesDone).arg(root.semantic.notesTotal)
        case "failed":
            return qsTr("Search by meaning stopped: %1").arg(root.semantic.error)
        default:
            return ""
        }
    }
    function baseName(path: string): string {
        const i = Js.lastIndexOf(path, "/")
        return i < 0 ? path : Js.sliceFrom(path, i + 1)
    }
    /// Open the current result (the side panel with `newPane`).
    function openCurrent(newPane: bool): void {
        if (results.count > 0)
            openResult(Math.max(0, list.currentIndex), newPane, "")
    }
    function move(delta: int): void {
        if (results.count === 0)
            return
        list.currentIndex = Math.max(0, Math.min(results.count - 1, list.currentIndex + delta))
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }
    function applyQuery(q: string): void {
        field.text = q
        field.cursorPosition = q.length
        results.query = q
    }
    /// The route keeps the query and the folder (back returns to them; folder moves follow).
    function syncRoute(): void {
        if (AppController.routeText(AppController.currentRoute, "view") !== "search")
            return
        // (Empty parameters are left out of the route.)
        AppController.replaceRoute({ view: "search", query: query !== "" ? field.text : "",
                                     folder: results.folder })
    }
    function setFolder(folder: string): void {
        results.folder = folder
        syncRoute()
    }

    /// Take the route's query and folder (when they differ from the shown ones).
    function followRoute(): void {
        if (Js.hasKey(route, "query") && AppController.routeText(route, "query") !== field.text)
            applyQuery(AppController.routeText(route, "query"))
        const folder = AppController.routeText(route, "folder")
        if (folder !== results.folder)
            results.folder = folder
    }

    focus: true
    Component.onCompleted: {
        loadRecent()
        followRoute()
        if (visible)
            takeFocus()
    }
    /// The keyboard to the query, selected (typing replaces it), and on touch the on-screen
    /// keyboard up: whenever the view is shown — made shown, shown the first time after being
    /// built ahead, or shown again (kept while other views were).
    function takeFocus(): void {
        field.forceActiveFocus()
        field.selectAll()
        if (Theme.touchUi)
            DeviceInfo.showInputMethod()
    }
    onVisibleChanged: if (visible) takeFocus()
    onRouteChanged: followRoute()

    SearchResultModel {
        id: results
        workspace: Workspace
        // Hidden (kept by ShellRouteView): edits do not run the search again until it is shown.
        active: root.visible
        // A new query starts at its first result — also once the rows came (the old query's current
        // row kept by the diff would otherwise stay current).
        property bool fresh: false
        onQueryChanged: {
            list.currentIndex = 0
            fresh = true
        }
        onRefreshed: {
            if (fresh)
                list.currentIndex = 0
            fresh = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space6
        spacing: Theme.space3

        BSearchField {
            id: field
            objectName: "searchField"
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            placeholderText: results.folder !== "" ? qsTr("Search in %1…").arg(results.folder) : qsTr("Search in all notes…")
            accessibleName: qsTr("Search")
            debounce: 180
            onSearch: (q) => {
                results.query = q
                // Back from a result comes back to these results.
                if (AppController.routeText(AppController.currentRoute, "query") !== q)
                    root.syncRoute()
            }
            onAccepted: root.openCurrent(false)
            Keys.onDownPressed: root.move(1)
            Keys.onUpPressed: root.move(-1)
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    root.openCurrent(true)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    root.move(8)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    root.move(-8)
                    event.accepted = true
                }
            }
        }

        // ---- Filters ---------------------------------------------------------------------------------
        // One row; on compact windows two — the scope alone across the width, then Replace…, the
        // folder and the count —, so the row never makes the whole view wider than a phone (the
        // scope with its icons and the buttons needed ~426 px in Polish).
        GridLayout {
            visible: root.query !== "" || results.folder !== ""
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space2
            rowSpacing: Theme.space2
            BSegmented {
                Layout.fillWidth: Theme.compact
                fill: Theme.compact
                // (Without icons on compact windows: four choices across a phone.)
                model: [{ value: "all", text: qsTr("All"), icon: Theme.compact ? "" : "files" },
                        { value: "pages", text: qsTr("Pages"), icon: Theme.compact ? "" : "file-text" },
                        { value: "journals", text: qsTr("Journals"), icon: Theme.compact ? "" : "calendar-days" },
                        { value: "attachments", text: qsTr("Attachments"), icon: Theme.compact ? "" : "paperclip" }]
                currentValue: results.scope
                size: "small"
                accessibleName: qsTr("Search in")
                onActivated: (value) => results.scope = value
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BButton {
                    objectName: "searchReplace"
                    visible: !AppController.inDocsVault
                    size: "small"
                    variant: "ghost"
                    iconName: "pencil"
                    text: Theme.compact ? "" : qsTr("Replace…")
                    Accessible.name: qsTr("Replace in notebook")
                    onClicked: AppController.navigate({ view: "replace", query: root.query })
                }
                BButton {
                    id: folderButton
                    visible: results.folders.length > 1 || results.folder !== ""
                    Layout.maximumWidth: Theme.compact ? 120 : 260
                    size: "small"
                    variant: results.folder !== "" ? "secondary" : "ghost"
                    iconName: "folder"
                    text: results.folder !== "" ? results.folder : Theme.compact ? "" : qsTr("Folder")
                    Accessible.name: qsTr("Folder: %1").arg(results.folder !== "" ? results.folder : qsTr("all"))
                    onClicked: folderMenu.popup(folderButton, 0, folderButton.height)
                }
                Item { Layout.fillWidth: true }
                BText {
                    objectName: "searchCount"
                    text: results.count < results.totalCount ? qsTr("%1 of %n result(s)", "", results.totalCount).arg(results.count)
                                                             : qsTr("%n result(s)", "", results.count)
                    variant: "caption"
                    color: Theme.fgFaint
                }
            }
        }

        // ---- Attachments being read ---------------------------------------------------------------------
        // (Their hits come in as they are read: the user knows the list may grow, and can pause it.)
        RowLayout {
            objectName: "searchAttachmentsProgress"
            visible: root.query !== "" && root.attachmentsStatus !== "" && results.scope !== "pages"
                     && results.scope !== "journals"
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            spacing: Theme.space2
            BSpinner {
                visible: root.attachments.state === "reading" || root.attachments.state === "recognizing"
                running: visible
                size: Theme.iconSizeSm
            }
            BIcon {
                visible: !(root.attachments.state === "reading" || root.attachments.state === "recognizing")
                name: root.attachments.state === "paused" ? "pause" : "paperclip"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            BText {
                objectName: "searchAttachmentsStatus"
                Layout.fillWidth: true
                text: root.attachmentsStatus
                variant: "caption"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
            BButton {
                objectName: "searchAttachmentsPause"
                visible: root.attachments.state !== "waiting"
                size: "small"
                variant: "ghost"
                iconName: root.attachments.paused ? "play" : "pause"
                text: Theme.compact ? "" : root.attachments.paused ? qsTr("Resume") : qsTr("Pause")
                Accessible.name: root.attachments.paused ? qsTr("Resume reading attachments") : qsTr("Pause reading attachments")
                onClicked: root.attachments.paused = !root.attachments.paused
            }
        }

        // ---- Notes being read by meaning ----------------------------------------------------------------
        // (Until they are, the notes not read yet are found by their words alone.)
        RowLayout {
            objectName: "searchMeaningProgress"
            visible: root.query !== "" && root.semanticStatus !== "" && results.scope !== "attachments"
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            spacing: Theme.space2
            BSpinner {
                visible: root.semantic.state === "indexing"
                running: visible
                size: Theme.iconSizeSm
            }
            BIcon {
                visible: root.semantic.state !== "indexing"
                name: "sparkles"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            BText {
                objectName: "searchMeaningStatus"
                Layout.fillWidth: true
                text: root.semanticStatus
                variant: "caption"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }

        // ---- Results -----------------------------------------------------------------------------------
        ListView {
            id: list
            TouchFlick.enabled: true
            visible: root.query !== ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: results
            currentIndex: 0
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: Theme.space6
            highlightMoveDuration: 0
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Search results")

            delegate: T.ItemDelegate {
                id: hit
                required property int index
                required property string title
                required property string folder
                required property string excerpt
                required property list<var> excerptMatches
                required property list<var> titleMatches
                required property string journalDate
                required property string kind
                required property int page
                required property int pageCount
                required property int matchedPages
                required property bool ocr
                required property bool byMeaning
                readonly property bool journal: hit.journalDate !== ""
                readonly property bool attachment: hit.kind !== "note"
                readonly property string heading: hit.journal ? DatesHelper.formatLong(hit.journalDate) : hit.title
                /// An attachment's page and how many more pages match ("" for a note).
                readonly property string pageInfo: {
                    if (!hit.attachment)
                        return ""
                    if (hit.matchedPages === 0)
                        return qsTr("Found by its name")
                    if (hit.kind === "document" || hit.kind === "presentation") {
                        const slides = hit.kind === "presentation"
                        const place = slides ? qsTr("Slide %1 of %2").arg(hit.page).arg(hit.pageCount)
                                             : qsTr("Part %1 of %2").arg(hit.page).arg(hit.pageCount)
                        if (hit.matchedPages <= 1)
                            return place
                        return place + " · " + (slides ? qsTr("%n more slide(s) match", "", hit.matchedPages - 1)
                                                       : qsTr("%n more part(s) match", "", hit.matchedPages - 1))
                    }
                    const where = hit.kind === "pdf" ? qsTr("Page %1 of %2").arg(hit.page).arg(hit.pageCount) : qsTr("Picture")
                    return hit.matchedPages > 1 ? where + " · " + qsTr("%n more page(s) match", "", hit.matchedPages - 1) : where
                }
                // On the highlighted row the accent is too light: the text colour (the matches
                // stay bold).
                readonly property color matchColor: hit.highlighted ? Theme.fg : Theme.accentText

                // Full width (ListView places delegates at x 0); the row itself is inset.
                width: list.width
                implicitHeight: hitColumn.implicitHeight + 2 * Theme.space2 + 2
                padding: Theme.space2
                leftInset: root.sideMargin - Theme.space2
                rightInset: root.sideMargin - Theme.space2
                leftPadding: root.sideMargin + Theme.space1
                rightPadding: root.sideMargin + Theme.space1
                hoverEnabled: !Theme.touchUi
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: hit.heading + (hit.pageInfo !== "" ? ", " + hit.pageInfo : "")
                                 + (hit.byMeaning ? ", " + qsTr("Close in meaning") : "") + ": " + hit.excerpt
                // Ctrl+click: a new tab (OpenIn).
                onClicked: root.openResult(hit.index, false, OpenIn.clickTarget(Qt.LeftButton, EditorInput.keyboardModifiers()))
                TapHandler {
                    // A middle click: in a new tab.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton
                    enabled: OpenIn.tabs
                    onTapped: root.openResult(hit.index, false, "tab")
                }

                background: Rectangle {
                    visible: hit.down || hit.highlighted || hit.hovered
                    radius: Theme.radius
                    color: hit.down ? Theme.bgPressed : hit.highlighted ? Theme.bgSelected : Theme.bgHover
                }
                contentItem: ColumnLayout {
                    id: hitColumn
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        BIcon {
                            name: hit.journal ? "calendar-days" : hit.kind === "pdf" ? "file" : hit.kind === "picture" ? "image" : "file-text"
                            size: Theme.iconSizeSm
                            color: Theme.fgMuted
                        }
                        ContentHighlightText {
                            Layout.fillWidth: true
                            source: hit.heading
                            positions: hit.journal ? root.noMatches : hit.titleMatches
                            highlightColor: hit.matchColor
                            // The snippet's size: weight and colour set the title apart.
                            font.weight: Theme.weightSemiBold
                            font.pixelSize: Theme.textBase
                        }
                        BText {
                            visible: hit.folder !== "" && !hit.journal
                            Layout.maximumWidth: hitColumn.width * 0.4
                            text: hit.folder
                            variant: "caption"
                            // (fgFaint is AA on the page, not on the highlighted row.)
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                            elide: Text.ElideMiddle
                        }
                    }
                    ContentHighlightText {
                        visible: hit.excerpt !== ""
                        Layout.fillWidth: true
                        source: hit.excerpt
                        positions: hit.excerptMatches
                        highlightColor: hit.matchColor
                        color: Theme.fgMuted
                        font.pixelSize: Theme.textBase
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                    }
                    // A note found by meaning alone: none of the words typed are in it.
                    RowLayout {
                        objectName: "searchHitMeaning"
                        visible: hit.byMeaning
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BIcon {
                            name: "sparkles"
                            size: Theme.iconSizeSm
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                        }
                        BText {
                            Layout.fillWidth: true
                            text: qsTr("Close in meaning")
                            variant: "caption"
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                            elide: Text.ElideRight
                        }
                    }
                    // An attachment's page, its other pages, text read by OCR.
                    RowLayout {
                        objectName: "searchHitPage"
                        visible: hit.attachment
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BText {
                            Layout.fillWidth: !hit.ocr
                            text: hit.pageInfo
                            variant: "caption"
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                            elide: Text.ElideRight
                        }
                        BIcon {
                            visible: hit.ocr
                            Layout.leftMargin: Theme.space2
                            name: "scan-text"
                            size: Theme.iconSizeSm
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                        }
                        BText {
                            objectName: "searchHitOcr"
                            visible: hit.ocr
                            Layout.fillWidth: true
                            text: qsTr("Recognized text")
                            variant: "caption"
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: results.count === 0 && root.query !== "" && Workspace.ready
                iconName: "search"
                title: qsTr("Nothing found for “%1”").arg(root.query)
                description: results.totalCount > 0 ? qsTr("The filters hide %n result(s).", "", results.totalCount)
                             : results.queryFiltered ? qsTr("Nothing of that kind holds these words: try without “in:”.")
                             : qsTr("Try fewer or shorter words. Polish letters are optional: “zolw” finds “Żółw”.")
                actionText: results.totalCount > 0 ? qsTr("Show all results") : ""
                onActionTriggered: {
                    results.scope = "all"
                    root.setFolder("")
                }
            }
        }

        // ---- Nothing typed yet: recent searches ----------------------------------------------------------
        Flickable {
            visible: root.query === ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: idle.implicitHeight + Theme.space8
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: idle
                x: root.sideMargin
                width: parent.width - 2 * root.sideMargin
                spacing: Theme.space2

                RowLayout {
                    visible: root.recent.length > 0
                    Layout.fillWidth: true
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Recent searches")
                        variant: "label"
                        Accessible.role: Accessible.Heading
                    }
                    BButton {
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Clear")
                        onClicked: root.clearRecent()
                    }
                }
                Repeater {
                    model: root.recent
                    delegate: BListItem {
                        required property string modelData
                        Layout.fillWidth: true
                        dense: true
                        iconName: "history"
                        text: modelData
                        onClicked: {
                            root.applyQuery(modelData)
                            field.forceActiveFocus()
                        }
                    }
                }
                BEmptyState {
                    visible: root.recent.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.space12
                    iconName: "search"
                    title: qsTr("Search your notes")
                    description: qsTr("Words anywhere in titles and text, and in PDFs, documents and pictures. Polish letters are optional: “zolw” finds “Żółw”; the last word may be the start of a word. “in:pdf”, “in:documents” or “in:notes” narrows the search.")
                }
            }
        }
    }

    BMenu {
        id: folderMenu
        BMenuItem {
            text: qsTr("All folders")
            iconName: "files"
            checkable: true
            checked: results.folder === ""
            onTriggered: root.setFolder("")
        }
        BMenuSeparator {}
    }
    Instantiator {
        model: results.folders
        // results.folders: [{ folder, count }]
        delegate: BMenuItem {
            required property string folder
            required property int count
            text: (folder !== "" ? folder : qsTr("Notebook root")) + "  (" + count + ")"
            iconName: "folder"
            checkable: true
            checked: results.folder === folder && folder !== ""
            enabled: folder !== ""
            onTriggered: root.setFolder(folder)
        }
        onObjectAdded: (index, object) => folderMenu.insertItem(index + 2, object as Item)
        onObjectRemoved: (index, object) => folderMenu.removeItem(object as Item)
    }
}
