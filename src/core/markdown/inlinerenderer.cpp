#include "markdown/inlinerenderer.h"

#include "dates/format.h"
#include "dates/recurrence.h"
#include "markdown/inline_p.h"
#include "markdown/journal.h"
#include "markdown/lines_p.h"
#include "markdown/math.h"
#include "markdown/properties.h"
#include "markdown/structure_p.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "markdown/tokens_p.h"

#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <array>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// Labels of links are rendered with the inline renderer again (emphasis, code); deeper nesting
/// is shown as text.
constexpr int MaxDepth = 2;

/// The block's text a `[[Page#^id]]` link shows after the page name, at most (… when cut).
constexpr qsizetype MaxBlockLinkText = 60;

/// A rendered construct covering [from, to) of the text; an empty html hides the range.
struct Atom {
    qsizetype from = 0;
    qsizetype to = 0;
    QString html;
};

QString esc(QStringView s)
{
    return s.toString().toHtmlEscaped();
}

/// A colour from the palette, restricted to characters of CSS colour values.
QString safeColor(const QString &c)
{
    QString out;
    for (const QChar ch : c) {
        if (ch.isLetterOrNumber() || ch == u'#' || ch == u'(' || ch == u')' || ch == u',' || ch == u'.' || ch == u'%'
            || ch == u' ')
            out += ch;
    }
    return out;
}

QString percentEncoded(QStringView s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s.toString()));
}

/// Chips up to this many UTF-16 code units never wrap; longer ones (long recurrence rules)
/// may wrap between words, so a chip wider than the line is never broken inside a word.
constexpr qsizetype MaxUnbreakableChip = 32;

/// Escaped chip text. Spaces become &nbsp; (a short chip never wraps inside); in a long chip only
/// the space after the leading emoji and after one- or two-letter words ("co", "w", "2") stay
/// non-breaking, the others are break opportunities at word boundaries.
QString chipText(const QString &text)
{
    if (text.size() <= MaxUnbreakableChip)
        return esc(text).replace(u' ', u"&nbsp;"_s);
    QString out;
    const QList<QStringView> words = QStringView(text).split(u' ');
    for (qsizetype k = 0; k < words.size(); ++k) {
        out += esc(words[k]);
        if (k + 1 < words.size())
            out += k == 0 || words[k].size() <= 2 ? u"&nbsp;"_s : u" "_s;
    }
    return out;
}

QString chipStyle(const ChipColors &c)
{
    return u"background-color:"_s + safeColor(c.background) + u";color:"_s + safeColor(c.foreground);
}

QString chip(const ChipColors &c, const QString &text)
{
    return u"<span style=\""_s + chipStyle(c) + u"\">&nbsp;"_s + chipText(text) + u"&nbsp;</span>"_s;
}

/// A clickable chip: the <span> chip inside an anchor without decoration.
QString linkChip(const QString &href, const ChipColors &c, const QString &text)
{
    return u"<a href=\""_s + href.toHtmlEscaped() + u"\" style=\"text-decoration:none\">"_s + chip(c, text) + u"</a>"_s;
}

QString link(const QString &href, const QString &color, const QString &labelHtml)
{
    return u"<a href=\""_s + href.toHtmlEscaped() + u"\" style=\"color:"_s + safeColor(color)
           + u";text-decoration:none\">"_s + labelHtml + u"</a>"_s;
}

QString muted(const Palette &p, const QString &html)
{
    return u"<span style=\"color:"_s + safeColor(p.muted) + u"\">"_s + html + u"</span>"_s;
}

/// Text outside atoms: escaped, line breaks as <br/>.
QString escapeText(QStringView s)
{
    QString out = esc(s);
    if (out.contains(u'\r')) {
        out.replace(u"\r\n"_s, u"\n"_s);
        out.replace(u'\r', u'\n');
    }
    return out.replace(u'\n', u"<br/>"_s);
}

bool allowedExternal(QStringView url)
{
    const qsizetype colon = url.indexOf(u':');
    if (colon <= 0)
        return false;
    const QString scheme = url.first(colon).toString().toLower();
    return scheme == u"http" || scheme == u"https" || scheme == u"mailto";
}

QString externalHref(QStringView url)
{
    return u"ext:"_s + url.toString().replace(u' ', u"%20"_s);
}

/// "#Heading" / "#^id" part of a page: href.
QString fragment(const QString &heading, const QString &blockId)
{
    if (!heading.isEmpty())
        return u'#' + percentEncoded(heading);
    if (!blockId.isEmpty())
        return u"#^"_s + percentEncoded(blockId);
    return {};
}

QString taskGlyph(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"☐"_s;
    case TaskStatus::Doing: return u"◐"_s;
    case TaskStatus::Done: return u"☑"_s;
    case TaskStatus::Cancelled: return u"☒"_s;
    case TaskStatus::Deferred: return u"▷"_s;
    }
    return u"☐"_s;
}

bool isPunctuation(QChar c)
{
    return c.isPunct() || c.isSymbol();
}

// --- emphasis ---------------------------------------------------------------------------------

struct Piece {
    enum class Type { Text, Html, Delim };
    Type type = Type::Text;
    qsizetype from = 0; ///< Text: range in the text
    qsizetype to = 0;
    QString html; ///< Html
    QChar ch;     ///< Delim: '*', '_', '~', '='
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

/// Split the gaps between atoms into text and delimiter runs.
QList<Piece> buildPieces(QStringView text, const QList<Atom> &atoms)
{
    QList<Piece> pieces;
    // Character before / after a gap: atoms count as letters (hidden ones as whitespace).
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
                pieces.append(Piece{Piece::Type::Text, textStart, i, {}, {}, 0, false, false, {}, {}});
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
            pieces.append(Piece{Piece::Type::Text, textStart, b, {}, {}, 0, false, false, {}, {}});
    };
    const QChar space(u' ');
    const QChar letter(u'a');
    qsizetype pos = 0;
    QChar before = space;
    for (const Atom &atom : atoms) {
        addGap(pos, atom.from, before, atom.html.isEmpty() ? space : letter);
        pieces.append(Piece{Piece::Type::Html, atom.from, atom.to, atom.html, {}, 0, false, false, {}, {}});
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

/// CommonMark-style delimiter matching (without the "rule of 3"); tags nest properly. A failed
/// search for an opener of some character remembers how far down it looked ("openers bottom"),
/// so hostile input with thousands of unmatched delimiters stays linear.
void processEmphasis(QList<Piece> &pieces, const Palette &palette)
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
                    open = use == 2 ? u"<b>"_s : u"<i>"_s;
                    close = use == 2 ? u"</b>"_s : u"</i>"_s;
                } else if (d.ch == u'~') {
                    open = u"<s>"_s;
                    close = u"</s>"_s;
                } else {
                    open = u"<span style=\"background-color:"_s + safeColor(palette.highlightBackground) + u"\">"_s;
                    close = u"</span>"_s;
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

bool isOpening(QChar c)
{
    const auto cat = c.category();
    return cat == QChar::Punctuation_Open || cat == QChar::Punctuation_InitialQuote || c == u'"' || c == u'\'';
}

/// Polish typography: a one-letter word (a, i, o, u, w, z) never ends a line — the space after
/// it becomes a no-break space when a word follows (a letter, a digit, an opening bracket or
/// quote, a link, emphasis or a formula: "w [[Domu]]", "i **dalej**", "i $x$"; not "a = b").
/// `i` is the position of a space in `text`.
bool joinsNextWord(QStringView text, qsizetype i)
{
    if (i < 1 || i + 1 >= text.size() || text[i] != u' ')
        return false;
    static const QString letters = QStringLiteral("aiouwzAIOUWZ");
    if (!letters.contains(text[i - 1]))
        return false;
    // A word of its own: at the start, after a space or an opening bracket / quote.
    if (i >= 2 && !text[i - 2].isSpace() && !isOpening(text[i - 2]))
        return false;
    const QChar next = text[i + 1];
    static const QString markup = QStringLiteral("*_[`!#$");
    return next.isLetterOrNumber() || isOpening(next) || markup.contains(next);
}

/// Text piece [from, to) of `text`: escaped, line breaks as <br/>, no-break spaces after
/// one-letter words (joinsNextWord()). `nextShown`: what follows the piece is visible (not
/// hidden syntax such as an embed shown in a frame), so a space at its end may join it.
QString textPiece(QStringView text, qsizetype from, qsizetype to, bool nextShown)
{
    const QStringView piece = text.sliced(from, to - from);
    if (!piece.contains(u' '))
        return escapeText(piece);
    QString joined = piece.toString();
    for (qsizetype i = from; i < to; ++i) {
        if ((i + 1 < to || nextShown) && joinsNextWord(text, i))
            joined[i - from] = QChar(0x00A0);
    }
    return escapeText(joined);
}

QString assemble(QStringView text, const QList<Piece> &pieces)
{
    QString out;
    for (qsizetype k = 0; k < pieces.size(); ++k) {
        const Piece &p = pieces[k];
        switch (p.type) {
        case Piece::Type::Text: {
            const bool nextShown =
                    k + 1 < pieces.size() && !(pieces[k + 1].type == Piece::Type::Html && pieces[k + 1].html.isEmpty());
            out += textPiece(text, p.from, p.to, nextShown);
            break;
        }
        case Piece::Type::Html: out += p.html; break;
        case Piece::Type::Delim: out += p.closeTags + QString(p.remaining, p.ch) + p.openTags; break;
        }
    }
    while (out.endsWith(u"<br/>"))
        out.chop(5);
    while (out.startsWith(u"<br/>"))
        out.remove(0, 5);
    return out;
}

/// The recurrence rule of a 🔁 token and where its text ends: the whole value when it parses,
/// else the longest run of leading words that does ("co tydzień" of "co tydzień kupić mleko");
/// nullopt when no rule starts there (the token is shown as text).
std::optional<std::pair<dates::RecurrenceRule, qsizetype>> recurrencePrefix(QStringView masked, const Token &tok)
{
    if (tok.valueFrom < 0 || tok.valueTo <= tok.valueFrom)
        return std::nullopt;
    const QStringView value = masked.sliced(tok.valueFrom, tok.valueTo - tok.valueFrom);
    if (const auto rule = dates::parseRecurrence(value))
        return std::pair{*rule, tok.valueTo};
    // Ends of the leading words (at most a dozen: no rule is longer).
    constexpr int MaxRuleWords = 12;
    QList<qsizetype> ends;
    for (qsizetype i = 0; i < value.size() && ends.size() < MaxRuleWords;) {
        while (i < value.size() && value[i].isSpace())
            ++i;
        qsizetype j = i;
        while (j < value.size() && !value[j].isSpace())
            ++j;
        if (j > i)
            ends.append(j);
        i = j;
    }
    for (qsizetype k = ends.size() - 1; k >= 0; --k) {
        QStringView candidate = value.first(ends[k]);
        // "co tydzień, kupić" -> "co tydzień": punctuation glued to the last word is not rule text.
        while (!candidate.isEmpty() && (candidate.back() == u',' || candidate.back() == u';' || candidate.back() == u':'))
            candidate.chop(1);
        if (const auto rule = dates::parseRecurrence(candidate))
            return std::pair{*rule, tok.valueFrom + candidate.size()};
    }
    return std::nullopt;
}

} // namespace

std::function<std::optional<QString>(const QString &)> journalLinkTitle(const VaultConfig &config,
                                                                        dates::UiLocale locale, QDate today)
{
    const std::optional<int> year = today.isValid() ? std::optional(today.year()) : std::nullopt;
    return [config, locale, year](const QString &target) -> std::optional<QString> {
        const auto day = journalDateFromLinkTarget(target, config);
        if (!day)
            return std::nullopt;
        // An invalid `today`: the year is always shown (a year no date has). A date never
        // breaks across lines: no-break spaces.
        return dates::formatDateShort(*day, locale, year.value_or(0)).replace(u' ', QChar(0x00A0));
    };
}

InlineRenderer::InlineRenderer(RenderContext context) : m_ctx(std::move(context)) { }

QString InlineRenderer::escape(QStringView text)
{
    return escapeText(text);
}

QString InlineRenderer::render(QStringView text) const
{
    return renderInline(text, true, 0);
}

namespace {

/// Position in the block body of position `p` of a text built from verbatim runs of it.
qsizetype toBody(const QList<std::pair<qsizetype, qsizetype>> &runs, qsizetype p)
{
    if (runs.isEmpty())
        return p;
    auto it = std::upper_bound(runs.begin(), runs.end(), p,
                               [](qsizetype v, const std::pair<qsizetype, qsizetype> &r) { return v < r.first; });
    if (it != runs.begin())
        --it;
    return it->second + (p - it->first);
}

} // namespace

QString InlineRenderer::renderInline(QStringView text, bool topLevel, int depth, const SourceRuns &runs,
                                     bool taskMarker) const
{
    if (text.isEmpty())
        return {};
    if (depth > MaxDepth)
        return escapeText(text);
    const Palette &pal = m_ctx.palette;
    QList<Atom> atoms;

    // Code spans and comments.
    const QList<InlineSpan> spans = inlineSpans(text);
    QString masked = text.toString();
    for (const InlineSpan &span : spans) {
        for (qsizetype i = span.from; i < span.to; ++i) {
            if (!isLineBreak(masked[i]))
                masked[i] = u' ';
        }
        if (span.kind != InlineSpan::Kind::Code) {
            atoms.append({span.from, span.to, QString()});
            continue;
        }
        QString content = text.sliced(span.from + span.fence, span.to - span.from - 2 * span.fence).toString();
        content.replace(u"\r\n"_s, u" "_s).replace(u'\n', u' ').replace(u'\r', u' ');
        if (content.size() >= 2 && content.startsWith(u' ') && content.endsWith(u' ') && !content.trimmed().isEmpty())
            content = content.sliced(1, content.size() - 2);
        const QString trimmed = content.trimmed();
        if (m_ctx.inlineCalc && trimmed.startsWith(u'=')) {
            const QString expr = trimmed.sliced(1).trimmed();
            const QString result = expr.isEmpty() ? QString() : m_ctx.inlineCalc(expr);
            if (!result.isEmpty()) {
                atoms.append({span.from, span.to, chip(pal.neutral, result)});
                continue;
            }
        }
        atoms.append({span.from, span.to,
                      u"<code style=\"background-color:"_s + safeColor(pal.codeBackground) + u"\">"_s + esc(content)
                              + u"</code>"_s});
    }

    // Formulas: $…$ and $$…$$ (docs/SYNTAX.md §11, markdown/math.h). The scan runs
    // on the masked text, so a formula inside a code span or a comment is not one; like emphasis,
    // a formula works in a link label too.
    for (const MathSpan &span : findMathSpans(masked)) {
        atoms.append({span.from, span.to,
                      renderMath(text.sliced(span.contentFrom, span.contentTo - span.contentFrom), span.display)});
    }

    std::optional<TaskStatus> taskStatus;
    if (topLevel) {
        // Line-level syntax: hidden lines (`id::`, `collapsed::`, drawers), property keys, block ids.
        const QList<text::Line> lines = text::splitLines(masked);
        bool inDrawer = false;
        for (qsizetype k = 0; k < lines.size(); ++k) {
            const text::Line &line = lines[k];
            const qsizetype indent = indentOf(line.text).chars;
            const QStringView content = QStringView(line.text).sliced(indent);
            bool hidden = false;
            if (inDrawer) {
                hidden = true;
                inDrawer = !isDrawerEnd(content);
            } else if (k > 0 && isDrawerStart(content) && !isDrawerEnd(content)) {
                hidden = true;
                inDrawer = true;
            } else if (const auto pm = matchPropertyLine(content)) {
                const QString key = pm->key.toLower();
                if (key == u"id" || key == u"collapsed") {
                    hidden = true;
                } else {
                    atoms.append(
                            {line.start + indent, line.start + indent + pm->valueFrom,
                             muted(pal, esc(pm->key) + u':') + (pm->valueFrom == pm->valueTo ? QString() : u" "_s)});
                }
            }
            if (hidden) {
                atoms.append({line.start, line.start + line.text.size() + line.eol.size(), QString()});
                continue;
            }
            if (const auto id = blockIdAtEnd(line.text))
                atoms.append({line.start + id->from, line.start + id->to, QString()});
        }

        // Leading task marker.
        if (const auto marker = taskMarker ? matchTaskMarker(text, masked, 0) : std::nullopt) {
            taskStatus = marker->status;
            QString html;
            if (m_ctx.taskCheckbox) {
                const bool closed = marker->status == TaskStatus::Done || marker->status == TaskStatus::Cancelled;
                html = link(u"task:toggle"_s, closed ? pal.done.foreground : pal.link, taskGlyph(marker->status))
                       + u"&nbsp;"_s;
            }
            atoms.append({marker->from, marker->contentStart, html});
        }

        // Links, embeds, block references, URLs, tags. A page link shows its target as written,
        // or what RenderContext::linkTitle makes of it (daily notes by their date).
        const auto shownTarget = [this](const QString &target) {
            if (target.isEmpty() || !m_ctx.linkTitle)
                return target;
            return m_ctx.linkTitle(target).value_or(target);
        };
        lexInline(text, masked, [&](const InlineItem &item) {
            const QStringView source = text.sliced(item.from, item.to - item.from);
            QString html;
            switch (item.kind) {
            case InlineKind::Wiki: {
                const WikiParts &w = item.wiki;
                QString label = w.display;
                if (label.isEmpty()) {
                    QString frag = !w.heading.isEmpty()   ? w.heading
                                   : !w.blockId.isEmpty() ? u'^' + w.blockId
                                                          : QString();
                    // [[Page#^id]] reads like ((id)): "Page › the block's text…".
                    if (w.heading.isEmpty() && !w.blockId.isEmpty() && m_ctx.blockText) {
                        if (const auto blockText = m_ctx.blockText(w.blockId)) {
                            const QString shown = blockText->simplified();
                            if (!shown.isEmpty())
                                frag = shown.size() > MaxBlockLinkText
                                               ? shown.first(MaxBlockLinkText - 1).trimmed() + u'…'
                                               : shown;
                        }
                    }
                    const QString page = shownTarget(w.target);
                    label = page.isEmpty() ? frag : frag.isEmpty() ? page : page + u" › "_s + frag;
                }
                const bool resolved =
                        w.target.isEmpty() || !m_ctx.resolvePage || m_ctx.resolvePage(w.target).has_value();
                html = link(u"page:"_s + percentEncoded(w.target) + fragment(w.heading, w.blockId),
                            resolved ? pal.link : pal.unresolved, esc(label));
                break;
            }
            case InlineKind::Embed: {
                const WikiParts &w = item.wiki;
                const QString ext =
                        w.target.contains(u'.') ? w.target.sliced(w.target.lastIndexOf(u'.')).toLower() : QString();
                const bool attachment = !ext.isEmpty() && ext != u".md" && !ext.contains(u'/');
                if (!attachment && m_ctx.hideTransclusions
                    && (!w.target.isEmpty() || !w.heading.isEmpty() || !w.blockId.isEmpty()))
                    break; // shown next to the text by the caller
                QString name = attachment ? w.target : shownTarget(w.target);
                if (!w.heading.isEmpty())
                    name += u" › "_s + w.heading;
                else if (!w.blockId.isEmpty())
                    name += u" › ^"_s + w.blockId;
                const bool resolved =
                        w.target.isEmpty() || !m_ctx.resolvePage || m_ctx.resolvePage(w.target).has_value();
                html = link(u"page:"_s + percentEncoded(w.target) + fragment(w.heading, w.blockId),
                            resolved ? pal.link : pal.unresolved,
                            esc(QString((attachment ? u"📎 "_s : u"⧉ "_s) + name)));
                break;
            }
            case InlineKind::Markdown:
            case InlineKind::Image: {
                const MdDestination &dest = item.dest;
                const QStringView labelText = text.sliced(item.md.textFrom, item.md.textTo - item.md.textFrom);
                QString label = renderInline(labelText, false, depth + 1);
                if (label.isEmpty())
                    label = esc(dest.target);
                if (item.kind == InlineKind::Image)
                    label = u"🖼&nbsp;"_s + label;
                if (hasUrlScheme(dest.target)) {
                    html = allowedExternal(dest.target) ? link(externalHref(dest.target), pal.link, label)
                                                        : escapeText(source);
                } else {
                    const bool resolved =
                            dest.target.isEmpty() || !m_ctx.resolvePage || m_ctx.resolvePage(dest.target).has_value();
                    html = link(u"page:"_s + percentEncoded(dest.target) + fragment(dest.heading, dest.blockId),
                                resolved ? pal.link : pal.unresolved, label);
                }
                break;
            }
            case InlineKind::BlockRef:
            case InlineKind::BlockEmbed: {
                if (item.kind == InlineKind::BlockEmbed && m_ctx.hideTransclusions)
                    break; // shown next to the text by the caller
                const auto blockText = m_ctx.blockText ? m_ctx.blockText(item.target) : std::nullopt;
                const QString prefix = item.kind == InlineKind::BlockEmbed ? u"⧉ "_s : QString();
                html = link(u"block:"_s + item.target, blockText ? pal.link : pal.unresolved,
                            esc(QString(prefix + (blockText ? *blockText : u"(("_s + item.target + u"))"_s))));
                break;
            }
            case InlineKind::Url:
            case InlineKind::Autolink: {
                const QString shown =
                        item.kind == InlineKind::Url ? item.target : source.sliced(1, source.size() - 2).toString();
                html = allowedExternal(item.target) ? link(externalHref(item.target), pal.link, esc(shown))
                                                    : escapeText(source);
                break;
            }
            case InlineKind::Tag:
                html = u"<a href=\"tag:"_s + percentEncoded(item.target) + u"\" style=\"color:"_s
                       + safeColor(pal.tag.foreground) + u";background-color:"_s + safeColor(pal.tag.background)
                       + u";text-decoration:none\">#"_s + esc(item.target) + u"</a>"_s;
                break;
            }
            atoms.append({item.from, item.to, html});
        });

        // Metadata tokens as chips.
        const auto human = [&](const DateSpec &spec) { return dates::formatHuman(spec, m_ctx.locale, m_ctx.today); };
        const bool open = taskStatus && isOpenStatus(*taskStatus);
        const auto dueChip = [&](const DateSpec &spec) {
            const bool overdue = open && m_ctx.today.isValid() && spec.date < m_ctx.today;
            return chip(overdue ? pal.overdue : pal.due, u"📅 "_s + human(spec));
        };
        for (const Token &tok : findTokens(masked)) {
            const QStringView source = text.sliced(tok.from, tok.to - tok.from);
            QString html;
            qsizetype to = tok.to;
            switch (tok.kind) {
            case TokenKind::Due: html = dueChip(tok.spec()); break;
            case TokenKind::Scheduled: html = chip(pal.scheduled, u"⏳ "_s + human(tok.spec())); break;
            case TokenKind::Start: html = chip(pal.neutral, u"🛫 "_s + human(tok.spec())); break;
            case TokenKind::Created: html = chip(pal.neutral, u"➕ "_s + human(tok.spec())); break;
            case TokenKind::Done: html = chip(pal.done, u"✅ "_s + human(tok.spec())); break;
            case TokenKind::Cancelled: html = chip(pal.done, u"❌ "_s + human(tok.spec())); break;
            case TokenKind::Recurrence: {
                // Only a rule that parses is a chip (the longest one at the start of the text
                // after 🔁: "🔁 co tydzień kupić mleko" -> chip + text); anything else stays text.
                const auto rule = recurrencePrefix(masked, tok);
                if (!rule)
                    continue;
                html = chip(pal.recurrence, u"🔁 "_s + dates::formatRecurrence(rule->first, m_ctx.locale));
                to = rule->second;
                break;
            }
            case TokenKind::Reminder:
            case TokenKind::ReminderCompat:
                // docs/UI.md: ⏰ reminders, a bell for alarms.
                html = linkChip(u"reminder:"_s + QString::number(toBody(runs, tok.from)) + u'-'
                                        + QString::number(toBody(runs, tok.to)),
                                tok.alarm ? pal.alarm : pal.reminder,
                                (tok.alarm ? u"🔔 "_s : u"⏰ "_s) + human(tok.spec()));
                break;
            case TokenKind::Priority: html = esc(source); break;
            case TokenKind::LogseqPriority: html = chip(pal.neutral, source.sliced(2, 1).toString()); break;
            case TokenKind::TaskId:
            case TokenKind::DependsOn:
            case TokenKind::OnCompletion: html = muted(pal, esc(source)); break;
            case TokenKind::Planning:
                html = tok.field == TokenKind::Due ? dueChip(tok.spec())
                                                   : chip(pal.scheduled, u"⏳ "_s + human(tok.spec()));
                if (const auto rule = dates::repeaterToRule(tok.repeater))
                    html += u' ' + chip(pal.recurrence, u"🔁 "_s + dates::formatRecurrence(*rule, m_ctx.locale));
                break;
            }
            atoms.append({tok.from, to, html});
        }

        // Footnote references [^1].
        for (qsizetype i = masked.indexOf(u"[^"); i >= 0; i = masked.indexOf(u"[^", i + 2)) {
            qsizetype j = i + 2;
            while (j < masked.size() && masked[j] != u']' && !masked[j].isSpace())
                ++j;
            if (j > i + 2 && charAt(masked, j) == u']' && charAt(masked, j + 1) != u':')
                atoms.append({i, j + 1, u"<sup>"_s + esc(QStringView(text).sliced(i + 2, j - i - 2)) + u"</sup>"_s});
        }
    }

    // Backslash escapes; a backslash at the end of a line is a hard line break (hidden).
    for (qsizetype i = 0; i + 1 < masked.size(); ++i) {
        if (masked[i] == u'\\' && isAsciiPunct(masked[i + 1])) {
            atoms.append({i, i + 2, esc(QStringView(text).sliced(i + 1, 1))});
            ++i;
        } else if (masked[i] == u'\\' && isLineBreak(masked[i + 1])) {
            atoms.append({i, i + 1, QString()});
        }
    }

    // The earliest (then longest) atom wins; atoms overlapping a kept one are dropped.
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
    processEmphasis(pieces, m_ctx.palette);
    return assemble(text, pieces);
}

QString InlineRenderer::renderBlock(QStringView body, BlockKind kind) const
{
    const QList<text::Line> lines = text::splitLines(body);
    // The lines from `from` on without their quote prefixes (bullets shown as "•"), plus the
    // runs mapping the result back to the body.
    const auto stripQuotes = [&](qsizetype from, SourceRuns &runs) {
        // Lines of a display formula between `$$` lines (SYNTAX §11) keep their "- " / "+ ".
        QList<bool> formula(lines.size(), false);
        const auto unquoted = [&](qsizetype k) {
            const QStringView t = QStringView(lines[k].text);
            return t.sliced(quotePrefixOf(t).length).trimmed();
        };
        for (qsizetype k = from; k < lines.size(); ++k) {
            if (unquoted(k) != u"$$")
                continue;
            qsizetype j = k + 1;
            while (j < lines.size() && !unquoted(j).isEmpty() && unquoted(j) != u"$$")
                ++j;
            if (j == lines.size() || unquoted(j) != u"$$")
                continue; // no closing line before a blank one: text
            for (qsizetype m = k + 1; m < j; ++m)
                formula[m] = true;
            k = j;
        }
        QString out;
        for (qsizetype k = from; k < lines.size(); ++k) {
            if (k > from)
                out += u'\n';
            QStringView t = QStringView(lines[k].text);
            const qsizetype q = quotePrefixOf(t).length;
            t = t.sliced(q);
            const qsizetype lineStart = lines[k].start + q;
            runs.append({out.size(), lineStart});
            if (const auto lm = formula[k] ? std::nullopt : matchListMarker(t); lm && !lm->ordered) {
                out += t.first(lm->indent.chars) + u"• "_s;
                runs.append({out.size(), lineStart + lm->contentStart});
                out += t.sliced(lm->contentStart);
            } else {
                out += t;
            }
        }
        return out;
    };
    switch (kind) {
    case BlockKind::ListItem: return render(body);
    case BlockKind::Paragraph: return renderInline(body, true, 0, {}, false);
    case BlockKind::Heading: {
        const QString first = lines.first().text;
        if (const auto atx = matchAtxHeading(first)) {
            QString title = first.sliced(atx->prefixLength);
            // Closing sequence: `(?:^|[ \t]+)#+[ \t]*$`
            qsizetype e = trimEndBlanks(title).size();
            qsizetype k = e;
            while (k > 0 && title[k - 1] == u'#')
                --k;
            if (k < e && (k == 0 || isSpaceOrTab(title[k - 1])))
                title = trimEndBlanks(QStringView(title).first(k)).toString();
            return renderInline(title, true, 0, {{0, atx->prefixLength}}, false);
        }
        // Setext: the lines above the underline (line breaks normalised to "\n").
        QString text;
        SourceRuns runs;
        for (qsizetype k = 0; k + 1 < lines.size(); ++k) {
            if (k > 0)
                text += u'\n';
            runs.append({text.size(), lines[k].start});
            text += lines[k].text;
        }
        return renderInline(runs.isEmpty() ? body : QStringView(text), true, 0, runs, false);
    }
    case BlockKind::Quote: {
        SourceRuns runs;
        const QString text = stripQuotes(0, runs);
        return renderInline(text, true, 0, runs, false);
    }
    case BlockKind::Callout: {
        QStringView first = QStringView(lines.first().text);
        const qsizetype q = quotePrefixOf(first).length;
        first = first.sliced(q);
        QString title;
        qsizetype titleStart = q;
        if (const auto header = matchCalloutHeader(first)) {
            title = first.sliced(header->titleStart).toString();
            titleStart += header->titleStart;
            if (title.trimmed().isEmpty() && !header->type.isEmpty())
                title = header->type.first(1).toUpper() + header->type.sliced(1);
        } else {
            title = first.toString();
        }
        SourceRuns runs;
        const QString rest = stripQuotes(1, runs);
        const QString restHtml = renderInline(rest, true, 0, runs, false);
        QString out = u"<b>"_s + renderInline(title, true, 0, {{0, titleStart}}, false) + u"</b>"_s;
        if (!restHtml.isEmpty())
            out += u"<br/>"_s + restHtml;
        return out;
    }
    case BlockKind::Rule: return u"<hr/>"_s;
    case BlockKind::Html: return escapeText(body);
    case BlockKind::Code:
    case BlockKind::Table:
    case BlockKind::Frontmatter: return u"<pre>"_s + esc(body) + u"</pre>"_s;
    }
    return escapeText(body);
}

} // namespace brulion::markdown
