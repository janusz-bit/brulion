pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Brulion

// ShellPageWindow — a page in a window of its own (desktop; docs/UI.md "Tabs, split view and
// windows"): a tab outside the main window's bar (AppController.windowTabs), made by AppShell as
// that list grows; it destroys itself once its tab is not in it. Its bar has back / forward (the
// tab's own history), the title and "Przenieś do okna głównego" (AppController.attachTab: a tab
// of the bar again); under it the tab's view (ShellTabPane). It edits through the same Workspace
// as the main window, so an edit in one shows in the other (the editors merge each other's saves:
// EditorHost::fileChanged).
// While it has the focus, its tab is the one navigation goes to (links followed in it open in it);
// the main window takes that back when it gets the focus (Main.qml, AppController.
// mainWindowActivated). Its view's own shortcuts (Ctrl+E, Ctrl+F…) work in it; Alt+← / Alt+→ go
// back and forward, Ctrl+W closes it. Closing it drops its tab. No transient parent: it is a window
// of its own, not one that hides with the main window.
ApplicationWindow {
    id: window
    objectName: "shellPageWindow"

    required property int tabId
    /// The tab's route (followed: AppController.tabRouteChanged).
    property var route: AppController.tabRoute(tabId)
    readonly property string pageTitle: pane.routeView.viewTitle !== "" ? pane.routeView.viewTitle
                                                                         : titles.routeTitle(titles.field(window.route, "view"),
                                                                                             titles.field(window.route, "path"),
                                                                                             titles.field(window.route, "date"),
                                                                                             titles.field(window.route, "tag"))

    ShellRouteTitles { id: titles }

    width: 760
    height: 720
    minimumWidth: 320
    minimumHeight: 320
    transientParent: null
    title: pageTitle + " — Brulion"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    onActiveChanged: if (active) AppController.selectTab(window.tabId)
    // Closed by the user: its tab goes (AppShell destroys the window when the list no longer has it).
    onClosing: AppController.closeTab(window.tabId)

    Connections {
        target: AppController
        function onTabRouteChanged(id: int) {
            if (id === window.tabId)
                window.route = AppController.tabRoute(window.tabId)
        }
        function onTabsChanged() {
            // (Its back/forward buttons.)
            window.route = AppController.tabRoute(window.tabId)
        }
        // Its tab went (closed, or moved to the main window): hidden at once (destroy() waits
        // for the event loop), then destroyed. (AppShell makes the windows.)
        function onWindowTabsChanged() {
            if (AppController.windowTabs.indexOf(window.tabId) < 0) {
                window.hide()
                window.destroy()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.touchUi ? 56 : 46
            color: Theme.bg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space2
                anchors.rightMargin: Theme.space2
                spacing: 2

                BIconButton {
                    objectName: "pageWindowBack"
                    enabled: titles.field(window.route, "view") !== "" && AppController.tabCanGoBack(window.tabId)
                    iconName: "arrow-left"
                    label: qsTr("Back")
                    shortcut: "Alt+Left"
                    onClicked: AppController.back(window.tabId)
                }
                BIconButton {
                    // (window.route: the binding follows the tab's navigation.)
                    enabled: titles.field(window.route, "view") !== "" && AppController.tabCanGoForward(window.tabId)
                    iconName: "arrow-right"
                    label: qsTr("Forward")
                    shortcut: "Alt+Right"
                    onClicked: AppController.forward(window.tabId)
                }
                BText {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space2
                    text: window.pageTitle
                    variant: "title"
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                BIconButton {
                    objectName: "pageWindowAttach"
                    iconName: "panel-left"
                    label: qsTr("Move to main window")
                    onClicked: AppController.attachTab(window.tabId)
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }

        ShellTabPane {
            id: pane
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            tabId: window.tabId
            // The main window's palette and shortcuts act on the main window's views.
            paneActive: window.active
            onActivated: AppController.selectTab(window.tabId)
        }
    }

    Shortcut {
        sequence: ShortcutMap.sequences["nav.back"] || ""
        onActivated: AppController.back(window.tabId)
    }
    Shortcut {
        sequence: ShortcutMap.sequences["nav.forward"] || ""
        onActivated: AppController.forward(window.tabId)
    }
    Shortcut {
        sequence: ShortcutMap.sequences["tab.close"] || ""
        onActivated: window.close()
    }
}
