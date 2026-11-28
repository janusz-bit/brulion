pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// EditorAtramentJournal — the journal stream drawn by Atrament as one list (docs/ATRAMENT.md "The
// journal stream as one list"; editor/atrament/journalview.h). JournalsView loads it by file name
// in place of its ListView of ContentJournalDay items when the editor is drawn by Atrament
// (AppSettings.editorAtrament, `--atrament-editor`): the days are rows of one list, each day's
// blocks drawn by a PageView of its own, with no QML item per day. One BlockEditor serves the
// whole stream (`editorItem`, JournalsView's, without rows of its own: this view is its
// `atramentHost`) and acts on the day being written in: every signal of a day makes that day the
// active one first (`activeController` → the editor's controller), so the page's handlers
// (EditorAtramentPage, the root type) work on it. A day's controller is the stream's: its folds
// restored and kept (Workspace.foldState), a merge from elsewhere said in a toast, Ctrl+E and its
// date opening it as a page. Above the days the missed reminders and "Go to day…", below them the
// empty journal's hint — QML items over the list's header and footer.
EditorAtramentPage {
    id: root

    /// JournalsView's stream frame (EditorScroller): "Go to day…" opens its dialog.
    property Item journals: null

    view: "journal"
    // The days' controllers (ContentJournalDay's PageController).
    editorHost: Workspace
    daysActive: root.visible
    dayReadOnly: AppSettings.readingMode
    dayAllowNewBlockIds: !AppController.inDocsVault
    dayPalette: editor ? editor.rendererPalette : ({})
    dayLocale: DatesHelper.locale
    dayNow: DatesHelper.fixedNow
    journalTemplate: AppController.routeText(Workspace.config, "journalTemplate")
    // A day switched to a document among outline days keeps their left edge (BlockEditor.documentInset).
    documentGutter: Workspace.config.journalMode === "document" ? 0 : (editor ? editor.outlineGutter : 36)
    // The docked touch toolbar: what is revealed stays above it.
    bottomInset: editor && editor.toolbarVisible ? editor.toolbarHeight : 0
    journalLook: ({
        text: Theme.fg,
        muted: Theme.fgMuted,
        faint: Theme.fgFaint,
        accentText: Theme.accentText,
        accent: Theme.accent,
        onAccent: Theme.dark ? Theme.bg : "#ffffff",
        neutralBadge: Theme.chipBg("neutral"),
        separator: Theme.borderSubtle,
        hover: Theme.bgHover,
        pressed: Theme.bgPressed,
        background: Theme.bg,
        dateFamily: Theme.fontEditor,
        uiFamily: Theme.fontUi,
        dateSize: Theme.text2xl,
        todaySize: Theme.text3xl,
        compactDateSize: Theme.textXl,
        captionSize: Theme.textXs,
        buttonSize: Theme.textSm,
        badgeSize: Theme.textXs,
        lineHeightTight: Theme.lineHeightTight,
        gap: Theme.compact ? Theme.space5 : Theme.space8,
        space2: Theme.space2,
        space3: Theme.space3,
        touchTarget: Theme.touchTarget,
        radius: Theme.radius,
        iconSize: Theme.iconSize,
        compact: Theme.compact,
        touch: Theme.touchUi,
        readOnly: AppSettings.readingMode,
        today: qsTranslate("ContentJournalDay", "Today"),
        yesterday: qsTranslate("ContentJournalDay", "Yesterday"),
        openAsPage: qsTranslate("ContentJournalDay", "Open as page"),
        promptToday: Theme.touchUi ? qsTranslate("ContentJournalDay", "What's on today? Tap to start writing.")
                                   : qsTranslate("ContentJournalDay", "What's on today? Click to start writing."),
        promptDay: Theme.touchUi ? qsTranslate("ContentJournalDay", "Nothing written on this day. Tap to start writing.")
                                 : qsTranslate("ContentJournalDay", "Nothing written on this day. Click to start writing."),
        readOnlyToday: qsTranslate("ContentJournalDay", "Nothing written today yet."),
        readOnlyDay: qsTranslate("ContentJournalDay", "Nothing written on this day.")
    })
    Accessible.name: qsTranslate("JournalsView", "Journals")

    // The days' geometry is an outline day's: a document day's gutter is `documentGutter` (each
    // day's own note mode, not the editor's, which follows the day written in).
    gutter: editor ? editor.outlineGutter : 36
    documentMode: false

    // ---- The day written in: the stream's editor acts on it ---------------------------------------
    // The end of a selection over several days moved into a day: the keys go there (and on touch UIs
    // the selection mode), `reveal` scrolls to it.
    Connections {
        target: root.streamSelection as StreamSelection
        function onFocusMoved(controller: PageController, row: int, reveal: bool) {
            root.activateController(controller)
            if (root.editor)
                root.editor.spanFocusHere(row, reveal)
        }
    }
    onActiveControllerChanged: {
        if (root.editor)
            root.editor.controller = root.activeController as PageController
    }
    Component.onCompleted: {
        if (root.editor)
            root.editor.controller = root.activeController as PageController
    }

    // ---- The days' controllers (ContentJournalDay's parts) ----------------------------------------
    /// A day's folds as the notebook keeps them (Workspace.foldState), once it is loaded.
    property list<var> _folds: []
    function restoreFolds(controller: QtObject): void {
        const page = controller as PageController
        if (!page || !page.loaded || page.loading)
            return
        root._folds = Workspace.foldState(page.path)
        if (root._folds.length > 0)
            page.setFoldState(root._folds)
        root.markFoldsRestored(page)
    }
    onDayMade: (controller) => root.restoreFolds(controller)
    onDayLoaded: (controller) => root.restoreFolds(controller)
    onDayFoldStateChanged: (controller) => {
        const page = controller as PageController
        if (page)
            Workspace.setFoldState(page.path, page.foldState)
    }
    onDayExternallyChanged: (controller, merged) => {
        if (merged)
            AppController.toast(qsTranslate("ContentJournalDay", "Merged changes made elsewhere into this day"), "info")
    }
    // The date, "Open as page", Ctrl+E: the day as a page (in source mode for Ctrl+E).
    onOpenAsPageRequested: (date, source) => {
        const page = root.activeController as PageController
        if (page)
            page.save()
        if (source)
            AppController.navigate({ view: "journal", date: date, source: true })
        else
            AppController.navigate({ view: "journal", date: date })
    }
    // The prompt of a day without a note: reading mode says why it does not begin.
    onBeginRequested: (row) => {
        if (root.editor && root.editor.readOnlyRefuses())
            return
        root.beginDay(row)
    }
    Connections {
        target: Workspace
        function onPathRenamed(oldPath: string, newPath: string) { root.followRename(oldPath, newPath) }
        // The notebook's journal mode changed (settings, another device).
        function onConfigChanged() { root.refreshModes() }
    }

    // ---- Above and below the days (JournalsView's list header and footer) ----------------------
    readonly property JournalListModel days: root.daysModel as JournalListModel
    streamHeader: Item {
        width: root.width
        implicitHeight: headerColumn.implicitHeight + (Theme.compact ? Theme.space3 : Theme.space6)
        height: implicitHeight

        ColumnLayout {
            id: headerColumn
            // The text column of the days (BlockEditor.contentX / contentWidth after the gutter).
            readonly property real maxWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
            readonly property real pad: Theme.compact ? Theme.space3 : Theme.space8
            readonly property real arrow: Workspace.config.journalMode === "document" ? 0
                                          : Theme.touchUi ? 18 + 22 : 16 + 20
            x: Math.max(pad, (root.width - maxWidth) / 2) + arrow
            y: Theme.compact ? Theme.space3 : Theme.space5
            width: Math.max(120, Math.min(maxWidth, root.width - 2 * pad)) - arrow
            spacing: Theme.space3

            // The missed reminders (ContentMissedBanner), loaded from its file only while there are some.
            Loader {
                Layout.fillWidth: true
                active: Workspace.reminders !== null && Workspace.reminders.missedCount > 0
                visible: active
                source: Qt.resolvedUrl("../content/ContentMissedBanner.qml")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: Workspace.ready && root.days !== null && root.days.count > 1
                          ? qsTranslate("JournalsView", "%n day(s) with notes", "", root.days.count - 1 + (root.days.todayExists ? 1 : 0))
                          : Workspace.indexing ? qsTranslate("JournalsView", "Reading the notebook…") : ""
                    variant: "caption"
                    color: Theme.fgFaint
                    elide: Text.ElideRight
                }
                BButton {
                    size: "small"
                    variant: "ghost"
                    iconName: "calendar-days"
                    text: qsTranslate("JournalsView", "Go to day…")
                    onClicked: QmlCall.call(root.journals, "openJumpDialog")
                }
            }
        }
    }
    /// The journal holds at most today (the hint under it).
    readonly property bool emptyJournal: root.days !== null && root.days.count <= 1 && Workspace.ready
    streamFooter: Item {
        width: root.width
        height: root.emptyJournal ? emptyHint.implicitHeight + Theme.space12 : Theme.space8
        BEmptyState {
            id: emptyHint
            objectName: "journalsEmptyHint"
            visible: root.emptyJournal
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.space8
            compact: true
            iconName: "book-open"
            title: qsTranslate("JournalsView", "Your journal starts today")
            description: qsTranslate("JournalsView", "Every day gets its own page. Write what happened, plans, tasks (-\u2060\u00a0[\u00a0]\u00a0…) and reminders (⏰\u00a0tomorrow\u00a09:00); earlier days appear below.")
            actionText: root.days !== null && root.days.count <= 1 && Workspace.pageCount <= 1 ? qsTranslate("JournalsView", "Import notes") : ""
            actionIcon: "download"
            onActionTriggered: AppController.navigate({ view: "importExport", tab: "import" })
        }
    }

    // ---- A day zoomed into a block: its breadcrumb above its blocks (ContentJournalDay's, in its
    // editor's header), made for that day while the zoom lasts and placed over its page's header.
    dayHeader: Component {
        Item {
            id: crumbs
            required property PageController controller
            objectName: "dayZoomBreadcrumb"
            width: root.width
            height: breadcrumb.implicitHeight
            ZoomBreadcrumb {
                id: breadcrumb
                x: root.editor ? root.editor.contentX : 0
                width: root.editor ? root.editor.contentWidth : 0
                controller: crumbs.controller
            }
        }
    }
}
