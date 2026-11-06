pragma ComponentBehavior: Bound
import QtQml
import Brulion

// ShellMoveWords — the words of moves and renames, shared by the move and rename dialogs, the
// tree's drag and drop and the shell's toasts (one translation each).
//
//   ShellMoveWords { id: words }
//   words.impactText(3, 0, 2, [])                          // "Updated links: 3 (in 2 notes)"
//   words.movedText("", 3, false, "")                      // "Moved to the notebook root, …"
//   words.movedText("Archive", 0, false, "Home 2")          // "Moved to “Archive” as “Home 2”"
QtObject {
    /// What a move or rename rewrites (Workspace.moveImpact() / renameImpact() that is ok: its
    /// `links`, `tags`, `notes` and `specials`, read by the caller — a `var` map cannot be read more
    /// than once by compiled code): "Updated links: 3 (in 2 notes)" or "No link needs to change.",
    /// and the notebook settings that follow (its `specials`).
    /// A field of an impact (typed, as ShellRouteTitles.field): AppController.routeInt (0 when
    /// missing) / routeStrings.
    function count(impact: var, key: string): int {
        return AppController.routeInt(impact, key, 0)
    }
    function strings(impact: var, key: string): list<string> {
        return AppController.routeStrings(impact, key)
    }
    function impactText(links: int, tags: int, notes: int, specials: list<string>): string {
        const n = links + tags
        let text = n === 0 ? qsTr("No link needs to change.")
                 : (tags > 0 ? qsTr("Updated links and tags: %n", "", n) : qsTr("Updated links: %n", "", n))
                   + " " + qsTr("(in %n note(s))", "", notes)
        if (specials.length === 0)
            return text
        // Each a setting of .brulion/config.json (Workspace.folderInfo()'s `special`). (In the
        // loop itself: a call would leave `specials` unreadable for compiled code.)
        let names = ""
        for (let i = 0; i < specials.length; ++i) {
            let name = specials[i]
            switch (specials[i]) {
            case "journals": name = qsTr("journals folder"); break
            case "templates": name = qsTr("templates folder"); break
            case "attachments": name = qsTr("attachments folder"); break
            case "newPages": name = qsTr("folder for new pages"); break
            case "journalTemplate": name = qsTr("journal template"); break
            case "weekly": name = qsTr("folder of weekly notes"); break
            case "monthly": name = qsTr("folder of monthly notes"); break
            case "weeklyTemplate": name = qsTr("weekly note template"); break
            case "monthlyTemplate": name = qsTr("monthly note template"); break
            }
            names += (i > 0 ? ", " : "") + name
        }
        return text + " " + qsTr("Notebook settings will be updated: %1.").arg(names)
    }
    /// The toast after moving into `folder` ("" = the notebook's root), `n` links and tags
    /// rewritten (`tags`: some of them tags); `name`: the new name when the move renamed it too (a
    /// "/" in the rename dialog: "Moved to “Archive” as “Home 2”"), else "".
    function movedText(folder: string, n: int, tags: bool, name: string): string {
        if (name !== "")
            return movedRenamedText(folder, n, tags, name)
        if (folder === "")
            return n === 0 ? qsTr("Moved to the notebook root")
                 : tags ? qsTr("Moved to the notebook root, %n link(s) and tag(s) updated", "", n)
                 : qsTr("Moved to the notebook root, %n link(s) updated", "", n)
        const place = Js.sliceFrom(folder, Js.lastIndexOf(folder, "/") + 1)
        return n === 0 ? qsTr("Moved to “%1”").arg(place)
             : tags ? qsTr("Moved to “%1”, %n link(s) and tag(s) updated", "", n).arg(place)
             : qsTr("Moved to “%1”, %n link(s) updated", "", n).arg(place)
    }
    function movedRenamedText(folder: string, n: int, tags: bool, name: string): string {
        if (folder === "")
            return n === 0 ? qsTr("Moved to the notebook root as “%1”").arg(name)
                 : tags ? qsTr("Moved to the notebook root as “%1”, %n link(s) and tag(s) updated", "", n).arg(name)
                 : qsTr("Moved to the notebook root as “%1”, %n link(s) updated", "", n).arg(name)
        const place = Js.sliceFrom(folder, Js.lastIndexOf(folder, "/") + 1)
        return n === 0 ? qsTr("Moved to “%1” as “%2”").arg(place).arg(name)
             : tags ? qsTr("Moved to “%1” as “%2”, %n link(s) and tag(s) updated", "", n).arg(place).arg(name)
             : qsTr("Moved to “%1” as “%2”, %n link(s) updated", "", n).arg(place).arg(name)
    }
}
