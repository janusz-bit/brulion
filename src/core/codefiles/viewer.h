// The file viewer's and the composer's helpers (docs/CODE-FILES.md "The file viewer",
// "@-mentions", "Review comments", "Links in answers"). Part of the codefiles module; pure
// functions except readForViewer() and the `exists` checks, which read the disk.
//
//   readForViewer(abs)          what the viewer shows: text (at most 2 MiB, cut at a line), a
//                               picture, or why not (binary, missing, a folder, unreadable)
//   parseFileLink(href)         "src/a.cpp:12", "src/a.cpp#L12-L20", "file:///…", "/abs/a.cpp:3:7"
//   relativeInside(folder, p)   p (absolute or relative) as a path inside the folder, else ""
//   mentionAt / withMention     "@src/ap" being typed at the caret → "@src/app/main.cpp "
//   mentionedPaths(message)     every @path of a message
//   reviewComment(…)            the composer's text for a comment on lines of a file
#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::codefiles {

/// The most of a file the viewer reads.
inline constexpr qint64 MaxViewBytes = 2 * 1024 * 1024;

enum class FileKind { Text, Picture, Binary, Missing, Folder, Unreadable };

/// How a text file is written, for an edit to be saved as it was read (docs/CODE-FILES.md
/// "Editing"): its line break (the first one in it; "\n" when none), a UTF-8 byte order mark, a
/// line break after its last line, and whether its bytes were no valid UTF-8 (`lossy`: an edit would
/// replace them — not offered).
struct TextShape {
    QString newline = QStringLiteral("\n");
    bool bom = false;
    bool finalNewline = false;
    bool lossy = false;
};
TextShape shapeOf(QByteArrayView bytes);
/// `text` as the editor gives it (any line breaks) written in `shape`: its breaks the file's, its
/// byte order mark, a last break added when the file had one; UTF-8.
QByteArray encodeEdited(QStringView text, const TextShape &shape);

struct ViewerFile {
    FileKind kind = FileKind::Missing;
    /// The text (Text only): UTF-8 (invalid sequences as U+FFFD), at most maxBytes of the file,
    /// cut after its last whole line then.
    QString text;
    /// Only the start of the file is in `text`.
    bool truncated = false;
    /// The file's size in bytes.
    qint64 size = 0;
    /// How it is written (Text only: shapeOf() of what was read) and a hash of its bytes (SHA-1 of the
    /// whole file when not truncated): a save refuses a file that changed meanwhile.
    TextShape shape;
    QByteArray hash;
};


/// The file at `absolutePath` for the viewer: a picture by its name (isPictureName()), a folder,
/// missing, unreadable, binary (looksBinary() of its start), else its text.
ViewerFile readForViewer(const QString &absolutePath, qint64 maxBytes = MaxViewBytes);
/// The start of a file is not text: a NUL in its first 8 KiB, or more than a tenth of it control
/// characters other than tab, line breaks, form feed and escape.
bool looksBinary(QByteArrayView head);
/// A picture the viewer shows as one (by the extension): png, jpg, jpeg, gif, webp, bmp, svg, ico.
bool isPictureName(QStringView name);

/// The language of a file by its name, as a Markdown fence names it ("cpp", "python", "qml",
/// "json", "dockerfile", …; "" unknown).
QString languageForPath(QStringView path);

/// A file a link of an agent's answer names.
struct FileLink {
    QString path; ///< as written ("" none), percent-decoded, without the line part
    int line = 0; ///< 0: none
    int column = 0;
    int endLine = 0; ///< "#L12-L20", "12-20": 20; 0 none
};
/// A link's target as a file: "path", "path:12", "path:12:5", "path:12-20", "path#L12",
/// "path#L12-L20", "path#L12C5", "file:///abs/path…" (and "file://localhost/…"); nullopt for a URL
/// of another scheme (http:, mailto: …), an empty path, or one with a line break.
std::optional<FileLink> parseFileLink(QStringView link);
/// `path` — absolute, or relative to `folder` — as a "/"-separated path relative to `folder`
/// when it is inside it ("." parts resolved, no ".." leading out; "" otherwise, and for the
/// folder itself). Text only: symbolic links are not followed.
QString relativeInside(const QString &folder, const QString &path);

/// Where an @-mention being written at `caret` starts and what it is so far: "@" at the start or
/// after a space or "(" and the characters of a path (letters, digits, marks, "-", "_", ".",
/// "/", "+", "~"); start -1: none.
struct MentionAt {
    int start = -1;
    QString query;
};
MentionAt mentionAt(QStringView text, int caret);
/// `text` with the mention being written at `caret` replaced by "@<path> " — a path with a space
/// in it quoted, `@"a b/c.txt" ` —; "" when none is being written.
QString withMention(QStringView text, int caret, QStringView path);
/// Where the caret goes after withMention().
int mentionEnd(QStringView text, int caret, QStringView path);
/// The paths a message names with "@" (and `@"…"`), in order, each once; a trailing ".", ",",
/// ":", ";", ")", "!" or "?" is no part of one.
QStringList mentionedPaths(QStringView message);

/// Files dropped on a thread (absolute paths): the pictures (png, jpg, jpeg, gif, webp, bmp, svg —
/// any letter case) go with the message, the others are named in its text, each once, separated
/// by spaces: "@path" for one inside `folder` (quoted with a space: `@"a b.md"`; the folder itself
/// "@./"), else the path in a code span.
struct Dropped {
    QStringList pictures;
    QString text;
};
Dropped droppedFiles(const QString &folder, const QStringList &paths);

/// The composer's text for a review comment on lines `from` … `to` (1-based) of `path`:
///
///     src/a.cpp:12-14
///     ```cpp
///     <the lines>
///     ```
///     <the comment>
///
/// (one line: "src/a.cpp:12"; the fence longer than any run of backticks in the lines; no
/// comment: no last line).
QString reviewComment(QStringView path, int from, int to, QStringView lines, QStringView comment);

} // namespace brulion::codefiles
