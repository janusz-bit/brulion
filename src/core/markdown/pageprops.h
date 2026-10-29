// Page properties as typed fields (docs/SYNTAX.md §4 "Editing page properties"): reading the
// properties of a page — the YAML frontmatter and the leading Logseq `key:: value` lines — with
// a type per value, and pure single-property edits (content in → content out) that change only
// the lines of that one property. Part of the markdown module — see markdown/markdown.h for the
// module overview.
//
// Writing rules in short: the key part of an edited YAML line is kept as written (quotes, `key :`
// spacing), lists keep their style (block items at their indentation, untouched items verbatim,
// flow lists stay flow), new keys go before the closing delimiter, a page without properties gets
// a new frontmatter block; Logseq lines change only in their value (or key) range. Every YAML edit
// is verified by parsing the result (the body byte-identical, every other key unchanged, the
// edited key reads back as intended); frontmatter that cannot be edited safely is refused
// (PropertyEditStatus::Unsafe) and the content returned unchanged.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>

#include <optional>

namespace brulion::markdown {

/// The type of a property value (how the panel edits it). Types are never stored: they are read
/// from the value (inferPropertyType()), for an empty value from the key elsewhere in the notebook.
enum class PropertyType { Text, Number, Checkbox, Date, DateTime, List, Tags, Aliases, Unsupported };
/// Where a property is written: the YAML frontmatter or the leading `key:: value` lines.
enum class PropertySource { Yaml, Logseq };

/// "text", "number", "checkbox", "date", "datetime", "list", "tags", "aliases", "unsupported".
QString propertyTypeName(PropertyType t);
/// Inverse of propertyTypeName() (case-insensitive); nullopt for any other text.
std::optional<PropertyType> parsePropertyType(QStringView name);
/// "yaml" | "logseq".
QString propertySourceName(PropertySource s);
/// Inverse of propertySourceName(); nullopt for any other text.
std::optional<PropertySource> parsePropertySource(QStringView name);

/// Keys with a fixed type: tags/tag → Tags, aliases/alias → Aliases (case-insensitive).
std::optional<PropertyType> fixedPropertyType(QStringView key);

/// The type a value reads as (`empty` for an empty value).
/// YAML (`value` from parseYaml): maps, lists holding maps or deeper lists → Unsupported; fixed
/// keys; bool → Checkbox; qlonglong / double → Number; a string "YYYY-MM-DD" (a valid date) →
/// Date; "YYYY-MM-DD[T ]HH:mm[:ss]" (valid) → DateTime; a list of scalars and one-string nested
/// lists (an unquoted [[Page]]) → List (also []); null → `empty`; any other string (also
/// multi-line, also a quoted "12") → Text.
/// Logseq (`value` = the raw value text): fixed keys; "true" / "false" (any case) → Checkbox;
/// a strict number `[-+]?\d+(\.\d+)?` → Number ("3,5" and "1 200" stay Text); a date / datetime
/// as above, also "[[2026-10-28]]" → Date; all comma parts `[[…]]` / `#…` → List; "" → `empty`;
/// else Text. A QStringList (parsePropertyValue()'s list) reads as List.
PropertyType inferPropertyType(const QString &key, const QVariant &value, PropertySource source,
                               PropertyType empty = PropertyType::Text);

struct PageProperty {
    QString key;     ///< as written (YAML key unquoted)
    PropertySource source = PropertySource::Yaml;
    PropertyType type = PropertyType::Text;
    QVariant value;  ///< typed: QString (text; date "YYYY-MM-DD"; datetime as written), qlonglong /
                     ///< double, bool, QStringList (List / Tags / Aliases); invalid when empty
    QStringList items; ///< List: items as written ("[[Dom]]" kept); Tags / Aliases: names without
                       ///< '#' (namesFrom() rules)
    QString text;    ///< one line for display and previews ("a, b"; "{…}" for a map)
    bool empty = false;
    bool editable = true;   ///< false: Unsupported, or the block cannot be edited safely
    bool multiline = false; ///< a text with line breaks
    bool linkDate = false;  ///< Logseq date written as [[YYYY-MM-DD]]
    int line = -1;          ///< document line of the key
};

struct PagePropertyBlock {
    PropertySource source = PropertySource::Yaml;
    int firstLine = -1;   ///< YAML: the opening `---`; Logseq: the first property line
    int lastLine = -1;    ///< YAML: the closing delimiter; Logseq: the last property line
    bool editable = true; ///< keys can be set and added safely
    QString problem;      ///< "" | "notMapping" | "indented" (a key not at column 0)
};

struct PageProperties {
    /// YAML entries in document order (without the note mode key `brulion`), then the Logseq lines.
    QList<PageProperty> items;
    std::optional<PagePropertyBlock> yaml;
    std::optional<PagePropertyBlock> logseq;
    /// Where a new property goes: YAML when there is a YAML block or no block at all (a new one
    /// is created), the Logseq lines when only they exist.
    PropertySource addTarget() const;
    /// The addTarget() block (or a new YAML block) is editable.
    bool canAdd() const;
    /// The property `key` of `source` (exact key; Logseq keys case-insensitively), or nullptr.
    const PageProperty *find(PropertySource source, const QString &key) const;
};

/// The page properties of `content` — a whole note, or a frontmatter row's body (`---` … `---`,
/// or `key:: value` lines). The Logseq lines are found as parseNote() finds them (after the
/// frontmatter and blank lines, `%%comments%%` masked, a BOM ignored). Linear in `content`.
PageProperties readPageProperties(QStringView content);

enum class PropertyEditStatus {
    Changed,
    Unchanged,
    NotFound,
    Unsafe,       ///< the block cannot be edited safely (or the edit did not verify): nothing changed
    InvalidKey,   ///< empty / line break / (Logseq) not [A-Za-z0-9_-]+
    KeyExists,    ///< another property of the page has this key (case-insensitive, NFC)
    Reserved,     ///< `brulion` (the page header's mode toggle stands for it)
    InvalidValue, ///< a number that is none, a date that is none, a bad tag / alias
};
struct PropertyEditResult {
    QString content; ///< the new content (== the input unless Changed)
    PropertyEditStatus status = PropertyEditStatus::Unchanged;
};
/// "changed", "unchanged", "notFound", "unsafe", "invalidKey", "keyExists", "reserved",
/// "invalidValue".
QString propertyEditStatusName(PropertyEditStatus s);

/// Set `key` of `source` to `value` written as `type`, adding the key when the block has no such
/// key (and no other property of the page has it, else KeyExists); `key` is matched as written
/// (the YAML key unquoted; Logseq keys case-insensitively). Value forms accepted:
///   Text: QString (multi-line → YAML block scalar; Logseq: line breaks become spaces);
///   Number: a number or a text (parseNumberInput()); Checkbox: bool (or "true" / "false");
///   Date: "YYYY-MM-DD"; DateTime: "YYYY-MM-DD HH:mm" or "…THH:mm" (seconds optional);
///   List / Tags / Aliases: QStringList / QVariantList of strings (blank items dropped; tags lose a
///   leading '#'; duplicates dropped case-insensitively). Invalid QVariant / "" / [] = empty.
/// Unchanged when the value already reads the same.
PropertyEditResult setPageProperty(const QString &content, PropertySource source, const QString &key,
                                   const QVariant &value, PropertyType type);
/// Remove `key` of `source` (YAML: every duplicate entry). Removing the last property removes the
/// YAML block (unless the body would then start a frontmatter of its own) or the Logseq lines
/// with the blank lines after them.
PropertyEditResult removePageProperty(const QString &content, PropertySource source, const QString &key);
/// Rename `oldKey` of `source` to `newKey`: only the key text changes (YAML: the key part of the
/// entry's first line; the value lines stay byte-identical). KeyExists when another property of
/// the block has `newKey` (case-insensitive; a change of case alone is allowed).
PropertyEditResult renamePageProperty(const QString &content, PropertySource source, const QString &oldKey,
                                      const QString &newKey);

/// The page's tags / aliases (`kind` Tags | Aliases) for the page header: add `item` to the
/// property holding them (YAML `tags` / `tag` resp. `aliases` / `alias`, else Logseq `tags::` /
/// `tag::` …), creating `tags` / `aliases` where new properties go; Unchanged when the page has it
/// already (case-insensitive, NFC).
PropertyEditResult addPageListItem(const QString &content, PropertyType kind, const QString &item);
/// Take `item` out of every page-level property holding it (case-insensitive, NFC); a property
/// whose list becomes empty is removed. NotFound when no property holds it.
PropertyEditResult removePageListItem(const QString &content, PropertyType kind, const QString &item);
/// The tags / aliases of the page's properties (YAML and Logseq, deduplicated case-insensitively).
QStringList pageListItems(const PageProperties &props, PropertyType kind);

/// The type menu: `value` (PageProperty::value) as type `to`; nullopt when it cannot be kept
/// (text "abc" → Number). Text → List splits on top-level commas; List / Tags / Aliases → Text
/// joins with ", "; List ↔ Tags ↔ Aliases keep the items ('#' stripped for tags); Text → Number
/// parseNumberInput(); Number → Text its text; Text → Checkbox "true / tak / yes / 1" → true,
/// "false / nie / no / 0" → false, else nullopt; Checkbox → Text "true" / "false"; Text → Date a
/// valid "YYYY-MM-DD" (also the date of a datetime); Date → DateTime at 00:00; DateTime → Date
/// drops the time; an empty value stays empty (an invalid QVariant).
std::optional<QVariant> convertPropertyValue(const QVariant &value, PropertyType from, PropertyType to);

/// A number typed by a person: blanks (also NBSP, thin spaces) between digits are dropped; with
/// both '.' and ',' the last one is the decimal mark; a lone ',' is the decimal mark ("3,5"); then
/// [-+]?digits[.digits][e±n]. qlonglong when integral and |n| < 2^53, else double; nullopt for
/// anything else (also NaN, Infinity, "1,2,3", "").
std::optional<QVariant> parseNumberInput(QStringView text);

/// Key check for `source`: "" when fine, else "empty", "lineBreak", "logseqKey" (a Logseq key may
/// only have the characters [A-Za-z0-9_-]), "reserved" (the note mode key `brulion`).
QString checkPropertyKey(QStringView key, PropertySource source);

/// A tag / alias a person typed, checked: the name to write (trimmed; a tag without its '#'), or
/// "" when it cannot be one (empty, a line break; an alias with `[[`, `]]`, `|`, `#` or `^`, which
/// no link could use).
QString cleanListItem(PropertyType kind, const QString &item);

} // namespace brulion::markdown
