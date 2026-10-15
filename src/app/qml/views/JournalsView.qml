import QtQuick
import Brulion

// TEMPORARY — until the views agent writes the real JournalsView (the daily notes as one
// scrolling list, newest first): shows one day in the block editor of the temporary PageView —
// route.date (route.path when the shell passed it), else today, created from the journal
// template like Ctrl+J does. Replace this whole file then.
//
// JournalsView — routes "journals" (home: the daily notes, newest first; route.date scrolls to that day) and "journal" ({date, path}: one day).
// Contract with the shell (qml/shell/ShellRouteView.qml): the view fills its area and gets
//   property var route            the route it shows (QVariantMap, see src/app/shell/navigation.h)
// and may offer
//   property string title         the top bar title (default: derived from the route)
//   property Component sidePanel  content for the side panel (backlinks, outline, mini calendar)
//   property bool editing         text is being edited (the shell hides its capture button)
// and gets `inSidePanel` (true in the side panel). `--editor-script <state>` drives the editor
// for screenshots through qml/dev/PageScript.qml.
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false

    readonly property string date: route && route.date ? route.date : AppController.today(0)
    readonly property string path: route && route.path ? route.path : Workspace.ensureJournal(date)

    focus: true

    readonly property bool editing: pageView.editing

    PageView {
        id: pageView
        anchors.fill: parent
        inSidePanel: root.inSidePanel
        focus: true
        visible: root.path !== ""
        route: ({ view: "page", path: root.path })
    }

    BEmptyState {
        anchors.centerIn: parent
        visible: root.path === ""
        iconName: "book-open"
        title: qsTr("Journals")
        description: qsTr("Today's journal page could not be opened.")
    }
}
