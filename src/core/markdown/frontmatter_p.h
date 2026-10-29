// Internal helpers of the frontmatter edits (not part of the API): locating the lines of
// top-level entries, rebuilding a document around them, and the serializer's pieces. Shared by
// the careful single-key editors — the note mode key (notemode.cpp), the page properties
// (pageprops.cpp) — and the tag scanner (notetags.cpp). Implemented in frontmatter.cpp.
#pragma once

#include "markdown/frontmatter.h"
#include "text/text.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>

#include <optional>

namespace brulion::markdown::detail {

inline constexpr char16_t ByteOrderMark = 0xfeff;

inline bool startsWithBom(QStringView content)
{
    return !content.isEmpty() && content[0].unicode() == ByteOrderMark;
}

/// Document lines [first, last) of one top-level entry of the frontmatter.
struct FrontmatterEntry {
    QString key; ///< as parseYaml() reads it (quotes removed)
    int first = 0;
    int last = 0;
};

/// The top-level entries between the opening delimiter (line 0) and the closing one (`endLine`),
/// the way parseYaml() reads them: a key line starts at column 0 and is read with the YAML parser
/// itself (quoted keys, `key :`); the entry continues with indented lines, `- ` items at column 0
/// (a block sequence value) and a flow collection's closing bracket at column 0, blank lines only
/// when more of the entry follows.
QList<FrontmatterEntry> frontmatterEntries(const QList<text::Line> &lines, int endLine);
/// frontmatterEntries() of one key (exact spelling).
QList<FrontmatterEntry> frontmatterEntries(const QList<text::Line> &lines, int endLine, const QString &key);

/// `lines` joined, with the lines of `entries` (in document order) dropped and `replacement`
/// (with its terminators) put at line `at` (an entry start, or the closing delimiter for an
/// insertion; -1: nowhere).
QString rebuildLines(const QList<text::Line> &lines, const QList<FrontmatterEntry> &entries, int at,
                     const QString &replacement);

/// The frontmatter is a YAML mapping a key can be added to: it has top-level keys, or nothing but
/// blank lines and comments. A root sequence, scalar or flow collection is not one — nor plain
/// text between a note's first `---` and a later one (horizontal rules).
bool isYamlMapping(QStringView yaml);

/// End of `s` before a ` #comment` (a '#' at `from` or after a blank, outside quotes).
qsizetype yamlCommentStart(QStringView s, qsizetype from = 0);

/// A top-level key line split at its key: `line[0, colon]` is the key part as written (quotes,
/// `key :` spacing), the value starts at `rest` (after the blanks following the colon; the line's
/// length when nothing follows).
struct YamlKeyLine {
    QString key;
    qsizetype colon = 0;
    qsizetype rest = 0;
};
std::optional<YamlKeyLine> splitYamlKeyLine(QStringView line);

// ---- serializer pieces (the rules of stringifyYaml()) -------------------------------------------

/// A string as a YAML scalar: plain, or double-quoted when another YAML reader (also YAML 1.1)
/// could read it differently.
QString yamlString(const QString &s);
/// A string as an item of a flow collection (`[a, "b, c"]`): also quoted when it holds `,[]{}`.
QString yamlFlowString(const QString &s);
/// A scalar (string, number, bool, null, QDate, QDateTime) in inline position; nullopt for a
/// collection.
std::optional<QString> yamlScalar(const QVariant &v);
/// The lines of a `|` / `|-` / `|+` block scalar (header first, then the lines with `indent`)
/// for a multi-line string, or nullopt when quoting is safer.
std::optional<QStringList> yamlBlockScalar(const QString &s, const QString &indent);
/// A double-quoted (JSON) string.
QString yamlQuoted(QStringView s);

} // namespace brulion::markdown::detail
