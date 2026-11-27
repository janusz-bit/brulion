pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EditorAtramentPage — a page's blocks drawn by Atrament (AppSettings.editorAtrament, Settings →
// Editor; docs/ATRAMENT.md "The block editor in Atrament (M2)", step 1 "Reading"). BlockEditor
// loads it by file name where Atrament is built (DeviceInfo.atrament) and shows it instead of its
// ListView's rows; the list stays, empty, as the holder of the keyboard (as for PageItemView).
// Text rows are Atrament's, drawn and edited (step 2: a click edits a block at the character under
// it, the editor's keys go to the controller); every other row (code, tables, math, pictures,
// queries, the properties…) is the editor's BlockDelegate placed over its place in the list, as are
// the page's header (the title, the first property's draft, the zoom's breadcrumb) and footer
// (docs/ATRAMENT.md "Rows Atrament does not draw yet").
AtramentView {
    id: root

    /// The editor as an Item (a file cannot give its own root type to a property in compiled code).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor

    pageModel: editor && editor.controller ? editor.controller.blocks : null
    pageController: editor ? editor.controller : null
    caretColor: Theme.accent
    selectionColor: Theme.selection
    placeholderText: qsTr("Type / for commands")
    editLabel: qsTr("Block text")
    highlighterPalette: editor ? editor.highlighterPalette : ({})
    // Code blocks drawn and edited by Atrament (CodeBlockView's look); line numbers stay the QML
    // view's: with them on, code blocks are its rows.
    drawsCode: !(editor && editor.controller && editor.controller.codeLineNumbers >= 0
                 ? editor.controller.codeLineNumbers > 0 : AppSettings.codeLineNumbers)
    codeLook: ({
        background: Theme.bgSunken,
        border: Theme.borderSubtle,
        label: Theme.fgFaint,
        radius: Theme.radius,
        fontSize: Theme.textSm,
        lineHeight: Theme.lineHeightPx(Theme.textSm, 1.5),
        labelSize: Theme.textXs
    })
    // Pictures, files, embedded blocks and query results drawn by Atrament (docs/ATRAMENT.md M2
    // step 5; drawsPictures, drawsFiles, drawsTransclusions, drawsQueries: on), in EmbedView's,
    // TransclusionView's and QueryBlockView's look. Pictures get their hairline frame and, in a
    // dark theme, their light backing by the note's own option, else the device's setting.
    readonly property int notePictureFrames: editor && editor.controller ? editor.controller.pictureFrames : -1
    readonly property int notePictureBacking: editor && editor.controller ? editor.controller.pictureBacking : -1
    mediaLook: ({
        frame: Theme.borderSubtle,
        border: Theme.border,
        borderStrong: Theme.borderStrong,
        faint: Theme.fgFaint,
        linkMissing: Theme.linkMissing,
        danger: Theme.dangerText,
        backing: Theme.pictureBacking,
        chipBackground: Theme.chipBg("neutral"),
        chipText: Theme.chipFg("neutral"),
        embedFill: Theme.alpha(Theme.bgSunken, Theme.dark ? 0.55 : 0.7),
        queryBackground: Theme.bgElevated,
        hover: Theme.bgHover,
        focusRing: Theme.focusRing,
        radius: Theme.radius,
        radiusSm: Theme.radiusSm,
        spacing: Theme.space2,
        smallSize: Theme.textSm,
        tinySize: Theme.textXs,
        bodySize: Theme.textEditor,
        h1Size: Theme.textLg,
        h2Size: Theme.textMd,
        headerHeight: Theme.touchUi ? 36 : 28,
        indentStep: Theme.compact ? 16 : 20,
        taskBoxWidth: editor ? editor.taskBoxWidth : 24,
        queryItems: Theme.touchUi ? 20 : 50,
        frames: notePictureFrames >= 0 ? notePictureFrames > 0 : AppSettings.pictureFrames,
        lightBacking: Theme.dark && (notePictureBacking >= 0 ? notePictureBacking > 0 : AppSettings.lightPictureBacking),
        uiFamily: Theme.fontUi
    })
    bandColor: Theme.bgSelected
    ringColor: Theme.focusRing
    bandRadius: Theme.radiusSm
    calloutInfo: Theme.info
    calloutSuccess: Theme.success
    calloutWarning: Theme.warning
    calloutDanger: Theme.marginRed
    calloutQuote: Theme.fgFaint
    calloutFill: Theme.dark ? 0.12 : 0.10
    calloutRadius: Theme.radius
    fontSize: editor ? editor.fontSize : 16
    lineHeight: editor ? editor.lineHeight : 24
    indentStep: editor ? editor.indentStep : 26
    gutter: editor ? editor.gutter : 16
    bulletColumn: editor ? editor.bulletColumn : 20
    documentMode: editor ? editor.documentMode : false
    listIndent: editor ? editor.listIndent : 27
    contentX: editor ? editor.contentX : 0
    contentWidth: editor ? editor.contentWidth : 0
    arrowColumn: editor ? editor.arrowColumn : 16
    touchUi: Theme.touchUi
    textColor: Theme.fg
    mutedColor: Theme.fgMuted
    linkColor: Theme.link
    guideColor: Theme.guide
    backgroundColor: Theme.bg
    view: "page"
    onLinkActivated: (row, link) => {
        if (root.editor)
            root.editor.activateLink(row, link)
    }
    // A click on a link of a block's text (BlockText's): a date chip opens the date dialog, a recurrence or
    // a priority chip its menu at the chip, any other link opens.
    onLinkClicked: (row, link, x, y) => {
        if (!root.editor || root.editor.openDateDialogAt(row, link))
            return
        if (!root.editor.editsOnClick(link) || !root.editor.openDateMenu(row, link, root, x, y))
            root.editor.activateLink(row, link)
    }
    onTaskToggled: row => {
        if (root.editor && root.editor.controller)
            root.editor.controller.toggleTask(row)
    }
    // Editing (docs/ATRAMENT.md M2, step 2): what the QML editor decides around Atrament's field.
    onEditRequested: (row, cursor, modifiers) => {
        if (!root.editor || root.editor.blockTappedWith(row, modifiers))
            return
        root.editor.startEditing(row, cursor)
    }
    onEscapePressed: {
        if (root.editor)
            root.editor.stopEditing(true)
    }
    onUndoRequested: redo => {
        if (root.editor)
            root.editor.undoStep(redo)
    }
    onFollowLinkRequested: (row, cursor, newPane) => {
        if (root.editor)
            root.editor.followLinkAtCursor(row, cursor, newPane && !Theme.narrow)
    }
    // The editor's suggestions (links, tags, slash commands, dates) follow the field's text and
    // caret; their keys come back while the list is open.
    suggestionsOpen: editor ? editor.suggestionsOpen : false
    onEditStateChanged: {
        if (!root.editor || root.editRow < 0)
            return
        root.editor.updateCompletionAt(root, root.editRow, root.editText, root.editCursor, root.editCaretRect)
        // A journal day (autoHeight) does not scroll: the journal list keeps the caret in view.
        if (root.editor.autoHeight && root.activeFocus)
            root.editor.ensureVisible(root, root.editCaretRect)
    }
    // A journal day is as tall as its blocks (BlockEditor.atramentContentHeight, ContentJournalDay).
    onContentHeightChanged: {
        if (root.editor)
            root.editor.atramentContentHeight = root.contentHeight
    }
    onBulletClicked: row => {
        if (root.editor && root.editor.controller && !root.editor.blockTapped(row))
            root.editor.controller.zoomInto(row)
    }
    onDragSelectRequested: (anchor, focus) => {
        if (!root.editor || !root.editor.controller)
            return
        root.editor.leaveEditingForSelection()
        root.editor.controller.selectRange(anchor, focus)
    }
    onRowLongPressed: row => {
        if (root.editor)
            root.editor.longPressText(row)
    }
    onBlockMenuRequested: (row, sceneX, sceneY) => {
        if (root.editor)
            root.editor.openBlockMenuAt(row, Qt.point(sceneX, sceneY))
    }
    onSelectionExtendRequested: (row, step) => {
        if (root.editor)
            root.editor.extendBlocksFrom(row, step)
    }
    onSelectAllBlocksRequested: {
        if (root.editor)
            root.editor.selectAllBlocks()
    }
    onEditingStarted: {
        if (root.editor && !root.editor.findKeepsKeyboard && !root.activeFocus)
            root.forceActiveFocus()
        root.selectFindMatch()
    }
    // The find bar: its matches tinted, its current match selected in the edited block.
    readonly property NoteFind find: editor ? editor.find : null
    findQuery: find ? find.appliedQuery : ""
    findMatchCase: find ? find.matchCase : false
    findMatchDiacritics: find ? find.matchDiacritics : false
    findWholeWord: find ? find.wholeWord : false
    findColor: editor ? editor.findHitColor : "transparent"
    function selectFindMatch(): void {
        if (root.editor && root.editor.findSelectionRow >= 0
                && root.selectInEdited(root.editor.findSelectionRow, root.editor.findSelectionFrom,
                                       root.editor.findSelectionTo))
            root.editor.clearFindSelection()
    }
    Connections {
        target: root.editor
        function onFindSelectionChanged() { root.selectFindMatch() }
    }
    // ---- Pictures, files, embedded blocks, queries (what EmbedView, TransclusionView and
    // QueryBlockView open)
    MapHolder { id: embedInfo }
    MapHolder { id: queryAnswer }
    MapHolder { id: locationAnswer }
    /// A click on a picture, a PDF or a file (EmbedView's clicked / drawingClicked / pdfClicked / webOpen).
    function openEmbed(row: int, embed: var): void {
        embedInfo.map = embed
        const web = embedInfo.map.web === true
        const drawing = embedInfo.map.drawing === true
        const pdf = embedInfo.map.pdf === true
        const path = "" + (embedInfo.map.path || "")
        const target = "" + (embedInfo.map.target || "")
        const page = embedInfo.map.page || 1
        if (!root.editor || !root.editor.controller)
            return
        if (web)
            root.editor.openWebPictureViewer(row, embedInfo.map)
        else if (drawing)
            root.editor.controller.openDrawing(path)
        else if (pdf)
            root.editor.controller.openPdf(row, path, page)
        else
            root.editor.controller.activateLink(row, "page:" + Js.encodeURIComponent(target), false)
    }
    onEmbedClicked: (row, embed) => root.openEmbed(row, embed)
    // A picture from the internet: its menu (WebPictureMenu) at the pointer.
    onEmbedMenuRequested: (row, embed, x, y) => {
        embedInfo.map = embed
        const target = "" + (embedInfo.map.target || "")
        const host = "" + (embedInfo.map.host || "")
        if (root.editor)
            root.editor.openWebPictureMenu(row, target, host, root, x, y)
    }
    /// A link of an embedded block's text (TransclusionView's tap and activate()): a date chip's
    /// dialog; a 🔁 or priority chip edits the block with the embed; tasks and reminders belong to
    /// the source block (its place opens); any other link opens.
    function activateEmbedded(row: int, link: string, path: string, line: int, raw: string): void {
        if (!root.editor || !root.editor.controller)
            return
        if (root.editor.openTokenDialogIn(path, line, raw, link))
            return
        if (root.editor.editsOnClick(link)) {
            root.editor.startEditing(row, -1)
            return
        }
        if (Js.startsWith(link, "task:") || Js.startsWith(link, "reminder:")) {
            if (path !== "") {
                locationAnswer.map = root.editor.controller.openLocation(path, line, false)
                if (locationAnswer.map.action === "scrollTo")
                    root.editor.revealRow(locationAnswer.map.row)
            }
            return
        }
        root.editor.activateLink(row, link)
    }
    onEmbeddedLinkActivated: (row, link, path, line, raw) => root.activateEmbedded(row, link, path, line, raw)
    /// What a query's results answered (QueryBlockView.followAnswer).
    function followQueryAnswer(): void {
        if (!root.editor)
            return
        if (queryAnswer.map.action === "scrollTo")
            root.editor.revealRow(queryAnswer.map.row)
        else if (queryAnswer.map.action === "notFound")
            root.editor.noticeNotFound(queryAnswer.map)
    }
    // A link of a query result: a date chip's dialog on its token, else the results follow it.
    onQueryLinkActivated: (row, item, link, path, line, raw) => {
        if (!root.editor || root.editor.openTokenDialogIn(path, line, raw, link))
            return
        queryAnswer.map = root.activateQueryLink(row, item, link)
        root.followQueryAnswer()
    }
    onQueryAnswered: (row, answer) => {
        queryAnswer.map = answer
        root.followQueryAnswer()
    }
    onSuggestionKeyPressed: key => {
        if (root.editor)
            root.editor.suggestionKey(key)
    }
    activeFocusOnTab: true
    Accessible.name: qsTr("Page")

    // ---- The rows Atrament does not draw yet, the header and the footer (QML items over the list)
    delegate: Component {
        BlockDelegate {
            editorItem: root.editor
        }
    }
    header: Column {
        width: root.width
        Loader {
            width: parent.width
            active: root.editor !== null && root.editor.header !== null
            sourceComponent: root.editor ? root.editor.header : null
        }
        // The first property of a page that shows none (BlockEditor.addProperty()): only the add row.
        Loader {
            objectName: "propertyDraft"
            x: root.editor ? root.editor.contentX : 0
            width: root.editor ? root.editor.contentWidth : 0
            active: root.editor !== null && root.editor.propertyDraft && root.editor.controller !== null
            visible: active
            sourceComponent: Rectangle {
                implicitHeight: draftPanel.implicitHeight + 2 * Theme.space1
                radius: Theme.radius
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
                // (PropertiesPanel, loaded from its file: docs/PERFORMANCE.md "QML files loaded when
                // first used".)
                Loader {
                    id: draftPanel
                    x: Theme.space1
                    y: Theme.space1
                    width: parent.width - 2 * Theme.space1
                    Component.onCompleted: {
                        root.editor._popupValues = { editorItem: root.editor, controller: root.editor.controller,
                            draft: true }
                        draftPanel.setSource(root.editor._propertiesSource, root.editor._popupValues)
                    }
                }
            }
        }
        Loader {
            x: root.editor ? root.editor.contentX : 0
            width: root.editor ? root.editor.contentWidth : 0
            active: root.editor !== null && root.editor.controller !== null && root.editor.controller.zoomBlock >= 0
            // An inactive Loader keeps the implicit height of the item it had: hidden, the Column
            // leaves no room for it after zooming out.
            visible: active
            sourceComponent: ZoomBreadcrumb {
                controller: root.editor.controller
            }
        }
    }
    // The room under the last block: a tap there appends an empty block and edits it.
    footer: Item {
        width: root.width
        height: root.editor ? root.editor.bottomSpace : 0
        TapHandler {
            onTapped: {
                // (Reading mode: a tap under the blocks, often the end of a scroll, does nothing.)
                if (!root.editor || !root.editor.controller || root.editor.selectionMode || root.editor.readOnly)
                    return
                const row = root.editor.controller.appendEmptyBlock()
                root.editor.startEditing(row, -1)
            }
        }
        HoverHandler {
            cursorShape: root.editor && root.editor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
        }
    }
}
