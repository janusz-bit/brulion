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
