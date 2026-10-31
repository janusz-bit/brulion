pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentSidePanel — the side panel's context for the open page (wide windows, ShellSidePanel
// loads a view's `sidePanel`): tabs Odnośniki (the page's linked references, compact), Konspekt
// (its headings, live while typing; a click scrolls the editor there) and Kalendarz (a month
// with the days that have a journal; a click opens that day, the day's dates and reminders are
// listed under it). The chosen tab is remembered.
//
//   ContentSidePanel { path: page.path; controller: page; editor: editor }
Item {
    id: panel

    property string path
    property PageController controller
    property BlockEditor editor
    /// The tab shown first: "links" | "outline" | "calendar" (default: the last one used).
    property string initialTab: ""

    readonly property var tabs: ["links", "outline", "calendar"]
    /// The tab shown now ("links" | "outline" | "calendar"): the page leaves out its own references
    /// section while this panel shows them (PageView.refsInSidePanel). A property, not the
    /// remembered setting: AppSettings.value() is not tracked by bindings.
    readonly property string currentTab: tabs[tabBar.currentIndex] || "links"
    readonly property string journalDate: path !== "" ? Workspace.journalDate(path) : ""

    function rendererPalette() {
        return editor ? editor.rendererPalette : ({})
    }

    Component.onCompleted: {
        const remembered = String(AppSettings.value("ui/sidePanel/tab", "links"))
        const tab = initialTab !== "" ? initialTab : remembered
        tabBar.currentIndex = Math.max(0, tabs.indexOf(tab))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BTabBar {
            id: tabBar
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            stretch: true
            onCurrentIndexChanged: AppSettings.setValue("ui/sidePanel/tab", panel.tabs[currentIndex] || "links")
            BTabButton { text: qsTr("Links"); iconName: "link"; count: links.linkedCount }
            BTabButton { text: qsTr("Outline"); iconName: "list" }
            BTabButton { text: qsTr("Calendar"); iconName: "calendar-days" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // ---- Odnośniki --------------------------------------------------------------------------
            Flickable {
                id: linksFlick
                TouchFlick.enabled: true
                clip: true
                contentWidth: width
                contentHeight: links.implicitHeight + 2 * Theme.space3
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: BScrollBar {}

                ContentBacklinks {
                    id: links
                    x: Theme.space2
                    y: Theme.space3
                    width: linksFlick.width - 2 * Theme.space2
                    path: panel.path
                    rendererPalette: panel.rendererPalette()
                    compact: true
                    showUnlinked: true
                }
            }

            // ---- Konspekt ---------------------------------------------------------------------------
            ContentOutline {
                controller: panel.controller
                editor: panel.editor
            }

            // ---- Kalendarz --------------------------------------------------------------------------
            ContentMiniCalendar {
                selectedDate: panel.journalDate !== "" ? panel.journalDate : DatesHelper.today
            }
        }
    }
}
