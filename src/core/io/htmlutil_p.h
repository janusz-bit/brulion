// Small HTML helpers shared by the HTML conversion and the importers (Evernote, Trilium). Internal
// to the io module.
#pragma once

#include "io/htmlmarkdown.h"

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

/// `html` with every `<ul>`/`<ol>` of more than `maxItems` items split into consecutive lists of
/// at most `maxItems` items (QTextDocument needs time quadratic in the items of one list). The
/// split points are exactly where QTextHtmlParser starts a new item of that list; each part
/// repeats the list's attributes, its margins between parts are 0 and its list format carries
/// a number prefix that htmlToMarkdown() uses to continue the numbering, so the Markdown is
/// that of the whole list. Lists whose start tag Qt would not read to the end, documents with
/// `<!--StartFragment-->`, entities in tag names and end tags of void elements are left as they
/// are. Linear time. `parts` receives the number of lists split.
QString splitLongLists(const QString &html, int maxItems, int *parts = nullptr);

/// htmlToMarkdown() / htmlToPlainText() with lists split after `maxListItems` items (the public
/// functions use a fixed limit; tests compare the results for other limits).
HtmlMarkdown htmlToMarkdownWithListLimit(const QString &html, const HtmlToMarkdownOptions &options, int maxListItems);
QString htmlToPlainTextWithListLimit(const QString &html, int maxListItems);

/// A formula an importer writes on purpose travels between these private-use characters (the
/// Trilium importer removes them from its input first) until the importer has checked it in its
/// paragraph and removed them; escapeTextFormulas() never changes what is between them.
constexpr char16_t FormulaOpen = 0xE0F2;
constexpr char16_t FormulaClose = 0xE0F3;

/// `markdown` an importer wrote from plain text (HTML, notes of other apps, calendar texts), with
/// the dollars that would make a formula of that text in the editor (findMathSpans(), SYNTAX §11:
/// "Cena $10, Netflix 15$", "$x$", a paragraph that is only `$$`) escaped as `\$`. Found block by
/// block as the editor renders them (markdown::BlockDocument; table cells one by one, a callout's
/// title apart; a block's fenced code and quote-only lines end a formula) and escaped as little as
/// possible: the opening dollar of each such formula, over a few rounds, until none is left.
/// Dollars that pair with nothing stay byte for byte ("$5 i $10", "15$, 10$"). Never changed:
/// code spans and fenced code, link destinations, autolinks, wiki links, block references, bare
/// URLs, `key:: value` properties (a formula they open is broken at its closing dollar instead)
/// and the formulas between FormulaOpen and FormulaClose. Linear time per round.
QString escapeTextFormulas(const QString &markdown);

} // namespace brulion::io::detail
