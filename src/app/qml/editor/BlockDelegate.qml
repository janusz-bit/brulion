pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockDelegate — one block of the BlockEditor (one row of BlockListModel): indentation with
// thin vertical guide lines (one per ancestor level, continuous across rows), the fold arrow
// (on hover; always shown when folded or on touch), the round bullet of list items (click:
// zoom into the block; right click / long press: the editor's block menu — a right-click
// anywhere else on a block opens it too, BlockEditor), the task checkbox (click: toggle;
// right click / long press: the editor's status menu), and the content for the block's `view`
// (rendered text / inline editor, code, calc, math, table, query, callout, quote, rule,
// frontmatter) with image embeds and embedded pages / sections / blocks (TransclusionView) below.
// Document mode (BlockEditor.documentMode): no guides, fold arrows or bullets for paragraphs and
// headings; a list item shows its Markdown marker in its level's marker column — a bullet (disc,
// circle, square by depth), its number ("3."), or the checkbox of a task — and the text starts
// one column in per level. Spacing follows the previous block (`prevKind`): paragraph gaps,
// tight list items, air above headings.
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
    required property var model
    property BlockEditor editor

    readonly property PageController controller: editor ? editor.controller : null
    readonly property bool shown: model.visible
    readonly property bool editing: model.isEditing
    readonly property string view: model.view
    readonly property bool listItem: model.kind === "listItem"
    readonly property string taskStatus: model.taskStatus
    readonly property bool isTask: taskStatus !== "" && listItem
    readonly property bool closedTask: taskStatus === "done" || taskStatus === "cancelled"
    readonly property int headingLevel: view === "heading" ? Math.max(1, model.headingLevel) : 0
    readonly property int depth: model.depth
    readonly property bool hovered: hover.hovered
    readonly property bool doc: editor.documentMode
    readonly property string listMarker: model.listMarker || ""
    readonly property bool ordered: listItem && listMarker.length > 0 && listMarker.charAt(0) >= "0" && listMarker.charAt(0) <= "9"

    // Horizontal geometry. Outline: the text column of this depth, the bullet centred left of it.
    // Document: paragraphs at the text column (after the editor's documentInset, its gutter); a
    // list item one marker column in per level, its marker (bullet, number, checkbox) in the
    // column before its text.
    readonly property real levelX: doc ? editor.contentX + editor.gutter + (listItem ? (depth + 1) * editor.listIndent : 0)
                                       : editor.contentX + editor.gutter + depth * editor.indentStep
    readonly property real bulletX: doc ? levelX - Math.round(editor.listIndent * 0.55) : levelX - editor.bulletColumn / 2
    readonly property real contentLeft: levelX + (isTask && !doc ? editor.taskBoxWidth : 0)
    readonly property real contentRight: editor.contentX + editor.contentWidth
    // Vertical rhythm: headings get air above them, framed blocks a little on both sides.
    readonly property bool framed: view === "code" || view === "calc" || view === "query" || view === "math"
                                   || view === "table" || view === "callout" || view === "frontmatter"
    // The title shown above the properties panel (see the file comment).
    readonly property bool titleFirst: editor.titleFirst && index === editor.controller.titleRow
    readonly property bool propertiesAfterTitle: editor.titleFirst && index === editor.controller.titleRow - 1
    readonly property real topPad: doc ? docTopPad : outlineTopPad
    readonly property real outlineTopPad: {
        if (headingLevel > 0 && index > 0 && !titleFirst)
            return headingLevel === 1 ? Theme.space5 : headingLevel === 2 ? Theme.space4 : Theme.space3
        if (framed)
            return Theme.space2
        return view === "rule" ? Theme.space2 : rowPad
    }
    /// Document: the space above a block follows the block before it (a page of text: paragraph
    /// gaps, list items close together, headings with air above and close to what follows).
    readonly property string prevKind: model.prevKind || ""
    readonly property real docTopPad: {
        if (propertiesAfterTitle)
            return Theme.space3 // the properties panel under the title
        if (prevKind === "" || titleFirst)
            return 0
        if (headingLevel > 0 && !listItem)
            return editor.headingGapAbove(headingLevel)
        // A frame (table, code, callout, …), a quote's bar or a picture right under a heading:
        // their edges sat a few pixels under its descenders, a paragraph's text does not need it.
        // (Not `onlyPictures`: it changes with editing, the gap must not.)
        if (prevKind === "heading")
            return framed || view === "quote" || (hasEmbeds && model.onlyEmbeds && !listItem)
                   ? editor.headingGapBelow + Theme.space2 : editor.headingGapBelow
        if (prevKind === "frontmatter")
            return Theme.space3
        if (listItem && prevKind === "listItem")
            return editor.listGap
        return editor.paragraphGap
    }
    /// Text rows: a little air between blocks; more on touch so 44 px targets fit the rows.
    readonly property real rowPad: Theme.touchUi ? 4 : 2
    readonly property real bottomPad: doc ? (listItem ? editor.listGap / 2 : 0)
                                          : framed ? Theme.space2 : headingLevel > 0 ? Theme.space1 : rowPad
    readonly property var embeds: model.embeds
    readonly property bool hasEmbeds: embeds !== undefined && embeds !== null && embeds.length > 0
                                      && (view === "text" || view === "heading" || view === "quote" || view === "callout")
    readonly property var transclusions: model.transclusions
    readonly property bool hasTransclusions: transclusions !== undefined && transclusions !== null && transclusions.length > 0
    /// Nothing but embedded pages / sections / blocks: no empty text line above their frames.
    readonly property bool onlyTransclusions: hasTransclusions && !hasEmbeds && model.onlyEmbeds && !editing && view === "text"
    /// Document: a paragraph of nothing but pictures shows just them, like a picture in a page
    /// of text (the outline keeps the line its bullet sits on); a click beside one edits it.
    readonly property bool onlyPictures: doc && hasEmbeds && !listItem && model.onlyEmbeds && !editing && view === "text"
    readonly property bool noTextLine: onlyTransclusions || onlyPictures
    /// Paragraphs, list items, headings (most blocks): their text is created with the block, not
    /// through the content Loader — the Loader's own work was a large part of creating a block.
    readonly property bool textView: view !== "code" && view !== "calc" && view !== "query" && view !== "math"
                                     && view !== "table" && view !== "quote" && view !== "callout" && view !== "rule"
                                     && view !== "frontmatter"
    readonly property real contentHeight: noTextLine ? 0 : textView ? textContent.height : content.height
    readonly property real embedsY: topPad + contentHeight + (contentHeight > 0 && !model.onlyEmbeds ? Theme.space2 : 0)
    readonly property real transclusionsY: (hasEmbeds ? embedsY + embedView.height : topPad + contentHeight)
                                           + (contentHeight > 0 || hasEmbeds ? Theme.space2 : 0)
    readonly property real bodyBottom: hasTransclusions ? transclusionsY + transclusionView.height
                                     : hasEmbeds ? embedsY + embedView.height : topPad + contentHeight

    /// y of the first line's x-height centre (bullet, checkbox, fold arrow).
    readonly property real firstLineCenter: topPad + (onlyTransclusions ? transclusionView.firstLineCenter
                                                      : textView ? textContent.firstLineCenter
                                                      : content.item ? content.item.firstLineCenter : editor.firstLineCenter(0))

    width: ListView.view ? ListView.view.width : 0
    height: shown ? Math.ceil(bodyBottom + bottomPad) : 0
    visible: shown
    clip: !shown
    // Title above the properties panel: the two delegates trade places on screen.
    transform: Translate {
        y: block.titleFirst ? -block.editor.propertiesHeight : block.propertiesAfterTitle ? block.editor.titleHeight : 0
    }
    Binding { when: block.titleFirst; target: block.editor; property: "titleHeight"; value: block.height }
    Binding { when: block.propertiesAfterTitle; target: block.editor; property: "propertiesHeight"; value: block.height }

    Accessible.role: Accessible.ListItem
    Accessible.name: model.plainText

    /// The view of the block's content (BlockText, QueryBlockView, TableBlockView, …).
    readonly property Item contentItem: textView ? textContent : content.item as Item
    readonly property alias transclusionView: transclusionView
    readonly property alias embedView: embedView

    /// Give the keyboard to the block content (tables: the first cell).
    function focusContent() {
        if (content.item && content.item.focusFirstCell)
            content.item.focusFirstCell()
    }

    HoverHandler { id: hover; enabled: !Theme.touchUi }

    // ---- Selection ---------------------------------------------------------------------------
    /// Where the block sits in the band of selected blocks: "" | "single" | "first" | "middle" |
    /// "last" (BlockListModel's selectionEdge: visible rows only).
    readonly property string selectionEdge: shown ? (model.selectionEdge || "") : ""
    readonly property bool bandStart: selectionEdge === "single" || selectionEdge === "first"
    readonly property bool bandEnd: selectionEdge === "single" || selectionEdge === "last"
    /// The band's left edge: a block alone from its own level, a range from the editor's (one
    /// straight band over every level).
    readonly property real bandX: selectionEdge === "single"
                                  ? (doc ? (listItem ? levelX - editor.listIndent : levelX) - Theme.space2
                                         : bulletX - editor.bulletColumn / 2 - 2)
                                  : (doc ? editor.contentX + editor.gutter - Theme.space2
                                         : editor.contentX + editor.gutter - editor.bulletColumn - 2)
    readonly property real bandRight: contentRight + Theme.space2
    readonly property real bandTop: bandStart ? 1 : 0
    readonly property real bandBottom: bandEnd ? height - 1 : height
    // Made only for selected blocks (most blocks never are).
    Loader {
        active: block.selectionEdge !== ""
        x: block.bandX
        y: block.bandTop
        width: block.bandRight - x
        height: Math.max(0, block.bandBottom - block.bandTop)
        sourceComponent: Rectangle {
            objectName: "selectionBand"
            topLeftRadius: block.bandStart ? Theme.radiusSm : 0
            topRightRadius: block.bandStart ? Theme.radiusSm : 0
            bottomLeftRadius: block.bandEnd ? Theme.radiusSm : 0
            bottomRightRadius: block.bandEnd ? Theme.radiusSm : 0
            color: Theme.bgSelected
            border.width: block.selectionEdge === "single" ? 1 : 0
            border.color: Theme.alpha(Theme.focusRing, 0.35)
            // A range: a line down its left edge marks the band as one.
            Rectangle {
                visible: block.selectionEdge !== "single"
                width: 2
                height: parent.height
                color: Theme.alpha(Theme.focusRing, 0.6)
            }
        }
    }

    // ---- Flash (a link to the block was followed) ------------------------------------------------
    /// The highlight is showing (BlockEditor.revealRow); it ends by itself.
    property bool flashing: false
    function flash() {
        flashing = false // again from the start when it is still showing
        flashing = true
    }
    Loader {
        active: block.flashing && block.shown
        x: block.doc ? (block.listItem ? block.levelX - block.editor.listIndent : block.levelX) - Theme.space2
                     : block.bulletX - block.editor.bulletColumn / 2 - 2
        y: 1
        width: block.contentRight - x + Theme.space2
        height: Math.max(0, block.height - 2)
        sourceComponent: Rectangle {
            objectName: "blockFlash"
            radius: Theme.radiusSm
            color: Theme.mark
            SequentialAnimation on opacity {
                NumberAnimation { from: 0; to: 1; duration: Theme.reducedMotion ? 0 : 150; easing.type: Theme.easing }
                PauseAnimation { duration: 700 }
                NumberAnimation { to: 0; duration: Theme.reducedMotion ? 0 : 350; easing.type: Theme.easing }
                ScriptAction { script: block.flashing = false }
            }
        }
    }

    // ---- Guide lines -------------------------------------------------------------------------
    Repeater {
        model: block.shown && !block.doc ? block.depth : 0
        delegate: Rectangle {
            required property int index
            x: Math.round(block.editor.contentX + block.editor.gutter - block.editor.bulletColumn / 2 + index * block.editor.indentStep)
            width: 1
            height: block.height
            color: Theme.borderSubtle
        }
    }
    // From this bullet down to its (visible) children.
    Rectangle {
        visible: block.shown && !block.doc && block.listItem && block.model.hasChildren && !block.model.collapsed
        x: Math.round(block.bulletX)
        y: Math.round(block.firstLineCenter + 9)
        width: 1
        height: Math.max(0, block.height - y)
        color: Theme.borderSubtle
    }

    // The parts below that only some blocks have — the fold arrow, the bullet, an ordered list
    // item's number, the task checkbox, the block menu — are created only for those blocks (the
    // menu only when it opens): a block is created whenever it scrolls into view, so what every
    // block carries decides how smoothly long pages and the journal scroll (docs/UI.md
    // "Scrolling performance").

    // ---- Fold arrow ------------------------------------------------------------------------
    Loader {
        id: arrow
        active: block.model.hasChildren && block.shown && !block.doc
        width: block.editor.arrowColumn
        height: Math.max(block.editor.arrowColumn, 24)
        // Left of the bullet's hit area (with a mouse 24 px wide, a little wider than its column).
        x: (Theme.touchUi ? block.bulletX - block.editor.bulletColumn / 2 : block.levelX - bulletBox.width) - width + 2
        y: block.firstLineCenter - height / 2
        sourceComponent: Item {
            visible: block.model.collapsed || block.hovered || Theme.touchUi || arrowTap.pressed
            opacity: block.model.collapsed ? 0.9 : 0.6
            Accessible.role: Accessible.Button
            Accessible.name: block.model.collapsed ? qsTr("Expand") : qsTr("Collapse")

            BIcon {
                anchors.centerIn: parent
                name: "chevron-right"
                size: Theme.iconSizeSm
                color: arrowHover.hovered ? Theme.fg : Theme.fgMuted
                rotation: block.model.collapsed ? 0 : 90
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
            // Touch: a 44 px hit area around the small arrow (the bullet, stacked above, keeps the
            // part that overlaps it).
            Item {
                anchors.centerIn: parent
                width: Theme.touchUi ? Theme.touchTarget : parent.width
                height: Theme.touchUi ? Theme.touchTarget : parent.height
                HoverHandler { id: arrowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: arrowTap
                    onTapped: block.controller.toggleFold(block.index)
                }
            }
        }
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
        readonly property bool besideArrow: !block.doc && block.model.hasChildren
        width: Theme.touchUi ? (besideArrow ? block.editor.bulletColumn + 6 : Theme.touchTarget)
                             : Math.max(24, block.editor.bulletColumn)
        height: Theme.touchUi ? Theme.touchTarget : Math.max(24, block.editor.bulletColumn)
        x: Theme.touchUi && besideArrow ? block.bulletX - width / 2
                                        : block.doc ? Math.max(0, block.levelX - width) : block.levelX - width
        y: block.firstLineCenter - height / 2
        sourceComponent: Item {
            id: bullet
            /// The bullet's centre in the hit area.
            readonly property real dotX: block.bulletX - bulletBox.x
            Accessible.role: block.doc ? Accessible.Graphic : Accessible.Button
            Accessible.name: block.doc ? qsTr("List item") : qsTr("Zoom into block")

            // Folded blocks show a halo around the bullet (Logseq style); hover shows it too.
            Rectangle {
                x: bullet.dotX - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: block.model.collapsed ? Theme.bgPressed : Theme.bgHover
                visible: !block.doc && (block.model.collapsed || bulletHover.hovered)
            }
            Rectangle {
                // Outline: the round bullet. Document: a Markdown list's disc, circle, square by depth.
                readonly property int shape: block.doc ? block.depth % 3 : 0
                x: bullet.dotX - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: shape === 2 ? 5 : 6
                height: width
                radius: shape === 2 ? 1 : width / 2
                color: shape === 1 ? "transparent"
                       : block.closedTask ? Theme.fgFaint : bulletHover.hovered && !block.doc ? Theme.fg : Theme.fgMuted
                border.width: shape === 1 ? 1.5 : 0
                border.color: Theme.fgMuted
            }
            HoverHandler {
                id: bulletHover
                cursorShape: block.doc ? Qt.ArrowCursor : Qt.PointingHandCursor
                enabled: !Theme.touchUi
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                // A document has no zoom: the bullet is part of the text, a click edits the item.
                // (The touch selection mode: a tap moves the selection's end here.)
                onTapped: {
                    if (block.editor.blockTapped(block.index))
                        return
                    if (block.doc)
                        block.editor.startEditing(block.index, 0)
                    else
                        block.controller.zoomInto(block.index)
                }
                onLongPressed: block.openMenu(bullet, bullet.dotX, bullet.height)
            }
            TapHandler {
                // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so a tap on
                // a phone zoomed in and opened the menu at once. Touch: the long press above.
                // WithinBounds: the press is the bullet's, the editor's right-click on the block
                // (menu at the pointer) does not open it a second time.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.WithinBounds
                onTapped: block.openMenu(bullet, bullet.dotX, bullet.height)
            }
        }
    }

    // ---- Number of an ordered list item (document mode) -----------------------------------------
    Loader {
        id: numberBox
        active: block.doc && block.ordered && !block.isTask && block.shown
        readonly property real gap: Math.round(block.editor.listIndent * 0.25)
        // Right-aligned in the marker column, on the first line's baseline (body font).
        x: block.levelX - width - gap
        y: block.firstLineCenter - block.editor.firstLineCenter(0)
        sourceComponent: Text {
            id: number
            // Rich text like the item's own rendered text: the same line layout, the same baseline.
            text: TextHelper.escapeHtml(block.listMarker)
            textFormat: Text.RichText
            font.family: Theme.fontEditor
            font.pixelSize: block.editor.fontSize
            font.features: { "tnum": 1 }
            lineHeightMode: Text.FixedHeight
            lineHeight: block.editor.lineHeight
            color: block.closedTask ? Theme.fgFaint : Theme.fgMuted
            Accessible.ignored: true
            // The hit area, as the bullet's: a touch target (24 px with a mouse) around the number,
            // its right edge at the text column (with no room left of it, under the first
            // letters, which edit the item as well).
            Item {
                readonly property real size: Theme.touchUi ? Theme.touchTarget : 24
                readonly property real textEdge: number.width + numberBox.gap
                width: Math.max(size, textEdge)
                height: Math.max(size, number.height)
                x: Math.max(-numberBox.x, textEdge - width)
                y: (number.height - height) / 2
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: if (!block.editor.blockTapped(block.index)) block.editor.startEditing(block.index, 0)
                    onLongPressed: block.openMenu(number, 0, number.height)
                }
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus // not a finger
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.WithinBounds // the number's press (as the bullet's)
                    onTapped: block.openMenu(number, 0, number.height)
                }
            }
        }
    }

    // ---- Task checkbox ---------------------------------------------------------------------------
    Loader {
        active: block.isTask && block.shown
        // Outline: after the bullet; document: in the marker column, instead of the bullet.
        readonly property real boxSize: Theme.touchUi ? 18 : 16
        x: block.doc ? block.bulletX - width / 2
                     : block.levelX + (block.editor.taskBoxWidth - boxSize) / 2 - 2 - (width - boxSize) / 2
        y: block.firstLineCenter - height / 2
        sourceComponent: TaskBox {
            id: box
            objectName: "taskBox"
            status: block.taskStatus
            onToggled: block.controller.toggleTask(block.index)
            // The editor's status menu (it follows the block while it is open).
            ownMenu: false
            onMenuRequested: block.editor.openTaskStatusMenu(block.index, box, 0, box.height)
        }
    }

    // ---- Content ---------------------------------------------------------------------------
    BlockText {
        id: textContent
        x: block.contentLeft
        y: block.topPad
        width: Math.max(40, block.contentRight - x)
        visible: textContent.live && !block.noTextLine
        live: block.textView && block.shown
        block: block
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
            case "code": return codeComponent
            case "calc": return calcComponent
            case "query": return queryComponent
            case "math": return mathComponent
            case "table": return tableComponent
            case "quote":
            case "callout": return calloutComponent
            case "rule": return ruleComponent
            case "frontmatter": return frontmatterComponent
            default: return null
            }
        }
    }

    EmbedView {
        id: embedView
        visible: block.hasEmbeds
        x: block.contentLeft
        y: block.embedsY
        width: Math.max(40, block.contentRight - x)
        embeds: block.hasEmbeds ? block.embeds : []
        onClicked: (target) => block.controller.activateLink(block.index, "page:" + encodeURIComponent(target), false)
        onDrawingClicked: (path) => block.controller.openDrawing(path)
        onBackgroundClicked: if (!block.editor.blockTapped(block.index)) block.editor.startEditing(block.index, -1)
        onBackgroundLongPressed: block.editor.longPressText(block.index)
        onShowWeb: (url) => block.controller.showWebPicture(url)
        onDownloadWeb: (url) => block.editor.downloadWebPictures(url)
        onWebMenu: (url, host, item, x, y) => block.editor.openWebPictureMenu(block.index, url, host, item, x, y)
        onWebOpen: (picture) => block.editor.openWebPictureViewer(block.index, picture)
    }

    TransclusionView {
        id: transclusionView
        visible: block.hasTransclusions
        x: block.contentLeft
        y: block.transclusionsY
        width: Math.max(40, block.contentRight - x)
        block: block
        transclusions: block.hasTransclusions ? block.transclusions : []
    }

    Component {
        id: calloutComponent
        Rectangle {
            id: frame
            readonly property bool callout: block.view === "callout"
            readonly property string type: block.model.calloutType
            readonly property color tint: {
                switch (type) {
                case "tip": case "success": case "check": case "done": return Theme.success
                case "warning": case "caution": case "attention": return Theme.warning
                case "danger": case "error": case "bug": case "failure": return Theme.marginRed // whatever the accent
                case "todo": case "info": return Theme.info
                case "quote": case "cite": return Theme.fgFaint
                default: return Theme.info
                }
            }
            readonly property real firstLineCenter: inner.y + text.firstLineCenter
            implicitHeight: inner.implicitHeight + (callout ? 2 * Theme.space3 : 0)
            height: implicitHeight
            radius: callout ? Theme.radius : 0
            color: callout ? Theme.alpha(tint, Theme.dark ? 0.12 : 0.10) : "transparent"
            Rectangle {
                width: 3
                height: parent.height
                radius: 1.5
                color: frame.callout ? frame.tint : Theme.borderStrong
            }
            Item {
                id: inner
                x: frame.callout ? Theme.space4 : Theme.space4
                y: frame.callout ? Theme.space3 : 0
                width: parent.width - x - (frame.callout ? Theme.space3 : 0)
                implicitHeight: text.implicitHeight
                BlockText {
                    id: text
                    width: parent.width
                    block: block
                    quoted: !frame.callout
                }
            }
        }
    }
    Component {
        id: codeComponent
        CodeBlockView { block: block }
    }
    Component {
        id: calcComponent
        CalcBlockView { block: block }
    }
    Component {
        id: queryComponent
        QueryBlockView { block: block }
    }
    Component {
        id: mathComponent
        MathBlockView { block: block }
    }
    Component {
        id: tableComponent
        TableBlockView { block: block }
    }
    Component {
        id: frontmatterComponent
        FrontmatterView { block: block }
    }
    Component {
        id: ruleComponent
        Item {
            id: rule
            readonly property real firstLineCenter: height / 2
            implicitHeight: block.editing ? ruleText.implicitHeight : block.editor.lineHeight
            height: implicitHeight
            Rectangle {
                visible: !block.editing
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 1
                color: Theme.border
            }
            TapHandler {
                enabled: !block.editing
                onTapped: if (!block.editor.blockTapped(block.index)) block.editor.startEditing(block.index, -1)
            }
            BlockText {
                id: ruleText
                visible: block.editing
                width: parent.width
                block: block
            }
        }
    }

    // ---- The selection's grips (touch selection mode) -----------------------------------------------
    // A round grip at the band's start (top left) and end (bottom right), 44 px to hold: the finger
    // moves that end of the selection (the editor follows it, also when this block scrolls away).
    component Grip: Item {
        id: grip
        property bool atStart: true
        objectName: atStart ? "selectionGripStart" : "selectionGripEnd"
        width: Theme.touchTarget
        height: Theme.touchTarget
        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: 18
            radius: 9
            color: Theme.link
            border.width: 2
            border.color: Theme.bg
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            onPressed: block.editor.beginGripDrag(grip.atStart)
        }
        Accessible.role: Accessible.Button
        Accessible.name: atStart ? qsTr("Start of the selection") : qsTr("End of the selection")
    }
    Loader {
        active: block.bandStart && block.editor.selectionGrips
        z: 5
        // (Inside the window: a document's band starts near the screen's edge on a phone.)
        x: Math.max(block.bandX, 12) - Theme.touchTarget / 2
        y: block.bandTop + 9 - Theme.touchTarget / 2
        sourceComponent: Grip { atStart: true }
    }
    Loader {
        active: block.bandEnd && block.editor.selectionGrips
        z: 5
        // (Inside the window: the band reaches the screen's edge on a phone.)
        x: Math.min(block.bandRight, block.width - 12) - Theme.touchTarget / 2
        y: block.bandBottom - 9 - Theme.touchTarget / 2
        sourceComponent: Grip { atStart: false }
    }

    // ---- Block menu ----------------------------------------------------------------------------
    /// The editor's block menu for this block (BlockMenu, one per editor, made when first opened).
    function openMenu(item: Item, x: real, y: real) {
        block.editor.openBlockMenu(block.index, item, x, y)
    }
}
