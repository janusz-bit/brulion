pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// LinkMenu — the menu of a link in rendered text: a [[page link]] (also a Markdown link to a
// note, and each path part of [[Folder/Page]]), a #tag, a ((block reference)) or a web address. It opens with a right-click (mouse,
// touchpad, stylus) or a long press on the link — in the editor's blocks (BlockText), query
// results, embeds, table cells and Tasks (TasksRow) — never with the block menu.
//
//   LinkMenu { kind: info.kind; onChosen: (action) => … }   // info: PageController.linkMenu()
//   menu.popup(item, x, y)
//
// chosen(action): "open" (what a click does: the page — created by its first content when it does
// not exist yet —, a path part's page or folder, the tag's page, the block, the address),
// "openSide" (in the side panel; wide windows only), "openTab" / "openWindow" (what "open" does, in a
// new tab / a window of its own: where the app's shell offers them, OpenIn), "copy" (the link, a folder's path, the tag or
// the address as text), and for a tag "overview" (the
// tag view: everything tagged), "tasks" (Tasks with the tag), "filter" (Tasks: filter the list by
// it — `filterFirst`).
BMenu {
    id: menu

    /// "page" | "tag" | "block" | "ext"
    property string kind: "page"
    /// A page link whose page does not exist yet ("Create page").
    property bool unresolved: false
    /// A path part of a link naming a folder (a part: link): "Show folder", "Copy folder path".
    property bool folder: false
    /// Tasks: "Filter by tag" comes first (a tag's click there).
    property bool filterFirst: false
    /// "Open in side panel" offered (wide windows, a host with panels).
    property bool sidePanel: !Theme.narrow

    readonly property bool tag: kind === "tag"
    readonly property bool external: kind === "ext"

    signal chosen(string action)

    /// The Tasks query of "tasks" for `tag`: `tag:dom`, `tag:"ważne sprawy"`.
    function tasksQuery(tag: string): string {
        return "tag:" + (Js.test(tag, "[\\s,\"]", "") ? "\"" + tag + "\"" : tag)
    }

    component ShownItem: BMenuItem {
        property bool shown: true
        visible: shown
        height: shown ? implicitHeight : 0
    }

    ShownItem {
        objectName: "linkMenuFilter"
        shown: menu.tag && menu.filterFirst
        text: qsTr("Filter by tag")
        iconName: "filter"
        onTriggered: menu.chosen("filter")
    }
    BMenuItem {
        objectName: "linkMenuOpen"
        text: menu.tag ? qsTr("Open tag page")
                       : menu.folder ? qsTr("Show folder")
                       : menu.unresolved ? qsTr("Create page")
                       : menu.external ? qsTr("Open in browser")
                       : qsTr("Open")
        iconName: menu.tag ? "hash" : menu.folder ? "folder" : menu.unresolved ? "plus"
                  : menu.external ? "external-link" : "file-text"
        onTriggered: menu.chosen("open")
    }
    ShownItem {
        objectName: "linkMenuOpenTab"
        shown: OpenIn.tabs && !menu.external
        text: qsTr("Open in new tab")
        iconName: "plus"
        onTriggered: menu.chosen("openTab")
    }
    ShownItem {
        objectName: "linkMenuOpenSide"
        shown: menu.sidePanel && !menu.external
        text: qsTr("Open in side panel")
        iconName: "panel-right"
        onTriggered: menu.chosen("openSide")
    }
    ShownItem {
        objectName: "linkMenuOpenWindow"
        shown: OpenIn.windows && !menu.external
        text: qsTr("Open in new window")
        iconName: "app-window"
        onTriggered: menu.chosen("openWindow")
    }
    ShownItem {
        objectName: "linkMenuOverview"
        shown: menu.tag
        text: qsTr("Tag overview")
        iconName: "hash"
        onTriggered: menu.chosen("overview")
    }
    ShownItem {
        objectName: "linkMenuTasks"
        shown: menu.tag && !menu.filterFirst
        text: qsTr("Tasks with this tag")
        iconName: "list-todo"
        onTriggered: menu.chosen("tasks")
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "linkMenuCopy"
        text: menu.tag ? qsTr("Copy tag") : menu.external ? qsTr("Copy address")
                       : menu.folder ? qsTr("Copy folder path") : qsTr("Copy link")
        iconName: "copy"
        onTriggered: menu.chosen("copy")
    }
}
