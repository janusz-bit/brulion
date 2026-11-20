// A clip as the browser extension sends it (docs/CLIPPER.md "Messages"): what was clipped (the
// article, the selection, the whole page, a screenshot of the visible part, a link or a picture),
// the page's metadata, where it goes in the notebook (a new page in a folder, today's journal, the
// end of a heading's section) and the template. Everything here comes from a web page, so it is
// read as hostile: unknown kinds and malformed values are refused, sizes are bounded, folders and
// page paths with "..", hidden or absolute parts are refused, text loses control characters, and
// links keep only http(s) (and file:) addresses. Pure functions. Part of the clipper module — see
// clipper/clipper.h.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion::clipper {

/// The largest HTML of an article or a page (characters); a longer one is refused.
inline constexpr qsizetype MaxHtmlChars = 24 * 1024 * 1024;
/// The largest HTML of a selection (characters).
inline constexpr qsizetype MaxSelectionChars = 8 * 1024 * 1024;
/// The largest screenshot (characters of its data: URL; about 30 MB of picture).
inline constexpr qsizetype MaxScreenshotChars = 40 * 1024 * 1024;
/// The largest template text (characters).
inline constexpr qsizetype MaxTemplateChars = 64 * 1024;
/// The longest title kept (characters; a longer one is cut, never inside a surrogate pair).
inline constexpr qsizetype MaxTitleChars = 300;
/// The longest metadata value kept (author, site, description; characters).
inline constexpr qsizetype MaxMetaChars = 2000;
/// The longest URL accepted (characters).
inline constexpr qsizetype MaxUrlChars = 8192;
/// Most tags kept (more are dropped), and the longest tag (characters).
inline constexpr int MaxTags = 32;
inline constexpr qsizetype MaxTagChars = 64;
/// Most parts of a folder path, and the longest part (characters).
inline constexpr int MaxFolderDepth = 12;
inline constexpr qsizetype MaxSegmentChars = 120;

enum class ClipKind {
    Article,    ///< the readable content (Readability's article)
    Selection,  ///< the selected part of the page
    Page,       ///< the whole page's body
    Screenshot, ///< a picture of the visible part of the page
    Bookmark,   ///< only the page's title and address (with the selection when there is one)
    Picture,    ///< one picture of the page (its context menu)
    Link,       ///< one link of the page (its context menu)
};
QString clipKindName(ClipKind kind);
std::optional<ClipKind> parseClipKind(QStringView name);

enum class PlaceKind {
    Page,    ///< a new page in a folder
    Journal, ///< appended to today's daily note
    Heading, ///< at the end of a heading's section of an existing page
};
QString placeKindName(PlaceKind kind);
std::optional<PlaceKind> parsePlaceKind(QStringView name);

struct ClipPlace {
    PlaceKind kind = PlaceKind::Page;
    /// Page: the folder of the new page ("a/b", checked; "" = the notebook's root).
    QString folder;
    /// Page: no folder was given: the notebook's folder for new pages (`newPagesFolder`).
    bool defaultFolder = true;
    /// Heading: the page, a vault path ("Czytelnia.md", "Projekty/Dom.md"; checked).
    QString page;
    /// Heading: the heading's text ("Do przeczytania", "## Do przeczytania").
    QString heading;
    /// Page: also a block in today's journal linking to the new page.
    bool journalLink = false;
};

/// Which template fills the clip: a built-in one, a page of the notebook, or a text.
struct TemplateChoice {
    QString builtin; ///< "brulion", "obsidian", "joplin", "link" (builtinTemplates())
    QString page;    ///< a vault path in the notebook (checked like ClipPlace::page)
    QString text;    ///< the template itself (the extension's own templates)
};

struct ClipRequest {
    ClipKind kind = ClipKind::Article;
    QString url;           ///< the page's address (http, https or file; "" when none was valid)
    QString title;         ///< one line, control characters removed, at most MaxTitleChars
    QString html;          ///< Article / Page: the HTML
    QString selectionHtml; ///< the selection's HTML (also with Article: {{selection}})
    QString selectionText; ///< the selection as plain text (when there is no HTML of it)
    QString baseUrl;       ///< the document's base URL (relative links and pictures); "" = url
    QString author;
    QString site;
    QString published;   ///< as the page gives it ("2026-11-19T08:00:00+01:00", "19 listopada 2026")
    QString description; ///< meta description or the article's excerpt
    QString language;    ///< the page's language ("pl", "en-US")
    QString image;       ///< the page's picture (og:image; http(s) only)
    QString screenshot;  ///< Screenshot: data:image/png|jpeg|webp;base64,…
    QString pictureUrl;  ///< Picture: the picture's address (http(s) or a data: URL)
    QString linkUrl;     ///< Link: the link's address (http(s), file:, mailto:)
    QString linkText;    ///< Link: its text
    QStringList tags;    ///< without '#', trimmed, deduplicated (case-insensitive), at most MaxTags
    ClipPlace place;
    TemplateChoice templ;
    bool downloadPictures = true; ///< save the pictures into the notebook's attachments
    QString vault;                ///< the notebook (an absolute folder path; "" = the default one)
};

/// Why a request was refused: a stable code ("bad_kind", "too_large", "bad_folder", …), the JSON
/// key it is about and an English message.
struct RequestProblem {
    QString code;
    QString field;
    QString message;
};

/// The clip of `object` (the host's `clip` and `preview` messages; brulion-cli clip's arguments
/// turned into the same keys): `{kind, url, title, html, selectionHtml, selectionText, baseUrl,
/// author, site, published, description, language, image, screenshot, pictureUrl, linkUrl,
/// linkText, tags: [..] | "a, b", place: {kind, folder, page, heading, journalLink}, template:
/// {builtin | page | text}, downloadPictures, vault}`. Missing keys take their defaults, a value of
/// the wrong type is refused (`bad_value`). nullopt with `problem` when refused.
std::optional<ClipRequest> parseClipRequest(const QJsonObject &object, RequestProblem *problem);

/// `text` as one line: line breaks and tabs become spaces, other control characters, bidi
/// overrides and lone surrogates are dropped, blanks collapsed, trimmed, cut at `maxChars`.
QString cleanLine(QStringView text, qsizetype maxChars);

/// A folder typed or chosen for a clip, made a vault path: "/" separators (a backslash is
/// refused), blanks around parts trimmed, an empty part refused; ".", "..", hidden parts
/// (".git", ".brulion"), a leading "/" or a drive ("C:"), control characters and more than
/// MaxFolderDepth parts or MaxSegmentChars characters in a part are refused (nullopt).
/// "" stays "" (the root).
std::optional<QString> checkedFolder(QStringView folder);
/// A page's vault path (the same rules; ".md" added when it has no extension).
std::optional<QString> checkedPagePath(QStringView path);

/// The tag as Brulion writes it: `#name`, or `#[[name with spaces]]`.
QString tagText(const QString &tag);
/// Clean tags: '#' and "[[ ]]" around a tag removed, blanks collapsed, empty and duplicate
/// (case-insensitive) ones dropped, at most MaxTags of at most MaxTagChars characters.
QStringList cleanTags(const QStringList &tags);

/// `url` when it is an address a link may lead to (http, https, file, mailto; at most
/// MaxUrlChars, no blanks or control characters), else "".
QString checkedLinkUrl(QStringView url);
/// `url` when it is http(s) (webpicture::isWebUrl()), else "".
QString checkedWebUrl(QStringView url);

} // namespace brulion::clipper
