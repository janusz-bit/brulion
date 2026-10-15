pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockDelegate — one block of the BlockEditor (one row of BlockListModel): indentation with
// thin vertical guide lines (one per ancestor level, continuous across rows), the fold arrow
// (on hover; always shown when folded or on touch), the round bullet of list items (click:
// zoom into the block; right click / long press: block menu), the task checkbox, and the
// content for the block's `view` (rendered text / inline editor, code, calc, table, query,
// callout, quote, rule, frontmatter) with image embeds below.
//
// Hidden rows (zoom, folds) stay in the model with `visible` false: the delegate collapses to
// zero height.
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

    // Horizontal geometry: the text column of this depth, the bullet centred left of it.
    readonly property real levelX: editor.contentX + editor.gutter + depth * editor.indentStep
    readonly property real bulletX: levelX - editor.bulletColumn / 2
    readonly property real contentLeft: levelX + (isTask ? editor.taskBoxWidth : 0)
    readonly property real contentRight: editor.contentX + editor.contentWidth
    // Vertical rhythm: headings get air above them, framed blocks a little on both sides.
    readonly property bool framed: view === "code" || view === "calc" || view === "query" || view === "table"
                                   || view === "callout" || view === "frontmatter"
    readonly property real topPad: {
        if (headingLevel > 0 && index > 0)
            return headingLevel === 1 ? Theme.space5 : headingLevel === 2 ? Theme.space4 : Theme.space3
        if (framed)
            return Theme.space2
        return view === "rule" ? Theme.space2 : rowPad
    }
    /// Text rows: a little air between blocks; more on touch so 44 px targets fit the rows.
    readonly property real rowPad: Theme.touchUi ? 4 : 2
    readonly property real bottomPad: framed ? Theme.space2 : headingLevel > 0 ? Theme.space1 : rowPad
    /// y of the first line's x-height centre (bullet, checkbox, fold arrow).
    readonly property real firstLineCenter: topPad + (content.item ? content.item.firstLineCenter : editor.firstLineCenter(0))

    readonly property var embeds: model.embeds
    readonly property bool hasEmbeds: embeds !== undefined && embeds !== null && embeds.length > 0
                                      && (view === "text" || view === "heading" || view === "quote" || view === "callout")

    width: ListView.view ? ListView.view.width : 0
    height: shown ? Math.ceil(topPad + content.height + (hasEmbeds ? embedView.height + (model.onlyEmbeds ? 0 : Theme.space2) : 0) + bottomPad) : 0
    visible: shown
    clip: !shown

    Accessible.role: Accessible.ListItem
    Accessible.name: model.plainText

    /// Give the keyboard to the block content (tables: the first cell).
    function focusContent() {
        if (content.item && content.item.focusFirstCell)
            content.item.focusFirstCell()
    }

    HoverHandler { id: hover; enabled: !Theme.touchUi }

    // ---- Selection ---------------------------------------------------------------------------
    Rectangle {
        visible: block.model.isSelected && block.shown
        x: block.bulletX - block.editor.bulletColumn / 2 - 2
        width: block.contentRight - x + Theme.space2
        y: 1
        height: block.height - 2
        radius: Theme.radiusSm
        color: Theme.bgSelected
        border.width: 1
        border.color: Theme.alpha(Theme.focusRing, 0.35)
    }

    // ---- Guide lines -------------------------------------------------------------------------
    Repeater {
        model: block.shown ? block.depth : 0
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
        visible: block.shown && block.listItem && block.model.hasChildren && !block.model.collapsed
        x: Math.round(block.bulletX)
        y: Math.round(block.firstLineCenter + 9)
        width: 1
        height: Math.max(0, block.height - y)
        color: Theme.borderSubtle
    }

    // ---- Fold arrow ------------------------------------------------------------------------
    Item {
        id: arrow
        readonly property bool available: block.model.hasChildren && block.shown
        visible: available && (block.model.collapsed || block.hovered || Theme.touchUi || arrowTap.pressed)
        width: block.editor.arrowColumn
        height: Math.max(block.editor.arrowColumn, 24)
        x: block.bulletX - block.editor.bulletColumn / 2 - width + 2
        y: block.firstLineCenter - height / 2
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

    // ---- Bullet --------------------------------------------------------------------------------
    Item {
        id: bullet
        visible: block.listItem && block.shown
        width: block.editor.bulletColumn + (Theme.touchUi ? 6 : 0)
        height: Math.max(block.editor.bulletColumn, Theme.touchUi ? Theme.touchTarget : 22)
        x: block.bulletX - width / 2
        y: block.firstLineCenter - height / 2
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Zoom into block")

        // Folded blocks show a halo around the bullet (Logseq style); hover shows it too.
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 16
            radius: 8
            color: block.model.collapsed ? Theme.bgPressed : Theme.bgHover
            visible: block.model.collapsed || bulletHover.hovered
        }
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: block.closedTask ? Theme.fgFaint : bulletHover.hovered ? Theme.fg : Theme.fgMuted
        }
        HoverHandler { id: bulletHover; cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: block.controller.zoomInto(block.index)
            onLongPressed: blockMenu.popup(bullet, bullet.width / 2, bullet.height)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: blockMenu.popup(bullet, bullet.width / 2, bullet.height)
        }
    }

    // ---- Task checkbox ---------------------------------------------------------------------------
    TaskBox {
        visible: block.isTask && block.shown
        status: block.taskStatus
        x: block.levelX + (block.editor.taskBoxWidth - boxSize) / 2 - 2 - (width - boxSize) / 2
        y: block.firstLineCenter - height / 2
        onToggled: block.controller.toggleTask(block.index)
    }

    // ---- Content ---------------------------------------------------------------------------
    Loader {
        id: content
        x: block.view === "callout" || block.view === "quote" || block.framed ? block.levelX : block.contentLeft
        y: block.topPad
        width: Math.max(40, block.contentRight - x)
        active: block.shown
        sourceComponent: {
            switch (block.view) {
            case "code": return codeComponent
            case "calc": return calcComponent
            case "query": return queryComponent
            case "table": return tableComponent
            case "quote":
            case "callout": return calloutComponent
            case "rule": return ruleComponent
            case "frontmatter": return frontmatterComponent
            default: return textComponent
            }
        }
    }

    EmbedView {
        id: embedView
        visible: block.hasEmbeds
        x: block.contentLeft
        y: block.topPad + content.height + (block.model.onlyEmbeds ? 0 : Theme.space2)
        width: Math.max(40, block.contentRight - x)
        embeds: block.hasEmbeds ? block.embeds : []
        onClicked: (target) => block.controller.activateLink(block.index, "page:" + encodeURIComponent(target), false)
    }

    Component {
        id: textComponent
        BlockText { block: block }
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
                case "danger": case "error": case "bug": case "failure": return Theme.accent
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
                onTapped: block.editor.startEditing(block.index, -1)
            }
            BlockText {
                id: ruleText
                visible: block.editing
                width: parent.width
                block: block
            }
        }
    }

    // ---- Block menu ----------------------------------------------------------------------------
    BMenu {
        id: blockMenu
        BMenuItem {
            text: qsTr("Zoom in")
            iconName: "search"
            onTriggered: block.controller.zoomInto(block.index)
        }
        BMenuItem {
            visible: block.model.hasChildren
            height: visible ? implicitHeight : 0
            text: block.model.collapsed ? qsTr("Expand") : qsTr("Collapse")
            iconName: block.model.collapsed ? "chevrons-up-down" : "chevrons-down-up"
            shortcut: "Ctrl+."
            onTriggered: block.controller.toggleFold(block.index)
        }
        BMenuItem {
            text: qsTr("Copy block reference")
            iconName: "link"
            onTriggered: block.controller.copyBlockRef(block.index)
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Indent")
            iconName: "list-indent-increase"
            shortcut: "Tab"
            onTriggered: block.controller.indent(block.index)
        }
        BMenuItem {
            text: qsTr("Outdent")
            iconName: "list-indent-decrease"
            shortcut: "Shift+Tab"
            onTriggered: block.controller.outdent(block.index)
        }
        BMenuItem {
            text: qsTr("Move up")
            iconName: "move-up"
            shortcut: "Alt+Up"
            onTriggered: block.controller.moveUp(block.index)
        }
        BMenuItem {
            text: qsTr("Move down")
            iconName: "move-down"
            shortcut: "Alt+Down"
            onTriggered: block.controller.moveDown(block.index)
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Delete block")
            iconName: "trash-2"
            danger: true
            onTriggered: block.controller.deleteBlock(block.index)
        }
    }
}
