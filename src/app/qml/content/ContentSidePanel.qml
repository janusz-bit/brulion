pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentSidePanel — the open page's context (docs/UI.md "Side panel"), one component for both
// hosts: the side panel docked beside the text (wide windows, ShellSidePanel loads a view's
// `sidePanel`) and the drawer from the right edge of narrow windows (ShellContextDrawer). An
// accordion of folding sections (ContentPanelSection), all in one scrolling column:
//
//   a page        Outline (its headings, live while typing; a click scrolls the editor there),
//                 Backlinks (linked references), Unlinked mentions (folded: they read every note),
//                 Local graph (the pages around it);
//   a journal day Day plan (its timed reminders and dates, Workspace.dayPlan), Tasks (open tasks
//                 due by that day, overdue in red), A year ago (that day's journal a year before),
//                 Backlinks, Calendar (folded: the month with its journal days);
//   both          the extensions' panels (b.view with meta.panel, docs/EXTENSIONS.md "Panels"):
//                 each a section with the view drawn by ShellExtensionView for the note on screen
//                 (its `page`, `path`, `date` among the view's params), after the app's own.
//
// What is folded is no setting: every panel starts with the defaults above (a drawer opened again
// is a new panel). `initialTab` names a section to unfold and scroll to ("links", "unlinked",
// "outline", "calendar", "graph", "plan", "tasks", "yearAgo"; the page menu's and route.open's
// side panel requests).
//
//   ContentSidePanel { path: page.path; controller: page; editor: editor }
Item {
    id: panel

    property string path
    property PageController controller
    property BlockEditor editor
    /// The section unfolded and shown first ("" none).
    property string initialTab: ""
    /// The path is an attachment (a picture, a PDF): only its references.
    property bool attachment: false

    readonly property string journalDate: path !== "" ? Workspace.journalDate(path) : ""
    readonly property bool journal: journalDate !== "" && !attachment
    /// The extensions' panels beside this note: {extension, view, title, icon} (followed while
    /// extensions load, are approved or switched).
    readonly property list<var> extensionPanels: panel.attachment || Workspace.extensions.revision < 0 ? panel.noPanels
                                                 : Workspace.extensions.panels(panel.journal ? "journal" : "page")
    readonly property list<var> noPanels: []
    /// "links" while the linked references are listed here (the page leaves out its own section
    /// under the blocks then: PageView.refsInSidePanel), else "outline".
    readonly property string currentTab: backlinksSection.expanded ? "links" : "outline"
    /// The page's linked references (the drawer handle's badge), -1 while not known.
    readonly property int backlinkCount: backlinksSection.contentItem
                                         ? (backlinksSection.contentItem as ContentBacklinks).linkedCount : -1

    function rendererPalette(): var {
        return panel.editor ? panel.editor.rendererPalette : ({})
    }
    function sectionFor(name: string): ContentPanelSection {
        switch (name) {
        case "links": return backlinksSection
        case "unlinked": return unlinkedSection
        case "outline": return outlineSection
        case "calendar": return calendarSection
        case "graph": return graphSection
        case "plan": return planSection
        case "tasks": return tasksSection
        case "yearAgo": return yearAgoSection
        }
        return null
    }
    /// Unfold section `name` and scroll to it.
    function reveal(name: string): void {
        const s = panel.sectionFor(name)
        if (!s || !s.visible)
            return
        s.expanded = true
        scrollLater.target = s
        scrollLater.restart()
    }
    // (One turn later: the unfolded section has its height then.)
    Later {
        id: scrollLater
        property Item target: null
        onTriggered: {
            const s = scrollLater.target
            if (s)
                flick.contentY = Math.max(0, Math.min(s.y, flick.contentHeight - flick.height))
        }
    }

    Component.onCompleted: {
        // A one-off request (the page menu, route.open): used once, then forgotten.
        const requested = panel.initialTab !== "" ? panel.initialTab : "" + AppSettings.value("ui/sidePanel/tab", "")
        AppSettings.setValue("ui/sidePanel/tab", "")
        if (requested !== "")
            panel.reveal(requested)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Flickable {
        id: flick
        objectName: "sidePanelSections"
        TouchFlick.enabled: true
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.space4
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        ColumnLayout {
            id: column
            y: Theme.space1
            width: flick.width
            spacing: 0

            // ---- A journal day ---------------------------------------------------------------
            ContentPanelSection {
                id: planSection
                objectName: "sectionPlan"
                visible: panel.journal
                Layout.fillWidth: true
                title: qsTr("Day plan")
                count: contentItem ? (contentItem as ContentDayPlan).count : -1
                content: Component { ContentDayPlan { date: panel.journalDate } }
            }
            ContentPanelSection {
                id: tasksSection
                objectName: "sectionTasks"
                visible: panel.journal
                Layout.fillWidth: true
                title: qsTr("Tasks")
                count: contentItem ? (contentItem as ContentDayTasks).count : -1
                content: Component { ContentDayTasks { date: panel.journalDate } }
            }
            ContentPanelSection {
                id: yearAgoSection
                objectName: "sectionYearAgo"
                visible: panel.journal
                Layout.fillWidth: true
                title: qsTr("A year ago")
                content: Component { ContentYearAgo { date: panel.journalDate } }
            }

            // ---- A page ------------------------------------------------------------------------
            ContentPanelSection {
                id: outlineSection
                objectName: "sectionOutline"
                visible: !panel.journal && !panel.attachment
                Layout.fillWidth: true
                title: qsTr("Outline")
                count: contentItem ? (contentItem as ContentOutline).count : -1
                content: Component {
                    ContentOutline {
                        embedded: true
                        controller: panel.controller
                        editor: panel.editor
                    }
                }
            }

            // ---- Both --------------------------------------------------------------------------
            ContentPanelSection {
                id: backlinksSection
                objectName: "sectionBacklinks"
                Layout.fillWidth: true
                title: qsTr("Backlinks")
                count: panel.backlinkCount
                content: Component {
                    ContentBacklinks {
                        path: panel.path
                        rendererPalette: panel.rendererPalette()
                        compact: true
                        sectioned: true
                        showUnlinked: false
                    }
                }
            }
            ContentPanelSection {
                id: unlinkedSection
                objectName: "sectionUnlinked"
                visible: !panel.journal && !panel.attachment
                // Folded at first: listing them reads every note.
                expanded: false
                Layout.fillWidth: true
                title: qsTr("Unlinked mentions")
                count: contentItem ? (contentItem as ContentBacklinks).unlinkedCount : -1
                content: Component {
                    ContentBacklinks {
                        path: panel.path
                        rendererPalette: panel.rendererPalette()
                        compact: true
                        sectioned: true
                        showLinked: false
                        unlinkedOpen: true
                    }
                }
            }
            ContentPanelSection {
                id: graphSection
                objectName: "sectionGraph"
                visible: !panel.journal && !panel.attachment
                Layout.fillWidth: true
                title: qsTr("Local graph")
                count: contentItem ? (contentItem as ContentLocalGraph).count : -1
                content: Component { ContentLocalGraph { path: panel.path } }
            }
            // ---- The extensions' panels (b.view's meta.panel) ---------------------------------------
            Repeater {
                model: panel.extensionPanels
                delegate: ContentPanelSection {
                    id: extensionSection
                    required property var modelData
                    readonly property string extensionId: AppController.routeText(extensionSection.modelData, "extension")
                    readonly property string viewName: AppController.routeText(extensionSection.modelData, "view")
                    objectName: "sectionExtension_" + extensionSection.extensionId + "_" + extensionSection.viewName
                    // A view that shows nothing for this note (an empty column) leaves its section out.
                    visible: panelModel.count > 0 || panelModel.error !== ""
                    Layout.fillWidth: true
                    title: AppController.routeText(extensionSection.modelData, "title")
                    ExtensionView {
                        id: panelModel
                        objectName: "sidePanelExtensionModel"
                        extensions: Workspace.extensions
                        extension: extensionSection.extensionId
                        view: extensionSection.viewName
                        notePath: panel.path
                        live: panel.visible
                        onPageRequested: (page, line) => AppController.openNamedPage(page, line)
                        onJournalRequested: (date) => AppController.openJournal(date)
                        onViewRequested: (view, params) => AppController.navigate(panelModel.routeFor(view, params))
                        Component.onCompleted: panelModel.refresh()
                    }
                    content: Component {
                        Item {
                            id: extensionPanel
                            implicitHeight: panelLines.height + Theme.space2
                            ShellExtensionView {
                                id: panelLines
                                objectName: "sidePanelExtensionLines"
                                x: Theme.space3
                                width: parent.width - 2 * Theme.space3
                                embedded: true
                                lines: panelModel
                                Accessible.name: extensionSection.title
                            }
                            BText {
                                visible: panelModel.error !== ""
                                x: Theme.space3
                                width: parent.width - 2 * Theme.space3
                                text: qsTr("The view could not be drawn: %1").arg(panelModel.error)
                                variant: "small"
                                color: Theme.dangerText
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            ContentPanelSection {
                id: calendarSection
                objectName: "sectionCalendar"
                visible: panel.journal
                expanded: false
                Layout.fillWidth: true
                title: qsTr("Calendar")
                content: Component {
                    ContentMiniCalendar {
                        implicitHeight: 420
                        selectedDate: panel.journalDate !== "" ? panel.journalDate : DatesHelper.today
                    }
                }
            }
        }
    }
}
