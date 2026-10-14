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
BPalette {
    id: control

    property var pages: []
    property bool allowCreate: true

    signal pageChosen(var page)
    signal createRequested(string title)

    items: pages
    placeholderText: qsTr("Go to page…")
    emptyTitle: qsTr("No pages found")
    defaultIcon: "file-text"
    extraResults: {
        const q = query.trim()
        if (!allowCreate || q === "")
            return []
        const folded = TextHelper.fold(q)
        const exact = (pages || []).some(p => TextHelper.fold(p.title || "") === folded)
        return exact ? [] : [{ title: qsTr("Create page “%1”").arg(q), icon: "plus", create: true, name: q }]
    }

    onItemActivated: (item) => {
        if (item.create)
            createRequested(item.name)
        else
            pageChosen(item)
    }
}
