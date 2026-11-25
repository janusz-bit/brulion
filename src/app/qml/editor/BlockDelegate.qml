pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockDelegate — one block of the BlockEditor (one row of BlockListModel): indentation with
// thin vertical guide lines (one per ancestor level, continuous across rows), the fold arrow
// (on hover; always shown when folded or on touch), the round bullet of list items (click:
// zoom into the block; right click / long press: the editor's block menu — a right-click
// anywhere else on a block opens it too, BlockEditor), the task checkbox (click: toggle;
// right click / long press: the editor's status menu), and the content for the block's `view`
// (rendered text / inline editor, code, calc, math, a Mermaid diagram, a chart, table, query, callout, quote,
// rule, frontmatter) with image embeds and embedded pages / sections / blocks (TransclusionView) below.
// Document mode (BlockEditor.documentMode): no guides, fold arrows or bullets for paragraphs and
// headings; a list item shows its Markdown marker in its level's marker column — a bullet (the
// same round dot at every depth), its number ("3."), or the checkbox of a task — and the text starts
// one column in per level. A nested list item draws a guide line per ancestor level at that
// level's marker centre (under a number's column), and a list item with children one from under
// its marker down, as the outline does. Spacing follows the previous block (`prevKind`):
// paragraph gaps, tight list items, air above headings (inside the delegates, so the lines join).
//
// A selected block is drawn on a band (`selectionEdge`: one band over a range of blocks, rounded
// at its ends, from the editor's left edge; a block alone from its own level — blocks picked one by
// one with gaps between them: a band for each run); in the touch selection mode the band's first
// and last blocks carry its grips (BlockEditor.beginGripDrag(); a selection without gaps only).
// Hidden rows (zoom, folds) stay in the model with `visible` false: the delegate collapses to
// zero height. flash() highlights the block for a moment (a link to it was followed: Theme.mark
// fading out in about 1.2 s, drawn over the selection and under the text, no height change).
// A leading "# Title" repeating the page title right after the page properties
// (PageController.titleRow) trades places with the properties panel on screen — the title
// first, the panel under it — through a vertical translation of both delegates; the file and
// the row order stay as they are.
Item {
    id: block

    required property int index
    /// The row's model object (untyped: compiled QML reads the typed roles below and rowData).
    required property var model
    // The roles every block reads, typed (BlockListModel; read when the block is created and
    // updated with the row). `shown` is the role `visible` (Item has a property of that name).
    required property bool shown
    required property bool isEditing
    required property string view
    required property string kind
    required property string taskStatus
    required property bool taskDated
    required property int headingLevel
    required property int depth
    required property string listMarker
    required property string prevKind
    required property string selectionEdge
    required property bool hasChildren
    required property bool collapsed
    required property bool onlyEmbeds
    required property string plainText
    required property list<var> embeds
    required property list<var> transclusions
    // (final: the views of the block look into them through `block`.)
    final property BlockEditor editor: editorItem as BlockEditor
    /// The editor as an Item: what BlockEditor gives its delegates (a file cannot give its own
    /// root to a property of its own type in compiled code).
    property Item editorItem

    final property PageController controller: editor ? editor.controller : null
    /// The roles only some blocks or views read (the rendered text, a table's data, …), typed and
    /// read on demand (BlockRowData).
    final property BlockRowData rowData: BlockRowData {}
    readonly property bool editing: isEditing
    readonly property bool listItem: kind === "listItem"
    readonly property bool isTask: taskStatus !== "" && listItem
    readonly property bool closedTask: taskStatus === "done" || taskStatus === "cancelled"
    /// An open task without a 📅 / ⏳ date (taskDated): the "Add date" button at the end of its first
    /// line (BlockEditor.openQuickDateMenu) — on hover, and while the block is edited (on touch only
    /// then). Its room is kept while it is hidden, so the text never reflows when it shows.
    readonly property bool datable: isTask && !closedTask && !taskDated && textView && !editor.readOnly
    /// An open task with a date: its "+" adds a reminder, an alarm (or the date it lacks) next to it.
    readonly property bool addable: isTask && !closedTask && taskDated && textView && !editor.readOnly
    readonly property real dateButtonRoom: datable || addable ? (Theme.touchUi ? 32 : 28) : 0
    readonly property int shownHeadingLevel: view === "heading" ? Math.max(1, headingLevel) : 0
    readonly property bool hovered: hover.hovered
    readonly property bool doc: editor.documentMode
    readonly property bool ordered: listItem && listMarker.length > 0 && listMarker[0] >= "0" && listMarker[0] <= "9"

    // Horizontal geometry. Outline: the text column of this depth, the bullet centred left of it.
    // Document: paragraphs at the text column (after the editor's documentInset, its gutter); a
    // list item one marker column in per level, its marker (bullet, number, checkbox) in the
    // column before its text.
    readonly property real levelX: doc ? editor.contentX + editor.gutter + (listItem ? (depth + 1) * editor.listIndent : 0)
                                       : editor.contentX + editor.gutter + depth * editor.indentStep
    readonly property real bulletX: markerX(doc && !listItem ? -1 : depth)
    /// The centre of the marker column of list level `level` (the bullet's, a number's, a task's
    /// box): the bullets and the guide lines through the ancestors' markers share it.
    function markerX(level: int): real {
        return doc ? editor.contentX + editor.gutter + (level + 1) * editor.listIndent - Math.round(editor.listIndent * 0.55)
                   : editor.contentX + editor.gutter + level * editor.indentStep - editor.bulletColumn / 2
    }
    /// The task checkbox's size (TaskBox.boxSize).
    readonly property real taskBoxSize: Theme.touchUi ? 18 : 16
    readonly property real contentLeft: levelX + (isTask && !doc ? editor.taskBoxWidth : 0)
    readonly property real contentRight: editor.contentX + editor.contentWidth
    // Vertical rhythm: headings get air above them, framed blocks a little on both sides.
    readonly property bool framed: view === "code" || view === "calc" || view === "query" || view === "math"
                                   || view === "mermaid" || view === "chart" || view === "table" || view === "callout"
                                   || view === "frontmatter" || view === "view"
    // The title shown above the properties panel (see the file comment). (`controller` checked
    // too: when the editor's page goes, a row created in the background reads it before the
    // editor's titleFirst follows.)
    readonly property bool titleFirst: editor.titleFirst && controller !== null && index === controller.titleRow
    readonly property bool propertiesAfterTitle: editor.titleFirst && controller !== null
                                                 && index === controller.titleRow - 1
    /// The first block drawn under the properties panel — the one after it in the file, or after
    /// the title drawn above it —: 24 px under the panel in both modes (constant per row).
    readonly property bool belowProperties: (prevKind === "frontmatter" && !titleFirst)
                                            || (editor.titleFirst && controller !== null
                                                && index === controller.titleRow + 1)
    readonly property real topPad: doc ? docTopPad : outlineTopPad
    readonly property real outlineTopPad: {
        // With the panel's 8 px bottom padding: 24 px.
        if (belowProperties)
            return shownHeadingLevel === 1 ? Theme.space5 : Theme.space4
        if (shownHeadingLevel > 0 && index > 0 && !titleFirst)
            return shownHeadingLevel === 1 ? Theme.space5 : shownHeadingLevel === 2 ? Theme.space4 : Theme.space3
        if (framed)
            return Theme.space2
        return view === "rule" ? Theme.space2 : rowPad
    }
    /// Document: the space above a block follows the block before it (a page of text: paragraph
    /// gaps, list items close together, headings with air above and close to what follows).
    readonly property real docTopPad: {
        if (propertiesAfterTitle)
            return Theme.space3 // the properties panel under the title
        if (prevKind === "" || titleFirst)
            return 0
        if (belowProperties) // air between the properties panel and the page's text
            return shownHeadingLevel > 0 && !listItem ? Math.max(Theme.space6, editor.headingGapAbove(shownHeadingLevel))
                                                 : Theme.space6
        if (shownHeadingLevel > 0 && !listItem)
            return editor.headingGapAbove(shownHeadingLevel)
        // A frame (table, code, callout, …), a quote's bar or a picture right under a heading:
        // their edges sat a few pixels under its descenders, a paragraph's text does not need it.
        // (Not `onlyPictures`: it changes with editing, the gap must not.)
        if (prevKind === "heading")
            return framed || view === "quote" || (hasEmbeds && onlyEmbeds && !listItem)
                   ? editor.headingGapBelow + Theme.space2 : editor.headingGapBelow
        if (listItem && prevKind === "listItem")
            return editor.listGap
        return editor.paragraphGap
    }
    /// Text rows: a little air between blocks; more on touch so 44 px targets fit the rows.
    readonly property real rowPad: Theme.touchUi ? 4 : 2
    readonly property real bottomPad: doc ? (listItem ? editor.listGap / 2 : 0)
                                          : framed ? Theme.space2 : shownHeadingLevel > 0 ? Theme.space1 : rowPad
    readonly property bool hasEmbeds: embeds.length > 0
                                      && (view === "text" || view === "heading" || view === "quote" || view === "callout")
    readonly property bool hasTransclusions: transclusions.length > 0
    readonly property list<var> noItems: []
    /// Nothing but embedded pages / sections / blocks: no empty text line above their frames.
    readonly property bool onlyTransclusions: hasTransclusions && !hasEmbeds && onlyEmbeds && !editing && view === "text"
    /// Document: a paragraph of nothing but pictures shows just them, like a picture in a page
    /// of text (the outline keeps the line its bullet sits on); a click beside one edits it.
    readonly property bool onlyPictures: doc && hasEmbeds && !listItem && onlyEmbeds && !editing && view === "text"
    readonly property bool noTextLine: onlyTransclusions || onlyPictures
    /// Paragraphs, list items, headings (most blocks): their text is created with the block, not
    /// through the content Loader — the Loader's own work was a large part of creating a block.
    readonly property bool textView: view !== "code" && view !== "calc" && view !== "query" && view !== "math"
                                     && view !== "mermaid" && view !== "chart" && view !== "table" && view !== "quote"
                                     && view !== "callout" && view !== "view"
                                     && view !== "rule" && view !== "frontmatter"
    readonly property real contentHeight: noTextLine ? 0 : textView ? textContent.height : content.height
    readonly property real embedsY: topPad + contentHeight + (contentHeight > 0 && !onlyEmbeds ? Theme.space2 : 0)
    readonly property real transclusionsY: (hasEmbeds ? embedsY + embedLoader.height : topPad + contentHeight)
                                           + (contentHeight > 0 || hasEmbeds ? Theme.space2 : 0)
    readonly property real bodyBottom: hasTransclusions ? transclusionsY + transclusionLoader.height
                                     : hasEmbeds ? embedsY + embedLoader.height : topPad + contentHeight

    /// y of the first line's x-height centre (bullet, checkbox, fold arrow).
    readonly property real firstLineCenter: topPad + (onlyTransclusions && transclusionView !== null ? transclusionLineCenter
                                                      : textView ? textContent.firstLineCenter
                                                      : contentFirstLineCenter)

    // (Hosted by a PageItem, not by a list: the editor's width.)
    width: ListView.view ? ListView.view.width : editor ? editor.width : 0
    height: shown ? Math.ceil(bodyBottom + bottomPad) : 0
    visible: shown
    // Title above the properties panel: the two delegates trade places on screen.
    transform: Translate {
        y: block.titleFirst ? -block.editor.propertiesHeight : block.propertiesAfterTitle ? block.editor.titleHeight : 0
    }
    Binding { when: block.titleFirst; target: block.editor; property: "titleHeight"; value: block.height }
    Binding { when: block.propertiesAfterTitle; target: block.editor; property: "propertiesHeight"; value: block.height }

    Accessible.role: Accessible.ListItem
    Accessible.name: plainText

    /// The view of the block's content (BlockText, QueryBlockView, TableBlockView, …).
    readonly property Item contentItem: textView ? textContent : content.item as Item
    /// The views of the block's embedded blocks and of its pictures and files (null until it has
    /// some). Like the views of the kinds of blocks, they are not named here as types (a QML file
    /// named is loaded with the editor, docs/PERFORMANCE.md "QML files loaded when first used"):
    /// their members are reached by name (QmlCall), their first line followed (loadedLineCenter,
    /// transclusionLineCenter).
    readonly property Item transclusionView: transclusionLoader.item as Item
    readonly property Item embedView: embedLoader.item as Item

    /// Give the keyboard to the block content (tables: the first cell).
    function focusContent(): void {
        if (block.view === "table" && content.item !== null)
            QmlCall.call(content.item, "focusFirstCell")
    }
    /// The first line's centre of the view of the block's kind and of its embedded blocks, as their
    /// `firstLineCenter` says (followed by QmlCall.connect: no object of its own per block).
    property real loadedLineCenter: 0
    property real transclusionLineCenter: 0
    function readLineCenter(): void {
        block.loadedLineCenter = QmlCall.number(QmlCall.get(content.item, "firstLineCenter"))
    }
    function readTransclusionLine(): void {
        block.transclusionLineCenter = QmlCall.number(QmlCall.get(transclusionLoader.item, "firstLineCenter"))
    }
    /// The first line's centre in the view loaded for the block's content (not a text view).
    readonly property real contentFirstLineCenter: {
        const item = content.item as Item
        const fallback = editor.firstLineCenter(0)
        if (item === null)
            return fallback
        switch (view) {
        case "code":
        case "calc":
        case "query":
        case "math":
        case "mermaid":
        case "chart":
        case "view":
        case "table":
        case "frontmatter": return block.loadedLineCenter
        // (The quote / callout frame and the rule below are untyped components: computed here.)
        case "quote":
        case "callout": return (view === "callout" ? Theme.space3 : 0) + editor.firstLineCenter(shownHeadingLevel)
        case "rule": return item.height / 2
        }
        return fallback
    }

    HoverHandler { id: hover; enabled: !Theme.touchUi }

    // ---- Selection ---------------------------------------------------------------------------
    /// Where the block sits in the band of selected blocks: "" | "single" | "first" | "middle" |
    /// "last" (BlockListModel's selectionEdge: visible rows only).
    readonly property string bandEdge: shown ? selectionEdge : ""
    readonly property bool bandStart: bandEdge === "single" || bandEdge === "first"
    readonly property bool bandEnd: bandEdge === "single" || bandEdge === "last"
    /// The left edge of a band around this block alone, from its own level (the flash's too).
    readonly property real ownBandX: doc ? (listItem ? levelX - editor.listIndent : levelX) - Theme.space2
                                         : bulletX - editor.bulletColumn / 2 - 2
    /// The band's left edge: a block alone from its own level, a range from the editor's (one
    /// straight band over every level).
    readonly property real bandX: bandEdge === "single" ? ownBandX
                                  : doc ? editor.contentX + editor.gutter - Theme.space2
                                        : editor.contentX + editor.gutter - editor.bulletColumn - 2
    readonly property real bandRight: contentRight + Theme.space2
    readonly property real bandTop: bandStart ? 1 : 0
    readonly property real bandBottom: bandEnd ? height - 1 : height
    // ---- Style by tag (an extension's b.app.tagStyle: docs/UI.md "Styles of blocks by tag") -------
    /// The tint of the block's styled tag ("" none): a colour of the theme by name, or "#rrggbb".
    readonly property string tagTint: shown ? rowData.tagTint : ""
    // Made only for blocks with a styled tag (none unless an extension declared a style).
    Loader {
        active: block.tagTint !== ""
        x: block.ownBandX
        y: 1
        width: block.bandRight - x
        height: Math.max(0, block.height - 2)
        sourceComponent: BlockParts.part("TagTint")
    }

    // Made only for selected blocks (most blocks never are).
    Loader {
        active: block.bandEdge !== ""
        x: block.bandX
        y: block.bandTop
        width: block.bandRight - x
        height: Math.max(0, block.bandBottom - block.bandTop)
        sourceComponent: BlockParts.part("SelectionBand")
    }

    // ---- Flash (a link to the block was followed) ------------------------------------------------
    /// The highlight is showing (BlockEditor.revealRow); it ends by itself.
    property bool flashing: false
    function flash(): void {
        flashing = false // again from the start when it is still showing
        flashing = true
    }
    Loader {
        active: block.flashing && block.shown
        x: block.ownBandX
        y: 1
        width: block.bandRight - x
        height: Math.max(0, block.height - 2)
        sourceComponent: BlockParts.part("Flash")
    }

    // ---- Guide lines -------------------------------------------------------------------------
    // One per ancestor level, over the row's full height. Outline: every block, at the bullets of
    // its ancestors. Document: list items only (paragraphs and headings have none), at the marker
    // centre of each ancestor list level — the bulletX of that level, so never through the row's
    // own marker.
    Repeater {
        model: !block.shown ? 0 : !block.doc || block.listItem ? block.depth : 0
        delegate: Rectangle {
            required property int index
            objectName: "guideLine"
            x: Math.round(block.markerX(index))
            width: 1
            height: block.height
            color: Theme.guide
        }
    }
    // From this bullet (a document's marker: bullet, number or checkbox) down to its (visible)
    // children.
    Rectangle {
        objectName: "parentGuide"
        visible: block.shown && block.listItem && block.hasChildren && !block.collapsed
        x: Math.round(block.bulletX)
        // (Under a document's task box: half its size and 3 px below its centre.)
        y: Math.round(block.firstLineCenter + (block.doc && block.isTask ? block.taskBoxSize / 2 + 3 : 9))
        width: 1
        height: Math.max(0, block.height - y)
        color: Theme.guide
    }

    // The parts below that only some blocks have — the fold arrow, the bullet, an ordered list
    // item's number, the task checkbox, the block menu — are created only for those blocks (the
    // menu only when it opens): a block is created whenever it scrolls into view, so what every
    // block carries decides how smoothly long pages and the journal scroll (docs/UI.md
    // "Scrolling performance").

    // ---- Fold arrow ------------------------------------------------------------------------
    Loader {
        id: arrow
        active: block.hasChildren && block.shown && !block.doc
        width: block.editor.arrowColumn
        height: Math.max(block.editor.arrowColumn, 24)
        // Left of the bullet's hit area (with a mouse 24 px wide, a little wider than its column).
        x: (Theme.touchUi ? block.bulletX - block.editor.bulletColumn / 2 : block.levelX - bulletBox.width) - width + 2
        y: block.firstLineCenter - height / 2
        sourceComponent: BlockParts.part("FoldArrow")
    }

    // ---- Bullet --------------------------------------------------------------------------------
    Loader {
        id: bulletBox
        // Document: tasks show their checkbox, numbered items their number instead.
        active: block.listItem && block.shown && !(block.doc && (block.isTask || block.ordered))
        // The hit area: at least 24 × 24 with a mouse (WCAG 2.5.8), its right edge at the text
        // column, so a click on the first letters still edits. Touch: a bullet without a fold
        // arrow (a leaf; every bullet of a document) takes the empty column left of it too, 44 ×
        // 44 — a document's first level on a phone has less room there and reaches under the
        // item's first letters, which edit the item as well; a parent keeps the arrow / bullet
        // split (both cannot be 44 wide in the outline's gutter).
        readonly property bool besideArrow: !block.doc && block.hasChildren
        width: Theme.touchUi ? (besideArrow ? block.editor.bulletColumn + 6 : Theme.touchTarget)
                             : Math.max(24, block.editor.bulletColumn)
        height: Theme.touchUi ? Theme.touchTarget : Math.max(24, block.editor.bulletColumn)
        x: Theme.touchUi && besideArrow ? block.bulletX - width / 2
                                        : block.doc ? Math.max(0, block.levelX - width) : block.levelX - width
        y: block.firstLineCenter - height / 2
        sourceComponent: BlockParts.part("Bullet")
    }

    // ---- Number of an ordered list item (document mode) -----------------------------------------
    Loader {
        id: numberBox
        active: block.doc && block.ordered && !block.isTask && block.shown
        readonly property real gap: Math.round(block.editor.listIndent * 0.25)
        // Right-aligned in the marker column, on the first line's baseline (body font).
        x: block.levelX - width - gap
        y: block.firstLineCenter - block.editor.firstLineCenter(0)
        sourceComponent: BlockParts.part("Number")
    }

    // ---- Task checkbox ---------------------------------------------------------------------------
    Loader {
        active: block.isTask && block.shown
        // Outline: after the bullet; document: in the marker column, instead of the bullet.
        readonly property real boxSize: block.taskBoxSize
        x: block.doc ? block.bulletX - width / 2
                     : block.levelX + (block.editor.taskBoxWidth - boxSize) / 2 - 2 - (width - boxSize) / 2
        y: block.firstLineCenter - height / 2
        sourceComponent: BlockParts.part("TaskBox")
    }

    // ---- "Add date" (an open task without a date; with one, "+" adds another) ---------------------
    Loader {
        active: (block.datable || block.addable) && block.shown
                && (block.hovered || block.editing || block.editor.quickDateRow === block.index)
        x: block.contentRight - block.dateButtonRoom + (block.dateButtonRoom - width) / 2
        y: block.firstLineCenter - height / 2
        sourceComponent: BlockParts.part("DateButton")
    }

    // ---- Content ---------------------------------------------------------------------------
    BlockText {
        id: textContent
        x: block.contentLeft
        y: block.topPad
        width: Math.max(40, block.contentRight - block.dateButtonRoom - x)
        visible: textContent.live && !block.noTextLine
        live: block.textView && block.shown
        blockItem: block
    }
    Loader {
        id: content
        x: block.view === "callout" || block.view === "quote" || block.framed ? block.levelX : block.contentLeft
        y: block.topPad
        width: Math.max(40, block.contentRight - x)
        active: block.shown && !block.textView
        visible: !block.noTextLine
        sourceComponent: {
            switch (block.view) {
            case "code": return BlockParts.part("Code")
            case "calc": return BlockParts.part("Calc")
            case "query": return BlockParts.part("Query")
            case "math": return BlockParts.part("Math")
            case "mermaid": return BlockParts.part("Mermaid")
            case "chart": return BlockParts.part("Chart")
            case "view": return BlockParts.part("View")
            case "table": return BlockParts.part("Table")
            case "quote":
            case "callout": return BlockParts.part("Callout")
            case "rule": return BlockParts.part("Rule")
            case "frontmatter": return BlockParts.part("Frontmatter")
            default: return null
            }
        }
        onLoaded: {
            if (block.view === "quote" || block.view === "callout" || block.view === "rule")
                return
            QmlCall.connect(content.item, "firstLineCenterChanged", block, "readLineCenter")
            block.readLineCenter()
        }
    }

    // A block's pictures and files, and its embedded blocks: made the first time it has some
    // (most blocks have neither: the views with their Repeaters were a tenth of a block's objects)
    // and kept, also while a menu of theirs is still open when they go.
    property bool embedsMade: hasEmbeds
    onHasEmbedsChanged: if (hasEmbeds) embedsMade = true
    property bool transclusionsMade: hasTransclusions
    onHasTransclusionsChanged: if (hasTransclusions) transclusionsMade = true
    Loader {
        id: embedLoader
        active: block.embedsMade
        visible: block.hasEmbeds
        x: block.contentLeft
        y: block.embedsY
        width: Math.max(40, block.contentRight - x)
        sourceComponent: BlockParts.part("Embeds")
    }

    Loader {
        id: transclusionLoader
        active: block.transclusionsMade
        visible: block.hasTransclusions
        x: block.contentLeft
        y: block.transclusionsY
        width: Math.max(40, block.contentRight - x)
        sourceComponent: BlockParts.part("Transclusions")
        onLoaded: {
            QmlCall.connect(transclusionLoader.item, "firstLineCenterChanged", block, "readTransclusionLine")
            block.readTransclusionLine()
        }
    }


    // ---- The selection's grips (touch selection mode) -----------------------------------------------
    // A round grip at the band's start (top left) and end (bottom right), 44 px to hold: the finger
    // moves that end of the selection (the editor follows it, also when this block scrolls away).
    Loader {
        active: block.bandStart && block.editor.selectionGrips
        z: 5
        // (Inside the window: a document's band starts near the screen's edge on a phone.)
        x: Math.max(block.bandX, 12) - Theme.touchTarget / 2
        y: block.bandTop + 9 - Theme.touchTarget / 2
        sourceComponent: BlockParts.part("GripStart")
    }
    Loader {
        active: block.bandEnd && block.editor.selectionGrips
        z: 5
        // (Inside the window: the band reaches the screen's edge on a phone.)
        x: Math.min(block.bandRight, block.width - 12) - Theme.touchTarget / 2
        y: block.bandBottom - 9 - Theme.touchTarget / 2
        sourceComponent: BlockParts.part("GripEnd")
    }

    // ---- Block menu ----------------------------------------------------------------------------
    /// The editor's block menu for this block (BlockMenu, one per editor, made when first opened).
    function openMenu(item: Item, x: real, y: real): void {
        block.editor.openBlockMenu(block.index, item, x, y)
    }
    /// A tap beside the text (a marker, a rule, a picture's margin) edits the block at `pos`
    /// (-1: the end) — unless the editor took the tap (the touch selection mode moves its end).
    function editOnTap(pos: int): void {
        if (!editor.blockTapped(index))
            editor.startEditing(index, pos)
    }
}
