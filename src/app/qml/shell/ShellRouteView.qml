pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellRouteView — shows the view component for a route (AppController.currentRoute in the main
// area, AppController.sidePanelRoute in the side panel). The view instance is kept while only
// the route's parameters change (another page, another day) and replaced when the view changes,
// with a short fade.
//
// View contract: every view has `property var route`; optional `property string title` (top
// bar), `property Component sidePanel` (context panel: backlinks, outline, mini calendar) and
// `property bool editing` (text is being edited, or a picture fills the view: the floating capture
// button hides),
// `property bool toolbarDocked` (an editor's touch toolbar is docked at the view's bottom edge:
// on the keyboard, or on the phone's bottom navigation once the keyboard went — the floating
// capture button hides), `replacesBottomNav` (DrawingView on phones: the view's own bar at the
// bottom takes the bottom navigation's place),
// `pictureRoute` / `pictureAsText` (PageView: a picture is shown, as text or not — the top bar's
// menu acts on it), `sourceMode` (PageView: the page is shown as Markdown source — the palette's
// block commands are off), `edgeSwipe` (CalendarView: the view's own horizontal swipe starts at
// the left edge — the drawer's edge strip makes way). The shell sets `covered` (CalendarView: a
// modal panel lies over the view).
Item {
    id: root

    property var route: ({})
    /// The view is shown in the side panel (views may render more compactly).
    property bool inSidePanel: false
    /// A modal panel of the shell (the drawer) lies over the view: views with their own swipes
    /// (CalendarView's `covered`) turn them off, so the panel's swipe stays the panel's.
    property bool covered: false

    readonly property Item view: loader.item as Item
    readonly property string viewName: route && route.view ? route.view : ""
    // The optional view properties are looked up dynamically (views share no base type).
    readonly property var _view: loader.item
    /// The view's own title, "" when it has none.
    readonly property string viewTitle: _view && typeof _view.title === "string" ? _view.title : ""
    /// The view's context panel, or null.
    readonly property Component viewSidePanel: (_view && _view.sidePanel) || null
    /// The view is editing text (a block, source mode): the floating capture button would cover
    /// the editor's toolbar and caret.
    readonly property bool viewEditing: !!_view && _view.editing === true
    /// The view's editing toolbar is docked at its bottom edge (touch UIs).
    readonly property bool viewToolbarDocked: !!_view && _view.toolbarDocked === true
    /// The view has a bar of its own at the bottom instead of the bottom navigation (phones).
    readonly property bool viewReplacesBottomNav: !!_view && _view.replacesBottomNav === true
    /// The view shows a picture attachment (not a note), and that picture's text instead.
    readonly property bool viewPicture: !!_view && _view.pictureRoute === true
    readonly property bool viewPictureAsText: viewPicture && _view.pictureAsText === true
    /// The view's editor shows the page's Markdown source (source mode).
    readonly property bool viewSourceMode: !!_view && _view.sourceMode === true
    /// The view swipes sideways from the left edge itself (the calendar's grids): no edge strip.
    readonly property bool viewEdgeSwipe: !!_view && _view.edgeSwipe === true

    function componentFor(view) {
        switch (view) {
        case "journals":
        case "journal": return journalsView
        case "page": return pageView
        case "calendar": return calendarView
        case "tasks": return tasksView
        case "graph": return graphView
        case "search": return searchView
        case "allPages": return allPagesView
        case "tags":
        case "tag": return tagsView
        case "sync": return syncView
        case "importExport": return importExportView
        case "history": return historyView
        case "trash": return trashView
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
            item.opacity = 0
            fadeIn.restart()
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
        onStopped: if (loader.item) loader.item.opacity = 1
    }

    Component { id: journalsView; JournalsView { route: root.route; inSidePanel: root.inSidePanel } }
    Component { id: pageView; PageView { route: root.route; inSidePanel: root.inSidePanel } }
    Component { id: calendarView; CalendarView { route: root.route; covered: root.covered } }
    Component { id: tasksView; TasksView { route: root.route } }
    Component { id: graphView; GraphView { route: root.route } }
    Component { id: searchView; SearchView { route: root.route } }
    Component { id: allPagesView; AllPagesView { route: root.route } }
    Component { id: tagsView; TagsView { route: root.route } }
    Component { id: syncView; SyncView { route: root.route } }
    Component { id: importExportView; ImportExportView { route: root.route } }
    Component { id: historyView; HistoryView { route: root.route } }
    Component { id: trashView; TrashView { route: root.route } }
    Component { id: settingsView; SettingsView { route: root.route } }
    Component { id: logsView; LogsView { route: root.route } }
    Component { id: drawingView; DrawingView { route: root.route; inSidePanel: root.inSidePanel } }
}
