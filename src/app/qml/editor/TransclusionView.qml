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
// right-click or long press: the status menu). A click on the header opens the source; a click
// on the content edits the block with the embed.
Column {
    id: root

    property BlockDelegate block
    property var transclusions: []
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
    // come after its count.)
    height: {
        let h = 0
        void frames.revision
        for (let i = 0; i < frames.count; ++i) {
            const frame = frames.itemAt(i)
            if (frame)
                h += frame.height + (i > 0 ? spacing : 0)
        }
        return h
    }

    function iconFor(kind) {
        return kind === "heading" ? "heading" : kind === "block" ? "link" : "file-text"
    }
    function messageText(message, title) {
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
    function headingSize(level) {
        return level === 1 ? Theme.textLg : level === 2 ? Theme.textMd : root.bodySize
    }
    function open(href) {
        if (root.editor && href !== "")
            root.editor.activateLink(root.block.index, href)
    }
    /// A link inside the embedded text: tasks and reminders belong to the source block.
    function activate(link, path, line) {
        if (link.startsWith("task:") || link.startsWith("reminder:")) {
            if (path !== "") {
                const r = root.controller.openLocation(path, line, false)
                if (r && r.action === "scrollTo")
                    root.editor.revealRow(r.row)
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
        model: root.transclusions
        delegate: Rectangle {
            id: frame
            required property var modelData
            required property int index
            readonly property var entries: modelData.blocks || []
            width: root.width
            height: frameColumn.height + Theme.space2
            radius: Theme.radius
            color: Theme.dark ? Theme.alpha(Theme.bgSunken, 0.55) : Theme.alpha(Theme.bgSunken, 0.7)
            border.width: 1
            border.color: Theme.borderSubtle
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Embedded: %1").arg(modelData.title)

            // The source's edge: a thin line in the link colour.
            Rectangle {
                x: 0
                y: Theme.radius
                width: 2
                height: parent.height - 2 * Theme.radius
                radius: 1
                color: frame.modelData.found ? Theme.alpha(Theme.link, 0.55) : Theme.borderStrong
            }

            Column {
                id: frameColumn
                x: Theme.space3
                width: parent.width - Theme.space3 - Theme.space2
                spacing: 0
                height: {
                    let h = header.height
                    void entryRows.revision
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
                            name: root.iconFor(frame.modelData.kind)
                            size: Theme.iconSizeSm - 2
                            color: frame.modelData.found ? Theme.link : Theme.fgFaint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: frame.modelData.title
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            font.weight: Theme.weightMedium
                            font.underline: headerHover.hovered
                            color: frame.modelData.found ? Theme.link : Theme.linkMissing
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
                        onTapped: root.open(frame.modelData.href)
                    }
                    Accessible.role: Accessible.Link
                    Accessible.name: frame.modelData.title
                }

                // ---- Embedded blocks ------------------------------------------------------------------
                Repeater {
                    id: entryRows
                    property int revision: 0
                    onItemAdded: revision++
                    onItemRemoved: revision++
                    model: frame.entries
                    delegate: Item {
                        id: entry
                        required property var modelData
                        readonly property bool nested: modelData.kind === "embed"
                        readonly property string task: modelData.task || ""
                        readonly property bool isTask: !nested && task !== ""
                        readonly property bool closed: task === "done" || task === "cancelled"
                        readonly property int level: modelData.headingLevel || 0
                        readonly property bool rule: modelData.view === "rule"
                        readonly property int fontSize: entry.level > 0 ? root.headingSize(entry.level) : root.bodySize
                        readonly property int lineHeightPx: Theme.lineHeightPx(fontSize, entry.level > 0 ? 1.4 : 1.55)
                        readonly property real indent: Math.min(6, modelData.depth || 0) * root.indentStep
                        width: frameColumn.width
                        height: nested ? nestedHeader.topPadding + Math.max(Theme.iconSizeSm - 3, nestedTitle.implicitHeight) + Theme.space1
                              : rule ? Theme.space4
                              : Math.max(body.implicitHeight, isTask ? taskBox.implicitHeight : 0) + (entry.level > 0 ? Theme.space1 : 2)

                        // Guide lines for nested blocks (like the outline).
                        Repeater {
                            model: Math.min(6, entry.modelData.depth || 0)
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
                                name: entry.modelData.message === "cycle" ? "repeat" : "link"
                                size: Theme.iconSizeSm - 3
                                color: Theme.fgFaint
                            }
                            Text {
                                id: nestedTitle
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, nestedHeader.width - 20)
                                text: entry.nested ? entry.modelData.title + (entry.modelData.message !== ""
                                      ? " — " + root.messageText(entry.modelData.message, entry.modelData.title) : "") : ""
                                textFormat: Text.PlainText
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXs
                                font.italic: entry.modelData.message !== ""
                                color: entry.modelData.message !== "" ? Theme.fgFaint : Theme.link
                                elide: Text.ElideRight
                                TapHandler {
                                    gesturePolicy: TapHandler.ReleaseWithinBounds
                                    onTapped: root.open(entry.modelData.href || "")
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
                                     && (root.controller.queriesAvailable || entry.modelData.path === root.controller.path)
                            opacity: enabled ? 1 : 0.6
                            x: entry.indent - Math.round((width - boxSize) / 2) + 2
                            y: Math.round(entry.lineHeightPx / 2 - height / 2) + 1
                            onToggled: root.controller.toggleTaskAt(entry.modelData.path, entry.modelData.line, entry.task)
                            onStatusChosen: (status) => {
                                if (status !== entry.task)
                                    root.controller.setTaskStatusAt(entry.modelData.path, entry.modelData.line, status)
                            }
                        }

                        Text {
                            id: body
                            visible: !entry.nested && !entry.rule
                            x: entry.indent + (entry.isTask ? root.editor.taskBoxWidth + 2 : 0)
                            y: 1
                            width: parent.width - x
                            textFormat: Text.RichText
                            text: entry.nested || entry.rule ? "" : entry.modelData.html
                            wrapMode: Text.Wrap
                            font.family: entry.modelData.view === "code" ? Theme.fontMono : Theme.fontEditor
                            font.pixelSize: entry.modelData.view === "code" ? Theme.textSm : entry.fontSize
                            font.weight: entry.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
                            font.strikeout: entry.task === "cancelled"
                            lineHeightMode: Text.FixedHeight
                            lineHeight: entry.lineHeightPx
                            color: entry.closed ? Theme.fgFaint : entry.modelData.view === "quote" ? Theme.fgMuted : Theme.fg
                            linkColor: Theme.link
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text

                            HoverHandler {
                                enabled: !Theme.touchUi
                                cursorShape: body.linkAt(point.position.x, point.position.y) !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                            }
                        }
                        // Clicks go to an item above the rich text, not to the Text (which would
                        // pass mouse presses on to the editor's ListView: see BlockText.qml).
                        Item {
                            anchors.fill: body
                            visible: body.visible
                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: (eventPoint) => {
                                    if (root.editor.blockTapped(root.block.index))
                                        return // the touch selection mode
                                    const link = body.linkAt(eventPoint.position.x, eventPoint.position.y)
                                    if (link !== "")
                                        root.activate(link, entry.modelData.path || "", entry.modelData.line)
                                    else
                                        root.editor.startEditing(root.block.index, -1)
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
                    text: frame.modelData.message !== "" ? root.messageText(frame.modelData.message, frame.modelData.title) : ""
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
                    visible: (frame.modelData.more || 0) > 0
                    width: parent.width
                    topPadding: Theme.space1
                    bottomPadding: Theme.space1
                    text: qsTr("%n more block(s) — open the page", "", frame.modelData.more || 0)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.link
                    TapHandler { onTapped: root.open(frame.modelData.href) }
                    HoverHandler { enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }
}
