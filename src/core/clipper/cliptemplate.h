// The templates of clips (docs/CLIPPER.md "Templates", docs/TEMPLATES.md): the built-in ones (in
// the style of Brulion, Obsidian's Web Clipper, Joplin, and a short link for the journal) and
// filling a template with a clip's values. The variables of every template (`{{date}}`,
// `{{date:FORMAT}}`, `{{time}}`, `{{weekday}}`, `{{month}}`, `{{week}}`, `{{cursor}}`) work as in
// markdown::applyTemplate(); a clip adds `{{title}}`, `{{url}}`, `{{domain}}`, `{{site}}`,
// `{{author}}`, `{{published}}` / `{{published:FORMAT}}`, `{{description}}`, `{{content}}`,
// `{{selection}}` (both also `:quote` and `:text`), `{{tags}}`, `{{image}}` and `{{screenshot}}`.
// Values are put in as data, never read as a template again; where they go decides their form
// (YAML in the frontmatter, `key:: value` properties, Markdown in the text). Part of the clipper
// module — see clipper/clipper.h.
#pragma once

#include "dates/names.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::clipper {

struct BuiltinTemplate {
    QString id;       ///< "brulion", "obsidian", "joplin", "link"
    const char *name; ///< English, for QCoreApplication::translate("brulion::clipper", …)
    QString text;
};

/// The built-in templates, the default one first.
const QList<BuiltinTemplate> &builtinTemplates();
/// The built-in template `id` (case-insensitive), or nullptr.
const BuiltinTemplate *builtinTemplate(QStringView id);
/// The template a place starts with: "brulion" for a new page, "link" for the journal and a
/// heading's section.
QString defaultTemplateFor(bool newPage);

/// A clip's values for a template. Text values are plain text (escaped for Markdown where the
/// template's text needs it); `content` and `selection` are Markdown; `image` and `screenshot`
/// Markdown of a picture (an embed), or "".
struct ClipValues {
    QString title;
    QString url;
    QString domain; ///< the URL's host without "www."
    QString site;   ///< the site's name, else `domain`
    QString author;
    QString published; ///< as the page gives it
    QString description;
    QString content;       ///< Markdown
    QString contentText;   ///< plain text of the content
    QString selection;     ///< Markdown
    QString selectionText; ///< plain text of the selection
    QStringList tags;      ///< without '#'
    QString image;         ///< Markdown of the page's picture
    QString screenshot;    ///< Markdown of the screenshot
};

struct TemplateFill {
    QDateTime now; ///< local date and time of the clip
    dates::UiLocale locale = dates::UiLocale::Polish;
};

/// Fill `tpl` with `values`. Rules (docs/CLIPPER.md "Templates"):
/// * in a YAML frontmatter at the template's start a value inside quotes is escaped for them, a bare
///   value is quoted when YAML would read it differently (a URL, "a: b", "yes", "12"), line breaks
///   become spaces, `{{tags}}` alone is a flow list `[a, "b c"]` and a `- {{tags}}` item one item per
///   tag; a key or item whose variables are all empty (and nothing else written) is left out;
/// * in `key:: value` lines values are one line, `{{tags}}` is `#a, #[[b c]]`, and an empty one is
///   left out;
/// * elsewhere plain-text values are escaped for Markdown, `{{url}}` keeps link syntax intact,
///   `{{tags}}` is `#a #[[b c]]`, a multi-line value continues with its line's indentation, and a
///   line holding only variables that are all empty is left out (with one blank line of two around
///   it);
/// * unknown variables stay as written; nothing a value holds is filled again.
QString fillClipTemplate(QStringView tpl, const ClipValues &values, const TemplateFill &fill);

/// `markdown` (a filled template) as the text of one block for a journal or a heading's section:
/// a YAML frontmatter at its start becomes `key:: value` properties after the first line of the
/// rest (lists joined with ", ", tags as `#tag`). Text without frontmatter stays as it is.
QString frontmatterAsBlockProperties(const QString &markdown);

/// The date a page gives as its publication date ("2026-11-19", "2026-11-19T08:00:00+01:00",
/// RFC 2822 "Thu, 19 Nov 2026 08:00:00 GMT"): the date and time as written on the page (never
/// moved through UTC), or invalid.
QDateTime parsePublished(QStringView text);

/// `markdown` as a quote: "> " before every line ("> " alone for a blank one).
QString asQuote(const QString &markdown);

} // namespace brulion::clipper
