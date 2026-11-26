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
// (`endEditing()`), and keeps the route it was shown with. Kept, not hoarded (ViewKeeper,
// MemoryPolicy; docs/PERFORMANCE.md "Memory: only what is in use"): the three shown most recently
// stay, for five minutes after the last switch of views, and none while the app is away; they
// all go when the pane shows no route (a split pane closed). Nothing else keeps a view (the shell
// self-test's "5n"). A hidden
// kept view never keeps the keyboard: given it (a popup closed hands it back to an item Qt
// remembered from long ago, a view's own forceActiveFocus()), it passes it to the view shown at
// once — `focusShownView()` (docs/UI.md "Keyboard shortcuts (desktop)").
//
// Every view is loaded from its file when it is first shown (`viewSource()`, `Loader.setSource`
// with the view's first properties), never named as a type here: a QML file named in another one
// is loaded with it, and this file named all of them — the whole app's QML was loaded, and its
// compiled units held, before the first window (docs/PERFORMANCE.md "QML files loaded when first
// used"). `Binding`s hand the views their route and the pane's state afterwards.
//
// View contract: every view has `property var route`; optional `property string title` (top
// bar), `property Component sidePanel` (context panel: backlinks, outline, mini calendar) and
// `property bool editing` (text is being edited, or a picture fills the view: toasts go to the top
// while a toolbar is docked), `function endEditing()` (the editing a docked touch toolbar is for
// ends — the bottom navigation's quick capture, which would open over it), `property bool
// canStepBack` and `function stepBack()` (Back has a step of its own in the view, taken before
// the history: PageView and JournalsView zoom a block zoomed into out one level),
// `property bool toolbarDocked` (an editor's touch toolbar is docked at the view's bottom edge:
// on the keyboard, or on the phone's bottom navigation once the keyboard went), `replacesBottomNav` (DrawingView, CanvasView on phones: the view's own bar at the
// bottom takes the bottom navigation's place),
// `takeFocus()` (a kept view built ahead, hidden, shown the first time: the keyboard to its list or
// field — what its Component.onCompleted does only when made shown),
// `pictureRoute` / `pictureAsText` (PageView: a picture is shown, as text or not — the top bar's
// menu acts on it), `sourceMode` (PageView: the page is shown as Markdown source — the palette's
// block commands are off), `edgeSwipe` (CalendarView: the view's own horizontal swipe starts at
// the left edge — the drawer's edge strip makes way), `statusText` / `statusPosition` (PageView: the
// status bar's word count and edited block), `statusVim` (PageView, JournalsView: Vim's mode). The shell sets `covered` (CalendarView: a modal
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
    /// Vim's mode / command line of the view's editor (PageView, JournalsView: `statusVim`).
    readonly property string viewStatusVim: traits.statusVim

    /// Gives the keyboard to the view shown, where its focus was (its editor's block): a hidden
    /// kept view got it (KeptLoader.hiddenFocused).
    function focusShownView(): void {
        root.shownLoader.forceActiveFocus()
    }

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
    /// The QML file of the view `view` (an empty URL for a name without one).
    function viewSource(view: string): url {
        switch (view) {
        case "journals":
        case "journal": return Qt.resolvedUrl("../views/JournalsView.qml")
        case "allPages": return Qt.resolvedUrl("../views/AllPagesView.qml")
        case "search": return Qt.resolvedUrl("../views/SearchView.qml")
        case "tasks": return Qt.resolvedUrl("../views/TasksView.qml")
        case "calendar": return Qt.resolvedUrl("../views/CalendarView.qml")
        case "page": return Qt.resolvedUrl("../views/PageView.qml")
        case "habits": return Qt.resolvedUrl("../views/HabitsView.qml")
        case "gym": return Qt.resolvedUrl("../views/GymView.qml")
        case "flashcards": return Qt.resolvedUrl("../views/FlashcardsView.qml")
        case "console": return Qt.resolvedUrl("../views/ConsoleView.qml")
        case "chat": return Qt.resolvedUrl("../views/ChatView.qml")
        case "extension": return Qt.resolvedUrl("../views/ExtensionRouteView.qml")
        case "graph": return Qt.resolvedUrl("../views/GraphView.qml")
        case "tags":
        case "tag": return Qt.resolvedUrl("../views/TagsView.qml")
        case "sync": return Qt.resolvedUrl("../views/SyncView.qml")
        case "importExport": return Qt.resolvedUrl("../views/ImportExportView.qml")
        case "history": return Qt.resolvedUrl("../views/HistoryView.qml")
        case "trash": return Qt.resolvedUrl("../views/TrashView.qml")
        case "notifications": return Qt.resolvedUrl("../views/NotificationsView.qml")
        case "code": return Qt.resolvedUrl("../code/CodeView.qml")
        case "cleanup": return Qt.resolvedUrl("../views/CleanupView.qml")
        case "replace": return Qt.resolvedUrl("../views/ReplaceView.qml")
        case "settings": return Qt.resolvedUrl("../views/SettingsView.qml")
        case "logs": return Qt.resolvedUrl("../diag/LogsView.qml")
        case "drawing": return Qt.resolvedUrl("../drawing/DrawingView.qml")
        case "canvas": return Qt.resolvedUrl("../canvas/CanvasView.qml")
        default: return ""
        }
    }
    /// The properties the view `view` starts with — its route, and what else it takes of the pane
    /// (`shown`: it is made to be shown now, not built ahead). The Bindings below keep them
    /// current afterwards: a view must not start without its route.
    function firstValues(view: string, shown: bool): var {
        switch (view) {
        case "journals":
        case "journal":
            return { route: root.journalsRoute, inSidePanel: root.inSidePanel, paneActive: root.paneActive && shown }
        case "allPages": return { route: root.allPagesRoute }
        case "search": return { route: root.searchRoute }
        case "tasks": return { route: root.tasksRoute }
        case "calendar": return { route: root.calendarRoute, covered: root.covered && shown }
        case "page": return { route: root.otherRoute, inSidePanel: root.inSidePanel, paneActive: root.paneActive }
        case "chat":
        case "drawing":
        case "canvas": return { route: root.otherRoute, inSidePanel: root.inSidePanel }
        default: return { route: root.otherRoute }
        }
    }
    /// The view shown by `loader` (not one of the kept ones): its name while its file is loaded.
    property string otherView
    readonly property bool otherInSidePanel: otherView === "page" || otherView === "chat" || otherView === "drawing"
                                             || otherView === "canvas"
    /// Show the view of `route`: its route first (a view made now starts with it), then the view.
    function showRoute(): void {
        // (Not viewName: its binding follows the route only after this handler.)
        const name = "" + AppController.routeText(root.route, "view")
        const kept = "" + root.keptKey(name)
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
            keeper.clear()
        }
        // The view's file and its first properties, before the Loader that shows it is active.
        if (kept !== "") {
            root.prepare(kept, true)
            root.showOther("")
        } else {
            root.showOther(name)
        }
        root.shownKey = key
        if (root.keepViews && name !== "") {
            keeper.show(kept)
            if (root.preloadViews && kept !== "" && kept !== "journals" && root.recentView !== kept) {
                root.recentView = kept
                AppSettings.setValue("ui/recentView", kept)
            }
        }
    }
    /// What the next setSource() loads: read from properties, as compiled code wants a call's
    /// arguments ("Compiled QML": no value another call may have changed).
    property url _source
    property var _values: ({})
    /// The file `loader` shows, as text ("": none).
    property string otherFile
    /// The kept view `key` gets its file and the properties it starts with, unless it exists
    /// (`shown`: it is about to be shown, not built ahead).
    function prepare(key: string, shown: bool): void {
        const kept = root.keptLoader(key)
        if (kept === null || kept.active)
            return
        root._source = root.viewSource(key)
        root._values = root.firstValues(key, shown)
        kept.setSource(root._source, root._values)
    }
    /// `loader` shows the view `name` ("": none — a kept view is shown). The same file again (a
    /// page after a page, "tags" after "tag") keeps its view: only its route changed.
    function showOther(name: string): void {
        root._source = root.viewSource(name)
        const file = "" + root._source
        if (file === root.otherFile) {
            root.otherView = name
            return
        }
        // (The view before, if any, is deleted on the event loop's next turn.)
        root.otherView = ""
        root.otherFile = file
        if (file === "") {
            loader.source = ""
            return
        }
        root._values = root.firstValues(name, true)
        loader.setSource(root._source, root._values)
        root.otherView = name
    }

    /// Which hidden kept views stay (the most recently shown, for a while), and lets go of the
    /// others: sets their `kept` to false.
    ViewKeeper {
        id: keeper
        limit: MemoryPolicy.keptViews
        idleMs: MemoryPolicy.hiddenViewMs
    }
    /// The hidden kept views of this pane, the one shown last first (tests, the self-test).
    readonly property list<string> hiddenViews: keeper.hidden

    /// The kept view the user went to last, on this device ("": none yet): the one built ahead.
    property string recentView: "" + AppSettings.value("ui/recentView", "")
    /// Every kept view, in the order they are built ahead with MemoryPolicy switched off.
    readonly property list<string> allKept: ["tasks", "calendar", "journals", "allPages", "search"]
    /// Views were built ahead for this notebook (once: what goes later is made when it is shown).
    property bool _preloaded: false
    /// The notebook is open and indexed, and the app in use: building ahead may start (and go on).
    readonly property bool preloadAllowed: root.preloadViews && root.keepViews && root.shownKey !== ""
                                           && Workspace.ready && !Workspace.indexing && MemoryPolicy.buildAhead
    onPreloadAllowedChanged: if (preloadAllowed && !_preloaded) preloadTimer.restart()
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
    /// Builds the kept view `key` ahead, hidden, with its plain route. False: it exists already.
    function preload(key: string): bool {
        const next = root.keptLoader(key)
        if (next === null || next.active)
            return false
        switch (key) {
        case "journals": if (!Js.hasKey(root.journalsRoute, "view")) root.journalsRoute = { view: "journals" }; break
        case "allPages": if (!Js.hasKey(root.allPagesRoute, "view")) root.allPagesRoute = { view: "allPages" }; break
        case "search": if (!Js.hasKey(root.searchRoute, "view")) root.searchRoute = { view: "search" }; break
        case "tasks": if (!Js.hasKey(root.tasksRoute, "view")) root.tasksRoute = { view: "tasks" }; break
        case "calendar": if (!Js.hasKey(root.calendarRoute, "view")) root.calendarRoute = { view: "calendar" }; break
        }
        root.prepare(key, false)
        next.kept = true
        keeper.built(key)
        return true
    }
    /// Builds ahead what is worth its memory: the one view the user went to last (the others are
    /// made when they are first shown). With MemoryPolicy switched off, every kept view, one
    /// after another (KeptLoader.onLoaded → preloadTimer).
    function preloadNext(): void {
        if (!root.preloadAllowed)
            return
        if (!MemoryPolicy.switchedOff) {
            if (!root._preloaded)
                root.preload(root.keptKey(root.recentView))
            root._preloaded = true
            return
        }
        for (let i = 0; i < root.allKept.length; ++i) {
            if (root.preload(root.allKept[i]))
                return
        }
        root._preloaded = true
    }
    /// After the notebook is ready, and between two views: the app's own work comes first.
    Timer {
        id: preloadTimer
        interval: 1200
        onTriggered: root.preloadNext()
    }
    /// A kept view finished loading: the next one a moment later.
    function preloaded(): void {
        if (root.preloadAllowed && !root._preloaded) {
            preloadTimer.interval = 400
            preloadTimer.restart()
        }
    }
    Connections {
        target: Workspace
        // Another notebook: its own views may be built ahead.
        function onOpenChanged(): void { root._preloaded = false }
    }

    onRouteChanged: showRoute()
    onKeepViewsChanged: showRoute()
    Component.onCompleted: {
        keeper.add("journals", journalsLoader)
        keeper.add("allPages", allPagesLoader)
        keeper.add("search", searchLoader)
        keeper.add("tasks", tasksLoader)
        keeper.add("calendar", calendarLoader)
        showRoute()
    }

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

    // Views not kept: one at a time (showOther()).
    Loader {
        id: loader
        anchors.fill: parent
        focus: root.shownLoader === loader
    }
    Binding { target: loader.item; property: "route"; value: root.otherRoute; restoreMode: Binding.RestoreNone }
    Binding {
        target: loader.item
        when: root.otherInSidePanel
        property: "inSidePanel"
        value: root.inSidePanel
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: loader.item
        when: root.otherView === "page"
        property: "paneActive"
        value: root.paneActive
        restoreMode: Binding.RestoreNone
    }
    // The kept views: made when first shown (or built ahead, `kept` set by preload()), then
    // (`kept`, with keepViews) only hidden — until the keeper lets go of them (`kept` false).
    // Built ahead while hidden: in pieces over several frames;
    // shown before it is ready, the rest at once (asynchronous turned off finishes it).
    component KeptLoader: Loader {
        required property bool shown
        required property bool keepViews
        property bool kept
        /// Its view has been shown (the first time, it takes the keyboard: onViewChanged).
        property bool everShown
        /// Something inside it got the keyboard while it is hidden (focusShownView() hands it on).
        signal hiddenFocused()
        onShownChanged: if (shown && keepViews) kept = true
        onActiveFocusChanged: if (activeFocus && !shown) hiddenFocused()
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
        onLoaded: root.preloaded()
        onHiddenFocused: root.focusShownView()
    }
    Binding { target: journalsLoader.item; property: "route"; value: root.journalsRoute; restoreMode: Binding.RestoreNone }
    Binding { target: journalsLoader.item; property: "inSidePanel"; value: root.inSidePanel; restoreMode: Binding.RestoreNone }
    Binding {
        target: journalsLoader.item
        property: "paneActive"
        value: root.paneActive && journalsLoader.shown
        restoreMode: Binding.RestoreNone
    }
    KeptLoader {
        id: allPagesLoader
        shown: root.shownKey === "allPages"
        keepViews: root.keepViews
        onLoaded: root.preloaded()
        onHiddenFocused: root.focusShownView()
    }
    Binding { target: allPagesLoader.item; property: "route"; value: root.allPagesRoute; restoreMode: Binding.RestoreNone }
    KeptLoader {
        id: searchLoader
        shown: root.shownKey === "search"
        keepViews: root.keepViews
        onLoaded: root.preloaded()
        onHiddenFocused: root.focusShownView()
    }
    Binding { target: searchLoader.item; property: "route"; value: root.searchRoute; restoreMode: Binding.RestoreNone }
    KeptLoader {
        id: tasksLoader
        shown: root.shownKey === "tasks"
        keepViews: root.keepViews
        onLoaded: root.preloaded()
        onHiddenFocused: root.focusShownView()
    }
    Binding { target: tasksLoader.item; property: "route"; value: root.tasksRoute; restoreMode: Binding.RestoreNone }
    KeptLoader {
        id: calendarLoader
        shown: root.shownKey === "calendar"
        keepViews: root.keepViews
        onLoaded: root.preloaded()
        onHiddenFocused: root.focusShownView()
    }
    Binding { target: calendarLoader.item; property: "route"; value: root.calendarRoute; restoreMode: Binding.RestoreNone }
    Binding {
        target: calendarLoader.item
        property: "covered"
        value: root.covered && calendarLoader.shown
        restoreMode: Binding.RestoreNone
    }
}
