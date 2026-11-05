pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TransclusionView — pages, sections and blocks embedded in a block (`![[Page]]`,
// `![[Page#Heading]]`, `![[Page#^id]]`, `![[#Heading]]`, `!((id))`; BlockListModel role
// `transclusions`): a quiet frame with the source as a header link ("Budżet domowy ›
// Październik") and the embedded blocks rendered read-only like the editor renders them —
// headings, links, tags, chips, nested embeds (depth-limited, a cycle is named instead of
// repeated). Tasks inside keep a working checkbox (it changes the task in its own file; a
// right-click or long press: the status menu; while it is open, changed embeds wait — new rows
// would take the menu away — and come a moment after it closed). A right-click or long press on a
// date, reminder, 🔁 or priority chip opens its menu (the change goes to the source's file, with a
// toast to undo it), on a link the link's menu. A click on the header opens the source; a click on
// the content edits the block with the embed. Tags and chips are rounded shapes (BRichText); the
// mouse pointer resting on a [[page]] or ((block)) link shows its preview (LinkPreviews).
Column {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    property list<var> transclusions
    /// A task's status menu is open in the embeds (or closed a moment ago: the menu is still
    /// closing on its row): they wait, `frozen` is shown.
    readonly property bool waiting: menuBox !== null || releaseTimer.running
    /// The embeds shown: `transclusions`, except while waiting — then the ones from when the menu
    /// opened.
    readonly property list<var> shown: waiting ? frozen : transclusions
    /// The checkbox whose status menu is open (null: none; also when it is destroyed).
    property TaskBox menuBox: null
    property list<var> frozen
    /// The embedded block whose link menu is open (its link opens there: onLinkMenuOpenRequested).
    property string menuPath: ""
    property int menuLine: -1
    readonly property BlockEditor editor: block ? block.editor : null
    readonly property PageController controller: block ? block.controller : null
    /// Centre of the first header line (the block's bullet aligns with it).
    readonly property real firstLineCenter: headerHeight / 2
    readonly property real headerHeight: Theme.touchUi ? 36 : 28
    readonly property real indentStep: Theme.compact ? 16 : 20
    readonly property int bodySize: Theme.textEditor

    spacing: Theme.space2
    // The heights are bindings, not the Columns' own (a positioner sets those only in its next
    // polish): the block has its final height when the editor's list creates it (docs/UI.md
    // "Scrolling performance"). (`revision`: itemAt() notifies nothing, a Repeater's items may
    // come after its count; read for real — qmlcachegen compiles a bare `void frames.revision` away
    // with the dependency.)
    height: {
        if (frames.revision < 0)
            return 0
        let h = 0
        for (let i = 0; i < frames.count; ++i) {
            const frame = frames.itemAt(i)
            if (frame)
                h += frame.height + (i > 0 ? spacing : 0)
        }
        return h
    }

    function iconFor(kind: string): string {
        return kind === "heading" ? "heading" : kind === "block" ? "link" : "file-text"
    }
    function messageText(message: string, title: string): string {
        switch (message) {
        case "missingPage": return qsTr("“%1” does not exist yet.").arg(title)
        case "missingSection": return qsTr("There is no such section on this page.")
        case "missingBlock": return qsTr("This block was not found.")
        case "empty": return qsTr("Nothing here yet.")
        case "cycle": return qsTr("Already shown above (it embeds itself).")
        case "depth": return qsTr("Embedded too deep to show here.")
        default: return ""
        }
    }
    function headingSize(level: int): int {
        return level === 1 ? Theme.textLg : level === 2 ? Theme.textMd : root.bodySize
    }
    function open(href: string): void {
        if (root.editor && href !== "")
            root.editor.activateLink(root.block.index, href)
    }
    /// A checkbox's status menu opened or closed.
    function menuShown(box: TaskBox, open: bool): void {
        if (open) {
            if (root.menuBox !== box) {
                root.frozen = root.shown // the same array: the rows stay
                root.menuBox = box
            }
        } else if (root.menuBox === box) {
            releaseTimer.restart()
            root.menuBox = null
        }
    }
    /// An embedded task's line in its file now: while the rows waited for a menu, the file may
    /// have changed (a line added above the task). The same block of the same file in the embeds
    /// as they are now (the nearest one when the text repeats); -1 when it is not there any more.
    function currentLine(path: string, line: int, html: string): int {
        if (!root.waiting)
            return line
        return BlockViews.transclusionTaskLine(root.transclusions, path, html, line)
    }
    function setStatus(path: string, line: int, html: string, status: string): void {
        const now = currentLine(path, line, html)
        if (now >= 0)
            root.controller.setTaskStatusAt(path, now, status)
    }

    Timer {
        id: releaseTimer
        interval: 0
    }

    /// The menu of the token or link at (x, y) of an embedded block's text (`data`: its entry;
    /// the change goes to the block's own file); false when there is none there.
    /// The link at (x, y) of `text` ("" none).
    function linkIn(text: BRichText, x: real, y: real): string {
        return text.linkAt(x, y)
    }
    function linkMenu(path: string, line: int, raw: string, text: BRichText, x: real, y: real): bool {
        const link = root.linkIn(text, x, y)
        if (link === "" || !root.editor)
            return false
        if (root.editor.openTokenMenuIn(path, line, raw, link, text, x, y))
            return true
        root.menuPath = path
        root.menuLine = line
        return root.editor.openLinkMenu(link, path, root.block.index, root, text, x, y)
    }
    Connections {
        target: root.editor
        function onLinkMenuOpenRequested(requester: Item, link: string, newPane: bool) {
            if (requester !== root)
                return
            if (newPane)
                root.controller.activateLink(root.block.index, link, true)
            else
                root.activate(link, root.menuPath, root.menuLine)
        }
    }
    /// What controller.openLocation() answered.
    MapHolder { id: answer }
    /// A link inside the embedded text: tasks and reminders belong to the source block; a click on
    /// a date, 🔁 or priority chip edits the block with the embed (their menu is the right button's).
    function activate(link: string, path: string, line: int): void {
        if (root.editor.editsOnClick(link)) {
            root.editor.startEditing(root.block.index, -1)
            return
        }
        if (Js.startsWith(link, "task:") || Js.startsWith(link, "reminder:")) {
            if (path !== "") {
                answer.map = root.controller.openLocation(path, line, false)
                if (answer.map.action === "scrollTo")
                    root.editor.revealRow(answer.map.row)
            }
            return
        }
        root.editor.activateLink(root.block.index, link)
    }

    Repeater {
        id: frames
        property int revision: 0
        onItemAdded: revision++
        onItemRemoved: revision++
        model: root.shown
        delegate: Rectangle {
            id: frame
            required property var modelData
            required property int index
            /// The embed: { kind, target, title, href, path, found, message, more, blocks }.
            readonly property MapHolder rec: MapHolder { map: frame.modelData }
            readonly property bool found: frame.rec.map.found === true
            readonly property string title: frame.rec.map.title
            readonly property string href: frame.rec.map.href
            readonly property string message: frame.rec.map.message
            readonly property int more: frame.rec.map.more || 0
            function entries(): list<var> {
                return frame.rec.map.blocks
            }
            width: root.width
            height: frameColumn.height + Theme.space2
            radius: Theme.radius
            color: Theme.dark ? Theme.alpha(Theme.bgSunken, 0.55) : Theme.alpha(Theme.bgSunken, 0.7)
            border.width: 1
            border.color: Theme.borderSubtle
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Embedded: %1").arg(frame.title)

            // The source's edge: a thin line in the link colour.
            Rectangle {
                x: 0
                y: Theme.radius
                width: 2
                height: parent.height - 2 * Theme.radius
                radius: 1
                color: frame.found ? Theme.alpha(Theme.link, 0.55) : Theme.borderStrong
            }

            Column {
                id: frameColumn
                x: Theme.space3
                width: parent.width - Theme.space3 - Theme.space2
                spacing: 0
                height: {
                    if (entryRows.revision < 0) // (see the view's height)
                        return 0
                    let h = header.height
                    for (let i = 0; i < entryRows.count; ++i) {
                        const row = entryRows.itemAt(i)
                        if (row)
                            h += row.height
                    }
                    return h + (messageText.visible ? messageText.implicitHeight : 0)
                           + (moreText.visible ? moreText.implicitHeight : 0)
                }

                // ---- Header: the source as a link -----------------------------------------------------
                Item {
                    id: header
                    width: parent.width
                    height: root.headerHeight
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.space1 + 2
                        BIcon {
                            name: root.iconFor(frame.rec.map.kind)
                            size: Theme.iconSizeSm - 2
                            color: frame.found ? Theme.link : Theme.fgFaint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: frame.title
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            font.weight: Theme.weightMedium
                            font.underline: headerHover.hovered
                            color: frame.found ? Theme.link : Theme.linkMissing
                            elide: Text.ElideRight
                        }
                        BIcon {
                            visible: headerHover.hovered || Theme.touchUi
                            name: "external-link"
                            size: Theme.iconSizeSm - 2
                            color: Theme.fgFaint
                        }
                    }
                    HoverHandler { id: headerHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.open(frame.href)
                    }
                    Accessible.role: Accessible.Link
                    Accessible.name: frame.title
                }

                // ---- Embedded blocks ------------------------------------------------------------------
                Repeater {
                    id: entryRows
                    property int revision: 0
                    onItemAdded: revision++
                    onItemRemoved: revision++
                    model: frame.entries()
                    delegate: Item {
                        id: entry
                        required property var modelData
                        /// The entry: a block { kind "block", html, depth, view, headingLevel, task,
                        /// path, line, raw } or a nested embed { kind "embed", title, href, depth,
                        /// found, message, path }.
                        readonly property MapHolder rec: MapHolder { map: entry.modelData }
                        readonly property bool nested: rec.map.kind === "embed"
                        readonly property string task: rec.map.task || ""
                        readonly property bool isTask: !nested && task !== ""
                        readonly property bool closed: task === "done" || task === "cancelled"
                        readonly property int level: rec.map.headingLevel || 0
                        readonly property bool rule: rec.map.view === "rule"
                        readonly property string view: rec.map.view || ""
                        readonly property string path: rec.map.path || ""
                        readonly property int line: rec.map.line || 0
                        readonly property string raw: rec.map.raw || ""
                        readonly property string html: rec.map.html || ""
                        readonly property string title: rec.map.title || ""
                        readonly property string message: rec.map.message || ""
                        readonly property int depth: rec.map.depth || 0
                        readonly property int fontSize: entry.level > 0 ? root.headingSize(entry.level) : root.bodySize
                        readonly property int lineHeightPx: Theme.lineHeightPx(fontSize, entry.level > 0 ? 1.4 : 1.55)
                        readonly property real indent: Math.min(6, entry.depth) * root.indentStep
                        /// The link under the mouse or the pressing finger, underlined (BlockText.qml).
                        property string hoveredLink: ""
                        /// Where a right-click was, in the scene (the block menu's place).
                        property point scene
                        function trackLink(x: real, y: real): void {
                            const link = body.linkAt(x, y)
                            const h = link !== "" && !Js.startsWith(link, "task:") && !root.editor.editsOnClick(link) ? link : ""
                            if (h !== entry.hoveredLink)
                                entry.hoveredLink = h
                        }
                        width: frameColumn.width
                        height: nested ? nestedHeader.topPadding + Math.max(Theme.iconSizeSm - 3, nestedTitle.implicitHeight) + Theme.space1
                              : rule ? Theme.space4
                              : Math.max(body.implicitHeight, isTask ? taskBox.implicitHeight : 0) + (entry.level > 0 ? Theme.space1 : 2)

                        // Guide lines for nested blocks (like the outline).
                        Repeater {
                            model: Math.min(6, entry.depth)
                            delegate: Rectangle {
                                required property int index
                                x: index * root.indentStep + 4
                                width: 1
                                height: entry.height
                                color: Theme.borderSubtle
                            }
                        }

                        Rectangle {
                            visible: entry.rule
                            x: entry.indent
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - entry.indent
                            height: 1
                            color: Theme.border
                        }

                        // A nested embed: its own source line (or why it is not shown).
                        Row {
                            id: nestedHeader
                            visible: entry.nested
                            x: entry.indent
                            width: parent.width - x
                            spacing: Theme.space1 + 2
                            topPadding: 2
                            BIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: entry.message === "cycle" ? "repeat" : "link"
                                size: Theme.iconSizeSm - 3
                                color: Theme.fgFaint
                            }
                            Text {
                                id: nestedTitle
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, nestedHeader.width - 20)
                                text: entry.nested ? entry.title + (entry.message !== ""
                                      ? " — " + root.messageText(entry.message, entry.title) : "") : ""
                                textFormat: Text.PlainText
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXs
                                font.italic: entry.message !== ""
                                color: entry.message !== "" ? Theme.fgFaint : Theme.link
                                elide: Text.ElideRight
                                TapHandler {
                                    gesturePolicy: TapHandler.ReleaseWithinBounds
                                    onTapped: root.open(entry.rec.map.href || "")
                                }
                                HoverHandler { enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                            }
                        }

                        TaskBox {
                            id: taskBox
                            objectName: "embedTaskBox"
                            visible: entry.isTask
                            status: entry.task
                            enabled: root.controller !== null
                                     && (root.controller.queriesAvailable || entry.path === root.controller.path)
                            opacity: enabled ? 1 : 0.6
                            x: entry.indent - Math.round((width - boxSize) / 2) + 2
                            y: Math.round(entry.lineHeightPx / 2 - height / 2) + 1
                            onToggled: root.controller.toggleTaskAt(entry.path, root.currentLine(entry.path, entry.line, entry.html), entry.task)
                            onStatusChosen: (status) => {
                                if (status !== entry.task)
                                    root.setStatus(entry.path, entry.line, entry.html, status)
                            }
                            onMenuOpenChanged: root.menuShown(taskBox, menuOpen)
                        }

                        BRichText {
                            id: body
                            visible: !entry.nested && !entry.rule
                            x: entry.indent + (entry.isTask ? root.editor.taskBoxWidth + 2 : 0)
                            y: 1
                            width: parent.width - x
                            html: entry.nested || entry.rule ? "" : entry.html
                            hoveredLink: entry.hoveredLink
                            font.family: entry.view === "code" ? Theme.fontMono : Theme.fontEditor
                            font.pixelSize: entry.view === "code" ? Theme.textSm : entry.fontSize
                            font.weight: entry.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
                            font.strikeout: entry.task === "cancelled"
                            lineHeight: entry.lineHeightPx
                            color: entry.closed ? Theme.fgFaint : entry.view === "quote" ? Theme.fgMuted : Theme.fg
                            accessibleName: entry.raw

                            HoverHandler {
                                id: bodyHover
                                enabled: !Theme.touchUi
                                cursorShape: body.linkAt(point.position.x, point.position.y) !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                                onPointChanged: {
                                    entry.trackLink(point.position.x, point.position.y)
                                    // The link under the resting pointer: its preview (LinkPreviews).
                                    LinkPreviews.hover(body, entry.hoveredLink, entry.path,
                                                       root.controller ? root.controller.host : null,
                                                       point.position.x, point.position.y)
                                }
                                onHoveredChanged: {
                                    if (!hovered) {
                                        LinkPreviews.leave(body)
                                        if (!bodyTap.pressed)
                                            entry.hoveredLink = ""
                                    }
                                }
                            }
                        }
                        BLinkTargets {
                            x: body.x
                            y: body.y
                            textItem: body.textItem
                            html: entry.nested || entry.rule ? "" : entry.html
                            lineHeight: entry.lineHeightPx
                            shown: body.visible
                            onActivated: (href, x, y) => {
                                if (root.editor.editsOnClick(href)) // a date chip: its menu
                                    root.linkMenu(entry.path, entry.line, entry.raw, body, x, y)
                                else
                                    root.activate(href, entry.path, entry.line)
                            }
                        }
                        // Clicks go to an item above the rich text, not to the Text (which would
                        // pass mouse presses on to the editor's ListView: see BlockText.qml).
                        Item {
                            anchors.fill: body
                            visible: body.visible
                            TapHandler {
                                id: bodyTap
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onPressedChanged: {
                                    if (pressed)
                                        entry.trackLink(bodyTap.point.position.x, bodyTap.point.position.y)
                                    else if (!bodyHover.hovered)
                                        entry.hoveredLink = ""
                                }
                                onTapped: (eventPoint) => {
                                    const x = eventPoint.position.x
                                    const y = eventPoint.position.y
                                    if (root.editor.blockTapped(root.block.index))
                                        return // the touch selection mode
                                    const link = body.linkAt(x, y)
                                    if (link !== "")
                                        root.activate(link, entry.path, entry.line)
                                    else
                                        root.editor.startEditing(root.block.index, -1)
                                }
                            }
                            // A right-click or a finger held on a token or a link: its menu.
                            TapHandler {
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                                acceptedButtons: Qt.RightButton
                                gesturePolicy: TapHandler.WithinBounds
                                onTapped: (eventPoint) => {
                                    const x = eventPoint.position.x
                                    const y = eventPoint.position.y
                                    entry.scene = eventPoint.scenePosition
                                    if (!root.linkMenu(entry.path, entry.line, entry.raw, body, x, y))
                                        root.editor.openBlockMenuAt(root.block.index, entry.scene)
                                }
                            }
                            TapHandler {
                                id: embedHold
                                enabled: Theme.touchUi
                                acceptedDevices: PointerDevice.TouchScreen
                                onLongPressed: {
                                    if (!root.editor.selectionMode)
                                        root.linkMenu(entry.path, entry.line, entry.raw, body, embedHold.point.position.x, embedHold.point.position.y)
                                }
                            }
                        }
                    }
                }

                // ---- Why nothing is shown / what is left out ---------------------------------------------
                Text {
                    id: messageText
                    visible: text !== ""
                    width: parent.width
                    bottomPadding: Theme.space1
                    text: frame.message !== "" ? root.messageText(frame.message, frame.title) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    font.italic: true
                    color: Theme.fgFaint
                    wrapMode: Text.Wrap
                    TapHandler { onTapped: if (!root.editor.blockTapped(root.block.index)) root.editor.startEditing(root.block.index, -1) }
                }
                Text {
                    id: moreText
                    visible: frame.more > 0
                    width: parent.width
                    topPadding: Theme.space1
                    bottomPadding: Theme.space1
                    text: qsTr("%n more block(s) — open the page", "", frame.more)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.link
                    TapHandler { onTapped: root.open(frame.href) }
                    HoverHandler { enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }
}
