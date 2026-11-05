// The rows of "Przenieś do…" (ShellMoveDialog.qml, docs/UI.md "Folders"): the notebook's root,
// the folders matching a query (TextHelper.fuzzyRank of their names, then paths; at most 100) and
// a new folder of the typed name. Pure; AppController::folderChoices() gives it the notebook's
// folders.
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

namespace brulion::shell {

/// The texts of the rows (translated by the dialog).
struct FolderChoiceTexts {
    QString rootName;      ///< "Notebook root"
    QString newFolder;     ///< "New folder “%1”"
    QString createSubtitle; ///< "Create it and move here" / "Create it"
};

/// Rows { folder, title, subtitle, icon, create, disabled } (every key in every row): the root
/// (when the query is empty or its folded name contains the folded query), the `folders` ranked
/// against `query` — without `ownFolder` and its subfolders ("" none: a folder being moved) —,
/// and, for a query naming no folder (letter case aside; slashes at its ends dropped), a row
/// creating it. `moving`: the row of `currentFolder` (not a new one) is disabled.
QVariantList folderChoices(const QStringList &folders, const QString &query, const QString &ownFolder,
                           const QString &currentFolder, bool moving, const FolderChoiceTexts &texts);

} // namespace brulion::shell
