pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellRouteTitles — the human title of a route (the top bar, the tab bar and the tab switcher,
// the side panel's header, a page window's title) and of a page, and the icon of a route's view
// (the tab bar: the command palette's icon of that view). No state: every file
// that needs it has one. (The texts keep AppShell's translation context: qsTranslate.) Dates are
// written in the language shown and follow a change of it.
QtObject {
    /// A page's title as people read it: daily notes by their date ("Wednesday, October 14,
    /// 2026"), not by their file name ("2026-10-14").
    function pageLabel(path: string): string {
        const day = path !== "" ? dayOf(path) : ""
        return day !== "" ? longDate(day) : Workspace.pageTitle(path)
    }
    /// `date` written out in the language shown. DatesHelper.locale is read for real, so a binding
    /// that calls this follows a language change (a method call alone is not tracked).
    function longDate(date: string): string {
        return DatesHelper.locale !== "" ? DatesHelper.formatLong(date) : date
    }

    function dayOf(path: string): string {
        return Workspace.journalDate(path)
    }
    /// A route's parameter as text (AppController.routeText, typed: several C++ results as the
    /// arguments of one call are not compiled, typed QML results are).
    function field(r: var, key: string): string {
        return AppController.routeText(r, key)
    }
    /// Human title of a route, from its `view`, `path`, `date`, `tag` and `title` (an extension's
    /// view keeps its label there; a `var` route cannot be read more than once by compiled code):
    ///   titles.routeTitle(titles.field(r, "view"), titles.field(r, "path"), titles.field(r, "date"),
    ///                     titles.field(r, "tag"), titles.field(r, "title"))
    function routeTitle(view: string, path: string, date: string, tag: string, title: string): string {
        switch (view) {
        case "journals": return qsTranslate("AppShell", "Journals")
        case "journal": return longDate(date)
        case "page": return pageLabel(path)
        case "calendar": return qsTranslate("AppShell", "Calendar")
        case "tasks": return qsTranslate("AppShell", "Tasks")
        case "habits": return qsTranslate("AppShell", "Habits")
        case "gym": return qsTranslate("AppShell", "Gym")
        case "console": return qsTranslate("AppShell", "Luau console")
        case "extension": return title !== "" ? title : qsTranslate("AppShell", "Extension")
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
        case "logs": return qsTranslate("AppShell", "Event log")
        case "settings": return qsTranslate("AppShell", "Settings")
        case "drawing": return Js.replaceRegExp(Workspace.pageTitle(path), "\\.(svg|excalidraw)$", "i", "")
        }
        return "Brulion"
    }
    /// The icon of a route's view: the command palette's for that view (CommandModel), a page's
    /// for a page and anything else.
    function routeIcon(view: string): string {
        switch (view) {
        case "journals": return "book-open"
        case "journal": return "notebook-pen"
        case "calendar": return "calendar"
        case "tasks": return "list-todo"
        case "habits": return "flame"
        case "gym": return "dumbbell"
        case "console": return "terminal"
        case "extension": return "puzzle"
        case "graph": return "waypoints"
        case "search": return "search"
        case "allPages": return "files"
        case "tags":
        case "tag": return "tags"
        case "sync": return "folder-sync"
        case "importExport": return "import"
        case "history": return "history"
        case "trash": return "trash-2"
        case "cleanup": return "sparkles"
        case "replace": return "repeat"
        case "logs": return "scroll-text"
        case "settings": return "settings"
        case "drawing": return "pencil"
        }
        return "file-text"
    }
}
