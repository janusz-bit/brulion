#include "io/htmlrender_p.h"

#include "dates/recurrence.h"
#include "markdown/inline.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QScopeGuard>

#include <algorithm>
#include <array>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxInlineDepth = 2;

struct Atom {
    qsizetype from = 0;
    qsizetype to = 0;
    QString html;
};

bool isAsciiPunct(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 0x21 && u <= 0x2F) || (u >= 0x3A && u <= 0x40) || (u >= 0x5B && u <= 0x60) || (u >= 0x7B && u <= 0x7E);
}

QString esc(QStringView s)
{
    return HtmlRenderer::escape(s);
}

QString escapeWithBreaks(QStringView s)
{
    QString out = esc(s);
    out.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    return out.replace(u'\n', u"<br>"_s);
}

bool hasScheme(QStringView target)
{
    static const QRegularExpression re(u"^[A-Za-z][A-Za-z0-9+.-]{1,31}:"_s);
    return re.matchView(target).hasMatch();
}

bool safeExternal(QStringView url)
{
    const qsizetype colon = url.indexOf(u':');
    if (colon <= 0)
        return false;
    const QString scheme = url.first(colon).toString().toLower();
    return scheme == u"http" || scheme == u"https" || scheme == u"mailto";
}

QString attr(const QString &value)
{
    return u'"' + esc(value) + u'"';
}

QString chip(const QString &cls, const QString &text)
{
    return u"<span class=\"chip "_s + cls + u"\">"_s + esc(text) + u"</span>"_s;
}

enum class MediaKind { Image, Audio, Video, Other };

MediaKind mediaKind(const QString &path)
{
    static const QStringList images = {u".png"_s, u".jpg"_s, u".jpeg"_s, u".gif"_s, u".webp"_s,
                                       u".svg"_s, u".bmp"_s, u".avif"_s, u".ico"_s};
    static const QStringList audio = {u".mp3"_s, u".m4a"_s, u".wav"_s, u".ogg"_s, u".opus"_s, u".flac"_s, u".aac"_s};
    static const QStringList video = {u".mp4"_s, u".webm"_s, u".mov"_s, u".m4v"_s};
    const QString ext = paths::extension(path);
    if (images.contains(ext))
        return MediaKind::Image;
    if (audio.contains(ext))
        return MediaKind::Audio;
    if (video.contains(ext))
        return MediaKind::Video;
    return MediaKind::Other;
}

/// What a link shows when it cannot be one (inside another link): its label, else its target.
QString visibleText(const Link &link, QStringView source)
{
    switch (link.kind) {
    case LinkKind::Wiki:
    case LinkKind::Embed:
    case LinkKind::Markdown:
        if (!link.display.isEmpty())
            return link.display;
        return link.target.isEmpty() ? source.toString() : link.target;
    case LinkKind::BlockRef:
    case LinkKind::BlockEmbed:
    case LinkKind::Url: break;
    }
    return source.toString();
}

bool isAttachmentTarget(const QString &target)
{
    const QString ext = paths::extension(target);
    return !ext.isEmpty() && ext != u".md" && ext != u".markdown";
}

/// An attachment as <img> / <audio> / <video> / link; the kind comes from `fileName`.
QString mediaHtml(const QString &url, const QString &label, const QString &fileName)
{
    switch (mediaKind(fileName)) {
    case MediaKind::Image: return u"<img src="_s + attr(url) + u" alt="_s + attr(label) + u" loading=\"lazy\">"_s;
    case MediaKind::Audio: return u"<audio controls src="_s + attr(url) + u"></audio>"_s;
    case MediaKind::Video: return u"<video controls src="_s + attr(url) + u"></video>"_s;
    case MediaKind::Other: break;
    }
    return u"<a class=\"attachment\" href="_s + attr(url) + u">"_s + esc(label) + u"</a>"_s;
}

// --- emphasis (CommonMark-like delimiter runs, as in markdown::InlineRenderer) -------------------

struct Piece {
    enum class Type { Text, Html, Delim };
    Type type = Type::Text;
    qsizetype from = 0;
    qsizetype to = 0;
    QString html;
    QChar ch;
    int remaining = 0;
    bool canOpen = false;
    bool canClose = false;
    QString openTags;
    QString closeTags;
};

bool isDelimChar(QChar c)
{
    return c == u'*' || c == u'_' || c == u'~' || c == u'=';
}

bool isPunctuation(QChar c)
{
    return c.isPunct() || c.isSymbol();
}

Piece textPiece(qsizetype from, qsizetype to)
{
    Piece p;
    p.from = from;
    p.to = to;
    return p;
}

QList<Piece> buildPieces(QStringView text, const QList<Atom> &atoms)
{
    QList<Piece> pieces;
    const auto addGap = [&](qsizetype a, qsizetype b, QChar before, QChar after) {
        qsizetype textStart = a;
        qsizetype i = a;
        while (i < b) {
            const QChar c = text[i];
            if (!isDelimChar(c)) {
                ++i;
                continue;
            }
            qsizetype j = i;
            while (j < b && text[j] == c)
                ++j;
            const qsizetype len = j - i;
            if ((c == u'~' || c == u'=') && len != 2) {
                i = j;
                continue;
            }
            if (i > textStart)
                pieces.append(textPiece(textStart, i));
            const QChar prev = i > a ? text[i - 1] : before;
            const QChar next = j < b ? text[j] : after;
            const bool prevSpace = prev.isSpace();
            const bool nextSpace = next.isSpace();
            const bool left = !nextSpace && (!isPunctuation(next) || prevSpace || isPunctuation(prev));
            const bool right = !prevSpace && (!isPunctuation(prev) || nextSpace || isPunctuation(next));
            Piece d;
            d.type = Piece::Type::Delim;
            d.from = i;
            d.to = j;
            d.ch = c;
            d.remaining = int(len);
            if (c == u'_') {
                d.canOpen = left && (!right || isPunctuation(prev));
                d.canClose = right && (!left || isPunctuation(next));
            } else {
                d.canOpen = left;
                d.canClose = right;
            }
            pieces.append(d);
            i = j;
            textStart = j;
        }
        if (b > textStart)
            pieces.append(textPiece(textStart, b));
    };
    const QChar space(u' ');
    const QChar letter(u'a');
    qsizetype pos = 0;
    QChar before = space;
    for (const Atom &atom : atoms) {
        addGap(pos, atom.from, before, atom.html.isEmpty() ? space : letter);
        Piece h;
        h.type = Piece::Type::Html;
        h.from = atom.from;
        h.to = atom.to;
        h.html = atom.html;
        pieces.append(h);
        pos = atom.to;
        before = atom.html.isEmpty() ? space : letter;
    }
    addGap(pos, text.size(), before, space);
    return pieces;
}

int delimSlot(QChar c)
{
    return c == u'*' ? 0 : c == u'_' ? 1 : c == u'~' ? 2 : 3;
}

void processEmphasis(QList<Piece> &pieces)
{
    QList<int> openers;
    std::array<qsizetype, 4> bottom{0, 0, 0, 0};
    const auto shrink = [&](qsizetype size) {
        openers.resize(size);
        for (qsizetype &b : bottom)
            b = std::min(b, size);
    };
    for (int k = 0; k < pieces.size(); ++k) {
        if (pieces[k].type != Piece::Type::Delim)
            continue;
        if (pieces[k].canClose) {
            const int slot = delimSlot(pieces[k].ch);
            while (pieces[k].remaining > 0) {
                qsizetype found = -1;
                for (qsizetype s = openers.size() - 1; s >= bottom[slot]; --s) {
                    if (pieces[openers[s]].ch == pieces[k].ch) {
                        found = s;
                        break;
                    }
                }
                if (found < 0) {
                    bottom[slot] = openers.size();
                    break;
                }
                Piece &o = pieces[openers[found]];
                Piece &d = pieces[k];
                const bool emphasis = d.ch == u'*' || d.ch == u'_';
                const int use = emphasis ? (o.remaining >= 2 && d.remaining >= 2 ? 2 : 1) : 2;
                QString open;
                QString close;
                if (emphasis) {
                    open = use == 2 ? u"<strong>"_s : u"<em>"_s;
                    close = use == 2 ? u"</strong>"_s : u"</em>"_s;
                } else if (d.ch == u'~') {
                    open = u"<del>"_s;
                    close = u"</del>"_s;
                } else {
                    open = u"<mark>"_s;
                    close = u"</mark>"_s;
                }
                o.openTags.prepend(open);
                d.closeTags.append(close);
                o.remaining -= use;
                d.remaining -= use;
                shrink(found + (o.remaining > 0 ? 1 : 0));
            }
        }
        if (pieces[k].canOpen && pieces[k].remaining > 0)
            openers.append(k);
    }
}

QString assemble(QStringView text, const QList<Piece> &pieces)
{
    QString out;
    for (const Piece &p : pieces) {
        switch (p.type) {
        case Piece::Type::Text: out += escapeWithBreaks(text.sliced(p.from, p.to - p.from)); break;
        case Piece::Type::Html: out += p.html; break;
        case Piece::Type::Delim: out += p.closeTags + QString(p.remaining, p.ch) + p.openTags; break;
        }
    }
    while (out.endsWith(u"<br>"))
        out.chop(4);
    return out;
}

/// Code spans and comments (blanked by maskInlineCode): code -> <code>, comments hidden.
void codeAndCommentAtoms(QStringView text, QStringView masked, QList<Atom> &atoms)
{
    for (qsizetype i = 0; i < text.size();) {
        if (masked[i] != u' ' || text[i] == u' ') {
            ++i;
            continue;
        }
        if (text[i] == u'`') {
            qsizetype n = 0;
            while (i + n < text.size() && text[i + n] == u'`')
                ++n;
            qsizetype j = i + n;
            qsizetype close = -1;
            while (j < text.size()) {
                if (text[j] == u'`') {
                    qsizetype m = 0;
                    while (j + m < text.size() && text[j + m] == u'`')
                        ++m;
                    if (m == n && masked[j] == u' ') {
                        close = j;
                        break;
                    }
                    j += m;
                } else {
                    ++j;
                }
            }
            if (close < 0) {
                i += n;
                continue;
            }
            QString content = text.sliced(i + n, close - i - n).toString();
            content.replace(u"\r\n"_s, u" "_s).replace(u'\n', u' ').replace(u'\r', u' ');
            if (content.size() >= 2 && content.startsWith(u' ') && content.endsWith(u' ')
                && !content.trimmed().isEmpty())
                content = content.sliced(1, content.size() - 2);
            atoms.append(Atom{i, close + n, u"<code>"_s + esc(content) + u"</code>"_s});
            i = close + n;
        } else if (text.sliced(i).startsWith(u"%%")) {
            const qsizetype close = text.indexOf(u"%%", i + 2);
            const qsizetype end = close < 0 ? text.size() : close + 2;
            atoms.append(Atom{i, end, QString()});
            i = end;
        } else if (text.sliced(i).startsWith(u"<!--")) {
            const qsizetype close = text.indexOf(u"-->", i + 4);
            const qsizetype end = close < 0 ? text.size() : close + 3;
            atoms.append(Atom{i, end, QString()});
            i = end;
        } else {
            ++i;
        }
    }
}

QString priorityName(Priority p)
{
    switch (p) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::None: break;
    }
    return {};
}

QString statusName(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return u"todo"_s;
}

bool isOrderedItem(const Block &b)
{
    const QStringView first = QStringView(b.raw).trimmed();
    qsizetype i = 0;
    while (i < first.size() && first[i].isDigit())
        ++i;
    return i > 0 && i < first.size() && (first[i] == u'.' || first[i] == u')');
}

/// Split a GFM table row into cells (`\|` is not a separator).
QStringList tableCells(QStringView row)
{
    QStringView r = row.trimmed();
    if (r.startsWith(u'|'))
        r = r.sliced(1);
    if (r.endsWith(u'|') && !r.endsWith(u"\\|"))
        r.chop(1);
    QStringList cells;
    QString cell;
    for (qsizetype i = 0; i < r.size(); ++i) {
        if (r[i] == u'\\' && i + 1 < r.size() && r[i + 1] == u'|') {
            cell += u'|';
            ++i;
        } else if (r[i] == u'|') {
            cells.append(cell.trimmed());
            cell.clear();
        } else {
            cell += r[i];
        }
    }
    cells.append(cell.trimmed());
    return cells;
}

/// The callout title: the first text line without `[!type]±`.
QString calloutTitle(const Block &b)
{
    static const QRegularExpression header(u"^\\s*>?\\s*\\[![^\\]]*\\][+-]?\\s*"_s);
    QString first = b.text.section(u'\n', 0, 0);
    first.remove(header);
    if (first.trimmed().isEmpty()) {
        const QString type = b.properties.value(u"callouttype"_s).toString();
        return type.isEmpty() ? QString() : type.first(1).toUpper() + type.sliced(1);
    }
    return first.trimmed();
}

} // namespace

HtmlRenderer::HtmlRenderer(HtmlLinks links) : m_links(std::move(links)) { }

QString HtmlRenderer::escape(QStringView text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&': out += u"&amp;"; break;
        case u'<': out += u"&lt;"; break;
        case u'>': out += u"&gt;"; break;
        case u'"': out += u"&quot;"; break;
        case u'\'': out += u"&#39;"; break;
        default: out += c;
        }
    }
    return out;
}

QString HtmlRenderer::headingAnchor(const QString &heading)
{
    QString out;
    bool dash = false;
    for (const QChar c : heading.toLower()) {
        if (c.isLetterOrNumber() || c == u'_') {
            if (dash && !out.isEmpty())
                out += u'-';
            out += c;
            dash = false;
        } else {
            dash = true;
        }
    }
    return out.isEmpty() ? u"section"_s : out;
}

QString HtmlRenderer::blockAnchor(const QString &id)
{
    return u"block-"_s + id;
}

QString HtmlRenderer::renderInline(QStringView text, bool insideLink) const
{
    if (text.isEmpty())
        return {};
    static thread_local int depth = 0;
    if (depth > MaxInlineDepth)
        return escapeWithBreaks(text);
    ++depth;
    const auto restore = qScopeGuard([] { --depth; });

    const QString masked = markdown::maskInlineCode(text);
    QList<Atom> atoms;
    codeAndCommentAtoms(text, masked, atoms);

    // Links, embeds, block references, URLs.
    const markdown::InlineScan scan = markdown::extractInline(text);
    for (const Link &l : scan.links) {
        const qsizetype from = l.from;
        const qsizetype to = l.to;
        if (from < 0 || to > text.size() || from >= to)
            continue;
        const QStringView source = text.sliced(from, to - from);
        if (insideLink) {
            atoms.append(Atom{from, to, esc(visibleText(l, source))});
            continue;
        }
        QString html;
        switch (l.kind) {
        case LinkKind::Wiki: {
            QString label = l.display;
            if (label.isEmpty()) {
                const QString frag = !l.heading.isEmpty()   ? l.heading
                                     : !l.blockId.isEmpty() ? u'^' + l.blockId
                                                            : QString();
                label = l.target.isEmpty() ? frag : frag.isEmpty() ? l.target : QString(l.target + u" › "_s + frag);
            }
            const auto href = m_links.page ? m_links.page(l.target, l.heading, l.blockId) : std::nullopt;
            html = href ? QString(u"<a class=\"internal\" href="_s + attr(*href) + u'>' + esc(label) + u"</a>"_s)
                        : QString(u"<span class=\"missing\">"_s + esc(label) + u"</span>"_s);
            break;
        }
        case LinkKind::Embed: {
            if (isAttachmentTarget(l.target)) {
                const auto url = m_links.attachment ? m_links.attachment(l.target) : std::nullopt;
                const QString label = l.display.isEmpty() ? paths::basename(l.target) : l.display;
                html = url ? mediaHtml(*url, label, l.target)
                           : QString(u"<span class=\"missing\">"_s + esc(label) + u"</span>"_s);
            } else {
                const auto href = m_links.page ? m_links.page(l.target, l.heading, l.blockId) : std::nullopt;
                const QString label = u"⧉ "_s + (l.display.isEmpty() ? l.target : l.display);
                html = href ? QString(u"<a class=\"embed-link\" href="_s + attr(*href) + u'>' + esc(label) + u"</a>"_s)
                            : QString(u"<span class=\"missing\">"_s + esc(label) + u"</span>"_s);
            }
            break;
        }
        case LinkKind::Markdown: {
            const bool image = source.startsWith(u'!');
            if (hasScheme(l.target)) {
                if (!safeExternal(l.target)) {
                    html = esc(source);
                } else if (image) {
                    html = u"<img src="_s + attr(l.target) + u" alt="_s + attr(l.display) + u" loading=\"lazy\">"_s;
                } else {
                    QString shown = renderInline(l.display, true);
                    if (shown.isEmpty())
                        shown = esc(l.target);
                    html = u"<a class=\"external\" href="_s + attr(l.target) + u" rel=\"noopener\">"_s + shown
                           + u"</a>"_s;
                }
                break;
            }
            std::optional<QString> url;
            if (!image && m_links.page && !isAttachmentTarget(l.target))
                url = m_links.page(l.target, l.heading, l.blockId);
            if (!url && m_links.attachment)
                url = m_links.attachment(l.target);
            if (!url && !image && m_links.page)
                url = m_links.page(l.target, l.heading, l.blockId);
            if (!url) {
                html = u"<span class=\"missing\">"_s + esc(l.display.isEmpty() ? l.target : l.display) + u"</span>"_s;
            } else if (image) {
                html = mediaHtml(*url, l.display, l.target);
            } else {
                QString shown = renderInline(l.display, true);
                if (shown.isEmpty())
                    shown = esc(l.target);
                html = u"<a class=\"internal\" href="_s + attr(*url) + u'>' + shown + u"</a>"_s;
            }
            break;
        }
        case LinkKind::BlockRef:
        case LinkKind::BlockEmbed: {
            const auto ref = m_links.block ? m_links.block(l.target) : std::nullopt;
            const QString cls = l.kind == LinkKind::BlockRef ? u"block-ref"_s : u"block-embed"_s;
            html = ref ? QString(u"<a class=\""_s + cls + u"\" href="_s + attr(ref->first) + u'>'
                                 + renderInline(ref->second, true) + u"</a>"_s)
                       : QString(u"<span class=\"missing\">(("_s + esc(l.target) + u"))</span>"_s);
            break;
        }
        case LinkKind::Url: {
            const QString url = l.target;
            const QString shown = source.startsWith(u'<') ? source.sliced(1, source.size() - 2).toString() : url;
            html = safeExternal(url) ? u"<a class=\"external\" href="_s + attr(url) + u" rel=\"noopener\">"_s
                                               + esc(shown) + u"</a>"_s
                                     : esc(source);
            break;
        }
        }
        atoms.append(Atom{from, to, html});
    }

    // Tags: exactly the ones the parser sees (SYNTAX §3); none inside link labels.
    for (const markdown::TagSpan &tag : insideLink ? QList<markdown::TagSpan>() : markdown::findTags(text)) {
        const QString href = m_links.tag ? m_links.tag(tag.name) : QString();
        atoms.append(Atom{tag.from, tag.to,
                          href.isEmpty() ? QString(u"<span class=\"tag\">#"_s + esc(tag.name) + u"</span>"_s)
                                         : QString(u"<a class=\"tag\" href="_s + attr(href) + u">#"_s + esc(tag.name)
                                                   + u"</a>"_s)});
    }

    // Metadata tokens as chips.
    for (const markdown::Token &t : markdown::findTokens(masked)) {
        const QString source = text.sliced(t.from, t.to - t.from).toString();
        QString html;
        switch (t.kind) {
        case markdown::TokenKind::Due: html = chip(u"due"_s, source); break;
        case markdown::TokenKind::Scheduled:
        case markdown::TokenKind::Start: html = chip(u"scheduled"_s, source); break;
        case markdown::TokenKind::Reminder:
        case markdown::TokenKind::ReminderCompat: html = chip(t.alarm ? u"alarm"_s : u"reminder"_s, source); break;
        case markdown::TokenKind::Recurrence: html = chip(u"recurrence"_s, source); break;
        case markdown::TokenKind::Planning:
            html = chip(t.field == markdown::TokenKind::Due ? u"due"_s : u"scheduled"_s, source);
            break;
        default: html = chip(u"meta"_s, source); break;
        }
        atoms.append(Atom{t.from, t.to, html});
    }

    // Block ids at line ends (hidden), footnote references, backslash escapes.
    for (const text::Line &line : text::splitLines(masked)) {
        if (const auto id = markdown::blockIdAtEnd(line.text))
            atoms.append(Atom{line.start + id->from, line.start + id->to, QString()});
    }
    for (qsizetype i = masked.indexOf(u"[^"); i >= 0; i = masked.indexOf(u"[^", i + 2)) {
        qsizetype j = i + 2;
        while (j < masked.size() && masked[j] != u']' && !masked[j].isSpace())
            ++j;
        if (j > i + 2 && j < masked.size() && masked[j] == u']' && (j + 1 >= masked.size() || masked[j + 1] != u':'))
            atoms.append(
                    Atom{i, j + 1, u"<sup class=\"footnote\">"_s + esc(text.sliced(i + 2, j - i - 2)) + u"</sup>"_s});
    }
    for (qsizetype i = 0; i + 1 < masked.size(); ++i) {
        if (masked[i] == u'\\' && isAsciiPunct(masked[i + 1])) {
            atoms.append(Atom{i, i + 2, esc(text.sliced(i + 1, 1))});
            ++i;
        }
    }

    std::sort(atoms.begin(), atoms.end(),
              [](const Atom &a, const Atom &b) { return a.from != b.from ? a.from < b.from : a.to > b.to; });
    QList<Atom> kept;
    qsizetype end = 0;
    for (Atom &a : atoms) {
        if (a.from >= end && a.to > a.from) {
            end = a.to;
            kept.append(std::move(a));
        }
    }
    QList<Piece> pieces = buildPieces(text, kept);
    processEmphasis(pieces);
    return assemble(text, pieces);
}

QString HtmlRenderer::metaChips(const Block &block) const
{
    QStringList chips;
    if (block.task) {
        const Task &t = *block.task;
        if (t.priority != Priority::None)
            chips.append(u"<span class=\"chip priority priority-"_s + priorityName(t.priority) + u"\">"_s
                         + esc(markdown::tokens::priorityEmoji(t.priority)) + u"</span>"_s);
        if (t.start)
            chips.append(chip(u"scheduled"_s, u"🛫 "_s + t.start->toString()));
        if (t.scheduled)
            chips.append(chip(u"scheduled"_s, u"⏳ "_s + t.scheduled->toString()));
        if (t.due)
            chips.append(chip(u"due"_s, u"📅 "_s + t.due->toString()));
        if (!t.recurrence.isEmpty())
            chips.append(chip(u"recurrence"_s, u"🔁 "_s + t.recurrence));
        if (t.doneDate)
            chips.append(chip(u"done"_s, u"✅ "_s + t.doneDate->toString(u"yyyy-MM-dd"_s)));
        if (t.cancelledDate)
            chips.append(chip(u"done"_s, u"❌ "_s + t.cancelledDate->toString(u"yyyy-MM-dd"_s)));
    }
    for (const Reminder &r : block.reminders) {
        QString text = (r.alarm ? u"⏰! "_s : u"⏰ "_s) + r.at.toString();
        if (!r.recurrence.isEmpty() && !block.task)
            text += u" 🔁 "_s + r.recurrence;
        chips.append(chip(r.alarm ? u"alarm"_s : u"reminder"_s, text));
    }
    QStringList props;
    for (auto it = block.properties.constBegin(); it != block.properties.constEnd(); ++it) {
        const QString &key = it.key();
        if (key == u"id" || key == u"collapsed" || key == u"callouttype" || key == u"calloutfold")
            continue;
        const QString value = it.value().typeId() == QMetaType::QStringList ? it.value().toStringList().join(u", "_s)
                                                                            : it.value().toString();
        props.append(u"<span class=\"prop\"><span class=\"key\">"_s + esc(key) + u"</span> "_s + esc(value)
                     + u"</span>"_s);
    }
    props.sort();
    chips.append(props);
    return chips.isEmpty() ? QString() : u" <span class=\"meta\">"_s + chips.join(u' ') + u"</span>"_s;
}

QString HtmlRenderer::renderNote(const ParsedNote &note) const
{
    QList<int> roots;
    for (int i = 0; i < note.blocks.size(); ++i) {
        if (note.blocks[i].parent < 0)
            roots.append(i);
    }
    return renderBlocks(note, roots);
}

QString HtmlRenderer::renderBlocks(const ParsedNote &note, const QList<int> &indices) const
{
    QString out;
    for (qsizetype k = 0; k < indices.size();) {
        const int idx = indices[k];
        const Block &b = note.blocks[idx];
        if (b.kind != BlockKind::ListItem) {
            out += renderBlock(note, idx);
            ++k;
            continue;
        }
        const bool ordered = isOrderedItem(b);
        out += ordered ? u"<ol>"_s : u"<ul>"_s;
        while (k < indices.size() && note.blocks[indices[k]].kind == BlockKind::ListItem
               && isOrderedItem(note.blocks[indices[k]]) == ordered) {
            out += renderListItem(note, indices[k]);
            ++k;
        }
        out += ordered ? u"</ol>"_s : u"</ul>"_s;
    }
    return out;
}

QString HtmlRenderer::renderListItem(const ParsedNote &note, int index) const
{
    const Block &b = note.blocks[index];
    QString cls;
    QString box;
    if (b.task) {
        const QString status = statusName(b.task->status);
        cls = u"task "_s + status;
        const bool checked = b.task->status == TaskStatus::Done || b.task->status == TaskStatus::Cancelled;
        box = u"<input type=\"checkbox\" disabled"_s + (checked ? u" checked"_s : QString()) + u"> "_s;
    }
    QString out = u"<li"_s;
    if (!cls.isEmpty())
        out += u" class=\""_s + cls + u'"';
    if (!b.id.isEmpty())
        out += u" id="_s + attr(m_links.idPrefix + blockAnchor(b.id));
    const QString embedded = b.task ? QString() : embeddedContent(b.text);
    out += u'>' + box
           + (embedded.isEmpty() ? QString(renderInline(b.text) + metaChips(b))
                                 : QString(u"<div class=\"embed\">"_s + embedded + u"</div>"_s));
    if (!b.children.isEmpty())
        out += renderBlocks(note, b.children);
    return out + u"</li>"_s;
}

QString HtmlRenderer::embeddedContent(const QString &text) const
{
    if (!m_links.embed)
        return {};
    static const QRegularExpression onlyEmbed(u"^!\\[\\[([^\\]|#]+)(?:#(\\^?)([^\\]|]+))?(?:\\|[^\\]]*)?\\]\\]$"_s);
    const auto m = onlyEmbed.match(text.trimmed());
    if (!m.hasMatch() || isAttachmentTarget(m.captured(1)))
        return {};
    const bool isBlock = m.capturedLength(2) > 0;
    const QString frag = m.captured(3);
    return m_links.embed(m.captured(1).trimmed(), isBlock ? QString() : frag, isBlock ? frag : QString());
}

QString HtmlRenderer::renderTable(const QString &source) const
{
    const QStringList rows = source.split(u'\n', Qt::SkipEmptyParts);
    if (rows.isEmpty())
        return {};
    QStringList aligns;
    const bool hasDelimiter =
            rows.size() > 1 && tableCells(rows[1]).join(QString()).remove(u'-').remove(u':').trimmed().isEmpty();
    if (hasDelimiter) {
        for (const QString &cell : tableCells(rows[1])) {
            const bool left = cell.startsWith(u':');
            const bool right = cell.endsWith(u':');
            aligns.append(left && right ? u"center"_s : right ? u"right"_s : left ? u"left"_s : QString());
        }
    }
    const auto cellHtml = [&](const QString &tag, const QString &cell, qsizetype column) -> QString {
        const QString align = column < aligns.size() ? aligns[column] : QString();
        const QString style = align.isEmpty() ? QString() : u" style=\"text-align:"_s + align + u'"';
        const QString content = cell.startsWith(u'=') ? u"<code>"_s + esc(cell) + u"</code>"_s : renderInline(cell);
        return u'<' + tag + style + u'>' + content + u"</"_s + tag + u'>';
    };
    QString out = u"<table><thead><tr>"_s;
    const QStringList header = tableCells(rows.first());
    for (qsizetype c = 0; c < header.size(); ++c)
        out += cellHtml(u"th"_s, header[c], c);
    out += u"</tr></thead><tbody>"_s;
    for (qsizetype r = hasDelimiter ? 2 : 1; r < rows.size(); ++r) {
        out += u"<tr>"_s;
        const QStringList cells = tableCells(rows[r]);
        for (qsizetype c = 0; c < cells.size(); ++c)
            out += cellHtml(u"td"_s, cells[c], c);
        out += u"</tr>"_s;
    }
    return out + u"</tbody></table>"_s;
}

QString HtmlRenderer::renderBlock(const ParsedNote &note, int index) const
{
    const Block &b = note.blocks[index];
    const QString id = b.id.isEmpty() ? QString() : u" id="_s + attr(m_links.idPrefix + blockAnchor(b.id));
    switch (b.kind) {
    case BlockKind::Frontmatter: return {};
    case BlockKind::Heading: {
        const int level = std::clamp(b.headingLevel, 1, 6);
        const QString anchor = m_links.idPrefix + (b.id.isEmpty() ? headingAnchor(b.text) : blockAnchor(b.id));
        return u"<h"_s + QString::number(level) + u" id="_s + attr(anchor) + u'>' + renderInline(b.text) + metaChips(b)
               + u"</h"_s + QString::number(level) + u'>';
    }
    case BlockKind::Paragraph: {
        // A paragraph that is only a page embed shows the embedded page.
        if (const QString embedded = embeddedContent(b.text); !embedded.isEmpty())
            return u"<div class=\"embed\""_s + id + u'>' + embedded + u"</div>"_s;
        return u"<p"_s + id + u'>' + renderInline(b.text) + metaChips(b) + u"</p>"_s;
    }
    case BlockKind::Code: {
        const QString lang = b.lang.section(u' ', 0, 0);
        const QString cls = lang.isEmpty() ? QString() : u" class="_s + attr(u"language-"_s + lang);
        return u"<pre"_s + id + u"><code"_s + cls + u'>' + esc(b.text) + u"</code></pre>"_s;
    }
    case BlockKind::Table: return u"<div class=\"table\""_s + id + u'>' + renderTable(b.text) + u"</div>"_s;
    case BlockKind::Quote: {
        if (!b.children.isEmpty())
            return u"<blockquote"_s + id + u'>' + renderBlocks(note, b.children) + u"</blockquote>"_s;
        QStringList lines;
        for (const QString &line : b.text.split(u'\n')) {
            QStringView v = QStringView(line).trimmed();
            while (v.startsWith(u'>'))
                v = v.sliced(1).trimmed();
            lines.append(v.toString());
        }
        QStringList paragraphs;
        QStringList current;
        for (const QString &l : std::as_const(lines)) {
            if (l.isEmpty()) {
                if (!current.isEmpty())
                    paragraphs.append(u"<p>"_s + renderInline(current.join(u'\n')) + u"</p>"_s);
                current.clear();
            } else {
                current.append(l);
            }
        }
        if (!current.isEmpty())
            paragraphs.append(u"<p>"_s + renderInline(current.join(u'\n')) + u"</p>"_s);
        return u"<blockquote"_s + id + u'>' + paragraphs.join(QString()) + u"</blockquote>"_s;
    }
    case BlockKind::Callout: {
        const QString type = b.properties.value(u"callouttype"_s).toString();
        QString out = u"<div class=\"callout callout-"_s + esc(type.isEmpty() ? u"note"_s : type) + u'"' + id
                      + u"><div class=\"callout-title\">"_s + renderInline(calloutTitle(b)) + u"</div>"_s;
        const QString rest = b.text.section(u'\n', 1);
        if (!rest.trimmed().isEmpty())
            out += u"<p>"_s + renderInline(rest.trimmed()) + u"</p>"_s;
        if (!b.children.isEmpty())
            out += renderBlocks(note, b.children);
        return out + u"</div>"_s;
    }
    case BlockKind::Rule: return u"<hr>"_s;
    case BlockKind::Html: return u"<pre class=\"raw-html\""_s + id + u'>' + esc(b.text) + u"</pre>"_s;
    case BlockKind::ListItem: return u"<ul>"_s + renderListItem(note, index) + u"</ul>"_s;
    }
    return {};
}

} // namespace brulion::io::detail
