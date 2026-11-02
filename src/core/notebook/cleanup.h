// Tidying a notebook: the notes and attachments nothing refers to (docs/UI.md "Cleaning up").
// Part of the notebook module — see notebook/notebook.h.
#pragma once

#include "types.h"

#include <QList>

namespace brulion {
class VaultIndex;
}

namespace brulion::notebook {

/// A note or attachment nothing refers to, with what deleting it would take away.
struct UnreferencedFile {
    VaultPath path;
    bool attachment = false;
    int openTasks = 0; ///< tasks still to do (to do, in progress, deferred)
    int reminders = 0; ///< reminders, without those on closed tasks

    friend bool operator==(const UnreferencedFile &, const UnreferencedFile &) = default;
};

/// The notes and attachments no other note refers to, notes first, each sorted by path. A file
/// is referred to by a link, an embed or a `((id))` reference of another note (also a daily note
/// or a template), and a note also by a tag that names it (carried by another note, or in a
/// ```query filter) or a ```query `ref:` / `page:` filter. An attachment is also referred to
/// when its file name (as written or percent-encoded, in any letter case) stands in the text of
/// any note: the index does not read `<img src="…">` or plugins' own syntax, and a file wrongly
/// left out costs less than one wrongly offered.
/// Never listed, as nobody links to them on purpose: daily notes, the templates folder and the
/// journal template, hidden files and folders (a sync client's `.sync_….db`, `.gitignore`).
QList<UnreferencedFile> unreferencedFiles(const VaultIndex &index, const VaultConfig &config);

} // namespace brulion::notebook
