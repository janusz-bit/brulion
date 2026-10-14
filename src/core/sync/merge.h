// Line-based three-way merge (diff3) of text notes, with a "union" rule for pure additions:
// when both sides inserted lines at the same place (typical: two devices appending to the same
// journal page) both insertions are kept instead of reporting a conflict. Also a key-level
// three-way merge for JSON objects (the vault config).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::sync {

/// One region of a merge result: merged lines, or an unresolved conflict.
struct MergeHunk {
    enum class Kind { Ok, Conflict };
    Kind kind = Kind::Ok;
    QStringList lines;               ///< Kind::Ok
    QStringList base, local, remote; ///< Kind::Conflict

    static MergeHunk ok(const QStringList &lines);
    static MergeHunk conflict(const QStringList &base, const QStringList &local, const QStringList &remote);
    friend bool operator==(const MergeHunk &, const MergeHunk &) = default;
};

struct MergeResult {
    QString text;           ///< merged text; unresolved regions contain the local side
    bool clean = true;      ///< every region merged automatically
    QList<MergeHunk> hunks; ///< regions in document order (adjacent Ok regions joined)
};

/// Three-way merge of `local` and `remote`, both derived from `base`, line by line.
///  * Changes of one side only are taken; regions both sides changed identically, once.
///  * Regions where both sides only inserted lines at the same place are unioned: local lines,
///    then the remote lines not already among the local ones (journal appends on two devices).
///  * Anything else is a conflict: `clean` is false and `text` holds the local side there.
/// CRLF/LF differences never conflict; the result uses local's line ending style (else
/// remote's, else base's, else LF). The final newline is merged three-way. An empty base
/// (first sync) makes every difference a union of both versions.
MergeResult merge3(const QString &base, const QString &local, const QString &remote);

/// Union of two insertions at the same place: common lines (LCS) once, local-only lines
/// first, then remote-only lines that are not already among the local-only ones (multiset).
QStringList unionLines(const QStringList &local, const QStringList &remote);

/// A text split into lines (line endings removed) plus its line-ending style.
struct SplitText {
    QStringList lines;
    QString eol;               ///< dominant line ending "\n" or "\r\n"; empty without line breaks
    bool finalNewline = false; ///< the text ends with a line break
};

/// Split on "\n" (a "\r" before it belongs to the line ending; CRLF is dominant when at least
/// half of the line breaks are CRLF). "" has no lines.
SplitText splitText(const QString &text);

/// Join lines back: lines joined by `eol`, plus `eol` when `finalNewline`. No lines -> "".
QString joinLines(const QStringList &lines, const QString &eol, bool finalNewline);

/// Key-level three-way merge of JSON objects: a key changed on one side only takes that change
/// (including deletion); a key changed on both sides keeps the local value. An invalid or empty
/// base counts as {}. std::nullopt when local or remote is not a JSON object (empty text counts
/// as {}). Output: indented JSON (QJsonDocument::Indented, keys sorted) ending with "\n".
std::optional<QString> mergeJson3(const QString &base, const QString &local, const QString &remote);

} // namespace brulion::sync
