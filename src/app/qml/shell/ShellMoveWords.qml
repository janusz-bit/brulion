import QtQml

// ShellMoveWords — the words of moves and renames, shared by the move and rename dialogs, the
// tree's drag and drop and the shell's toasts (one translation each).
//
//   ShellMoveWords { id: words }
//   words.impactText(Workspace.moveImpact(path, folder))  // "Zaktualizowane linki: 3 (w 2 notatkach)"
//   words.movedText("", 3, false)                          // "Przeniesiono do głównego folderu …"
QtObject {
    /// What a move or rename rewrites (Workspace.moveImpact() / renameImpact() that is ok):
    /// "Updated links: 3 (in 2 notes)" or "No link needs to change.", and the notebook settings
    /// that follow (its `specials`).
    function impactText(impact) {
        const n = (impact.links || 0) + (impact.tags || 0)
        let text = n === 0 ? qsTr("No link needs to change.")
                 : ((impact.tags || 0) > 0 ? qsTr("Updated links and tags: %n", "", n) : qsTr("Updated links: %n", "", n))
                   + " " + qsTr("(in %n note(s))", "", impact.notes || 0)
        const specials = impact.specials || []
        if (specials.length > 0)
            text += " " + qsTr("Notebook settings will be updated: %1.").arg(specials.map(specialName).join(", "))
        return text
    }
    /// A setting of .brulion/config.json (Workspace.folderInfo()'s `special`).
    function specialName(special) {
        switch (special) {
        case "journals": return qsTr("journals folder")
        case "templates": return qsTr("templates folder")
        case "attachments": return qsTr("attachments folder")
        case "newPages": return qsTr("folder for new pages")
        case "journalTemplate": return qsTr("journal template")
        }
        return special
    }
    /// The toast after moving into `folder` ("" = the notebook's root), `n` links and tags
    /// rewritten (`tags`: some of them tags).
    function movedText(folder, n, tags) {
        if (folder === "")
            return n === 0 ? qsTr("Moved to the notebook root")
                 : tags ? qsTr("Moved to the notebook root, %n link(s) and tag(s) updated", "", n)
                 : qsTr("Moved to the notebook root, %n link(s) updated", "", n)
        const place = folder.split("/").pop()
        return n === 0 ? qsTr("Moved to “%1”").arg(place)
             : tags ? qsTr("Moved to “%1”, %n link(s) and tag(s) updated", "", n).arg(place)
             : qsTr("Moved to “%1”, %n link(s) updated", "", n).arg(place)
    }
}
