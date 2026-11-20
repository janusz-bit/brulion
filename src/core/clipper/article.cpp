#include "clipper/article.h"

#include "io/htmlmarkdown.h"
#include "util/paths.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

constexpr char16_t TokenOpen = 0xE0B0;
constexpr char16_t TokenClose = 0xE0B1;

/// An address as a Markdown link's destination: what would end it or start other syntax is
/// percent-encoded (the address stays the same).
QString destination(const QString &url)
{
    QString out;
    out.reserve(url.size());
    for (const QChar c : url) {
        switch (c.unicode()) {
        case u' ': out += u"%20"_s; break;
        case u'(': out += u"%28"_s; break;
        case u')': out += u"%29"_s; break;
        case u'<': out += u"%3C"_s; break;
        case u'>': out += u"%3E"_s; break;
        case u'\n':
        case u'\r':
        case u'\t': break;
        default: out += c;
        }
    }
    return out;
}

/// The index of a label that is one picture token alone, else -1.
int loneToken(QStringView label)
{
    const QStringView t = label.trimmed();
    if (t.size() < 3 || t.front() != QChar(TokenOpen) || t.back() != QChar(TokenClose))
        return -1;
    bool ok = false;
    const int index = t.sliced(1, t.size() - 2).toInt(&ok);
    return ok ? index : -1;
}

bool isPictureAddress(const QUrl &url)
{
    return paths::isPicturePath(url.path());
}

} // namespace

QString pictureToken(int index)
{
    return QChar(TokenOpen) + QString::number(index) + QChar(TokenClose);
}

QString withoutTokenChars(QString text)
{
    text.removeIf([](QChar c) { return c.unicode() == TokenOpen || c.unicode() == TokenClose; });
    return text;
}

QString PictureList::add(const QString &source, const QString &alt)
{
    const bool data = source.startsWith(u"data:image/"_s, Qt::CaseInsensitive);
    if (!data && !source.startsWith(u"http://"_s, Qt::CaseInsensitive)
        && !source.startsWith(u"https://"_s, Qt::CaseInsensitive))
        return {};
    for (qsizetype i = 0; i < m_pictures.size(); ++i) {
        if (m_pictures.at(i).source == source)
            return pictureToken(int(i));
    }
    if (m_pictures.size() >= MaxPictures)
        return {};
    static const QRegularExpression blanks(u"\\s+"_s);
    QString description = withoutTokenChars(alt);
    description.replace(blanks, u" "_s);
    m_pictures.append(ClipPicture{source, description.trimmed().left(300)});
    return pictureToken(int(m_pictures.size() - 1));
}

QString replacePictureTokens(const QString &markdown, const std::function<QString(int index, bool inLink)> &embed)
{
    if (!markdown.contains(QChar(TokenOpen)))
        return markdown;
    static const QRegularExpression token(u"(\\[)?\\x{E0B0}(\\d{1,6})\\x{E0B1}(\\]\\([^)\\s]*\\))?"_s);
    QString out;
    qsizetype last = 0;
    for (auto it = token.globalMatch(markdown); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(markdown).sliced(last, m.capturedStart(0) - last);
        const int index = m.captured(2).toInt();
        const bool inLink = m.capturedLength(1) > 0 && m.capturedLength(3) > 0;
        if (inLink) {
            const QString linked = embed(index, true);
            // The embed may give the whole link; "" keeps the link around the picture's Markdown.
            out += linked.isEmpty() ? m.captured(1) + embed(index, false) + m.captured(3) : linked;
        } else {
            out += m.captured(1) + embed(index, false) + m.captured(3);
        }
        last = m.capturedEnd(0);
    }
    out += QStringView(markdown).sliced(last);
    // A token cut apart (never written by us) leaves nothing behind.
    out.removeIf([](QChar c) { return c.unicode() == TokenOpen || c.unicode() == TokenClose; });
    return out;
}

QUrl resolveWebUrl(QStringView raw, const QUrl &base)
{
    QString s = raw.trimmed().toString();
    s.removeIf([](QChar c) { return c == u'\t' || c == u'\n' || c == u'\r'; });
    if (s.isEmpty())
        return {};
    if (s.startsWith(u"//"))
        s.prepend(base.scheme().compare(u"http"_s, Qt::CaseInsensitive) == 0 ? u"http:"_s : u"https:"_s);
    QUrl url(s);
    if (url.isRelative() && base.isValid())
        url = base.resolved(url);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"http" && scheme != u"https") || url.host().isEmpty())
        return {};
    return url;
}

ConvertedHtml convertHtml(const QString &rawHtml, const QUrl &base, PictureList &pictures, bool plainText)
{
    const QString html = withoutTokenChars(rawHtml);
    io::HtmlToMarkdownOptions o;
    o.embedsAsLinks = true;
    o.image = [&](const io::HtmlImage &image) -> QString {
        if (image.src.startsWith(u"data:image/"_s, Qt::CaseInsensitive))
            return pictures.add(image.src, image.alt);
        const QUrl url = resolveWebUrl(image.src, base);
        return url.isValid() ? pictures.add(url.toString(QUrl::FullyEncoded), image.alt) : QString();
    };
    o.link = [&](const QString &href, const QString &label) -> std::optional<QString> {
        const QString h = href.trimmed();
        QUrl target;
        if (h.startsWith(u'#')) {
            // A note, a section of the same page: on the page itself.
            if (!base.isValid() || h.size() < 2)
                return label;
            target = base;
            target.setFragment(QUrl::fromPercentEncoding(h.sliced(1).toUtf8()));
        } else if (h.startsWith(u"mailto:"_s, Qt::CaseInsensitive)) {
            target = QUrl(h);
        } else {
            target = resolveWebUrl(h, base);
        }
        const QString scheme = target.scheme().toLower();
        if (!target.isValid() || (scheme != u"http" && scheme != u"https" && scheme != u"mailto"))
            return label;
        const QString address = target.toString(QUrl::FullyEncoded);
        // A picture that links to itself (or to a bigger copy): the picture alone.
        if (const int index = loneToken(label); index >= 0) {
            const ClipPicture *picture = index < pictures.pictures().size() ? &pictures.pictures().at(index) : nullptr;
            if (picture && (picture->source == address || isPictureAddress(target)))
                return label.trimmed();
        }
        if (label.trimmed().isEmpty())
            return scheme == u"mailto" ? QString() : u'<' + destination(address) + u'>';
        const QString plainLabel = QString(label).remove(u'\\');
        if (plainLabel == address || plainLabel == target.toString()
            || (scheme == u"mailto" && u"mailto:"_s + plainLabel == address))
            return u'<' + destination(address) + u'>';
        return u'[' + label + u"]("_s + destination(address) + u')';
    };
    ConvertedHtml out;
    out.markdown = io::htmlToMarkdown(html, o).markdown;
    if (plainText)
        out.plainText = io::htmlToPlainText(html).trimmed();
    return out;
}

} // namespace brulion::clipper
