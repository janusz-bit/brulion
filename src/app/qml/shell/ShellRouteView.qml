pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellRouteView — shows the view component for a route (a tab's in a ShellTabPane — the main
// window's panes, a page's own window —, AppController.sidePanelRoute in the side panel). The view
// instance is kept while only the route's parameters change (another page, another day; another tab
// showing the same view) and replaced when the view changes, with a short fade. The Loader detaches
// the old view and deletes it on the event loop's next turn (deleteLater), its editor with it.
// With `keepViews` (the tab panes) the views of the phone's bottom navigation — Journal, All pages,
// Search, Tasks, Calendar — are kept instead, hidden, once shown: going back to one shows it again
// at once (where it was scrolled, its filters) instead of building it anew, which took 0.5–1 s on
// a phone (docs/PERFORMANCE.md "Switching views"). A kept view is hidden with its editing ended
// (`endEditing()`), and keeps the route it was shown with; the kept views go when the pane shows
// no route (a split pane closed). Nothing else keeps a view (the shell self-test's "5n").
//
// View contract: every view has `property var route`; optional `property string title` (top
// bar), `property Component sidePanel` (context panel: backlinks, outline, mini calendar) and
// `property bool editing` (text is being edited, or a picture fills the view: toasts go to the top
// while a toolbar is docked), `function endEditing()` (the editing a docked touch toolbar is for
// ends — the bottom navigation's quick capture, which would open over it), `property bool
// canStepBack` and `function stepBack()` (Back has a step of its own in the view, taken before
// the history: PageView and JournalsView zoom a block zoomed into out one level),
// `property bool toolbarDocked` (an editor's touch toolbar is docked at the view's bottom edge:
// on the keyboard, or on the phone's bottom navigation once the keyboard went), `replacesBottomNav` (DrawingView on phones: the view's own bar at the
// bottom takes the bottom navigation's place),
// `takeFocus()` (a kept view built ahead, hidden, shown the first time: the keyboard to its list or
// field — what its Component.onCompleted does only when made shown),
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
    /// Keep the bottom navigation's views (Journal, All pages, Search, Tasks, Calendar) while
    /// other views are shown (the tab panes; not the side panel).
    property bool keepViews: false
    /// Build the kept views not shown yet ahead, in the background (with keepViews; the main pane).
    property bool preloadViews: false

    /// The view's state (where it is scrolled), {} for a view that keeps none.
    function viewState(): var {
        return traits.viewState()
    }
    /// Give the view of the route just set the state it had ({}: none, the route's own target).
    function restoreViewState(state: var): void {
        traits.restoreViewState(state)
    }

    readonly property string viewName: AppController.routeText(root.route, "view")
    /// The view shown: a kept view's key ("journals" for both journal routes, "allPages",
    /// "search", "tasks", "calendar") or another view's name (`loader`); set by showRoute() once
    /// the view's own route is.
    property string shownKey
    // Each view's route: the route it was last shown with (a kept view, hidden, keeps it).
    property var journalsRoute: ({})
    property var allPagesRoute: ({})
    property var searchRoute: ({})
    property var tasksRoute: ({})
    property var calendarRoute: ({})
    property var otherRoute: ({})
    readonly property Loader shownLoader: shownKey === "journals" ? journalsLoader
                                        : shownKey === "allPages" ? allPagesLoader
                                        : shownKey === "search" ? searchLoader
                                        : shownKey === "tasks" ? tasksLoader
                                        : shownKey === "calendar" ? calendarLoader : loader
    readonly property Item view: shownLoader.item as Item
    // The optional view properties are looked up through the view's meta-object (views share no
    // base type): ViewTraits.
    ViewTraits {
        id: traits
        view: root.view
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
    /// Back has a step to take in the view before the history (a zoomed block), and taking it.
    readonly property bool viewCanStepBack: traits.canStepBack
    function stepBack(): void {
        traits.stepBack()
    }

    /// The key of a view kept by `keepViews` ("" for another view).
    function keptKey(view: string): string {
        switch (view) {
        case "journals":
        case "journal": return "journals"
        case "allPages":
        case "search":
        case "tasks":
        case "calendar": return view
        default: return ""
        }
    }
    function componentFor(view: string): Component {
        switch (view) {
        case "page": return pageView
        case "habits": return habitsView
        case "gym": return gymView
        case "console": return consoleView
        case "extension": return extensionView
        case "graph": return graphView
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
    /// Show the view of `route`: its route first (a view made now starts with it), then the view.
    function showRoute(): void {
        // (Not viewName: its binding follows the route only after this handler.)
        const name = AppController.routeText(root.route, "view")
        const kept = root.keptKey(name)
        const key = kept !== "" ? kept : name
        // A kept view being hidden ends its editing (the touch keyboard goes with it).
        if (key !== root.shownKey && root.keepViews && root.keptKey(root.shownKey) !== "")
            traits.endEditing()
        switch (key) {
        case "journals": root.journalsRoute = root.route; break
        case "allPages": root.allPagesRoute = root.route; break
        case "search": root.searchRoute = root.route; break
        case "tasks": root.tasksRoute = root.route; break
        case "calendar": root.calendarRoute = root.route; break
        default: root.otherRoute = root.route
        }
        if (!root.keepViews || name === "") {
            journalsLoader.kept = false
            allPagesLoader.kept = false
            searchLoader.kept = false
            tasksLoader.kept = false
            calendarLoader.kept = false
        }
        root.shownKey = key
    }

    /// The views built ahead, in this order, and the plain route each starts with.
    readonly property list<string> preloadOrder: ["tasks", "calendar", "journals", "allPages", "search"]
    /// The notebook is open and indexed: building views ahead may start (and goes on).
    readonly property bool preloadAllowed: root.preloadViews && root.keepViews && root.shownKey !== ""
                                           && Workspace.ready && !Workspace.indexing
    onPreloadAllowedChanged: if (preloadAllowed) preloadTimer.restart()
    function keptLoader(key: string): KeptLoader {
        switch (key) {
        case "journals": return journalsLoader
        case "allPages": return allPagesLoader
        case "search": return searchLoader
        case "tasks": return tasksLoader
        case "calendar": return calendarLoader
        default: return null
        }
    }
    /// Starts building the next kept view that does not exist yet (none left: nothing). The next one
    /// follows once it is loaded (KeptLoader.onLoaded → preloadTimer).
    function preloadNext(): void {
        if (!root.preloadAllowed)
            return
        for (let i = 0; i < root.preloadOrder.length; ++i) {
            const key = root.preloadOrder[i]
            const next = root.keptLoader(key)
            if (next === null || next.active)
                continue
            switch (key) {
            case "journals": if (!Js.hasKey(root.journalsRoute, "view")) root.journalsRoute = { view: "journals" }; break
            case "allPages": if (!Js.hasKey(root.allPagesRoute, "view")) root.allPagesRoute = { view: "allPages" }; break
            case "search": if (!Js.hasKey(root.searchRoute, "view")) root.searchRoute = { view: "search" }; break
            case "tasks": if (!Js.hasKey(root.tasksRoute, "view")) root.tasksRoute = { view: "tasks" }; break
            case "calendar": if (!Js.hasKey(root.calendarRoute, "view")) root.calendarRoute = { view: "calendar" }; break
            }
            next.kept = true
            return
        }
    }
    /// After the notebook is ready, and between two views: the app's own work comes first.
    Timer {
        id: preloadTimer
        interval: 1200
        onTriggered: root.preloadNext()
    }
    /// A kept view finished loading: the next one a moment later.
    function preloaded(): void {
        if (root.preloadAllowed) {
            preloadTimer.interval = 400
            preloadTimer.restart()
        }
    }

    onRouteChanged: showRoute()
    onKeepViewsChanged: showRoute()
    Component.onCompleted: showRoute()

    // A view shown (made now or kept): it fades in, and gets the state restoreViewState() kept.
    onViewChanged: {
        fadeIn.stop() // (the view before, if it was fading in, is left whole)
        const item = root.view
        fadeIn.target = item
        if (item) {
            item.opacity = 0
            fadeIn.start()
        }
        traits.applyPendingState()
        // A kept view shown the first time (built ahead, hidden, it left the keyboard alone).
        const kept = root.shownLoader as KeptLoader
        if (item && kept && !kept.everShown) {
            kept.everShown = true
            traits.takeFocus()
        }
    }
    NumberAnimation {
        id: fadeIn
        property: "opacity"
        from: 0
        to: 1
        duration: Theme.durationFast
        easing.type: Theme.easing
        // Without motion (or before the animation runs) the view must still be visible.
        onStopped: {
            const item = fadeIn.target as Item
            if (item)
                item.opacity = 1
        }
    }

    // Views not kept: one at a time.
    Loader {
        id: loader
        anchors.fill: parent
        focus: root.shownLoader === loader
        sourceComponent: root.componentFor(root.shownKey)
    }
    // The kept views: made when first shown (or built ahead, `kept` set by preloadNext()), then
    // (`kept`, with keepViews) only hidden. Built ahead while hidden: in pieces over several frames;
    // shown before it is ready, the rest at once (asynchronous turned off finishes it).
    component KeptLoader: Loader {
        required property bool shown
        required property bool keepViews
        property bool kept
        /// Its view has been shown (the first time, it takes the keyboard: onViewChanged).
        property bool everShown
        onShownChanged: if (shown && keepViews) kept = true
        onActiveChanged: if (!active) everShown = false
        anchors.fill: parent
        active: shown || kept
        asynchronous: !shown
        visible: shown
        focus: shown
    }
    KeptLoader {
        id: journalsLoader
        shown: root.shownKey === "journals"
        keepViews: root.keepViews
        sourceComponent: journalsView
        onLoaded: root.preloaded()
    }
    KeptLoader {
        id: allPagesLoader
        shown: root.shownKey === "allPages"
        keepViews: root.keepViews
        sourceComponent: allPagesView
        onLoaded: root.preloaded()
    }
    KeptLoader {
        id: searchLoader
        shown: root.shownKey === "search"
        keepViews: root.keepViews
        sourceComponent: searchView
        onLoaded: root.preloaded()
    }
    KeptLoader {
        id: tasksLoader
        shown: root.shownKey === "tasks"
        keepViews: root.keepViews
        sourceComponent: tasksView
        onLoaded: root.preloaded()
    }
    KeptLoader {
        id: calendarLoader
        shown: root.shownKey === "calendar"
        keepViews: root.keepViews
        sourceComponent: calendarView
        onLoaded: root.preloaded()
    }

    Component { id: journalsView; JournalsView { route: root.journalsRoute; inSidePanel: root.inSidePanel; paneActive: root.paneActive && journalsLoader.shown } }
    Component { id: pageView; PageView { route: root.otherRoute; inSidePanel: root.inSidePanel; paneActive: root.paneActive } }
    Component { id: calendarView; CalendarView { route: root.calendarRoute; covered: root.covered && calendarLoader.shown } }
    Component { id: tasksView; TasksView { route: root.tasksRoute } }
    Component { id: habitsView; HabitsView { route: root.otherRoute } }
    Component { id: gymView; GymView { route: root.otherRoute } }
    Component { id: consoleView; ConsoleView { route: root.otherRoute } }
    Component { id: extensionView; ExtensionRouteView { route: root.otherRoute } }
    Component { id: graphView; GraphView { route: root.otherRoute } }
    Component { id: searchView; SearchView { route: root.searchRoute } }
    Component { id: allPagesView; AllPagesView { route: root.allPagesRoute } }
    Component { id: tagsView; TagsView { route: root.otherRoute } }
    Component { id: syncView; SyncView { route: root.otherRoute } }
    Component { id: importExportView; ImportExportView { route: root.otherRoute } }
    Component { id: historyView; HistoryView { route: root.otherRoute } }
    Component { id: trashView; TrashView { route: root.otherRoute } }
    Component { id: cleanupView; CleanupView { route: root.otherRoute } }
    Component { id: replaceView; ReplaceView { route: root.otherRoute } }
    Component { id: settingsView; SettingsView { route: root.otherRoute } }
    Component { id: logsView; LogsView { route: root.otherRoute } }
    Component { id: drawingView; DrawingView { route: root.otherRoute; inSidePanel: root.inSidePanel } }
}
