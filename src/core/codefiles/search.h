// The project search (docs/CODE-FILES.md "Project search"): the argument lists of ripgrep and
// git grep, their output parsed, Brulion's own search of one file's text, the matched ranges of a
// line. Part of the codefiles module; the app runs the programs (QProcess, no shell).
//
//   rg:       ripgrepArguments(options) in the folder; each line of its output →
//             parseRipgrepLine() (a match, or nothing for its other messages)
//   git grep: gitGrepArguments(options) after `git -C <folder>`; its output → parseGitGrep()
//   neither:  searchText(path, text, options, room) over the index's files
//
// The pattern is literal unless `regex` (then a regular expression — ripgrep's syntax, git grep's
// extended one, QRegularExpression's for Brulion's own and for marking git grep's matches: the
// common subset behaves the same); `wholeWord` wants it between non-word characters; the case is
// ignored unless `caseSensitive`. Lines are 1-based; ranges are [from, to) UTF-16 offsets in the
// match's text. A line longer than MaxLineLength is cut around its first match ("…" at a cut end,
// ranges moved with it).
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>
#include <utility>

namespace brulion::codefiles {

struct SearchOptions {
    QString pattern;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool regex = false;
    /// At most this many matches in all (the search stops there: `truncated`).
    int maxMatches = 2000;
    /// At most this many in one file.
    int maxPerFile = 200;
    /// Files bigger than this are not searched (bytes).
    qint64 maxFileSize = 4 * 1024 * 1024;
};

struct SearchMatch {
    /// Relative to the folder, "/"-separated.
    QString path;
    int line = 0;
    QString text;
    QList<std::pair<int, int>> ranges;
    friend bool operator==(const SearchMatch &, const SearchMatch &) = default;
};

inline constexpr int MaxLineLength = 400;

/// Why the options cannot be searched for ("" they can): an empty pattern, a regular expression
/// QRegularExpression refuses (its message).
QString patternError(const SearchOptions &options);
/// The expression that finds the pattern in a line (also for a literal pattern; invalid: its
/// isValid() is false).
QRegularExpression searchExpression(const SearchOptions &options);
/// The pattern's matches in `line`, at most `limit`; empty ones are skipped.
QList<std::pair<int, int>> matchRanges(QStringView line, const SearchOptions &options, int limit = 50);
QList<std::pair<int, int>> matchRanges(QStringView line, const QRegularExpression &expression, int limit = 50);

/// `rg --json …` for the options, to run in the project's folder (hidden files searched, .git
/// not; .gitignore and the like obeyed; the files sorted by path; no configuration file).
QStringList ripgrepArguments(const SearchOptions &options);
/// `grep -n -I --untracked --no-color -z --column …` for `git -C <folder>` (tracked and
/// untracked files, ignored ones not, binary ones not).
QStringList gitGrepArguments(const SearchOptions &options);

/// One line of `rg --json`: its match ("match" messages: path, line, text, the submatches as
/// ranges — byte offsets of the UTF-8 line turned into UTF-16 ones —, a path or line given as
/// base64 "bytes" too); nullopt for begin / end / context / summary messages and for garbage.
std::optional<SearchMatch> parseRipgrepLine(QByteArrayView line);
/// git grep's output with -n -z --column ("path\0line\0column\0text\n" each), the ranges found
/// again with `options`; at most `limit`. A last line without its "\n" counts.
QList<SearchMatch> parseGitGrep(QByteArrayView output, const SearchOptions &options, int limit);

/// The matches of `expression` in a file's `text` (lines split at \n, \r\n, \r), at most `limit`.
QList<SearchMatch> searchText(const QString &path, QStringView text, const QRegularExpression &expression,
                              int limit);
/// A match whose line is longer than MaxLineLength, cut around its first range.
SearchMatch clipped(SearchMatch match);

} // namespace brulion::codefiles
