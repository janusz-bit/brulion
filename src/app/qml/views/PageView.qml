pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// TEMPORARY — the block editor for route.path, so the app can be used end to end until the
// views agent writes the real PageView (page title editing, backlinks/outline side panel, page
// menu, history…). Replace this whole file then; JournalsView (also temporary) reuses it for
// a single day. What it shows the next PageView has to keep doing:
//   * PageController { host: Workspace } + BlockEditor; the route's path is applied one event
//     loop turn late (see applyRoute) and Workspace.pathRenamed goes to followRename(), so a
//     rename never saves unsaved edits under the old name (a duplicate page)
//   * route.blockId / route.line / route.heading scroll to that block after loading
//   * a page that does not exist yet (a link to a missing page) opens with an empty block to
//     type into; the editor creates the file with its first content
//   * settings: text width (AppSettings.editorWidth), "open pages as source", Ctrl+E
//   * merged external changes are told with a toast (save failures: Workspace.errorOccurred)
//
// PageView — route "page" ({path, line?, blockId?, heading?}): the block editor for one note.
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
    /// Shown in the side panel: the window's Ctrl+E and "Markdown source" act on the main view.
    property bool inSidePanel: false

    /// The page's day ("YYYY-MM-DD") when it is a daily note, else "".
    readonly property string journalDate: page.path !== "" ? Workspace.journalDate(page.path) : ""
    /// Top bar title: daily notes by their date; other pages use the shell's default (file name).
    readonly property string title: journalDate !== "" ? DatesHelper.formatLong(journalDate) : ""
    /// A block (or the source) is being edited: the shell hides its floating capture button.
    readonly property bool editing: page.editingRow >= 0 || page.sourceMode
    readonly property alias controller: page
    readonly property alias editor: editor

    readonly property string routePath: route && route.path ? route.path : ""
    property string _revealed: ""
    property bool _openedMissing: false
    /// The page starts with its own "# Title" (after the properties): no second title above it.
    property bool startsWithTitle: false

    function updateStartsWithTitle() {
        let first = null
        for (let i = 0; i < page.blocks.count && first === null; ++i) {
            const b = page.blocks.get(i)
            if (b.view !== "frontmatter")
                first = b
        }
        startsWithTitle = first !== null && first.view === "heading" && first.headingLevel === 1
                          && TextHelper.fold(first.plainText.trim()) === TextHelper.fold(page.title.trim())
    }
    Connections {
        target: page.blocks
        function onModelReset() { root.updateStartsWithTitle() }
        function onRowsInserted() { root.updateStartsWithTitle() }
        function onRowsRemoved() { root.updateStartsWithTitle() }
        function onDataChanged() { root.updateStartsWithTitle() }
    }

    focus: true

    // One event-loop turn late: on a rename the shell rewrites its routes before
    // Workspace.pathRenamed reaches followRename() below; setting the new path first would save
    // unsaved edits under the old name and bring that page back as a duplicate.
    onRouteChanged: Qt.callLater(root.applyRoute)
    Component.onCompleted: applyRoute()

    function applyRoute() {
        if (root.routePath === "")
            return
        if (page.path !== root.routePath) {
            root._revealed = ""
            page.path = root.routePath // saves the previous page, loads this one
            root.updateStartsWithTitle()
            root._openedMissing = page.loaded && !page.exists
            page.sourceMode = AppSettings.editorSourceMode
            if (page.loaded && !page.exists && page.blocks.count === 0)
                editor.startEditing(page.appendEmptyBlock(), -1)
        }
        revealTarget()
    }

    /// Scroll to the block the route names (once per route).
    function revealTarget() {
        if (!page.loaded || !route)
            return
        const key = JSON.stringify([page.path, route.blockId, route.line, route.heading])
        if (key === root._revealed)
            return
        root._revealed = key
        let row = -1
        if (route.blockId)
            row = page.rowOfBlockId(route.blockId)
        else if (route.line !== undefined)
            row = page.rowOfLine(route.line)
        else if (route.heading)
            row = headingRow(route.heading)
        if (row >= 0)
            Qt.callLater(() => editor.revealRow(row))
    }

    /// Row of the heading `name` ([[Page#Heading]]), -1 when there is none.
    function headingRow(name) {
        const wanted = TextHelper.fold(String(name).trim())
        for (let i = 0; i < page.blocks.count; ++i) {
            const b = page.blocks.get(i)
            if (b.headingLevel > 0 && TextHelper.fold(b.plainText.trim()) === wanted)
                return i
        }
        return -1
    }

    PageController {
        id: page
        objectName: "pageController" // the shell's self-test finds the editor by it
        host: Workspace
        // A page opened before it existed (a link to a missing page) joins the recent pages
        // once its first content created it.
        onExistsChanged: {
            if (page.exists && root._openedMissing && root.journalDate === "")
                Workspace.notePageOpened(page.path)
            root._openedMissing = false
        }
        onExternallyChanged: (merged) => {
            if (merged)
                AppController.toast(qsTr("Merged changes made elsewhere into this page"), "info")
        }
    }

    Connections {
        target: Workspace
        function onPathRenamed(oldPath, newPath) {
            if (page.path === oldPath)
                page.followRename(oldPath, newPath)
            else if (page.path.startsWith(oldPath + "/"))
                page.followRename(page.path, newPath + page.path.slice(oldPath.length))
        }
    }

    BlockEditor {
        id: editor
        objectName: "pageEditor"
        anchors.fill: parent
        controller: page
        focus: true
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
        // The side panel is narrow: phone-like margins there.
        horizontalPadding: Theme.compact || root.inSidePanel ? Theme.space3 : Theme.space8
        header: Component {
            Item {
                id: titleHeader
                width: editor.width
                readonly property bool small: Theme.compact || root.inSidePanel
                implicitHeight: (titleText.visible ? titleText.implicitHeight + (small ? Theme.space3 : Theme.space6) : 0)
                                + (small ? Theme.space4 : Theme.space8)
                                + (page.loaded && page.blocks.count === 0 ? Theme.controlHeight + Theme.space3 : 0)
                Text {
                    id: titleText
                    visible: !root.startsWithTitle || root.journalDate !== ""
                    x: editor.contentX + editor.gutter
                    y: titleHeader.small ? Theme.space4 : Theme.space8
                    width: editor.contentWidth - editor.gutter
                    text: root.journalDate !== "" ? DatesHelper.formatLong(root.journalDate) : page.title
                    textFormat: Text.PlainText
                    font.family: Theme.fontEditor
                    font.pixelSize: titleHeader.small ? Theme.text2xl : Theme.text3xl
                    font.weight: Theme.weightSemiBold
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightTight)
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                // An empty page (e.g. today's journal without a template): say where to type.
                BButton {
                    visible: page.loaded && page.blocks.count === 0
                    x: editor.contentX + editor.gutter - Theme.space3
                    anchors.top: titleText.visible ? titleText.bottom : undefined
                    anchors.topMargin: Theme.space3
                    y: titleHeader.small ? Theme.space4 : Theme.space8
                    variant: "ghost"
                    iconName: "square-pen"
                    text: qsTr("Start writing")
                    onClicked: editor.startEditing(page.appendEmptyBlock(), -1)
                }
            }
        }
    }

    Connections {
        target: page
        function onLoadingChanged() {
            if (page.loaded && !page.loading)
                root.revealTarget()
        }
    }

    // Dev: `--editor-script <state>` puts this editor into a state for screenshots of the real
    // app (qml/dev/PageScript.qml, tests/app/shell/screenshots.sh "page-*" shots).
    Loader {
        active: DevTools.editorScript !== "" && !DevTools.editor && !root.inSidePanel
        sourceComponent: PageScript {
            editor: editor
            controller: page
        }
    }

    // Command palette / page menu: "Markdown source".
    Connections {
        target: AppController
        function onUiActionRequested(action) {
            if (action === "toggleSourceMode" && root.visible && !root.inSidePanel)
                editor.toggleSourceMode()
        }
    }

    Shortcut {
        sequence: "Ctrl+E"
        enabled: root.visible && !root.inSidePanel // one per window (two would be ambiguous)
        onActivated: editor.toggleSourceMode()
    }
}
