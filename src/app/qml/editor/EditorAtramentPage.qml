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
    // Code blocks drawn and edited by Atrament (CodeBlockView's look), with its line numbers when
    // the device's setting or the note's `line-numbers` asks for them.
    drawsCode: true
    readonly property bool codeLineNumbers: editor && editor.controller && editor.controller.codeLineNumbers >= 0
                                            ? editor.controller.codeLineNumbers > 0 : AppSettings.codeLineNumbers
    codeLook: ({
        background: Theme.bgSunken,
        border: Theme.borderSubtle,
        label: Theme.fgFaint,
        radius: Theme.radius,
        fontSize: Theme.textSm,
        lineHeight: Theme.lineHeightPx(Theme.textSm, 1.5),
        labelSize: Theme.textXs,
        lineNumbers: root.codeLineNumbers
    })
    ruleColor: Theme.border
    // Calculations drawn and edited by Atrament (CalcBlockView's look).
    drawsCalc: true
    calcLook: ({
        background: Theme.bgSunken,
        frame: Theme.alpha(Theme.focusRing, 0.5),
        separator: Theme.borderSubtle,
        rule: Theme.borderStrong,
        label: Theme.fgFaint,
        muted: Theme.fgMuted,
        error: Theme.dangerText,
        radius: Theme.radius,
        labelSize: Theme.textXs,
        errorSize: Theme.textSm,
        uiFamily: Theme.fontUi,
        title: qsTr("Calculation")
    })
    // Charts, Mermaid diagrams and math formulas drawn and edited by Atrament (drawsDiagrams,
    // drawsMath: on), in ChartBlockView's, MermaidBlockView's and MathBlockView's look.
    diagramLook: ({
        panel: Theme.bgSunken,
        panelBorder: Theme.borderSubtle,
        editingBorder: Theme.alpha(Theme.focusRing, 0.5),
        label: Theme.fgFaint,
        text: Theme.fg,
        muted: Theme.fgMuted,
        border: Theme.border,
        accent: Theme.accent,
        danger: Theme.dangerText,
        dark: Theme.dark,
        radius: Theme.radius,
        framedPad: Theme.space2,
        codeSize: Theme.textSm,
        codeLineHeight: Theme.lineHeightPx(Theme.textSm, 1.5),
        labelSize: Theme.textXs,
        chartFontSize: Theme.textSm,
        mermaidFontSize: Theme.textSm + 1,
        family: Theme.fontUi,
        english: editor !== null && editor.controller !== null && editor.controller.locale === "en"
    })
    // Tables drawn and edited by Atrament (TableBlockView's look); a table showing its filter row
    // stays the QML view's.
    drawsTables: true
    tableLook: ({
        background: Theme.bgElevated,
        header: Theme.bgSunken,
        border: Theme.border,
        borderStrong: Theme.borderStrong,
        borderSubtle: Theme.borderSubtle,
        focusRing: Theme.focusRing,
        formula: Theme.alpha(Theme.info, Theme.dark ? 0.07 : 0.06),
        error: Theme.dangerText,
        input: Theme.bgElevated2,
        hover: Theme.bgHover,
        fontFamily: Theme.fontUi,
        monoFamily: Theme.fontMono,
        fontSize: Theme.textBase,
        lineHeight: Theme.lineHeightPx(Theme.textBase, 1.4),
        rowHeight: Theme.touchUi ? 40 : 32,
        cellPadding: Theme.space3,
        radius: Theme.radius,
        buttonHeight: Theme.controlHeightSm,
        buttonTextSize: Theme.textSm
    })
    // A click on a cell edits it (Shift / Ctrl+click and the touch selection mode select blocks).
    onTableCellTapped: (row, tableRow, column, cursor, modifiers) => {
        if (!root.editor || root.editor.blockTappedWith(row, modifiers) || root.editor.readOnlyRefuses())
            return
        root.editTableCell(row, tableRow, column, cursor)
    }
    // A link of a cell (TableBlockView's): a date chip opens the date dialog, a reminder, recurrence or
    // priority chip edits the cell (its menu is the right button's), any other link opens.
    onTableLinkClicked: (row, tableRow, column, link, x, y) => {
        if (!root.editor || root.editor.openCellTokenDialog(row, tableRow, column, link))
            return
        if (Js.startsWith(link, "reminder:") || root.editor.editsOnClick(link))
            root.editTableCell(row, tableRow, column, -1)
        else
            root.editor.activateLink(row, link)
    }
    onTableCellMenuRequested: (row, tableRow, column, rowCount, columnCount, align, sceneX, sceneY) => {
        if (root.editor)
            root.editor.openCellBlockMenuAt(row, Qt.point(sceneX, sceneY), tableRow, column, rowCount, columnCount, align)
    }
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
    // The edited text's spell marks (and the drawn rows' with SpellCheck.wholePage), its Vim keys.
    spellCheck: SpellCheck
    spellColor: Theme.danger
    vimSession: editor ? editor.vimSession : null
    onSpellingMenuRequested: (row, edited, spelling, x, y) => {
        if (root.editor)
            root.editor.openAtramentSpellingMenu(row, edited, spelling, root, x, y)
    }
    // A right-click in the edited text: the editor's text menu acting on this field (Cut, Copy,
    // Paste, Highlight…, the AI, Select all; a misspelled word's corrections first).
    onTextMenuRequested: (row, spelling, x, y) => {
        if (root.editor)
            root.editor.openAtramentTextMenu(row, spelling, root, x, y)
    }
    /// The text menu's "Highlight…" and "Block…" once it has closed: the colours, or the block's
    /// menu, under the edited text's caret (BlockEditor.openAfterTextMenu's).
    function openMenuAtCaret(highlight: bool): void {
        if (!root.editor || root.editRow < 0)
            return
        const caret = root.editCaretRect
        const x = caret.x
        const bottom = caret.y + caret.height
        if (highlight)
            root.editor.openHighlightMenu(null, root, x, bottom)
        else
            root.editor.openBlockMenu(root.editRow, root, x, bottom)
    }
    // A link's or a chip's menu inside an embedded block or a query result (TransclusionView's,
    // QueryBlockView's): the token's menu in its own file, else the link's, whose "Open" follows
    // the link their way (onLinkMenuOpenRequested below).
    property int menuRow: -1
    /// A query result's item (−1: an embedded block's link).
    property int menuItem: -1
    property string menuPath: ""
    property int menuLine: -1
    property string menuRaw: ""
    function openPartLinkMenu(row: int, item: int, link: string, path: string, line: int, raw: string, x: real,
                              y: real): void {
        if (!root.editor || root.editor.openTokenMenuIn(path, line, raw, link, root, x, y))
            return
        root.menuRow = row
        root.menuItem = item
        root.menuPath = path
        root.menuLine = line
        root.menuRaw = raw
        root.editor.openLinkMenu(link, path, row, root, root, x, y)
    }
    onEmbeddedLinkMenuRequested: (row, link, path, line, raw, x, y) => root.openPartLinkMenu(row, -1, link, path, line, raw, x, y)
    onQueryLinkMenuRequested: (row, item, link, path, line, raw, x, y) => root.openPartLinkMenu(row, item, link, path, line, raw, x, y)
    // A reminder chip of the edited text: its dialog, or (a right-click, a finger held on it) its menu.
    onReminderChipActivated: (row, from, to, menu, x, y) => {
        if (!root.editor)
            return
        if (menu)
            root.editor.openReminderMenuAt(row, from, to, root, x, y)
        else
            root.editor.editReminderInText(row, from, to)
    }
    onVimKeyHandled: {
        if (root.editor)
            root.editor.afterVimKey()
    }
    /// The touch toolbar's highlight (BlockEditor.applyHighlight for the block edited here): the
    /// selection highlighted in `color` (a palette name; "" removes), one undo step, the
    /// highlighted text selected again.
    function applyHighlight(color: string): void {
        if (!root.editor || !root.editor.controller || root.editRow < 0)
            return
        root.commitComposition()
        const row = root.editRow
        highlighted.map = root.editor.controller.setHighlight(row, root.editSelectionStart, root.editSelectionEnd, color)
        if (!highlighted.changed()) {
            root.editor.highlightNotice(color, highlighted.refused())
            return
        }
        const from = highlighted.from()
        const to = highlighted.to()
        root.selectInEdited(row, from, to)
    }
    /// PageController.setHighlight's result.
    MapHolder {
        id: highlighted
        function changed(): bool { return highlighted.map.changed === true }
        function refused(): bool { return highlighted.map.refused === true }
        function from(): int { return highlighted.map.from }
        function to(): int { return highlighted.map.to }
    }
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
    // A chip's or a link's own menu (a right-click, a finger held on it).
    onLinkMenuRequested: (row, link, x, y) => {
        if (root.editor && !root.editor.openDateMenu(row, link, root, x, y))
            root.editor.openBlockLinkMenu(row, link, root, x, y)
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
        function onLinkMenuOpenRequested(requester: Item, link: string, newPane: bool) {
            if (requester !== root || !root.editor || !root.editor.controller)
                return
            if (root.menuItem >= 0) {
                queryAnswer.map = root.activateQueryLink(root.menuRow, root.menuItem, link, newPane)
                root.followQueryAnswer()
            } else if (newPane) {
                root.editor.controller.activateLink(root.menuRow, link, true)
            } else {
                root.activateEmbedded(root.menuRow, link, root.menuPath, root.menuLine, root.menuRaw)
            }
        }
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
    // ---- The block selection (docs/ATRAMENT.md M2 steps 3 and 4): its keys are the page's (it keeps
    // the keyboard; the editor's own parts through BlockEditor.atramentSelectionCommand), the touch
    // selection mode's grips and its "One by one", the toolbar a drag scrolls above.
    selectionGrips: editor ? editor.selectionGrips : false
    selectionPicking: editor ? editor.pickMode : false
    dragBottomInset: editor && editor.toolbarVisible ? editor.toolbarHeight : 0
    onSelectionCommand: (command, row) => {
        if (!root.editor || !root.editor.controller)
            return
        // Enter on a selected table: its delegate takes the keys (its first cell).
        if (command === "edit" && root.editor.controller.blocks.viewAt(row) === "table") {
            root.editor.selectionMode = false
            const item = root.rowItem(row) as BlockDelegate
            if (item)
                item.focusContent()
            root.acceptSelectionCommand()
            return
        }
        if (root.editor.atramentSelectionCommand(command, row))
            root.acceptSelectionCommand()
    }
    // The selection's keys win over application-wide shortcuts while the page has the keyboard.
    Keys.onShortcutOverride: (event) => {
        const key = event.key
        const modifiers = event.modifiers
        if (!root.editor || !root.editor.controller || root.editor.controller.editingRow >= 0
                || root.editor.controller.selectedRow < 0 || !(modifiers & Qt.ControlModifier))
            return
        switch (key) {
        case Qt.Key_A: case Qt.Key_C: case Qt.Key_X: case Qt.Key_V: case Qt.Key_Z: case Qt.Key_Y:
        case Qt.Key_Return: case Qt.Key_Enter:
            event.accepted = true
        }
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
