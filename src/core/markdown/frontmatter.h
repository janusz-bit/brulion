// YAML frontmatter (docs/SYNTAX.md §4 "Page properties") — a small, dependency-free YAML subset
// parser, a matching serializer and pure edits of single frontmatter keys. Part of the markdown
// module — see markdown/markdown.h for the module overview.
//
// Supported: block mappings (any nesting), block sequences (`- a`, `- key: v`), flow sequences
// and mappings (`[a, "b c"]`, `{a: 1}`, also spanning lines), plain / single- / double-quoted
// scalars, `|` and `>` block scalars (with `-` / `+` chomping), comments, `null`/`~`, booleans
// and numbers. Dates stay strings. Anchors, aliases, tags and multi-document streams are not
// supported (read as plain text). Malformed input never fails: unparseable lines are skipped.
//
// Value mapping: string -> QString, integer (|n| < 2^53) -> qlonglong, other number -> double,
// true/false -> bool, null/~/empty -> invalid QVariant(), sequence -> QVariantList,
// mapping -> QVariantMap.
#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>
#include <QVariantMap>

#include <optional>

namespace brulion::markdown {

/// Location of a frontmatter block in a document.
struct FrontmatterBlock {
    int endLine = 0;         ///< line of the closing `---` / `...`
    QString yaml;            ///< YAML source between the delimiters (with its line breaks)
    qsizetype yamlStart = 0; ///< offset where the YAML source starts
    qsizetype bodyStart = 0; ///< offset of the first character after the block
    int bodyStartLine = 0;   ///< line where the body starts
};

/// Locate frontmatter: the document (after an optional BOM) must start with a `---` line and
/// contain a closing `---` or `...` line. Trailing spaces on the delimiters are allowed.
std::optional<FrontmatterBlock> findFrontmatter(QStringView content);

/// Parsed frontmatter of a document.
struct FrontmatterResult {
    QVariantMap data;
    QStringList keys;        ///< top-level keys in document order
    qsizetype bodyStart = 0; ///< 0 when there is no frontmatter
    int bodyStartLine = 0;
};

/// Parse the YAML frontmatter at the start of `content` (empty data without frontmatter).
FrontmatterResult parseFrontmatter(QStringView content);

/// Parse a YAML subset document into a mapping; non-mapping documents give an empty map.
/// `keys`, when given, receives the top-level keys in document order.
QVariantMap parseYaml(QStringView yaml, QStringList *keys = nullptr);

/// Serialize a mapping as YAML (block style, 2-space indent, Obsidian-like block lists).
/// Top-level keys listed in `keyOrder` come first in that order, the rest alphabetically.
/// Strings are quoted whenever another YAML reader (also YAML 1.1) could read them differently.
QString stringifyYaml(const QVariantMap &data, const QStringList &keyOrder = {});

/// A complete frontmatter block `---\n<yaml>---\n`, or an empty string when there is nothing
/// to write. Readable back with parseFrontmatter().
QString stringifyFrontmatter(const QVariantMap &data, const QStringList &keyOrder = {});

/// Set one top-level frontmatter key (content in → content out): the key's lines are replaced
/// in place (other keys, comments and formatting untouched), a new key is appended before the
/// closing delimiter, and a document without frontmatter gets one. An invalid QVariant() writes
/// `key:` (null). Line endings follow the document.
QString setFrontmatterValue(const QString &content, const QString &key, const QVariant &value);

/// Remove one top-level frontmatter key (and its nested lines); the frontmatter block itself is
/// kept even when it becomes empty. Unchanged content when the key is absent.
QString removeFrontmatterValue(const QString &content, const QString &key);

} // namespace brulion::markdown
