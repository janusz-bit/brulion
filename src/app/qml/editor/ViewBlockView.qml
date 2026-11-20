pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ViewBlockView — a ```view block (docs/SYNTAX.md §13 "View blocks"): an extension's view drawn in
// the page by ShellExtensionView (the same lines, controls and actions as the extension's own
// views), as tall as its lines. Its first line names the view, `extension/view`; the lines after
// it are the view's params (`key: value`), and the view gets the page's `page`, `path` and `date`
// too. A click beside the controls (or Enter on the selected block) edits the source in place: the
// monospace editor with the view under it, drawn again as you type. A view that cannot be drawn
// shows the source with the reason; one of an extension that does not run here (not approved, off,
// unknown) says so, with a button for what can be done (ShellMacroDialog). The view is drawn again
// only while it is live: its page on screen (PageController.active) and the block not folded
// away — a block scrolled out of the list has no delegate at all.
Rectangle {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    readonly property bool problem: lines.sourceProblem !== "" || lines.error !== "" || lines.extensionState !== ""
    /// The source is on screen: while editing, or when there is no view to show.
    readonly property bool showSource: root.block.editing || lines.sourceProblem !== "" || lines.error !== ""
    readonly property real codeLine: Theme.lineHeightPx(Theme.textSm, 1.5)
    readonly property real firstLineCenter: showSource ? sourceLoader.y + codeLine / 2 : header.y + header.height / 2

    implicitHeight: (viewList.visible ? viewList.y + viewList.height
                                      : message.visible ? message.y + message.height
                                                        : sourceLoader.y + sourceLoader.height) + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bg
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    ExtensionView {
        id: lines
        objectName: "viewBlockModel"
        extensions: Workspace.extensions
        source: root.block.rowData.editText
        notePath: root.block.controller ? root.block.controller.path : ""
        live: root.block.shown && (root.block.controller ? root.block.controller.active : false)
        onPageRequested: (page, line) => AppController.openNamedPage(page, line)
        onJournalRequested: (date) => AppController.openJournal(date)
        // Another view of the extension opens as a route of its own (the block stays as it is).
        onViewRequested: (view, params) => AppController.navigate(lines.routeFor(view, params))
        Component.onCompleted: lines.refresh() // drawn now: the row has its height in its first frame
    }

    // The view's title: its label, else extension/name; "view" at the right like a code block's language.
    Row {
        id: header
        x: Theme.space4
        y: Theme.space2
        width: parent.width - 2 * Theme.space4 - label.width - Theme.space2
        height: Math.max(titleText.implicitHeight, Theme.iconSizeSm)
        spacing: Theme.space1
        BIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "puzzle"
            size: Theme.iconSizeSm
            color: Theme.fgFaint
        }
        Text {
            id: titleText
            anchors.verticalCenter: parent.verticalCenter
            width: header.width - Theme.iconSizeSm - header.spacing
            text: lines.extension === "" ? "" : Workspace.extensions.revision >= 0
                                           ? Workspace.extensions.viewTitle(lines.extension, lines.view) : ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            font.weight: Theme.weightMedium
            color: Theme.fgMuted
        }
    }
    Text {
        id: label
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        y: Theme.space2
        text: "view"
        textFormat: Text.PlainText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    // The source: edited in place; shown read-only until clicked when the view cannot be drawn.
    Loader {
        id: sourceLoader
        x: Theme.space4
        y: header.y + header.height + Theme.space2
        width: parent.width - 2 * Theme.space4
        height: active && item ? (item as Item).implicitHeight : 0
        active: root.showSource
        sourceComponent: InlineEditor {
            objectName: "viewSource"
            width: sourceLoader.width
            block: root.block
            persistent: true
            multiline: true
            highlightMode: "code"
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            lineHeightPx: root.codeLine
            wrapMode: TextEdit.Wrap
        }
    }

    // Why nothing is drawn: the source's problem, the view's error, or its extension's state.
    Column {
        id: message
        objectName: "viewBlockMessage"
        visible: root.problem
        x: Theme.space4
        y: sourceLoader.y + sourceLoader.height + (root.showSource ? Theme.space2 : 0)
        width: parent.width - 2 * Theme.space4
        spacing: Theme.space2
        Text {
            width: parent.width
            text: lines.sourceProblem !== "" ? lines.sourceProblem
                : lines.error !== "" ? qsTr("The view could not be drawn: %1").arg(lines.error)
                : lines.extensionState === "approval" ? qsTr("Its extension has not run on this device yet.")
                : lines.extensionState === "off" ? qsTr("Its extension is off on this device.")
                : lines.extensionState === "error" ? qsTr("Its extension failed to load.")
                : qsTr("No extension on this device offers this view.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: lines.sourceProblem !== "" || lines.error !== "" || lines.extensionState === "error"
                   ? Theme.dangerText : Theme.fgMuted
        }
        BButton {
            objectName: "viewBlockExtension"
            visible: lines.sourceProblem === "" && lines.error === "" && lines.extensionState !== ""
            text: lines.extensionState === "approval" ? qsTr("Run…")
                : lines.extensionState === "off" ? qsTr("Switch on…") : qsTr("Details…")
            size: "small"
            onClicked: Workspace.extensions.showView(lines.extension, lines.view)
        }
    }

    ShellExtensionView {
        id: viewList
        objectName: "viewBlockLines"
        visible: !root.problem && count > 0
        embedded: true
        lines: lines
        x: Theme.space3
        y: (root.showSource ? sourceLoader.y + sourceLoader.height : header.y + header.height) + Theme.space2
        width: parent.width - 2 * Theme.space3
        Accessible.name: titleText.text
    }

    // Not editing: a click beside the controls starts editing (Shift / Ctrl+click select blocks),
    // a finger held selects the block. (The controls take their own taps first.)
    TapHandler {
        id: tap
        enabled: !root.block.editing
        onTapped: if (!root.block.editor.blockTappedWith(root.block.index, tap.point.modifiers))
                      root.block.editor.startEditing(root.block.index, -1)
    }
    TapHandler {
        enabled: !root.block.editing && Theme.touchUi
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: root.block.editor.longPressText(root.block.index)
    }
}
