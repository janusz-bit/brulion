pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellRouteView — shows the view component for a route (a tab's in a ShellTabPane — the main
// window's panes, a page's own window —, AppController.sidePanelRoute in the side panel). The view
// instance is kept while only the route's parameters change (another page, another day; another tab
// showing the same view) and replaced when the view changes, with a short fade.
//
// View contract: every view has `property var route`; optional `property string title` (top
// bar), `property Component sidePanel` (context panel: backlinks, outline, mini calendar) and
// `property bool editing` (text is being edited, or a picture fills the view: toasts go to the top
// while a toolbar is docked), `function endEditing()` (the editing a docked touch toolbar is for
// ends — the bottom navigation's quick capture, which would open over it),
// `property bool toolbarDocked` (an editor's touch toolbar is docked at the view's bottom edge:
// on the keyboard, or on the phone's bottom navigation once the keyboard went), `replacesBottomNav` (DrawingView on phones: the view's own bar at the
// bottom takes the bottom navigation's place),
// `pictureRoute` / `pictureAsText` (PageView: a picture is shown, as text or not — the top bar's
// menu acts on it), `sourceMode` (PageView: the page is shown as Markdown source — the palette's
// block commands are off), `edgeSwipe` (CalendarView: the view's own horizontal swipe starts at
// the left edge — the drawer's edge strip makes way), `statusText` / `statusPosition` (PageView: the
// status bar's word count and edited block). The shell sets `covered` (CalendarView: a modal
// panel lies over the view), `paneActive` (PageView, JournalsView: false in the other pane of
// a split view — the window-wide shortcuts and the shell's actions are the focused pane's view's).
// Optional `viewState()` / `restoreViewState(state)` (PageView, JournalsView): where the view is
// scrolled, kept by the tab while another one is shown (restoreViewState() here waits for the
// view of a route just set). ViewTraits (src/app/shell/viewtraits.h) reads these optional members.
Item {
    id: root

    property var route: ({})
    /// The view is shown in the side panel (views may render more compactly).
    property bool inSidePanel: false
    /// A modal panel of the shell (the drawer) lies over the view: views with their own swipes
    /// (CalendarView's `covered`) turn them off, so the panel's swipe stays the panel's.
    property bool covered: false
    /// The pane navigation goes to (false: the other pane of a split view).
    property bool paneActive: true

    /// The view's state (where it is scrolled), {} for a view that keeps none.
    function viewState(): var {
        return traits.viewState()
    }
    /// Give the view of the route just set the state it had ({}: none, the route's own target).
    function restoreViewState(state: var): void {
        traits.restoreViewState(state)
    }

    readonly property Item view: loader.item as Item
    readonly property string viewName: AppController.routeText(root.route, "view")
    // The optional view properties are looked up through the view's meta-object (views share no
    // base type): ViewTraits.
    ViewTraits {
        id: traits
        view: loader.item
    }
    /// The view's own title, "" when it has none.
    readonly property string viewTitle: traits.title
    /// The view's context panel, or null.
    readonly property Component viewSidePanel: traits.sidePanel
    /// The view is editing text (a block, source mode) or shows a picture.
    readonly property bool viewEditing: traits.editing
    /// The view's editing toolbar is docked at its bottom edge (touch UIs).
    readonly property bool viewToolbarDocked: traits.toolbarDocked
    /// The view has a bar of its own at the bottom instead of the bottom navigation (phones).
    readonly property bool viewReplacesBottomNav: traits.replacesBottomNav
    /// The view shows a picture attachment (not a note), and that picture's text instead.
    readonly property bool viewPicture: traits.pictureRoute
    readonly property bool viewPictureAsText: viewPicture && traits.pictureAsText
    /// The view shows a PDF in the PDF viewer (PageView.pdfRoute), or another file that is no note
    /// read-only (PageView.fileRoute): no Markdown, no note actions.
    readonly property bool viewPdf: traits.pdfRoute
    readonly property bool viewFile: traits.fileRoute
    /// The view's editor shows the page's Markdown source (source mode).
    readonly property bool viewSourceMode: traits.sourceMode
    /// The view's note mode, "document" | "outline" ("" for a view without a note to switch).
    readonly property string viewNoteMode: traits.noteMode
    /// The view swipes sideways from the left edge itself (the calendar's grids): no edge strip.
    readonly property bool viewEdgeSwipe: traits.edgeSwipe
    /// The status bar's texts of the view ("" for none): the word count, the edited block.
    readonly property string viewStatusText: traits.statusText
    readonly property string viewStatusPosition: traits.statusPosition

    /// Ends the editing the view's docked touch toolbar is for (views without one: nothing).
    function endEditing(): void {
        traits.endEditing()
    }

    function componentFor(view: string): Component {
        switch (view) {
        case "journals":
        case "journal": return journalsView
        case "page": return pageView
        case "calendar": return calendarView
        case "tasks": return tasksView
        case "habits": return habitsView
        case "graph": return graphView
        case "search": return searchView
        case "allPages": return allPagesView
        case "tags":
        case "tag": return tagsView
        case "sync": return syncView
        case "importExport": return importExportView
        case "history": return historyView
        case "trash": return trashView
        case "cleanup": return cleanupView
        case "replace": return replaceView
        case "settings": return settingsView
        case "logs": return logsView
        case "drawing": return drawingView
        default: return null
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        focus: true
        sourceComponent: root.componentFor(root.viewName)
        onLoaded: {
            const item = loader.item as Item
            if (item)
                item.opacity = 0
            fadeIn.restart()
            // (traits.view follows loader.item already: itemChanged comes before loaded.)
            traits.applyPendingState()
        }
    }
    NumberAnimation {
        id: fadeIn
        target: loader.item
        property: "opacity"
        from: 0
        to: 1
        duration: Theme.durationFast
        easing.type: Theme.easing
        // Without motion (or before the animation runs) the view must still be visible.
        onStopped: {
            const item = loader.item as Item
            if (item)
                item.opacity = 1
        }
    }

    Component { id: journalsView; JournalsView { route: root.route; inSidePanel: root.inSidePanel; paneActive: root.paneActive } }
    Component { id: pageView; PageView { route: root.route; inSidePanel: root.inSidePanel; paneActive: root.paneActive } }
    Component { id: calendarView; CalendarView { route: root.route; covered: root.covered } }
    Component { id: tasksView; TasksView { route: root.route } }
    Component { id: habitsView; HabitsView { route: root.route } }
    Component { id: graphView; GraphView { route: root.route } }
    Component { id: searchView; SearchView { route: root.route } }
    Component { id: allPagesView; AllPagesView { route: root.route } }
    Component { id: tagsView; TagsView { route: root.route } }
    Component { id: syncView; SyncView { route: root.route } }
    Component { id: importExportView; ImportExportView { route: root.route } }
    Component { id: historyView; HistoryView { route: root.route } }
    Component { id: trashView; TrashView { route: root.route } }
    Component { id: cleanupView; CleanupView { route: root.route } }
    Component { id: replaceView; ReplaceView { route: root.route } }
    Component { id: settingsView; SettingsView { route: root.route } }
    Component { id: logsView; LogsView { route: root.route } }
    Component { id: drawingView; DrawingView { route: root.route; inSidePanel: root.inSidePanel } }
}
