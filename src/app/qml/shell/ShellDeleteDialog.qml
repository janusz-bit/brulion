pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellDeleteDialog — "Move to trash?" for a page, an attachment or a folder (AppShell.confirmDelete():
// `path` set, then opened); a folder's pages are counted and what deleting a special folder or file
// does to the notebook is said. Accepted, it moves `path` to the trash, with the toast's Undo. Its texts
// keep AppShell's translation context, where it was written first.
// Loaded from its file the first time it opens (docs/PERFORMANCE.md "QML files loaded when first used").
BConfirmDialog {
    id: deleteDialog

    property string path
    /// Workspace.folderInfo() of a folder ({} for a file).
    readonly property var info: path !== "" && visible ? Workspace.folderInfo(path) : ({})
    readonly property bool folder: AppController.routeFlag(info, "exists")
    readonly property int pages: AppController.routeInt(info, "pages", 0)
    /// [{ special, path }]: the special folders and files it is or holds.
    readonly property list<var> specials: AppController.routeList(info, "specials")
    readonly property string name: Js.endsWith(path, ".md") ? Workspace.pageTitle(path) : Js.sliceFrom(path, Js.lastIndexOf(path, "/") + 1)
    /// The trash path of what was deleted last (its toast's Undo).
    property list<string> trashed

    /// What deleting a special folder, a folder holding some, or the journal template does to
    /// the notebook: a sentence for each, after a space.
    function specialWarnings(): string {
        let text = ""
        for (let i = 0; i < deleteDialog.specials.length; ++i) {
            const warning = deleteDialog.specialWarning(deleteDialog.fieldOf(i, "special"), deleteDialog.fieldOf(i, "path"))
            if (warning !== "")
                text += " " + warning
        }
        return text
    }
    function fieldOf(index: int, key: string): string {
        return AppController.routeText(deleteDialog.specials[index], key)
    }
    function specialWarning(special: string, specialPath: string): string {
        const self = specialPath === deleteDialog.path
        switch (special) {
        case "journals":
            return self ? qsTranslate("AppShell", "This is the journals folder: new daily notes will create it again.")
                        : qsTranslate("AppShell", "It holds the journals folder “%1”: new daily notes will create it again.").arg(specialPath)
        case "templates":
            return self ? qsTranslate("AppShell", "This is the templates folder: its templates will no longer be offered.")
                        : qsTranslate("AppShell", "It holds the templates folder “%1”: its templates will no longer be offered.").arg(specialPath)
        case "attachments":
            return self ? qsTranslate("AppShell", "This is the attachments folder: the pictures and files in it go too.")
                        : qsTranslate("AppShell", "It holds the attachments folder “%1”: the pictures and files in it go too.").arg(specialPath)
        case "newPages":
            return self ? qsTranslate("AppShell", "This is the folder for new pages: new pages will create it again.")
                        : qsTranslate("AppShell", "It holds the folder for new pages “%1”: new pages will create it again.").arg(specialPath)
        case "journalTemplate":
            return self ? qsTranslate("AppShell", "This is the journal template: new daily notes will start empty.")
                        : qsTranslate("AppShell", "It holds the journal template “%1”: new daily notes will start empty.").arg(specialPath)
        case "weekly":
            return self ? qsTranslate("AppShell", "This is the folder of weekly notes: new weekly notes will create it again.")
                        : qsTranslate("AppShell", "It holds the folder of weekly notes “%1”: new weekly notes will create it again.").arg(specialPath)
        case "monthly":
            return self ? qsTranslate("AppShell", "This is the folder of monthly notes: new monthly notes will create it again.")
                        : qsTranslate("AppShell", "It holds the folder of monthly notes “%1”: new monthly notes will create it again.").arg(specialPath)
        case "weeklyTemplate":
            return self ? qsTranslate("AppShell", "This is the weekly note template: new weekly notes will start empty.")
                        : qsTranslate("AppShell", "It holds the weekly note template “%1”: new weekly notes will start empty.").arg(specialPath)
        case "monthlyTemplate":
            return self ? qsTranslate("AppShell", "This is the monthly note template: new monthly notes will start empty.")
                        : qsTranslate("AppShell", "It holds the monthly note template “%1”: new monthly notes will start empty.").arg(specialPath)
        }
        return ""
    }

    title: qsTranslate("AppShell", "Move to trash?")
    subject: name
    subjectIcon: folder ? "folder" : Js.endsWith(path, ".md") ? "file-text" : "file"
    message: (folder && pages > 0
              ? qsTranslate("AppShell", "This folder and %n page(s) in it will be moved to the trash.", "", pages)
                + " " + qsTranslate("AppShell", "You can restore them from there.")
              : qsTranslate("AppShell", "It will be moved to the trash. You can restore it from there."))
             + specialWarnings()
    confirmText: qsTranslate("AppShell", "Move to trash")
    destructive: true

    onAccepted: {
        const trashPath = Workspace.deletePage(deleteDialog.path)
        if (trashPath !== "") {
            deleteDialog.trashed = [trashPath]
            AppController.toastRestore(qsTranslate("AppShell", "Moved to trash"), qsTranslate("AppShell", "Undo"), deleteDialog.trashed)
        }
    }
}
