// Block and page properties (docs/SYNTAX.md §4): `key:: value` lines (Logseq / Dataview), YAML
// frontmatter conversion, page tags and aliases. Part of the markdown module — see
// markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>
#include <QVariantMap>

#include <optional>

namespace brulion::markdown {

/// A `key:: value` line (`^([A-Za-z0-9_-]+)::(?:[ \t]+(.*?))?[ \t]*$`): positions in the line.
struct PropertyLine {
    QString key; ///< as written (callers lower-case it)
    qsizetype keyFrom = 0;
    qsizetype valueFrom = 0; ///< start of the (trimmed) value; == valueTo when empty
    qsizetype valueTo = 0;
};

/// Match a property line; `line` must start with the key (no indentation).
std::optional<PropertyLine> matchPropertyLine(QStringView line);

/// Interpret a `key:: value` value. `tags`/`tag`/`alias`/`aliases` always give a QStringList
/// (comma separated, `#`/`[[ ]]`/quotes stripped). For other keys a value whose comma-separated
/// parts are all `[[links]]` or `#tags` becomes a QStringList of names; anything else stays a
/// QString (trimmed).
QVariant parsePropertyValue(const QString &key, QStringView raw);

/// Page properties from frontmatter data: keys lower-cased; scalars as strings, lists as
/// QStringList (an unquoted `[[Page]]` — a nested YAML list — becomes "[[Page]]"); maps and
/// nulls skipped.
Properties frontmatterProperties(const QVariantMap &data);

/// Names from a frontmatter / property value: lists item by item, strings split on commas
/// (and, for tags, on whitespace); decoration (`#`, `[[ ]]`, quotes) stripped.
QStringList namesFrom(const QVariant &value, bool splitWhitespace);

/// Value of a frontmatter key looked up case-insensitively (first of `keys` found).
QVariant frontmatterGet(const QVariantMap &data, const QStringList &keys);

/// Deduplicate case-insensitively (NFC), keeping the first spelling.
QStringList uniqueCaseInsensitive(const QStringList &items);

namespace detail {
/// Split on commas outside `[[…]]`, `((…))` and double quotes.
QStringList splitTopLevelCommas(QStringView s);
/// `[[Page]]` -> "Page", `#tag` -> "tag", `#[[multi word]]` -> "multi word"; empty otherwise.
QString refName(QStringView part);
/// Strip `#`, `[[`/`]]` and surrounding quotes / whitespace from a loose list item.
QString cleanName(QStringView part);
} // namespace detail

} // namespace brulion::markdown
