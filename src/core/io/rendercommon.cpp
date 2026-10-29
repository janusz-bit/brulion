#include "io/rendercommon_p.h"

#include "util/paths.h"

#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <array>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

// --- emphasis (CommonMark-like delimiter runs, as in markdown::InlineRenderer) -------------------

struct Piece {
    enum class Type { Text, Atom, Delim };
    Type type = Type::Text;
    qsizetype from = 0;
    qsizetype to = 0;
    QString out;
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

QList<Piece> buildPieces(QStringView text, const QList<InlineAtom> &atoms)
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
    for (const InlineAtom &atom : atoms) {
        addGap(pos, atom.from, before, atom.out.isEmpty() ? space : letter);
        Piece h;
        h.type = Piece::Type::Atom;
        h.from = atom.from;
        h.to = atom.to;
        h.out = atom.out;
        pieces.append(h);
        pos = atom.to;
        before = atom.out.isEmpty() ? space : letter;
    }
    addGap(pos, text.size(), before, space);
    return pieces;
}

int delimSlot(QChar c)
{
    return c == u'*' ? 0 : c == u'_' ? 1 : c == u'~' ? 2 : 3;
}

void processEmphasis(QList<Piece> &pieces, const EmphasisMarks &marks)
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
                const auto [open, close] = marks(d.ch, use);
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

} // namespace

QString renderAtoms(QStringView text, QList<InlineAtom> atoms, const EmphasisMarks &marks, const TextOutput &plain)
{
    std::sort(atoms.begin(), atoms.end(), [](const InlineAtom &a, const InlineAtom &b) {
        return a.from != b.from ? a.from < b.from : a.to > b.to;
    });
    QList<InlineAtom> kept;
    qsizetype end = 0;
    for (InlineAtom &a : atoms) {
        if (a.from >= end && a.to > a.from) {
            end = a.to;
            kept.append(std::move(a));
        }
    }
    QList<Piece> pieces = buildPieces(text, kept);
    processEmphasis(pieces, marks);
    QString out;
    for (const Piece &p : pieces) {
        switch (p.type) {
        case Piece::Type::Text: out += plain(text.sliced(p.from, p.to - p.from)); break;
        case Piece::Type::Atom: out += p.out; break;
        case Piece::Type::Delim: out += p.closeTags + plain(QString(p.remaining, p.ch)) + p.openTags; break;
        }
    }
    return out;
}

void codeAndCommentAtoms(QStringView text, QStringView masked, QList<InlineAtom> &atoms,
                         const std::function<QString(const QString &content)> &code)
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
            atoms.append(InlineAtom{i, close + n, code(content)});
            i = close + n;
        } else if (text.sliced(i).startsWith(u"%%")) {
            const qsizetype close = text.indexOf(u"%%", i + 2);
            const qsizetype end = close < 0 ? text.size() : close + 2;
            atoms.append(InlineAtom{i, end, QString()});
            i = end;
        } else if (text.sliced(i).startsWith(u"<!--")) {
            const qsizetype close = text.indexOf(u"-->", i + 4);
            const qsizetype end = close < 0 ? text.size() : close + 3;
            atoms.append(InlineAtom{i, end, QString()});
            i = end;
        } else {
            ++i;
        }
    }
}

bool isAsciiPunct(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 0x21 && u <= 0x2F) || (u >= 0x3A && u <= 0x40) || (u >= 0x5B && u <= 0x60) || (u >= 0x7B && u <= 0x7E);
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

bool isWebPictureTarget(QStringView target)
{
    const QString text = target.trimmed().toString();
    if (!text.startsWith(u"http://", Qt::CaseInsensitive) && !text.startsWith(u"https://", Qt::CaseInsensitive))
        return false;
    const QUrl url(text, QUrl::TolerantMode);
    return url.isValid() && !url.host().isEmpty();
}

MediaKind mediaKind(const QString &path)
{
    static const QStringList audio = {u".mp3"_s, u".m4a"_s, u".wav"_s, u".ogg"_s, u".opus"_s, u".flac"_s, u".aac"_s};
    static const QStringList video = {u".mp4"_s, u".webm"_s, u".mov"_s, u".m4v"_s};
    // The pictures the app shows as pictures (one list: paths::pictureExtensions()).
    if (paths::isPicturePath(path))
        return MediaKind::Image;
    const QString ext = paths::extension(path);
    if (audio.contains(ext))
        return MediaKind::Audio;
    if (video.contains(ext))
        return MediaKind::Video;
    return MediaKind::Other;
}

bool isAttachmentTarget(const QString &target)
{
    const QString ext = paths::extension(target);
    return !ext.isEmpty() && ext != u".md" && ext != u".markdown";
}

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

bool isOrderedItem(const Block &b)
{
    const QStringView first = b.raw.view().trimmed();
    qsizetype i = 0;
    while (i < first.size() && first[i].isDigit())
        ++i;
    return i > 0 && i < first.size() && (first[i] == u'.' || first[i] == u')');
}

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

std::optional<std::pair<QString, QString>> itemFence(const QString &text)
{
    // The opening fence (``` or ~~~, 3 or more, any indentation, no backtick in a backtick
    // fence's info), and the last line a closing fence of the same character at least as long —
    // or more lines after an unclosed one.
    const QStringList lines = text.split(u'\n');
    const QString first = lines.first().trimmed();
    const QChar marker = first.isEmpty() ? QChar() : first.front();
    if (marker != u'`' && marker != u'~')
        return std::nullopt;
    qsizetype length = 0;
    while (length < first.size() && first[length] == marker)
        ++length;
    const QString info = first.sliced(length).trimmed();
    if (length < 3 || (marker == u'`' && info.contains(u'`')))
        return std::nullopt;
    const QString last = lines.last().trimmed();
    qsizetype closing = 0;
    while (closing < last.size() && last[closing] == marker)
        ++closing;
    const bool closed = lines.size() >= 2 && closing >= length && closing == last.size();
    if (!closed && lines.size() < 2)
        return std::nullopt; // a lone opening fence is text
    QString lang = info.section(u' ', 0, 0, QString::SectionSkipEmpty).toLower();
    if (lang.startsWith(u'{'))
        lang.clear();
    return std::pair{lang, lines.mid(1, lines.size() - (closed ? 2 : 1)).join(u'\n')};
}

bool isMathFence(const QString &lang)
{
    return lang == u"math" || lang == u"latex" || lang == u"tex";
}

} // namespace brulion::io::detail
