pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EditorAtramentPage — a page's blocks drawn by Atrament (AppSettings.editorAtrament, Settings →
// Editor; docs/ATRAMENT.md "The block editor in Atrament (M2)", step 1 "Reading"). BlockEditor
// loads it by file name where Atrament is built (DeviceInfo.atrament) and shows it instead of its
// ListView's rows; the list stays, empty, as the holder of the keyboard (as for PageItemView).
// Text rows are Atrament's, drawn and edited (step 2: a click edits a block at the character under
// it, the editor's keys go to the controller); every other row a placeholder of its height.
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
}
