// Small HTML helpers shared by the HTML conversion and the importers (Evernote, Trilium). Internal
// to the io module.
#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::io::detail {

/// Remove the elements named `tags` (lower case) with their content: from `<tag …>` to the
/// matching `</tag>`. A void `<embed>` and a self-closing `<tag …/>` are removed alone; an element
/// that is never closed loses only its start tag, except the raw-text elements (`script`,
/// `style`, `iframe`, `noscript`, `noembed`, `xmp`), which run to the end of the text as in
/// browsers. Case-insensitive, linear time. `removed` is set when anything was removed.
QString removeElements(const QString &html, const QStringList &tags, bool *removed = nullptr);

/// `html` without the start and end tags nested deeper than `maxDepth` elements (their content
/// stays): QTextDocument needs time quadratic in the nesting depth, and deep nesting is never
/// meaningful in notes. Void elements, self-closing tags and the elements HTML closes implicitly
/// (`p`, `li`, `td`, …) do not count. Linear time.
QString limitNesting(const QString &html, int maxDepth = 64);

/// Length of the longest run of `c` in `text` (0 when there is none).
qsizetype longestRun(QStringView text, QChar c);

/// A backtick code fence longer than every backtick run in `code`: at least "```". Linear time.
QString codeFenceFor(QStringView code);

/// Value of the attribute `name` (case-insensitive) in the attribute text of a start tag
/// (`class="a b" href='x'`): entities decoded, "" for an attribute without a value, nullopt when
/// absent. Quoted values may contain other attribute names; the first occurrence wins.
std::optional<QString> htmlAttribute(const QString &attributes, const QString &name);

/// Character references decoded: `&amp;` `&lt;` `&gt;` `&quot;` `&apos;` `&nbsp;` and numeric
/// ones (`&#261;`, `&#x105;`); anything else stays as written.
QString decodeHtmlEntities(const QString &text);

} // namespace brulion::io::detail
