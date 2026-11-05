pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BQuickSwitcher — Ctrl+O: jump to a page by typing part of its name; offers to create the
// page when nothing matches exactly.
//
//   BQuickSwitcher {
//       id: switcher
//       pages: vault.pageList        // [{ title, subtitle: folder path, icon?, path }]
//       onPageChosen: (page) => app.openPage(page.path)
//       onCreateRequested: (title) => app.createPage(title)
//   }
//
// Recent pages first when the query is empty (pass them first in `pages`).
// `offerSearch`: a last result searches the notes' text for the query (`searchRequested`).
// `commandPrefix`: a query starting with ">" asks for the command palette instead
// (`commandsRequested` with the rest of the query; the switcher closes).
BPalette {
    id: control

    property var pages: []
    property bool allowCreate: true
    property bool offerSearch: false
    property bool commandPrefix: false

    signal pageChosen(var page)
    signal createRequested(string title)
    signal searchRequested(string text)
    signal commandsRequested(string text)

    onQueryChanged: {
        if (commandPrefix && query.startsWith(">")) {
            const rest = query.slice(1).replace(/^\s+/, "")
            close()
            // (Once this keystroke is done: the palette then takes the keyboard.)
            Qt.callLater(() => control.commandsRequested(rest))
        }
    }

    items: pages
    placeholderText: qsTr("Go to page…")
    emptyTitle: qsTr("No pages found")
    defaultIcon: "file-text"
    extraResults: {
        const q = query.trim()
        if (q === "")
            return []
        const out = []
        if (allowCreate) {
            const folded = TextHelper.fold(q)
            if (!(pages || []).some(p => TextHelper.fold(p.title || "") === folded))
                out.push({ title: qsTr("Create page “%1”").arg(q), icon: "plus", create: true, name: q })
        }
        if (offerSearch)
            out.push({ title: qsTr("Search the notes for “%1”").arg(q), icon: "search", search: true, text: q })
        return out
    }

    onItemActivated: (item) => {
        if (item.create)
            createRequested(item.name)
        else if (item.search)
            searchRequested(item.text)
        else
            pageChosen(item)
    }
}
