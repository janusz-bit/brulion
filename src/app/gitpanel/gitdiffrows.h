#pragma once
// The rows of the changes panel's own diff view (qml/gitpanel/GitDiffView.qml) and of its file
// list as a tree — pure functions, GUI-free, unit-tested (tests/app/gitpanel/tst_gitdiffrows):
//
//   diffRows(old, new, {split, ignoreWhitespace, context})   hunks of a line diff (ui::diffSequences,
//                       Myers): a "hunk" row before each ("@@ -a,b +c,d @@", the lines it changes on
//                       each side — what CodeGit's stageHunk() … take), "skip" rows for the
//                       unchanged lines between, word changes of paired lines
//   findRows(rows, query)                   the rows whose text holds `query` (case-insensitive)
//   lineHtml(…)                             a line's rich text: changed words and found text marked
//   treeRows(changes, collapsed)            the changed files under their folders
//   reviewComment(path, rows, from, to, comment)   a comment on lines of the diff, for the chat
//
// Every row of diffRows() has every key (BDiffView's lesson: a compiled delegate requires them):
//   kind        "hunk" | "equal" | "delete" | "insert" | "change" (split: a deleted line beside the
//               line that replaced it) | "skip"
//   hunk        the hunk's index (-1 for "skip")
//   oldNo, newNo   1-based line numbers (0: none on that side)
//   line        unified: the line (an "equal" row's new text); "hunk": its header; split: ""
//   leftLine, rightLine   split: the old and the new line
//   segs, leftSegs, rightSegs   word changes [{text, changed}] of a paired line ([] none)
//   hiddenLines "skip": how many unchanged lines it stands for
//   oldFirst, oldLast, newFirst, newLast   "hunk": the lines it changes on each side (a deletion
//               counts on the new side at the line it comes before, an insertion on the old side
//               likewise — gitcli::Hunk's rule)
// Lines are compared without their terminators; with ignoreWhitespace without any whitespace
// (git diff -w).

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::gitpanel {

struct DiffOptions {
    bool split = false;
    bool ignoreWhitespace = false;
    /// Unchanged lines kept around each change; < 0 all of them (no "skip" rows).
    int context = 3;
};

QVariantList diffRows(const QString &oldText, const QString &newText, const DiffOptions &options);
/// The indexes of the rows whose line (or left / right line) holds `query`, case-insensitive
/// ("hunk" and "skip" rows never).
QList<int> findRows(const QVariantList &rows, const QString &query);
/// The longest line of the rows, in characters (a tab counted as 4).
int longestLine(const QVariantList &rows);
/// `text` as rich text: `segs`' changed parts on `changeColor`, every occurrence of `query`
/// (case-insensitive) on `findColor`; white space kept (`wrap`: pre-wrap, else pre). Escaped. ""
/// when nothing is marked (the line is plain text then).
QString lineHtml(const QString &text, const QVariantList &segs, const QString &changeColor, const QString &query,
                 const QString &findColor, bool wrap);

/// The file list as a tree: a "folder" row for each folder (a chain of folders with nothing else
/// in them in one row: "src/app/ui"), its files under it, folded ones' contents left out. Each row
/// is a change row (CodeGit.changes) with `kind` ("folder" | "file"), `depth`, `collapsed`,
/// `fileCount` (a folder's files, nested ones too) and, for a folder, `path` = its folder and
/// `name` = what the row shows. Folders first, then files, each by name.
QVariantList treeRows(const QVariantList &changes, const QStringList &collapsed);

/// The language of a fenced code block for a file's name ("cpp", "qml", "python" …; "" none).
QString languageOf(const QString &path);
/// A review comment for the chat: where (`path`, the lines), the lines of rows [from, to] in a
/// fenced block (the file's language; "diff" with + / − when the lines are not all of one side)
/// and the comment.
QString reviewComment(const QString &path, const QVariantList &rows, int from, int to, const QString &comment);

} // namespace brulion::gitpanel
