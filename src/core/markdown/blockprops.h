// Block properties (docs/SYNTAX.md §4 "Block properties"): the `key:: value` lines of one block,
// read as written and set by a pure edit (content in → content out). Part of the markdown module —
// see markdown/markdown.h for the module overview.
#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::markdown {

/// `content` with the property `key` of the block at `line` (its own lines up to `ownEndLine`) set
/// to `value`: an existing `key:: …` line (any letter case) gets the new value, else a line
/// `key:: value` is added after the block's last property line — or right after its first line —
/// indented as its other property lines, else under its text ("- " → two spaces). An empty
/// `value` removes the property's lines. Every other byte stays (line endings too). nullopt when
/// the line does not exist, the key is no property name (letters, digits, "-", "_") or `value` has
/// a line break. (The parser reads only `[A-Za-z0-9_-]+` keys: isBlockPropertyKey().)
std::optional<QString> setBlockProperty(const QString &content, int line, int ownEndLine, const QString &key,
                                        const QString &value);

/// The value of `key` (case-insensitive) as written on the block's own `key:: value` lines (lines
/// `line` + 1 … `ownEndLine`; the first line too for a paragraph block), trimmed; nullopt when the
/// block has no such line.
std::optional<QString> blockPropertyText(QStringView content, int line, int ownEndLine, QStringView key);

/// A key parseNote() reads as a block property: `[A-Za-z0-9_-]+`.
bool isBlockPropertyKey(QStringView key);

} // namespace brulion::markdown
