#include "clipper/request.h"

#include "util/paths.h"
#include "webpicture/webpicture.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

struct NamedKind {
    ClipKind kind;
    QStringView name;
};
constexpr NamedKind ClipKinds[] = {
        {ClipKind::Article, u"article"},   {ClipKind::Selection, u"selection"}, {ClipKind::Page, u"page"},
        {ClipKind::Screenshot, u"screenshot"}, {ClipKind::Bookmark, u"bookmark"}, {ClipKind::Picture, u"picture"},
        {ClipKind::Link, u"link"},
};

bool isDroppedChar(char32_t c)
{
    return (c < 0x20 && c != u'\t' && c != u'\n' && c != u'\r') || (c >= 0x7f && c < 0xa0) // controls
           || (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069)                 // bidi overrides
           || c == 0xfeff || c == 0xfffe || c == 0xffff;
}

/// `text` without the characters isDroppedChar() and lone surrogates; tabs and line breaks stay.
QString withoutControls(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate()) {
            if (i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
                const char32_t code = QChar::surrogateToUcs4(c, text.at(i + 1));
                if (!isDroppedChar(code)) {
                    out += c;
                    out += text.at(i + 1);
                }
                ++i;
            }
            continue;
        }
        if (c.isLowSurrogate() || isDroppedChar(c.unicode()))
            continue;
        out += c;
    }
    return out;
}

/// `text` cut to at most `maxChars` units, never inside a surrogate pair.
QString cut(QString text, qsizetype maxChars)
{
    if (text.size() <= maxChars)
        return text;
    text.truncate(maxChars > 0 && text.at(maxChars - 1).isHighSurrogate() ? maxChars - 1 : maxChars);
    return text;
}

/// A multi-line text (HTML, a selection's text): controls dropped, line breaks kept.
QString cleanText(QStringView text)
{
    return withoutControls(text);
}

/// A path's parts checked (checkedFolder()).
std::optional<QStringList> checkedParts(QStringView raw)
{
    if (raw.contains(u'\\'))
        return std::nullopt;
    const QString text = raw.toString();
    if (text.startsWith(u'/') || text.contains(QRegularExpression(u"^[A-Za-z]:"_s)))
        return std::nullopt;
    QStringList parts;
    const QStringList pieces = text.split(u'/');
    for (const QString &piece : pieces) {
        if (withoutControls(piece) != piece || piece.contains(u'\t') || piece.contains(u'\n') || piece.contains(u'\r'))
            return std::nullopt;
        const QString part = piece.trimmed();
        if (part.isEmpty() || part == u"." || part == u".." || part.startsWith(u'.') || part.size() > MaxSegmentChars)
            return std::nullopt;
        parts.append(part);
    }
    if (parts.size() > MaxFolderDepth)
        return std::nullopt;
    // Every name storage has for a hidden folder (".GIT", "GIT~1", ".git.") is refused too.
    if (paths::isIgnored(parts.join(u'/')))
        return std::nullopt;
    return parts;
}

/// The string of `o[key]`: absent or null = `fallback`; another type sets `bad`.
QString stringOf(const QJsonObject &o, QStringView key, bool *bad, const QString &fallback = QString())
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return fallback;
    if (!v.isString()) {
        *bad = true;
        return fallback;
    }
    return v.toString();
}

bool boolOf(const QJsonObject &o, QStringView key, bool fallback, bool *bad)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return fallback;
    if (!v.isBool()) {
        *bad = true;
        return fallback;
    }
    return v.toBool();
}

bool refuse(RequestProblem *problem, const QString &code, const QString &field, const QString &message)
{
    if (problem)
        *problem = RequestProblem{code, field, message};
    return false;
}

} // namespace

QString clipKindName(ClipKind kind)
{
    for (const NamedKind &k : ClipKinds) {
        if (k.kind == kind)
            return k.name.toString();
    }
    return u"article"_s;
}

std::optional<ClipKind> parseClipKind(QStringView name)
{
    for (const NamedKind &k : ClipKinds) {
        if (name.compare(k.name, Qt::CaseInsensitive) == 0)
            return k.kind;
    }
    return std::nullopt;
}

QString placeKindName(PlaceKind kind)
{
    switch (kind) {
    case PlaceKind::Page: return u"page"_s;
    case PlaceKind::Journal: return u"journal"_s;
    case PlaceKind::Heading: return u"heading"_s;
    }
    return u"page"_s;
}

std::optional<PlaceKind> parsePlaceKind(QStringView name)
{
    if (name.compare(u"page", Qt::CaseInsensitive) == 0)
        return PlaceKind::Page;
    if (name.compare(u"journal", Qt::CaseInsensitive) == 0)
        return PlaceKind::Journal;
    if (name.compare(u"heading", Qt::CaseInsensitive) == 0)
        return PlaceKind::Heading;
    return std::nullopt;
}

QString cleanLine(QStringView text, qsizetype maxChars)
{
    static const QRegularExpression blanks(u"\\s+"_s);
    QString out = withoutControls(text);
    out.replace(blanks, u" "_s);
    return cut(std::move(out).trimmed(), maxChars).trimmed();
}

std::optional<QString> checkedFolder(QStringView folder)
{
    if (folder.trimmed().isEmpty())
        return folder.isNull() ? QString() : u""_s;
    const auto parts = checkedParts(folder);
    if (!parts)
        return std::nullopt;
    return parts->join(u'/');
}

std::optional<QString> checkedPagePath(QStringView path)
{
    if (path.trimmed().isEmpty())
        return std::nullopt;
    const auto parts = checkedParts(path);
    if (!parts)
        return std::nullopt;
    QString joined = parts->join(u'/');
    if (paths::extension(joined).isEmpty())
        joined += u".md"_s;
    return joined;
}

QString tagText(const QString &tag)
{
    static const QRegularExpression plain(u"^[\\p{L}\\p{N}_/-]+$"_s);
    return plain.match(tag).hasMatch() ? QString(u'#' + tag) : QString(u"#[["_s + tag + u"]]"_s);
}

QStringList cleanTags(const QStringList &tags)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &raw : tags) {
        QString tag = cleanLine(raw, 4 * MaxTagChars);
        while (tag.startsWith(u'#'))
            tag = tag.sliced(1).trimmed();
        if (tag.startsWith(u"[[") && tag.endsWith(u"]]"))
            tag = tag.sliced(2, tag.size() - 4).trimmed();
        // What would end or break the tag's own syntax (`#[[a]]b`, `#a,b`) is no part of it.
        tag.remove(u'[').remove(u']').remove(u',');
        tag = cut(tag.trimmed(), MaxTagChars).trimmed();
        if (tag.isEmpty())
            continue;
        const QString key = tag.toCaseFolded();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(tag);
        if (out.size() >= MaxTags)
            break;
    }
    return out;
}

QString checkedLinkUrl(QStringView url)
{
    const QString text = url.trimmed().toString();
    if (text.isEmpty() || text.size() > MaxUrlChars || withoutControls(text) != text)
        return {};
    for (const QChar c : text) {
        if (c.isSpace())
            return {};
    }
    const QUrl parsed(text, QUrl::StrictMode);
    if (!parsed.isValid())
        return {};
    const QString scheme = parsed.scheme().toLower();
    if (scheme == u"http" || scheme == u"https")
        return parsed.host().isEmpty() ? QString() : text;
    if (scheme == u"file" || scheme == u"mailto")
        return text;
    return {};
}

QString checkedWebUrl(QStringView url)
{
    const QString text = checkedLinkUrl(url);
    return webpicture::isWebUrl(text) ? text : QString();
}

std::optional<ClipRequest> parseClipRequest(const QJsonObject &o, RequestProblem *problem)
{
    ClipRequest r;
    bool bad = false;
    const QString kind = stringOf(o, u"kind", &bad, u"article"_s);
    if (bad || !parseClipKind(kind)) {
        refuse(problem, u"bad_kind"_s, u"kind"_s, u"Unknown kind of clip: %1"_s.arg(kind.left(40)));
        return std::nullopt;
    }
    r.kind = *parseClipKind(kind);

    const auto text = [&](QStringView key, qsizetype max) -> std::optional<QString> {
        bool wrong = false;
        const QString value = stringOf(o, key, &wrong);
        if (wrong) {
            refuse(problem, u"bad_value"_s, key.toString(), u"%1 is not a string"_s.arg(key));
            return std::nullopt;
        }
        if (value.size() > max) {
            refuse(problem, u"too_large"_s, key.toString(),
                   u"%1 is too large (%2 characters, at most %3)"_s.arg(key).arg(value.size()).arg(max));
            return std::nullopt;
        }
        return value;
    };
    const auto line = [&](QStringView key, qsizetype keep) -> std::optional<QString> {
        // Metadata longer than what is kept is cut, not refused: a page decides its own lengths.
        const auto value = text(key, 64 * keep);
        if (!value)
            return std::nullopt;
        return cleanLine(*value, keep);
    };

    const auto url = text(u"url", 4 * MaxUrlChars);
    const auto title = line(u"title", MaxTitleChars);
    const auto html = text(u"html", MaxHtmlChars);
    const auto selectionHtml = text(u"selectionHtml", MaxSelectionChars);
    const auto selectionText = text(u"selectionText", MaxSelectionChars);
    const auto baseUrl = text(u"baseUrl", 4 * MaxUrlChars);
    const auto author = line(u"author", MaxMetaChars);
    const auto site = line(u"site", MaxMetaChars);
    const auto published = line(u"published", 100);
    const auto description = line(u"description", MaxMetaChars);
    const auto language = line(u"language", 35);
    const auto image = text(u"image", 4 * MaxUrlChars);
    const auto screenshot = text(u"screenshot", MaxScreenshotChars);
    const auto pictureUrl = text(u"pictureUrl", MaxScreenshotChars);
    const auto linkUrl = text(u"linkUrl", 4 * MaxUrlChars);
    const auto linkText = line(u"linkText", MaxTitleChars);
    const auto vault = text(u"vault", 4096);
    if (!url || !title || !html || !selectionHtml || !selectionText || !baseUrl || !author || !site || !published
        || !description || !language || !image || !screenshot || !pictureUrl || !linkUrl || !linkText || !vault)
        return std::nullopt;
    r.url = checkedLinkUrl(*url);
    if (r.url.startsWith(u"mailto:", Qt::CaseInsensitive))
        r.url.clear();
    r.title = *title;
    r.html = cleanText(*html);
    r.selectionHtml = cleanText(*selectionHtml);
    r.selectionText = cleanText(*selectionText);
    r.baseUrl = checkedLinkUrl(*baseUrl);
    if (r.baseUrl.startsWith(u"mailto:", Qt::CaseInsensitive))
        r.baseUrl.clear();
    r.author = *author;
    r.site = *site;
    r.published = *published;
    r.description = *description;
    r.language = *language;
    r.image = checkedWebUrl(*image);
    r.linkUrl = checkedLinkUrl(*linkUrl);
    r.linkText = *linkText;
    r.vault = vault->trimmed();

    static const QRegularExpression pictureData(u"^data:image/(png|jpeg|webp|gif);base64,[A-Za-z0-9+/=\\s]+$"_s,
                                                QRegularExpression::CaseInsensitiveOption);
    if (!screenshot->isEmpty()) {
        if (!pictureData.match(*screenshot).hasMatch()) {
            refuse(problem, u"bad_screenshot"_s, u"screenshot"_s, u"The screenshot is not a picture's data: URL"_s);
            return std::nullopt;
        }
        r.screenshot = *screenshot;
    }
    if (!pictureUrl->isEmpty()) {
        if (pictureData.match(*pictureUrl).hasMatch())
            r.pictureUrl = *pictureUrl;
        else if (pictureUrl->size() <= MaxUrlChars)
            r.pictureUrl = checkedWebUrl(*pictureUrl);
    }
    if (r.kind == ClipKind::Screenshot && r.screenshot.isEmpty()) {
        refuse(problem, u"no_screenshot"_s, u"screenshot"_s, u"A screenshot clip needs the screenshot"_s);
        return std::nullopt;
    }
    if (r.kind == ClipKind::Picture && r.pictureUrl.isEmpty()) {
        refuse(problem, u"no_picture"_s, u"pictureUrl"_s, u"A picture clip needs the picture's http(s) address"_s);
        return std::nullopt;
    }
    if (r.kind == ClipKind::Link && r.linkUrl.isEmpty()) {
        refuse(problem, u"no_link"_s, u"linkUrl"_s, u"A link clip needs the link's address"_s);
        return std::nullopt;
    }

    // Tags: a list of strings, or one string "a, b".
    const QJsonValue tags = o.value(u"tags");
    QStringList rawTags;
    if (tags.isString()) {
        rawTags = tags.toString().left(MaxTags * MaxTagChars * 4).split(QRegularExpression(u"[,;]"_s));
    } else if (tags.isArray()) {
        const QJsonArray array = tags.toArray();
        for (const QJsonValue &t : array) {
            if (!t.isString()) {
                refuse(problem, u"bad_value"_s, u"tags"_s, u"A tag is not a string"_s);
                return std::nullopt;
            }
            if (rawTags.size() < 4 * MaxTags)
                rawTags.append(t.toString().left(4 * MaxTagChars));
        }
    } else if (!tags.isUndefined() && !tags.isNull()) {
        refuse(problem, u"bad_value"_s, u"tags"_s, u"tags is neither a list nor a string"_s);
        return std::nullopt;
    }
    r.tags = cleanTags(rawTags);

    r.downloadPictures = boolOf(o, u"downloadPictures", true, &bad);
    if (bad) {
        refuse(problem, u"bad_value"_s, u"downloadPictures"_s, u"downloadPictures is not true or false"_s);
        return std::nullopt;
    }

    // Where it goes.
    const QJsonValue placeValue = o.value(u"place");
    if (!placeValue.isUndefined() && !placeValue.isNull() && !placeValue.isObject()) {
        refuse(problem, u"bad_value"_s, u"place"_s, u"place is not an object"_s);
        return std::nullopt;
    }
    const QJsonObject place = placeValue.toObject();
    const QString placeKind = stringOf(place, u"kind", &bad, u"page"_s);
    if (bad || !parsePlaceKind(placeKind)) {
        refuse(problem, u"bad_place"_s, u"place.kind"_s, u"Unknown place: %1"_s.arg(placeKind.left(40)));
        return std::nullopt;
    }
    r.place.kind = *parsePlaceKind(placeKind);
    r.place.journalLink = boolOf(place, u"journalLink", false, &bad);
    const QJsonValue folder = place.value(u"folder");
    if (!folder.isUndefined() && !folder.isNull()) {
        const auto checked = folder.isString() && folder.toString().size() <= 4096
                                     ? checkedFolder(folder.toString())
                                     : std::nullopt;
        if (!checked) {
            refuse(problem, u"bad_folder"_s, u"place.folder"_s,
                   u"The folder is not a folder of the notebook: %1"_s.arg(folder.toString().left(200)));
            return std::nullopt;
        }
        r.place.folder = *checked;
        r.place.defaultFolder = false;
    }
    if (r.place.kind == PlaceKind::Heading) {
        const QString page = stringOf(place, u"page", &bad);
        const auto checked = !bad && page.size() <= 4096 ? checkedPagePath(page) : std::nullopt;
        if (!checked) {
            refuse(problem, u"bad_page"_s, u"place.page"_s,
                   u"The page is not a page of the notebook: %1"_s.arg(page.left(200)));
            return std::nullopt;
        }
        r.place.page = *checked;
        r.place.heading = cleanLine(stringOf(place, u"heading", &bad).left(4 * MaxTitleChars), MaxTitleChars);
        if (r.place.heading.isEmpty()) {
            refuse(problem, u"no_heading"_s, u"place.heading"_s, u"Which heading? place.heading is empty"_s);
            return std::nullopt;
        }
    }
    if (bad) {
        refuse(problem, u"bad_value"_s, u"place"_s, u"A value of place has the wrong type"_s);
        return std::nullopt;
    }

    // The template.
    const QJsonValue templValue = o.value(u"template");
    if (templValue.isString()) {
        r.templ.builtin = templValue.toString().trimmed().toLower().left(40);
    } else if (templValue.isObject()) {
        const QJsonObject t = templValue.toObject();
        r.templ.builtin = stringOf(t, u"builtin", &bad).trimmed().toLower().left(40);
        const QString page = stringOf(t, u"page", &bad);
        const QString textValue = stringOf(t, u"text", &bad);
        if (bad) {
            refuse(problem, u"bad_value"_s, u"template"_s, u"A value of template has the wrong type"_s);
            return std::nullopt;
        }
        if (textValue.size() > MaxTemplateChars) {
            refuse(problem, u"too_large"_s, u"template.text"_s, u"The template is too large"_s);
            return std::nullopt;
        }
        r.templ.text = cleanText(textValue);
        if (!page.trimmed().isEmpty()) {
            const auto checked = page.size() <= 4096 ? checkedPagePath(page) : std::nullopt;
            if (!checked) {
                refuse(problem, u"bad_template"_s, u"template.page"_s,
                       u"The template is not a page of the notebook: %1"_s.arg(page.left(200)));
                return std::nullopt;
            }
            r.templ.page = *checked;
        }
    } else if (!templValue.isUndefined() && !templValue.isNull()) {
        refuse(problem, u"bad_value"_s, u"template"_s, u"template is neither a name nor an object"_s);
        return std::nullopt;
    }
    return r;
}

} // namespace brulion::clipper
