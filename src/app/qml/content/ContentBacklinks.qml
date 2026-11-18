pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentBacklinks — "Linked references" of a page: the blocks of other notes that link to it or carry a
// tag naming it (docs/SYNTAX.md §3), grouped by page (journals newest first; a page tagged as a
// whole is marked "whole page"), each rendered like in the editor and opening at its line; a
// page not written yet (a link's or a tag's) lists what already names it;
// below, collapsed, "Unlinked mentions": plain-text mentions of the page's title or aliases,
// each with "Link" turning the mention into a [[link]] in that note (BacklinksModel.linkMention
// through the Workspace, so open editors reload), plus "Link all".
//
//   ContentBacklinks { path: page.path; rendererPalette: editor.rendererPalette; width: … }
//
// Not a scrolling view: it grows with its content (PageView puts it under the blocks, the side
// panel in a Flickable). Long lists show `pageSize` references and a "show more" button.
// Under a page's blocks only what the reader can reach is made (`visibleBottom`, `lookahead`): the
// linked references of a long page sit far below the first screen and are listed — ten at a time,
// a frame or two apart, into a height reserved for them — once the view comes within a screen of
// them (docs/PERFORMANCE.md "L7"); their counts are there from the start.
// `compact` for the side panel. Group headers collapse their page's references. The side panel's
// accordion shows the two parts as sections of their own (`sectioned`: no headings here — the
// section's header has them —; `showLinked` / `showUnlinked` pick the part, the unlinked mentions
// listed at once).
FocusScope {
    id: root

    property string path
    /// Renderer colours (BlockEditor.rendererPalette).
    property var rendererPalette: ({})
    property bool compact: false
    property int pageSize: compact ? 30 : 50
    /// Show the linked references (an accordion section of unlinked mentions leaves them out).
    property bool showLinked: true
    /// Show the unlinked mentions section.
    property bool showUnlinked: true
    /// A section of the side panel's accordion: no headings of its own.
    property bool sectioned: false
    /// The unlinked mentions are listed (computing them reads every note: only when open).
    property bool unlinkedOpen: false
    /// How far down this section the view reaches, in px from its top (PageView: the bottom of the
    /// editor's viewport; negative while the section starts below it). The linked references are
    /// listed down to `lookahead` px below that. Default: all of them at once (the side panel).
    property real visibleBottom: 1e9
    property real lookahead: 800
    /// Linked references listed so far (0: none yet — the section is out of reach); grows by
    /// `revealStep` while the list ends within reach, up to pageSize.
    property int revealed: 0
    readonly property int revealStep: 10
    /// Room kept for the references not listed yet (an estimate: the page's height, and so its
    /// scroll bar, hardly change when they come).
    readonly property real reserved: !root.showLinked || root.revealed >= root.pageSize ? 0
                                     : Math.min(root.pageSize - root.revealed,
                                                root.revealed === 0 ? linked.refCount : linked.hiddenCount) * Theme.space12
    /// List every linked reference, made at once (route.open "unlinked" scrolls to the section's
    /// end right after).
    function revealAll(): void {
        root.revealUpTo(root.pageSize)
        linked.refreshNow()
    }
    function revealUpTo(count: int): void {
        revealTimer.stop()
        root.revealed = Math.max(root.revealed, count)
    }
    function reveal(): void {
        if (root.revealed >= root.pageSize || root.path === "" || !root.visible || !root.placed)
            return
        const reach = root.visibleBottom + root.lookahead
        if (reach >= 1e8) {
            root.revealUpTo(root.pageSize)
            return
        }
        if (root.revealed === 0) {
            // The first ones fill the part on screen at once (a reference takes 32 px or more),
            // the rest follows below it.
            if (reach > 0)
                root.revealed = Math.min(root.pageSize, Math.max(root.revealStep,
                                                                 Math.ceil(Math.max(0, root.visibleBottom) / 32) + 1))
            return
        }
        // More while some are left out and the listed ones end within reach — the next ten a
        // frame or two later (the rows of one step made at once).
        if (linked.hiddenCount > 0 && column.implicitHeight < reach && !revealTimer.running)
            revealTimer.start()
    }
    Timer {
        id: revealTimer
        interval: 32
        onTriggered: {
            root.revealed = Math.min(root.pageSize, root.revealed + root.revealStep)
        }
    }
    // From the event loop, not inside the change: listing references changes the section's height,
    // so the page's (the editor's footer) — and with the view at its end its scroll position, which
    // visibleBottom reads.
    onVisibleBottomChanged: revealLater.restart()
    onVisibleChanged: revealLater.restart()
    onPageSizeChanged: revealLater.restart()
    Later {
        id: revealLater
        onTriggered: root.reveal()
    }
    /// The page above has been laid out since `path` changed (its blocks are loaded after the
    /// path is set: until then visibleBottom measures the page before).
    property bool placed: false
    onPathChanged: {
        revealTimer.stop()
        root.revealed = 0
        root.placed = false
        placedLater.restart()
    }
    Later {
        id: placedLater
        onTriggered: {
            root.placed = true
            root.reveal()
        }
    }
    // (A path given at creation: no change signal for it.)
    Component.onCompleted: placedLater.restart()
    Connections {
        target: linked
        // (After each refresh: a step listed, the counts known.)
        function onRefreshed() { revealLater.restart() }
    }

    readonly property int linkedCount: linked.refCount
    readonly property int unlinkedCount: unlinked.refCount
    readonly property bool empty: linked.refCount === 0 && linked.pageCount === 0

    implicitHeight: column.implicitHeight + root.reserved

    PageTools { id: tools; workspace: Workspace }

    BacklinksModel {
        id: linked
        workspace: root.path !== "" ? Workspace : null
        path: root.path
        palette: root.rendererPalette
        locale: DatesHelper.locale
        today: DatesHelper.today
        // (One while out of reach: the counts without rendering the rows.)
        limit: Math.max(1, Math.min(root.pageSize, root.revealed))
    }
    BacklinksModel {
        id: unlinked
        workspace: root.unlinkedOpen && root.path !== "" ? Workspace : null
        path: root.path
        mode: "unlinked"
        palette: root.rendererPalette
        locale: DatesHelper.locale
        today: DatesHelper.today
        limit: root.pageSize
    }

    component RefList: Repeater {
        id: list
        property BacklinksModel refs
        property bool linkable: false
        property PageTools tools
        property bool compact: false
        property real rowWidth: 0
        model: refs
        delegate: Item {
            id: entry
            required property int index
            required property string rowType
            required property string path
            required property string title
            required property string folder
            required property string journalDate
            required property int count
            required property bool collapsed
            required property bool pageLevel
            required property int line
            required property string html
            required property string text
            required property string breadcrumb
            required property string taskStatus

            width: list.rowWidth
            Layout.fillWidth: true
            implicitHeight: entry.rowType === "page" ? headerItem.implicitHeight + (entry.index > 0 ? Theme.space2 : 0)
                                                     : refItem.implicitHeight

            ContentRefGroupHeader {
                id: headerItem
                visible: entry.rowType === "page"
                anchors.bottom: parent.bottom
                width: parent.width
                refs: list.refs
                path: entry.path
                title: entry.title
                folder: entry.folder
                journalDate: entry.journalDate
                count: entry.count
                collapsed: entry.collapsed
                pageLevel: entry.pageLevel
                compact: list.compact
            }
            ContentRefRow {
                id: refItem
                visible: entry.rowType === "ref"
                x: list.compact ? Theme.space2 : Theme.space6
                width: parent.width - x
                tools: list.tools
                compact: list.compact
                html: entry.html
                plain: entry.text
                breadcrumb: entry.breadcrumb
                taskStatus: entry.taskStatus
                sourcePath: entry.path
                line: entry.line
                actionText: list.linkable ? qsTr("Link") : ""
                actionIcon: list.linkable ? "link" : ""
                onOpened: (newPane) => AppController.openPage(entry.path, entry.line, newPane)
                onTaskToggled: list.refs.toggleTask(entry.index)
                onTaskStatusRequested: (status) => list.refs.setTaskStatus(entry.index, status)
                onActionTriggered: {
                    const where = entry.journalDate !== "" ? DatesHelper.formatLong(entry.journalDate) : entry.title
                    if (list.refs.linkMention(entry.index))
                        AppController.toast(qsTr("Linked in “%1”").arg(where), "success")
                    else
                        AppController.toast(qsTr("The mention is no longer there."), "warning")
                }
            }
        }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // ---- Linked references -------------------------------------------------------------------
        RowLayout {
            visible: root.showLinked && !root.sectioned
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space2
            BIcon { name: "link"; size: Theme.iconSize; color: Theme.fgMuted }
            BText {
                Layout.fillWidth: true
                text: linked.refCount > 0 ? qsTr("%n linked reference(s)", "", linked.refCount) : qsTr("Linked references")
                variant: "label"
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BText {
                visible: linked.pageCount > 1
                text: qsTr("in %n page(s)", "", linked.pageCount)
                variant: "caption"
                color: Theme.fgFaint
            }
        }
        BText {
            visible: root.showLinked && root.empty
            Layout.fillWidth: true
            Layout.leftMargin: root.sectioned ? Theme.space2 : Theme.space6
            Layout.bottomMargin: Theme.space2
            text: {
                // A tag names a page too (docs/SYNTAX.md §3): #name, or #[[two words]].
                const tag = root.path !== "" ? tools.tagFor(root.path) : ""
                return tag !== "" ? qsTr("No other note links here yet. Write [[%1]] or %2 in any note to link it.")
                                    .arg(Workspace.pageTitle(root.path)).arg(tag)
                                  : qsTr("No other note links here yet. Write [[%1]] in any note to link it.")
                                    .arg(Workspace.pageTitle(root.path))
            }
            variant: "muted"
            wrapMode: Text.Wrap
        }
        RefList {
            refs: root.showLinked && root.revealed > 0 ? linked : null
            tools: tools
            compact: root.compact
            rowWidth: column.width
        }
        BButton {
            // (Not while the references are still being listed.)
            visible: root.showLinked && linked.hiddenCount > 0 && root.revealed >= root.pageSize
            Layout.leftMargin: Theme.space6
            Layout.topMargin: Theme.space1
            variant: "ghost"
            size: "small"
            iconName: "chevron-down"
            text: qsTr("Show %n more", "", linked.hiddenCount)
            onClicked: root.pageSize += root.compact ? 30 : 50
        }

        // ---- Unlinked mentions --------------------------------------------------------------------
        RowLayout {
            visible: root.showUnlinked && (!root.sectioned || (root.unlinkedOpen && unlinked.refCount > 1))
            Layout.fillWidth: true
            Layout.topMargin: root.sectioned ? 0 : Theme.space5
            Layout.bottomMargin: root.sectioned ? Theme.space1 : 0
            spacing: Theme.space2
            BButton {
                visible: !root.sectioned
                variant: "ghost"
                iconName: root.unlinkedOpen ? "chevron-down" : "chevron-right"
                text: root.unlinkedOpen && unlinked.refCount > 0
                      ? qsTr("%n unlinked mention(s)", "", unlinked.refCount) : qsTr("Unlinked mentions")
                Accessible.description: qsTr("Plain-text mentions of this page in other notes")
                onClicked: root.unlinkedOpen = !root.unlinkedOpen
            }
            Item { Layout.fillWidth: true }
            BButton {
                visible: root.unlinkedOpen && unlinked.refCount > 1
                size: "small"
                iconName: "link"
                text: qsTr("Link all")
                onClicked: {
                    const n = unlinked.linkAll()
                    AppController.toast(qsTr("Linked %n mention(s)", "", n), n > 0 ? "success" : "info")
                }
            }
        }
        BText {
            visible: root.showUnlinked && root.unlinkedOpen && unlinked.refCount === 0
            Layout.fillWidth: true
            Layout.leftMargin: root.sectioned ? Theme.space2 : Theme.space6
            Layout.topMargin: root.sectioned ? 0 : Theme.space1
            text: qsTr("No other note mentions “%1” without a link.").arg(Workspace.pageTitle(root.path))
            variant: "muted"
            wrapMode: Text.Wrap
        }
        RefList {
            refs: unlinked
            linkable: true
            tools: tools
            compact: root.compact
            rowWidth: column.width
        }
        BButton {
            visible: root.showUnlinked && root.unlinkedOpen && unlinked.hiddenCount > 0
            Layout.leftMargin: Theme.space6
            variant: "ghost"
            size: "small"
            iconName: "chevron-down"
            text: qsTr("Show %n more", "", unlinked.hiddenCount)
            onClicked: root.pageSize += root.compact ? 30 : 50
        }
    }
}
