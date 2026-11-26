// Links in a terminal's output that the program did not mark with OSC 8 (term.h): web and file
// addresses, and file paths as compilers, test runners, grep and git print them — `src/main.cpp`,
// `src/main.cpp:12`, `src/main.cpp:12:5`, `main.cpp(12,5)`, `./a.out`, `~/notes.md`, `a/src/x.h` of
// a diff. The view looks for them only in the line under the pointer while Ctrl is held, and opens
// a path only once resolvePath() found the file.
//
//   const QList<TextLink> links = findLinks(u"error: src/main.cpp:12:5: expected ';'");
//   links[0].kind == TextLink::Path; links[0].target == "src/main.cpp"; links[0].line == 12
//   resolvePath(u"src/main.cpp"_s, u"/home/me/project"_s, home);   // "/home/me/project/src/main.cpp"
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::term {

struct TextLink {
    enum Kind { Url, Path };
    Kind kind = Url;
    /// Where it is in the text (UTF-16 units): the whole match, a path's :line:column included.
    qsizetype start = 0;
    qsizetype length = 0;
    /// The URL (http, https, file), or the path as written (without its line and column).
    QString target;
    /// A path's line and column (1-based; 0: none given).
    int line = 0;
    int column = 0;
};

/// The links of one line of text, in order. URLs: `http://`, `https://`, `file://` up to a space,
/// a quote or an angle bracket, without trailing punctuation or a closing bracket that has no
/// opening one in them. Paths: words with a `/` or a file name extension (`name.ext`), optionally
/// followed by `:line`, `:line:column`, `(line)` or `(line,column)` — candidates only: whether
/// such a file exists is resolvePath()'s question.
QList<TextLink> findLinks(QStringView text);

/// The link of `links` that covers unit `index` of the text (-1: none).
qsizetype linkAt(const QList<TextLink> &links, qsizetype index);

/// The absolute path of the file `path` names, "" when there is no such file (or it is a folder):
/// `~/…` in `home`, an absolute path as it is, anything else relative to `workingDirectory` — and,
/// when nothing is there, without a diff's `a/` or `b/` in front. `file://` URLs are not paths.
QString resolvePath(const QString &path, const QString &workingDirectory, const QString &home);

} // namespace brulion::term
