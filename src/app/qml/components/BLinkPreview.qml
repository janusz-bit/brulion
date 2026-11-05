pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BLinkPreview — the popup of a link's hover preview (docs/UI.md "Link previews"), opened by
// LinkPreviews: the page, section or block a [[link]] or ((reference)) opens, read-only and
// rendered like an embed (LinkPreview: InlineRenderer's escaped rich text in BRichText — never a
// note's own HTML), scrollable. The header names it (a click opens it; a Ctrl+click or a middle
// click in a new tab — OpenIn); a click on a link inside follows that link (in a new tab alike). A page that does not exist yet says so (a
// click on the header creates it). It never takes the keyboard focus: the note being written
// keeps it.
//
//   BLinkPreview { href: "page:Dom"; fromPath: "Plan.md"; host: Workspace
//                  onActivated: (url, path, newPane) => … }
//   preview.show(x, y)   // under (x, y) of its parent, above it near the bottom
T.Popup {
    id: popup

    property string href
    property string fromPath
    property EditorHost host: null
    /// The pointer is over the popup.
    readonly property bool pointerInside: popupHover.hovered
    /// The previewed text (LinkPreview.preview).
    readonly property var info: preview.preview
    /// Its rows, each with every key the delegate reads (embed headers and blocks differ).
    readonly property list<var> entries: UiModels.withDefaults(UiModels.field(preview.preview, "blocks"), {
        kind: "", view: "", task: "", headingLevel: 0, depth: 0, title: "", html: "", raw: "", path: ""
    })
    readonly property bool found: preview.preview.found === true
    readonly property string infoTitle: UiModels.stringField(preview.preview, "title")
    readonly property string infoKind: UiModels.stringField(preview.preview, "kind")
    readonly property string infoMessage: UiModels.stringField(preview.preview, "message")
    readonly property int infoMore: UiModels.numberField(preview.preview, "more", 0)
    /// Room for the text (the rest scrolls).
    readonly property real maxBodyHeight: Math.max(120, Math.min(360, (parent ? parent.height : 600) * 0.5))

    /// A link was clicked: follow `url` (written in `path`).
    signal activated(string url, string path, bool newPane)

    readonly property var rendererPalette: ({
        link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
        codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        tagFg: Theme.tagFg, tagBg: Theme.tagBg,
        dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
        overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
        scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
        reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
        alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
        recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
        doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
        neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
    })

    /// Opens (or moves) the preview at the pointer (x, y) of its parent: under it, above it when
    /// there is no room below. Nothing opens for a link without a preview (an attachment).
    function show(px: real, py: real): void {
        if (shownKey === href + "\n" + fromPath)
            preview.refresh() // (the same link again: the note may have changed)
        shownKey = href + "\n" + fromPath
        if (!preview.available) {
            close()
            return
        }
        flick.contentY = 0
        const margin = Theme.space2
        const w = parent ? parent.width : width
        const h = parent ? parent.height : height
        x = Math.max(margin, Math.min(px - Theme.space4, w - width - margin))
        const below = py + 18
        const above = py - 10 - implicitHeight
        y = below + implicitHeight <= h - margin || above < margin ? Math.max(margin, Math.min(below, h - implicitHeight - margin))
                                                                     : above
        open()
    }
    property string shownKey
    function openLink(url: string, path: string, newPane: bool): bool {
        return preview.openLink(url, path, newPane)
    }
    function messageText(message: string, title: string): string {
        switch (message) {
        case "missingPage": return qsTr("“%1” does not exist yet. Click the title to create it.").arg(title)
        case "missingSection": return qsTr("There is no such section on this page.")
        case "missingBlock": return qsTr("This block was not found.")
        case "empty": return qsTr("Nothing here yet.")
        default: return ""
        }
    }
    /// `link` when a click on it in the preview follows it ("" for a task's checkbox, a reminder
    /// and the chips a click edits in the note: the preview changes nothing).
    function followable(link: string): string {
        return Js.startsWith(link, "task:") || Js.startsWith(link, "reminder:") || Js.startsWith(link, "date:")
               || Js.startsWith(link, "recur:") || Js.startsWith(link, "prio:") || Js.startsWith(link, "day:") ? "" : link
    }
    function iconFor(kind: string): string {
        return kind === "heading" ? "heading" : kind === "block" ? "link" : "file-text"
    }
    function headingSize(level: int): int {
        return level === 1 ? Theme.textLg : level === 2 ? Theme.textMd : Theme.textBase
    }

    focus: false
    modal: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    padding: 0
    width: Math.min(440, (parent ? parent.width : 440) - 2 * Theme.space2)
    implicitHeight: body.height

    LinkPreview {
        id: preview
        host: popup.host
        href: popup.href
        sourcePath: popup.fromPath
        palette: popup.rendererPalette
        locale: DatesHelper.locale
        now: DatesHelper.fixedNow
    }

    background: Item {
        BShadow {
            target: previewBg
            level: 2
        }
        Rectangle {
            id: previewBg
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.dark ? Theme.bgElevated2 : Theme.bgElevated
            border.width: 1
            border.color: Theme.border
        }
    }

    contentItem: Item {
        // (On the content, not the background: hover goes to the item under the pointer and its
        // parents, not to the items beneath.)
        HoverHandler {
            id: popupHover
        }
        // Laid out with bindings, not positioners: the popup has its size when show() places it.
        Item {
            id: body
            objectName: "linkPreviewBody"
            width: parent.width
            height: more.y + (more.visible ? more.height : 0) + Theme.space2

            // ---- Header: what the link opens ----------------------------------------------------
            Item {
                id: header
                objectName: "linkPreviewHeader"
                x: Theme.space3
                y: Theme.space2
                width: parent.width - 2 * Theme.space3
                height: Math.max(24, title.implicitHeight)
                Accessible.role: Accessible.Link
                Accessible.name: popup.infoTitle
                Accessible.onPressAction: popup.activated(popup.href, popup.fromPath, false)
                BIcon {
                    id: kindIcon
                    anchors.verticalCenter: parent.verticalCenter
                    name: popup.iconFor(popup.infoKind !== "" ? popup.infoKind : "page")
                    size: Theme.iconSizeSm - 2
                    color: popup.found ? Theme.link : Theme.fgFaint
                }
                Text {
                    id: title
                    objectName: "linkPreviewTitle"
                    anchors.verticalCenter: parent.verticalCenter
                    x: kindIcon.width + Theme.space1 + 2
                    width: parent.width - x - openIcon.width - Theme.space1
                    text: popup.infoTitle
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightMedium
                    font.underline: headerHover.hovered
                    color: popup.found ? Theme.link : Theme.linkMissing
                    elide: Text.ElideRight
                }
                BIcon {
                    id: openIcon
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    name: "external-link"
                    size: Theme.iconSizeSm - 2
                    color: Theme.fgFaint
                    opacity: headerHover.hovered ? 1 : 0.6
                }
                HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: headerTap
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    acceptedButtons: OpenIn.tabs ? Qt.LeftButton | Qt.MiddleButton : Qt.LeftButton
                    // Ctrl+click, a middle click: in a new tab (OpenIn).
                    onTapped: (eventPoint, button) => {
                        OpenIn.begin(OpenIn.clickTarget(button, headerTap.point.modifiers))
                        popup.activated(popup.href, popup.fromPath, false)
                        OpenIn.end()
                    }
                }
            }

            // Why there is nothing to show (a page not written yet, a block not found…).
            Text {
                id: message
                objectName: "linkPreviewMessage"
                visible: text !== ""
                x: Theme.space3
                y: header.y + header.height + Theme.space1
                width: parent.width - 2 * Theme.space3
                text: popup.messageText(popup.infoMessage, popup.infoTitle)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.italic: true
                color: Theme.fgMuted
            }

            // ---- The text, scrollable ----------------------------------------------------------
            Flickable {
                id: flick
                visible: popup.entries.length > 0
                y: message.visible ? message.y + message.height + Theme.space1 : message.y
                width: parent.width
                height: visible ? Math.min(entryColumn.height, popup.maxBodyHeight) : 0
                contentWidth: width
                contentHeight: entryColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                T.ScrollBar.vertical: BScrollBar {}

                Item {
                    id: entryColumn
                    x: Theme.space3
                    width: flick.width - 2 * Theme.space3
                    // The last row's bottom (rows below one another: each starts where the one
                    // above it ends).
                    height: {
                        if (entryRows.revision < 0 || entryRows.count === 0)
                            return 0
                        const last = entryRows.itemAt(entryRows.count - 1)
                        return last ? last.y + last.height : 0
                    }
                    Repeater {
                        id: entryRows
                        property int revision: 0
                        onItemAdded: revision++
                        onItemRemoved: revision++
                        model: popup.entries
                        delegate: Item {
                            id: entry
                            required property int index
                            required property string kind
                            required property string view
                            required property string task
                            required property int headingLevel
                            required property int depth
                            required property string title
                            required property string html
                            required property string raw
                            required property string path
                            y: {
                                if (entryRows.revision < 0 || entry.index === 0)
                                    return 0
                                const above = entryRows.itemAt(entry.index - 1)
                                return above ? above.y + above.height : 0
                            }
                            readonly property bool nested: kind === "embed"
                            readonly property bool rule: view === "rule"
                            readonly property bool isTask: !nested && task !== ""
                            readonly property int level: headingLevel
                            readonly property bool code: view === "code"
                            readonly property int fontSize: code ? Theme.textSm : popup.headingSize(level)
                            readonly property real lineHeightPx: Theme.lineHeightPx(fontSize, level > 0 ? 1.35 : 1.5)
                            readonly property real indent: Math.min(6, depth) * 16
                            property string hoveredLink: ""
                            function trackLink(x: real, y: real): void {
                                const link = popup.followable(text.linkAt(x - text.x, y - text.y))
                                if (link !== entry.hoveredLink)
                                    entry.hoveredLink = link
                            }
                            width: entryColumn.width
                            height: nested ? 24 : rule ? Theme.space3 : text.height + (level > 0 ? Theme.space1 : 2)

                            Rectangle {
                                visible: entry.rule
                                x: entry.indent
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - x
                                height: 1
                                color: Theme.border
                            }
                            // A nested embed: its source (or why it is not shown).
                            Text {
                                visible: entry.nested
                                x: entry.indent
                                width: parent.width - x
                                anchors.verticalCenter: parent.verticalCenter
                                text: entry.nested ? "↳ " + entry.title : ""
                                textFormat: Text.PlainText
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXs
                                color: Theme.fgFaint
                                elide: Text.ElideRight
                            }
                            // A task's status (read-only here: the preview changes nothing).
                            TaskBox {
                                visible: entry.isTask
                                enabled: false
                                status: entry.task || "todo"
                                x: entry.indent
                                y: Math.round(entry.lineHeightPx / 2 - height / 2) + 1
                            }
                            BRichText {
                                id: text
                                visible: !entry.nested && !entry.rule
                                x: entry.indent + (entry.isTask ? 22 : 0)
                                y: 1
                                width: parent.width - x
                                html: visible ? entry.html : ""
                                hoveredLink: entry.hoveredLink
                                font.family: entry.code ? Theme.fontMono : Theme.fontEditor
                                font.pixelSize: entry.fontSize
                                font.weight: entry.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
                                font.strikeout: entry.task === "cancelled"
                                lineHeight: entry.lineHeightPx
                                color: entry.task === "done" || entry.task === "cancelled" ? Theme.fgFaint
                                       : entry.view === "quote" ? Theme.fgMuted : Theme.fg
                                accessibleName: entry.raw
                            }
                            HoverHandler {
                                id: entryHover
                                cursorShape: entry.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onPointChanged: entry.trackLink(point.position.x, point.position.y)
                                onHoveredChanged: if (!hovered) entry.hoveredLink = ""
                            }
                            TapHandler {
                                id: entryTap
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                acceptedButtons: OpenIn.tabs ? Qt.LeftButton | Qt.MiddleButton : Qt.LeftButton
                                onTapped: (eventPoint, button) => {
                                    const link = popup.followable(text.linkAt(eventPoint.position.x - text.x,
                                                                              eventPoint.position.y - text.y))
                                    if (link === "")
                                        return
                                    OpenIn.begin(OpenIn.clickTarget(button, entryTap.point.modifiers))
                                    popup.activated(link, entry.path !== "" ? entry.path : popup.fromPath, false)
                                    OpenIn.end()
                                }
                            }
                        }
                    }
                }
            }

            // Blocks left out (MaxTransclusionBlocks).
            Text {
                id: more
                visible: popup.infoMore > 0
                x: Theme.space3
                y: flick.visible ? flick.y + flick.height + Theme.space1 : flick.y
                width: parent.width - 2 * Theme.space3
                text: qsTr("… and %n more block(s)", "", popup.infoMore)
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgFaint
            }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
