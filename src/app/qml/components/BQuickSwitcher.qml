pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BQuickSwitcher — Ctrl+O: jump to a page by typing part of its name; offers to create the
// page when nothing matches exactly.
//
//   BQuickSwitcher {
//       id: switcher
//       pages: vault.pageList        // [{ title, subtitle: folder path, icon?, path }]
//       onPagePathChosen: (path) => app.openPage(path)
//       onCreateRequested: (title) => app.createPage(title)
//   }
//
// Recent pages first when the query is empty (pass them first in `pages`).
// pagePathChosen(path): a page's `path` ("" without one); pageChosen(page): the page's object.
// `offerSearch`: a last result searches the notes' text for the query (`searchRequested`).
// `commandPrefix`: a query starting with ">" asks for the command palette instead
// (`commandsRequested` with the rest of the query; the switcher closes).
BPalette {
    id: control

    property list<var> pages
    property bool allowCreate: true
    property bool offerSearch: false
    property bool commandPrefix: false

    signal pageChosen(var page)
    signal pagePathChosen(string path)
    signal createRequested(string title)
    signal searchRequested(string text)
    signal commandsRequested(string text)

    /// commandsRequested()'s text, once this keystroke is done (the palette then takes the keyboard).
    property string _commandsText
    Timer {
        id: commandsLater
        interval: 0
        onTriggered: control.commandsRequested(control._commandsText)
    }

    onQueryChanged: {
        if (control.commandPrefix && Js.startsWith(control.query, ">")) {
            control._commandsText = Js.trimStart(Js.sliceFrom(control.query, 1))
            control.close()
            commandsLater.restart()
        }
    }

    items: pages
    placeholderText: qsTr("Go to page…")
    emptyTitle: qsTr("No pages found")
    defaultIcon: "file-text"
    readonly property string _trimmedQuery: Js.trim(query)
    readonly property bool _offerCreate: allowCreate && _trimmedQuery !== "" && !hasTitle(_trimmedQuery)
    readonly property bool _offerSearch: offerSearch && _trimmedQuery !== ""
    extraResults: _offerCreate && _offerSearch
                  ? [_createRow, _searchRow]
                  : _offerCreate ? [_createRow] : _offerSearch ? [_searchRow] : []
    readonly property var _createRow: ({ title: qsTr("Create page “%1”").arg(_trimmedQuery), icon: "plus", kind: "create",
                                         name: _trimmedQuery })
    readonly property var _searchRow: ({ title: qsTr("Search the notes for “%1”").arg(_trimmedQuery), icon: "search",
                                         kind: "search", text: _trimmedQuery })

    onRowActivated: (row) => {
        const kind = control.isExtra(row) ? control.itemString(row, "kind") : ""
        if (kind === "create")
            control.createRequested(control.itemString(row, "name"))
        else if (kind === "search")
            control.searchRequested(control.itemString(row, "text"))
        else {
            control.pagePathChosen(control.itemString(row, "path"))
            control.pageChosen(control.itemAt(row))
        }
    }
}
