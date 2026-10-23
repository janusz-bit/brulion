// Pages and folders as the app and the command line change them (docs/SYNTAX.md §1 "Folders",
// §2 "Rewriting on rename and move"): new page paths and pages (letter-case twins are the same
// page), folders (names sanitised, hidden ones refused), and every rename and move through one
// relocate(): the index plans which notes to rewrite so that each link and tag keeps naming the
// file it named (VaultIndex::planRelocation()), the vault moves the files (history, watches), the
// planned notes are written (one history step each) and the settings of .brulion/config.json that
// name a moved special folder follow it. Deleting moves to .trash/. Part of the notebook module —
// see notebook/notebook.h.
//
// Problems come back as a PathProblem (an error kind and the name to show) — the app turns them
// into its translated messages, the command line into error codes.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion {
class Vault;
class VaultIndex;
struct FileMove;
} // namespace brulion

namespace brulion::notebook {

/// Writes a note's new text (relocate()'s rewritten notes). Default: Vault::writeText().
using WriteText = std::function<bool(const VaultPath &path, const QString &content)>;

/// Why a page or folder operation was refused or failed.
struct PathProblem {
    enum Kind {
        None,
        Missing,      ///< the source no longer exists (name: its title)
        NoName,       ///< no usable name
        IntoItself,   ///< a folder into itself
        HiddenPath,   ///< the target is a path the notebook ignores (name: its last segment)
        HiddenFolder, ///< a new folder name would be hidden (name: that segment)
        NotAFolder,   ///< a file where a folder is needed (name: its file name)
        FolderExists, ///< (name: the folder's name)
        PageExists,   ///< (name: the page's title)
        FileExists,   ///< an attachment of that name (name: the file name)
        Failed,       ///< I/O error (detail: Vault::errorString())
    };
    Kind kind = None;
    QString name;
    QString detail;

    bool isOk() const { return kind == None; }
};

/// A page title typed with its extension ("Notatka.md") names the page "Notatka"; otherwise the
/// title as given.
QString withoutMdExtension(const QString &title);

/// Path a new page named `target` gets: "Projekty/Dom" -> "<newPagesFolder>/Projekty/Dom.md"
/// (segments sanitised, folders spelled as the vault spells them), a date ("2026-10-20" or the
/// journal file format) that day's journal page; "" for an empty name. Never writes.
VaultPath newPagePath(const Vault &vault, const QString &target);

struct CreatedPage {
    VaultPath path;        ///< the new page ("Dom 2.md" when taken), the case twin, or "" on failure
    VaultPath wanted;      ///< the path asked for (before "Dom 2.md" or the twin)
    bool existing = false; ///< `path` is a page whose name differs only in letter case: nothing written
    PathProblem problem;   ///< NoName or Failed when `path` is ""
};

/// Where createPage() would put the page, without writing: `path` is the case twin (`existing`)
/// or the path asked for (createPage() picks "Dom 2.md" when that is taken).
CreatedPage plannedPage(const Vault &vault, const QString &title, const QString &folder = QString());

/// The text a new page at `path` gets from createPage(): `content`, with `mode` as the
/// frontmatter key `brulion:` when the page would not get that mode anyway. Never writes.
QString newPageText(const Vault &vault, const VaultPath &path, std::optional<NoteMode> mode, const QString &content);

/// Create a page `title` ("Projekty/Dom" creates folders) in `folder` (a null string: the vault's
/// newPagesFolder; "" the root) with `content`. `mode`, when given, is written as the frontmatter
/// key `brulion:` only when the page would not get that mode anyway (markdown::
/// withNoteModeChoice). A page differing only in letter case is that page (`existing`).
CreatedPage createPage(Vault &vault, const QString &title, const QString &folder = QString(),
                       std::optional<NoteMode> mode = std::nullopt, const QString &content = QString(),
                       ChangeSource source = ChangeSource::User);

/// Create a folder ("Projekty/Dom" creates both levels; folders used as spelled in the vault). Every
/// new name is sanitised like a page title; a name that would be hidden is refused (HiddenFolder),
/// so is a file of that name (NotAFolder). An existing folder is fine.
struct CreatedFolder {
    VaultPath path; ///< "" on failure
    PathProblem problem;
};
CreatedFolder createFolder(Vault &vault, const QString &path);

/// Tracked folders (not hidden or ignored ones) in Polish alphabetical order, segment by segment.
QStringList folderPaths(const Vault &vault);

/// A setting of .brulion/config.json that names a folder (or the journal template file).
struct SpecialPath {
    QString special; ///< "journals" | "templates" | "attachments" | "newPages" | "journalTemplate"
    QString key;     ///< the config key ("journalsFolder", …)
    VaultPath path;
};
/// The settings naming `path` or something in it.
QList<SpecialPath> specialsWithin(const VaultConfig &config, const VaultPath &path);

struct FolderInfo {
    bool exists = false;
    int pages = 0;       ///< Markdown files inside at any depth, without conflict copies
    int attachments = 0; ///< other files inside
    int folders = 0;     ///< subfolders at any depth
    QList<SpecialPath> specials;
};
FolderInfo folderInfo(const Vault &vault, const VaultPath &path);

/// `path` with its folders spelled as the vault spells them (case-insensitive storage).
VaultPath spelledFolders(const Vault &vault, const VaultPath &path);

/// The path a rename gives `path`: `newName` in its folder (an attachment keeps its type, a page
/// gets ".md"); with `slashMoves` a "/" makes it a path from the notebook root, else it is
/// sanitised like any character a file name cannot have. "" when unusable.
VaultPath renamedPath(const VaultPath &path, const QString &newName, bool isDir, bool slashMoves);

/// A folder name typed in a rename that the vault would hide, else "" (see renamedPath()).
QString hiddenTypedSegment(const QString &newName, bool isDir, bool slashMoves);

/// Why `from` cannot be moved or renamed to `to` (None when it can). `index` (complete, may be
/// null) finds pages whose path differs only in letter case.
PathProblem relocationProblem(const Vault &vault, const VaultIndex *index, const VaultPath &from, const VaultPath &to);

/// The file moves of moving `from` (a file, or a folder with everything in it) to `to`.
QList<FileMove> fileMoves(const Vault &vault, const VaultPath &from, const VaultPath &to);

/// A note relocate() would rewrite (its links or tags name a moved file).
struct PlannedRewrite {
    VaultPath path;    ///< where it is now
    VaultPath newPath; ///< where it is after the move (path, unless it moves itself)
    QString before;    ///< its text now
    QString after;     ///< its text with the links and tags rewritten
};

/// What relocate(from, to) would do.
struct RelocationImpact {
    bool ok = false;
    bool same = false; ///< `to` is `from`: nothing to do
    VaultPath path;    ///< the new path (spelled as the vault spells its folders)
    PathProblem problem;
    int links = 0;                  ///< links rewritten
    int tags = 0;                   ///< tag occurrences rewritten
    int notes = 0;                  ///< notes rewritten
    QStringList specials;           ///< the settings that follow ("journals", …)
    QList<PlannedRewrite> rewrites; ///< the notes rewritten, with their texts (with an index)
};
/// `index` must hold every note (the CLI builds it whole, the app finishes indexing first).
RelocationImpact relocationImpact(const Vault &vault, const VaultIndex *index, const VaultPath &from,
                                  const VaultPath &to);

/// A setting of .brulion/config.json rebased by relocate().
struct RebasedSetting {
    QString special; ///< as SpecialPath::special
    QString key;
    VaultPath value; ///< the new value
};

struct Relocation {
    VaultPath path;      ///< the new path ("" on failure; `from` itself when it is there already)
    PathProblem problem; ///< when `path` is ""
    bool folder = false;
    bool moving = false;           ///< into another folder (else a rename in place)
    QList<VaultPath> rewritten;    ///< notes whose links or tags were rewritten (their new paths)
    QList<RebasedSetting> rebased; ///< settings that followed the move
    QString configError;           ///< the settings could not be saved (Vault::errorString())
};

/// Move or rename `from` to `to` keeping every link and tag (see the file comment). `index` must
/// hold every note and follow the vault's changes (the planned notes are found where they are
/// after the move). Refused moves change nothing.
Relocation relocate(Vault &vault, const VaultIndex *index, const VaultPath &from, const VaultPath &to,
                    ChangeSource source = ChangeSource::User, const WriteText &write = {});

/// Rename a page (or attachment) in its folder; `slashMoves`: a "/" in `newTitle` moves it (a path
/// from the notebook root, the Rename dialog), else it is sanitised (the page header's title).
Relocation renamePage(Vault &vault, const VaultIndex *index, const VaultPath &path, const QString &newTitle,
                      bool slashMoves, ChangeSource source = ChangeSource::User, const WriteText &write = {});
/// Rename a folder the same way.
Relocation renameFolder(Vault &vault, const VaultIndex *index, const VaultPath &path, const QString &newName,
                        bool slashMoves, ChangeSource source = ChangeSource::User, const WriteText &write = {});
/// Move a page, an attachment or a folder into `targetFolder` ("" = the notebook root).
Relocation movePath(Vault &vault, const VaultIndex *index, const VaultPath &path, const QString &targetFolder,
                    ChangeSource source = ChangeSource::User, const WriteText &write = {});

struct DeletedPath {
    VaultPath trashPath; ///< its path inside .trash/ (a folder: its top entry); "" on failure
    bool folder = false;
    PathProblem problem; ///< Missing or Failed when trashPath is ""
};
/// Move a file or folder to the trash.
DeletedPath deletePath(Vault &vault, const VaultPath &path, ChangeSource source = ChangeSource::User);

} // namespace brulion::notebook
