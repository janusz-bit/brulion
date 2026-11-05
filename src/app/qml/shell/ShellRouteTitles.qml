pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellRouteTitles — the human title of a route (the top bar, the tab bar and the tab switcher,
// the side panel's header, a page window's title) and of a page. No state: every file that needs
// it has one. (The texts keep AppShell's translation context: qsTranslate.)
QtObject {
    /// A page's title as people read it: daily notes by their date ("środa, 14 października
    /// 2026"), not by their file name ("2026-10-14").
    function pageLabel(path: string): string {
        const day = path !== "" ? dayOf(path) : ""
        return day !== "" ? DatesHelper.formatLong(day) : Workspace.pageTitle(path)
    }

    function dayOf(path: string): string {
        return Workspace.journalDate(path)
    }
    /// A route's parameter as text (AppController.routeText, typed: several C++ results as the
    /// arguments of one call are not compiled, typed QML results are).
    function field(r: var, key: string): string {
        return AppController.routeText(r, key)
    }
    /// Human title of a route, from its `view`, `path`, `date` and `tag` (a `var` route cannot be
    /// read more than once by compiled code):
    ///   titles.routeTitle(titles.field(r, "view"), titles.field(r, "path"), titles.field(r, "date"),
    ///                     titles.field(r, "tag"))
    function routeTitle(view: string, path: string, date: string, tag: string): string {
        switch (view) {
        case "journals": return qsTranslate("AppShell", "Journals")
        case "journal": return DatesHelper.formatLong(date)
        case "page": return pageLabel(path)
        case "calendar": return qsTranslate("AppShell", "Calendar")
        case "tasks": return qsTranslate("AppShell", "Tasks")
        case "habits": return qsTranslate("AppShell", "Habits")
        case "graph": return qsTranslate("AppShell", "Graph")
        case "search": return qsTranslate("AppShell", "Search")
        case "allPages": return path !== "" ? Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
                                            : qsTranslate("AppShell", "All pages")
        case "tags": return qsTranslate("AppShell", "Tags")
        case "tag": return "#" + tag
        case "sync": return qsTranslate("AppShell", "Sync")
        case "importExport": return qsTranslate("AppShell", "Import and export")
        case "history": return qsTranslate("AppShell", "History of %1").arg(pageLabel(path))
        case "trash": return qsTranslate("AppShell", "Trash")
        case "cleanup": return qsTranslate("AppShell", "Clean up")
        case "replace": return qsTranslate("AppShell", "Replace in notebook")
        case "settings": return qsTranslate("AppShell", "Settings")
        case "drawing": return Js.replaceRegExp(Workspace.pageTitle(path), "\\.(svg|excalidraw)$", "i", "")
        }
        return "Brulion"
    }
}
