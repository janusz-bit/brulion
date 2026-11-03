#include "markdown/pagelook.h"

#include "markdown/inline.h"
#include "markdown/inline_p.h"

#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

namespace {

bool isAsciiUpper(QChar c)
{
    return c >= u'A' && c <= u'Z';
}

bool isAsciiLower(QChar c)
{
    return c >= u'a' && c <= u'z';
}

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

/// A value without the quotes a Logseq line may keep ("…" / '…').
QString unquoted(QStringView value)
{
    QStringView v = value.trimmed();
    if (v.size() >= 2 && ((v.front() == u'"' && v.back() == u'"') || (v.front() == u'\'' && v.back() == u'\'')))
        v = v.sliced(1, v.size() - 2).trimmed();
    return v.toString();
}

std::optional<QString> emoji(QStringView v)
{
    if (v.isEmpty() || v.size() > 32)
        return std::nullopt;
    bool wide = false;
    const QString s = v.toString();
    for (qsizetype i = 0; i < s.size(); ++i) {
        char32_t code = s[i].unicode();
        if (s[i].isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate()) {
            code = QChar::surrogateToUcs4(s[i], s[i + 1]);
            ++i;
        } else if (s[i].isSurrogate()) {
            return std::nullopt; // a lone surrogate
        }
        if (QChar::isLetter(code) || QChar::isSpace(code) || code < 0x20 || (code >= 0x7F && code < 0xA0))
            return std::nullopt;
        if (code >= 0x2000)
            wide = true;
    }
    if (!wide)
        return std::nullopt;
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, s);
    int clusters = 0;
    while (graphemes.toNextBoundary() >= 0) {
        if (++clusters > 2)
            return std::nullopt;
    }
    return s;
}

/// `LiBookOpen` → "book-open"; nullopt when it is no Iconize Lucide name.
std::optional<QString> lucideName(QStringView v)
{
    if (v.size() < 3 || v.size() > 64 || !v.startsWith(u"Li") || !isAsciiUpper(v[2]))
        return std::nullopt;
    QString out;
    for (qsizetype i = 2; i < v.size(); ++i) {
        const QChar c = v[i];
        if (isAsciiUpper(c)) {
            if (!out.isEmpty())
                out += u'-';
            out += c.toLower();
        } else if (isAsciiDigit(c)) {
            if (!out.isEmpty() && !isAsciiDigit(v[i - 1]))
                out += u'-';
            out += c;
        } else if (isAsciiLower(c)) {
            if (isAsciiDigit(v[i - 1]))
                out += u'-';
            out += c;
        } else {
            return std::nullopt;
        }
    }
    return out;
}

bool validAppName(QStringView name)
{
    if (name.isEmpty() || name.size() > 64 || name.front() == u'-' || name.back() == u'-' || name.contains(u"--"))
        return false;
    for (const QChar c : name) {
        if (!isAsciiLower(c) && !isAsciiDigit(c) && c != u'-')
            return false;
    }
    return true;
}

bool isWebUrl(QStringView text)
{
    if (!text.startsWith(u"http://", Qt::CaseInsensitive) && !text.startsWith(u"https://", Qt::CaseInsensitive))
        return false;
    QUrl url(text.toString(), QUrl::StrictMode);
    if (!url.isValid())
        url = QUrl(text.toString(), QUrl::TolerantMode);
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == u"http" || scheme == u"https") && !url.host().isEmpty();
}

/// A link target (or URL) as a cover.
PageCover coverOf(QString target)
{
    target = target.trimmed();
    PageCover cover;
    if (target.isEmpty() || target.contains(u'\n') || target.contains(u'\r'))
        return cover;
    if (isWebUrl(target)) {
        cover.kind = PageCover::Kind::Web;
        cover.target = target;
        return cover;
    }
    // Any other scheme (javascript:, data:, file:, C: is a drive) is no cover.
    static const QRegularExpression scheme(u"^[A-Za-z][A-Za-z0-9+.-]+:"_s);
    if (scheme.match(target).hasMatch())
        return cover;
    cover.kind = PageCover::Kind::File;
    cover.target = target;
    return cover;
}

} // namespace

PageIcon parsePageIcon(QStringView value)
{
    const QString v = unquoted(value);
    PageIcon icon;
    if (const auto name = lucideName(v)) {
        if (validAppName(*name)) {
            icon.kind = PageIcon::Kind::App;
            icon.name = *name;
        }
        return icon;
    }
    if (const auto e = emoji(v)) {
        icon.kind = PageIcon::Kind::Emoji;
        icon.name = *e;
    }
    return icon;
}

QString pageIconValue(const PageIcon &icon)
{
    switch (icon.kind) {
    case PageIcon::Kind::None: return {};
    case PageIcon::Kind::Emoji: return emoji(icon.name).value_or(QString());
    case PageIcon::Kind::App: {
        if (!validAppName(icon.name))
            return {};
        QString out = u"Li"_s;
        bool upper = true;
        for (const QChar c : icon.name) {
            if (c == u'-') {
                upper = true;
                continue;
            }
            out += upper ? c.toUpper() : c;
            upper = false;
        }
        return out;
    }
    }
    return {};
}

PageCover parsePageCover(const QVariant &value)
{
    QString text;
    if (value.typeId() == QMetaType::QStringList || value.typeId() == QMetaType::QVariantList) {
        const QVariantList items = value.toList();
        if (items.isEmpty())
            return {};
        text = items.first().toString();
    } else if (value.typeId() == QMetaType::QString) {
        text = value.toString();
    } else {
        return {};
    }
    text = unquoted(text);
    if (text.isEmpty())
        return {};
    QStringView v(text);
    if (v.startsWith(u'!'))
        v = v.sliced(1);
    if (v.startsWith(u"[[") && v.endsWith(u"]]")) {
        const QStringView inner = v.sliced(2, v.size() - 4);
        if (inner.contains(u"]]") || inner.contains(u"[["))
            return {};
        const auto parts = detail::parseWikiInner(inner);
        if (!parts || parts->target.isEmpty())
            return {};
        return coverOf(parts->target);
    }
    if (v.startsWith(u'[')) {
        const MarkdownDestination dest = markdownDestination(v);
        if (dest.from < 0)
            return {};
        // The whole value must be the one picture.
        const qsizetype close = v.lastIndexOf(u')');
        if (close != v.size() - 1)
            return {};
        QString target = v.sliced(dest.from, dest.to - dest.from).toString();
        if (!isWebUrl(target))
            target = QUrl::fromPercentEncoding(target.toUtf8());
        return coverOf(target);
    }
    if (text.startsWith(u'!'))
        return {};
    return coverOf(text);
}

double parseCoverPosition(const QVariant &value)
{
    bool ok = false;
    double v = 0;
    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::LongLong
        || value.typeId() == QMetaType::Int) {
        v = value.toDouble(&ok);
    } else if (value.typeId() == QMetaType::QString) {
        QString s = unquoted(value.toString());
        s.replace(u',', u'.');
        v = s.toDouble(&ok);
    }
    if (!ok || !std::isfinite(v) || v < 0 || v > 1)
        return 0.5;
    return v;
}

QString pageCoverValue(const PageCover &cover)
{
    switch (cover.kind) {
    case PageCover::Kind::None: return {};
    case PageCover::Kind::Web: return cover.target.trimmed();
    case PageCover::Kind::File: return u"![["_s + cover.target.trimmed() + u"]]"_s;
    }
    return {};
}

QString coverPositionValue(double position)
{
    if (!std::isfinite(position))
        position = 0.5;
    position = std::clamp(position, 0.0, 1.0);
    return QString::number(std::round(position * 100) / 100, 'g', 3);
}

PageLook pageLook(const Properties &properties)
{
    PageLook look;
    for (const QStringView key : {PageIconKey, PageIconFallbackKey}) {
        const QVariant v = properties.value(key.toString());
        if (v.typeId() == QMetaType::QString) {
            look.icon = parsePageIcon(v.toString());
            if (!look.icon.isNone())
                break;
        }
    }
    look.cover = parsePageCover(properties.value(PageCoverKey.toString()));
    if (look.cover.kind != PageCover::Kind::None)
        look.cover.position = parseCoverPosition(properties.value(PageCoverPositionKey.toString()));
    return look;
}

} // namespace brulion::markdown
