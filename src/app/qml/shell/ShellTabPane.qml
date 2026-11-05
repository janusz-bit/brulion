pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellTabPane — a pane showing one tab (docs/UI.md "Tabs, split view and windows"): the main
// window's main pane and split pane, a page's own window (ShellPageWindow). Its ShellRouteView
// shows the tab's current route (AppController.tabRoute(tabId), followed through
// tabRouteChanged). Before it shows another tab it hands the view's state — where it is scrolled —
// to the tab it showed (AppController.setTabViewState) and gives the next tab's view its own
// (ShellRouteView.restoreViewState); when the tabs are saved (tabViewStatesRequested) it hands the
// state over too. Only the tabs shown have views: the others are a route and a view state, no
// editor (docs/PERFORMANCE.md "Tabs"). The keyboard focus going into the pane (a click into an
// editor, a list, a field), or a press on its background, makes it the focused one (`activated()`)
// — nothing lies over the view: an item on top with a pointer handler, even a passive one, changed
// how the editor's drag selection ended.
FocusScope {
    id: pane

    /// The tab shown (0: none).
    property int tabId: 0
    /// A modal panel of the shell lies over the view (ShellRouteView.covered).
    property bool covered: false
    /// The pane navigation goes to (false: the other pane of a split view).
    property bool paneActive: true
    readonly property ShellRouteView routeView: view
    /// The tab whose route the view shows now.
    property int _shown: 0

    /// The keyboard focus went into the pane, or a press reached its background.
    signal activated()

    function sync(): void {
        // (tabRoute(0) and tabViewState(0) are {}.)
        if (pane._shown === pane.tabId) {
            view.route = AppController.tabRoute(pane.tabId)
            return
        }
        // The tab shown until now keeps where its view was (it may be gone: closed).
        if (pane._shown !== 0 && AppController.routeText(AppController.tabRoute(pane._shown), "view") !== "")
            AppController.setTabViewState(pane._shown, view.viewState())
        pane._shown = pane.tabId
        view.route = AppController.tabRoute(pane.tabId)
        view.restoreViewState(AppController.tabViewState(pane.tabId))
    }
    onTabIdChanged: sync()
    Component.onCompleted: sync()
    onActiveFocusChanged: if (activeFocus) pane.activated()

    Connections {
        target: AppController
        function onTabRouteChanged(id: int) {
            if (id === pane.tabId)
                pane.sync()
        }
        function onTabViewStatesRequested(): void {
            if (pane._shown !== 0 && pane._shown === pane.tabId)
                AppController.setTabViewState(pane._shown, view.viewState())
        }
    }

    // Under the view: a press nothing in the view took (its background).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => {
            pane.activated()
            mouse.accepted = false
        }
    }
    ShellRouteView {
        id: view
        anchors.fill: parent
        focus: true
        covered: pane.covered
        paneActive: pane.paneActive
    }
}
