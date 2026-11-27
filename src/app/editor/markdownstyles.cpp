#include "markdownstyles.h"

#include "markdown/cloze.h"
#include "markdown/inline.h"
#include "markdown/tokens.h"
#include "syntax/luau.h"
#include "text/text.h"

#include <QRegularExpression>
#include <QVarLengthArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// A line's state (the low bits of LineStyles::state): what it leaves open in Source mode.
enum State { Normal = 0, Fence = 1, Frontmatter = 2, Comment = 3 };
constexpr int StateMask = 0x7;
/// A fence of Luau code (source mode): its lines are coloured as Luau, the lexer's state above it.
constexpr int LuauFence = 1 << 12;
constexpr int LuauShift = 13;
/// A line of a display formula between `$$` lines (source mode; in a Normal state, whose fence
/// bits are free): no prose.
constexpr int MathFlag = 1 << 4;

/// A line that is only `$$` (in a list item or a quote too; not after a task box): it opens or
/// closes a display formula (docs/SYNTAX.md "Formulas").
bool isMathFence(const QString &line)
{
    static const QRegularExpression re(u"^\\s*(?:>\\s?)*\\s*(?:(?:[-*+]|\\d{1,9}[.)])\\s+)?\\$\\$\\s*$"_s);
    return line.contains(u"$$"_s) && re.matchView(line).hasMatch();
}

int encodeFence(QChar ch, int length)
{
    return 1 | (ch == u'~' ? 0x8 : 0) | (std::min(length, 255) << 4);
}

/// The language of a fence line's info string ("```luau title" → "luau").
QStringView fenceLanguage(QStringView line, qsizetype markerEnd)
{
    QStringView info = line.sliced(markerEnd).trimmed();
    if (info.startsWith(u'{'))
        info = info.sliced(1);
    if (info.startsWith(u'.'))
        info = info.sliced(1);
    qsizetype end = 0;
    while (end < info.size() && !info[end].isSpace() && info[end] != u'}')
        ++end;
    return info.first(end);
}

MarkdownStyle luauStyle(syntax::LuauToken kind)
{
    switch (kind) {
    case syntax::LuauToken::Keyword: return MarkdownStyle::LuauKeyword;
    case syntax::LuauToken::Constant: return MarkdownStyle::LuauConstant;
    case syntax::LuauToken::Builtin: return MarkdownStyle::LuauBuiltin;
    case syntax::LuauToken::Api: return MarkdownStyle::LuauApi;
    case syntax::LuauToken::Function: return MarkdownStyle::LuauFunction;
    case syntax::LuauToken::Number: return MarkdownStyle::LuauNumber;
    case syntax::LuauToken::String: return MarkdownStyle::LuauString;
    case syntax::LuauToken::Interpolation: return MarkdownStyle::LuauInterpolation;
    case syntax::LuauToken::Comment: return MarkdownStyle::LuauComment;
    }
    return MarkdownStyle::LuauKeyword;
}

MarkdownStyle tokenStyle(const markdown::Token &t)
{
    switch (t.kind) {
    case markdown::TokenKind::Due: return MarkdownStyle::Due;
    case markdown::TokenKind::Scheduled: return MarkdownStyle::Scheduled;
    case markdown::TokenKind::Start: return MarkdownStyle::Start;
    case markdown::TokenKind::Created:
    case markdown::TokenKind::Done:
    case markdown::TokenKind::Cancelled: return MarkdownStyle::Done;
    case markdown::TokenKind::Recurrence: return MarkdownStyle::Recurrence;
    case markdown::TokenKind::Priority:
    case markdown::TokenKind::LogseqPriority: return MarkdownStyle::Priority;
    case markdown::TokenKind::Reminder:
    case markdown::TokenKind::ReminderCompat: return t.alarm ? MarkdownStyle::Alarm : MarkdownStyle::Reminder;
    case markdown::TokenKind::Planning:
        return t.field == markdown::TokenKind::Due ? MarkdownStyle::Due : MarkdownStyle::Scheduled;
    case markdown::TokenKind::TaskId:
    case markdown::TokenKind::DependsOn:
    case markdown::TokenKind::OnCompletion: return MarkdownStyle::Muted;
    }
    return MarkdownStyle::Muted;
}

/// One line's styles being found (styleMarkdownLine()).
class Styler {
public:
    Styler(const QString &line, const StyleOptions &options, LineStyles &out)
        : m_line(line), m_options(options), m_out(out)
    { }

    void run(int previousState);

private:
    /// QSyntaxHighlighter::setFormat: the range takes the style's format.
    void set(int from, int length, MarkdownStyle style, StyleModifier modifier = StyleModifier::None)
    {
        if (length > 0)
            m_out.ops.append({from, length, style, false, modifier, markdown::HighlightColor::Default});
    }
    /// The style's format merged into the range's formats.
    void merge(int from, int length, MarkdownStyle style,
               markdown::HighlightColor tint = markdown::HighlightColor::Default)
    {
        if (length > 0)
            m_out.ops.append({from, length, style, true, StyleModifier::None, tint});
    }
    /// The format at `position` so far has the name of `style` (the last operation over it gives
    /// the name, as QSyntaxHighlighter::format() reads it back).
    bool styledAs(int position, MarkdownStyle style) const
    {
        for (auto it = m_out.ops.crbegin(); it != m_out.ops.crend(); ++it) {
            if (it->from <= position && position < it->from + it->length)
                return it->style == style;
        }
        return false;
    }
    /// Luau's colours over the line, starting in the packed lexer state `state`; returns the state
    /// the next line starts in.
    int luau(int state);
    /// The inline rules over [offset, offset + length) of the line (offset 0: the line-level ones
    /// too: quotes, callouts, list markers, checkboxes, task keywords, headings, properties).
    void inlineSpans(int offset, int length);

    const QString &m_line;
    const StyleOptions &m_options;
    LineStyles &m_out;
};

void Styler::run(int previousState)
{
    const QString &line = m_line;
    const int prev = previousState < 0 ? Normal : previousState;
    m_out.state = Normal;
    const int size = int(line.size());
    // The whole line is prose unless said otherwise below.
    m_out.spell = SpellRange();

    if (m_options.mode == MarkdownMode::Code) {
        m_out.spell.from = -1;
        if (m_options.luau) {
            m_out.state = luau(prev);
            return;
        }
        static const QRegularExpression commentRe(u"^\\s*(#|//).*$"_s);
        if (commentRe.match(line).hasMatch())
            set(0, size, MarkdownStyle::Comment);
        return;
    }

    const bool source = m_options.mode == MarkdownMode::Source;
    if (source) {
        const int kind = prev & StateMask;
        const QStringView trimmed = QStringView(line).trimmed();
        static const QRegularExpression fenceRe(u"^\\s{0,3}(`{3,}|~{3,})"_s);
        const auto fm = fenceRe.match(line);
        // Fences, code, the frontmatter, a comment over lines: no prose (set again below).
        m_out.spell.from = -1;
        if (kind == Fence) {
            const QChar ch = (prev & 0x8) ? u'~' : u'`';
            const int len = (prev >> 4) & 0xff;
            if (fm.hasMatch() && fm.capturedView(1).front() == ch && fm.capturedLength(1) >= len
                && trimmed.size() == fm.capturedLength(1)) {
                set(0, size, MarkdownStyle::Fence);
                return;
            }
            set(0, size, MarkdownStyle::Code);
            if (prev & LuauFence) {
                const int lexer = luau(prev >> LuauShift);
                m_out.state = (prev & ((1 << LuauShift) - 1)) | (lexer << LuauShift);
                return;
            }
            m_out.state = prev;
            return;
        }
        if (kind == Frontmatter || (m_options.firstLine && trimmed == u"---")) {
            // Its fences (the opening one starts it), else a `key:` line.
            const bool opening = kind != Frontmatter;
            if (opening || trimmed == u"---" || trimmed == u"...") {
                set(0, size, MarkdownStyle::Fence);
                if (opening)
                    m_out.state = Frontmatter;
                return;
            }
            static const QRegularExpression keyRe(u"^(\\s*[^:#\\s][^:]*:)"_s);
            set(0, size, MarkdownStyle::Muted);
            if (const auto km = keyRe.match(line); km.hasMatch())
                set(0, int(km.capturedLength(1)), MarkdownStyle::Property);
            m_out.state = Frontmatter;
            return;
        }
        if (fm.hasMatch()) {
            set(0, size, MarkdownStyle::Fence);
            const bool luauFence = syntax::isLuauLanguage(fenceLanguage(line, fm.capturedEnd(1)));
            m_out.state =
                    encodeFence(fm.capturedView(1).front(), int(fm.capturedLength(1))) | (luauFence ? LuauFence : 0);
            return;
        }
        if (kind == Comment) {
            const qsizetype close = line.indexOf(u"%%"_s);
            if (close < 0) {
                set(0, size, MarkdownStyle::Comment);
                m_out.state = Comment;
                return;
            }
            set(0, int(close + 2), MarkdownStyle::Comment);
            inlineSpans(int(close + 2), size - int(close + 2));
            m_out.spell = {int(close + 2), -1, false};
            return;
        }
        m_out.spell = SpellRange();
        // A display formula between lines that are only `$$`: its lines are no prose.
        const bool inMath = (prev & MathFlag) != 0;
        if (isMathFence(line)) {
            m_out.spell.from = -1;
            if (!inMath)
                m_out.state = Normal | MathFlag;
        } else if (inMath) {
            m_out.spell.from = -1;
            m_out.state = Normal | MathFlag;
        }
    }

    inlineSpans(0, size);
    if (source) {
        // An odd number of %% opens a comment that continues on the next lines (counted before
        // masking the code spans, which copies the line).
        const qsizetype marks = line.count(u"%%"_s);
        if (marks % 2 == 1 && markdown::maskInlineCode(line).count(u"%%"_s) == marks) {
            const qsizetype open = line.lastIndexOf(u"%%"_s);
            set(int(open), size - int(open), MarkdownStyle::Comment);
            m_out.state = Comment;
            if (m_out.spell.from >= 0)
                m_out.spell.to = int(open);
        }
    }
}

int Styler::luau(int state)
{
    syntax::LuauState lexer = syntax::LuauState::unpack(state);
    for (const syntax::LuauSpan &span : syntax::lexLuauLine(m_line, &lexer))
        set(span.from, span.length, luauStyle(span.kind));
    return lexer.pack();
}

void Styler::inlineSpans(int offset, int length)
{
    const QString text = m_line.mid(offset, length);
    if (text.isEmpty())
        return;
    const auto at = [offset](qsizetype p) { return int(offset + p); };
    QString masked = text;

    // Inline code spans and %%comments%% first: nothing inside them is syntax.
    static const QRegularExpression codeRe(u"(`+)(.+?)\\1"_s);
    for (auto it = codeRe.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Code);
        set(at(m.capturedStart()), int(m.capturedLength(1)), MarkdownStyle::Fence);
        set(at(m.capturedEnd() - m.capturedLength(1)), int(m.capturedLength(1)), MarkdownStyle::Fence);
        std::fill_n(masked.begin() + m.capturedStart(), m.capturedLength(), u' ');
    }
    // (HTML comments too: a flashcard's scheduling, `<!--SR:!2026-11-20,3,250-->`.)
    static const QRegularExpression commentRe(u"%%.*?%%|<!--.*?-->"_s);
    for (auto it = commentRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Comment);
        std::fill_n(masked.begin() + m.capturedStart(), m.capturedLength(), u' ');
    }
    // Clozes of flashcards (`{{c1::answer::hint}}`, `{{cloze answer}}`): the answer highlighted,
    // the braces, the number and the hint as markers (markdown/cloze.h).
    if (masked.contains(u"{{")) {
        for (const markdown::ClozeSpan &c : markdown::findClozes(masked)) {
            set(at(c.from), int(c.textFrom - c.from), MarkdownStyle::Marker);
            set(at(c.textFrom), int(c.textTo - c.textFrom), MarkdownStyle::Highlight);
            set(at(c.textTo), int(c.to - c.textTo), MarkdownStyle::Marker);
        }
    }

    // Line-level syntax.
    int contentStart = 0;
    if (offset == 0) {
        static const QRegularExpression listRe(u"^(\\s*)([-*+]|\\d{1,9}[.)])(\\s+)"_s);
        static const QRegularExpression quoteRe(u"^(\\s*>\\s?)+"_s);
        static const QRegularExpression checkboxRe(u"^\\[[^\\]]\\](?=\\s|$)"_s);
        static const QRegularExpression keywordRe(
                u"^(TODO|DOING|DONE|LATER|NOW|WAITING|CANCELED|CANCELLED|IN-PROGRESS)(?=\\s|$)"_s);
        if (const auto q = quoteRe.match(masked); q.hasMatch()) {
            set(0, int(q.capturedLength()), MarkdownStyle::Marker);
            contentStart = int(q.capturedLength());
            static const QRegularExpression calloutRe(u"^\\[![A-Za-z0-9_-]+\\][+-]?"_s);
            if (const auto c = calloutRe.matchView(QStringView(masked).sliced(contentStart)); c.hasMatch())
                set(contentStart, int(c.capturedLength()), MarkdownStyle::Muted, StyleModifier::DemiBold);
        }
        if (m_options.mode == MarkdownMode::Source) {
            if (const auto l = listRe.matchView(QStringView(masked).sliced(contentStart)); l.hasMatch()) {
                set(contentStart + int(l.capturedStart(2)), int(l.capturedLength(2)), MarkdownStyle::Marker);
                contentStart += int(l.capturedLength());
            }
        }
        const QStringView rest = QStringView(masked).sliced(contentStart);
        if (const auto c = checkboxRe.matchView(rest); c.hasMatch())
            set(contentStart, int(c.capturedLength()), MarkdownStyle::Marker);
        else if (const auto k = keywordRe.matchView(rest); k.hasMatch())
            set(contentStart, int(k.capturedLength()), MarkdownStyle::Marker, StyleModifier::DemiBold);
        // Headings ("# Title", also inside list items).
        static const QRegularExpression headingRe(u"^(\\s{0,3})(#{1,6})(\\s+|$)"_s);
        if (const auto h = headingRe.matchView(rest); h.hasMatch()) {
            set(contentStart, int(rest.size()), MarkdownStyle::Heading);
            // The # marks: the heading's weight, the marker's colour and name.
            set(contentStart + int(h.capturedStart(2)), int(h.capturedLength(2)), MarkdownStyle::Marker,
                StyleModifier::OnHeading);
        }
        // key:: value
        static const QRegularExpression propRe(u"^\\s*([A-Za-z0-9_-]+)::"_s);
        if (const auto p = propRe.matchView(rest); p.hasMatch())
            set(contentStart + int(p.capturedStart(1)), int(p.capturedLength(1)) + 2, MarkdownStyle::Property);
    }

    // Emphasis.
    const auto emphasis = [&](const QRegularExpression &re, MarkdownStyle style, int markLen) {
        for (auto it = re.globalMatch(masked); it.hasNext();) {
            const auto m = it.next();
            merge(at(m.capturedStart()), int(m.capturedLength()), style);
            merge(at(m.capturedStart()), markLen, MarkdownStyle::Marker);
            merge(at(m.capturedEnd() - markLen), markLen, MarkdownStyle::Marker);
        }
    };
    static const QRegularExpression boldRe(u"(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"_s);
    static const QRegularExpression italicRe(u"(?<![*\\w])\\*(?=[^\\s*])([^*\\n]+?)(?<=[^\\s*])\\*(?![*\\w])"_s);
    static const QRegularExpression italicUnderscoreRe =
            text::unicodeRegex(u"(?<![\\w_])_(?=[^\\s_])([^_\\n]+?)(?<=[^\\s_])_(?![\\w_])"_s);
    static const QRegularExpression strikeRe(u"~~(?=\\S)(.+?)(?<=\\S)~~"_s);
    static const QRegularExpression highlightRe(u"==(?=\\S)(.+?)(?<=\\S)=="_s);
    emphasis(boldRe, MarkdownStyle::Bold, 2);
    emphasis(italicRe, MarkdownStyle::Italic, 1);
    emphasis(italicUnderscoreRe, MarkdownStyle::Italic, 1);
    emphasis(strikeRe, MarkdownStyle::Strike, 2);
    emphasis(highlightRe, MarkdownStyle::Highlight, 2);
    // Coloured highlights `<mark …>…</mark>` (markdown/highlight.h): the text on its palette
    // colour, the tags faint.
    for (const markdown::MarkPair &p : markdown::findMarkPairs(masked)) {
        merge(at(p.open.to), int(p.close.from - p.open.to), MarkdownStyle::Highlight, p.open.color);
        merge(at(p.open.from), int(p.open.to - p.open.from), MarkdownStyle::Faint);
        merge(at(p.close.from), int(p.close.to - p.close.from), MarkdownStyle::Faint);
    }

    // Links.
    static const QRegularExpression wikiRe(u"!?\\[\\[([^\\[\\]\\n]+)\\]\\]"_s);
    for (auto it = wikiRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Link);
        const int open = int(m.capturedStart(1) - m.capturedStart());
        merge(at(m.capturedStart()), open, MarkdownStyle::Marker);
        merge(at(m.capturedEnd() - 2), 2, MarkdownStyle::Marker);
    }
    static const QRegularExpression refRe(u"!?\\(\\(([A-Za-z0-9-]+)\\)\\)"_s);
    for (auto it = refRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::BlockRef);
    }
    static const QRegularExpression mdLinkRe(u"!?\\[([^\\]\\n]*)\\]\\(([^)\\n]*)\\)"_s);
    for (auto it = mdLinkRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() > 0 && masked.at(m.capturedStart() - 1) == u'[')
            continue;
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Marker);
        set(at(m.capturedStart(1)), int(m.capturedLength(1)), MarkdownStyle::Link);
        set(at(m.capturedStart(2)), int(m.capturedLength(2)), MarkdownStyle::Faint);
    }
    static const QRegularExpression urlRe(u"(?<![\\w(/])(https?://|mailto:)[^\\s<>()\\]]+"_s);
    for (auto it = urlRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        // Not inside a Markdown link target (already faint).
        if (styledAs(at(m.capturedStart()), MarkdownStyle::Faint))
            continue;
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Url);
    }

    // Tags.
    static const QRegularExpression tagRe = text::unicodeRegex(
            u"(?:^|(?<=[\\s(]))#(\\[\\[[^\\]]+\\]\\]|[\\p{L}\\p{N}\\p{M}_\\-/]*[\\p{L}\\p{M}_\\-/][\\p{L}\\p{N}\\p{M}_\\-/]*)"_s);
    for (auto it = tagRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        set(at(m.capturedStart()), int(m.capturedLength()), MarkdownStyle::Tag);
    }

    // Metadata tokens (dates, reminders, recurrence, priorities, block ids).
    for (const markdown::Token &t : markdown::findTokens(masked)) {
        set(at(t.from), int(t.to - t.from), tokenStyle(t));
        if (m_options.reminderChips
            && (t.kind == markdown::TokenKind::Reminder || t.kind == markdown::TokenKind::ReminderCompat))
            m_out.chips.append(
                    {at(t.from), at(t.to), t.alarm,
                     markdown::reminderChipLabel(t.spec(), t.alarm, m_options.chipLocale, m_options.chipToday)});
    }
    if (const auto id = markdown::blockIdAtEnd(masked))
        set(at(id->from), int(id->to - id->from), MarkdownStyle::BlockId);
}

} // namespace

void styleMarkdownLine(const QString &line, int previousState, const StyleOptions &options, LineStyles *out)
{
    out->ops.clear();
    out->chips.clear();
    Styler(line, options, *out).run(previousState);
}

LineStyles styleMarkdownLine(const QString &line, int previousState, const StyleOptions &options)
{
    LineStyles out;
    styleMarkdownLine(line, previousState, options, &out);
    return out;
}

QLatin1StringView markdownStyleName(MarkdownStyle style)
{
    switch (style) {
    case MarkdownStyle::Heading: return "heading"_L1;
    case MarkdownStyle::Marker: return "marker"_L1;
    case MarkdownStyle::Link: return "link"_L1;
    case MarkdownStyle::BlockRef: return "blockRef"_L1;
    case MarkdownStyle::Url: return "url"_L1;
    case MarkdownStyle::Tag: return "tag"_L1;
    case MarkdownStyle::Code: return "code"_L1;
    case MarkdownStyle::Fence: return "fence"_L1;
    case MarkdownStyle::Bold: return "bold"_L1;
    case MarkdownStyle::Italic: return "italic"_L1;
    case MarkdownStyle::Strike: return "strike"_L1;
    case MarkdownStyle::Highlight: return "highlight"_L1;
    case MarkdownStyle::Muted: return "muted"_L1;
    case MarkdownStyle::Faint: return "faint"_L1;
    case MarkdownStyle::BlockId: return "blockId"_L1;
    case MarkdownStyle::Property: return "property"_L1;
    case MarkdownStyle::Comment: return "comment"_L1;
    case MarkdownStyle::Due: return "due"_L1;
    case MarkdownStyle::Scheduled: return "scheduled"_L1;
    case MarkdownStyle::Start: return "start"_L1;
    case MarkdownStyle::Reminder: return "reminder"_L1;
    case MarkdownStyle::Alarm: return "alarm"_L1;
    case MarkdownStyle::Recurrence: return "recurrence"_L1;
    case MarkdownStyle::Done: return "done"_L1;
    case MarkdownStyle::Priority: return "priority"_L1;
    case MarkdownStyle::LuauKeyword: return "luau.keyword"_L1;
    case MarkdownStyle::LuauConstant: return "luau.constant"_L1;
    case MarkdownStyle::LuauBuiltin: return "luau.builtin"_L1;
    case MarkdownStyle::LuauApi: return "luau.api"_L1;
    case MarkdownStyle::LuauFunction: return "luau.function"_L1;
    case MarkdownStyle::LuauNumber: return "luau.number"_L1;
    case MarkdownStyle::LuauString: return "luau.string"_L1;
    case MarkdownStyle::LuauInterpolation: return "luau.interpolation"_L1;
    case MarkdownStyle::LuauComment: return "luau.comment"_L1;
    }
    return {};
}

PaletteColor colorRolePalette(ColorRole role, bool reminderChips)
{
    constexpr QLatin1StringView none;
    switch (role) {
    case ColorRole::None: return {};
    case ColorRole::Heading: return {"heading"_L1, none, "#1f2430"_L1};
    case ColorRole::Marker: return {"marker"_L1, "faint"_L1, "#676c7b"_L1};
    case ColorRole::Link: return {"link"_L1, none, "#2c6aa3"_L1};
    case ColorRole::Url: return {"url"_L1, "link"_L1, "#2c6aa3"_L1};
    case ColorRole::Tag: return {"tag"_L1, none, "#2b5f8f"_L1};
    case ColorRole::Code: return {"code"_L1, none, "#1f2430"_L1};
    case ColorRole::Muted: return {"muted"_L1, none, "#535a6a"_L1};
    case ColorRole::Faint: return {"faint"_L1, none, "#676c7b"_L1};
    case ColorRole::Due: return {"due"_L1, "link"_L1, "#2c6aa3"_L1};
    case ColorRole::Scheduled: return {"scheduled"_L1, none, "#6a4f93"_L1};
    // As chips the text takes the chip's colours, on its background.
    case ColorRole::Reminder:
        return reminderChips ? PaletteColor{"reminderChip"_L1, "reminder"_L1, "#7a4f00"_L1}
                             : PaletteColor{"reminder"_L1, none, "#7a4f00"_L1};
    case ColorRole::Alarm:
        return reminderChips ? PaletteColor{"alarmChip"_L1, "alarm"_L1, "#7a4f00"_L1}
                             : PaletteColor{"alarm"_L1, none, "#7a4f00"_L1};
    case ColorRole::Recurrence: return {"recurrence"_L1, none, "#2a6e44"_L1};
    case ColorRole::Done: return {"done"_L1, "muted"_L1, "#535a6a"_L1};
    case ColorRole::Priority: return {"priority"_L1, none, "#a8382d"_L1};
    case ColorRole::Keyword: return {"keyword"_L1, "scheduled"_L1, "#6a4f93"_L1};
    case ColorRole::Constant: return {"constant"_L1, none, "#9a5b00"_L1};
    case ColorRole::Builtin: return {"builtin"_L1, "link"_L1, "#2c6aa3"_L1};
    case ColorRole::Function: return {"function"_L1, "heading"_L1, "#1f2430"_L1};
    case ColorRole::Number: return {"number"_L1, none, "#9a5b00"_L1};
    case ColorRole::String: return {"string"_L1, none, "#2a6e44"_L1};
    }
    return {};
}

PaletteColor backgroundRolePalette(BackgroundRole role)
{
    switch (role) {
    case BackgroundRole::None:
    case BackgroundRole::Tint: return {};
    case BackgroundRole::Code: return {"codeBackground"_L1, {}, "#f3eee2"_L1};
    case BackgroundRole::Highlight: return {"highlightBackground"_L1, {}, "#61f2b33d"_L1}; // rgba(242, 179, 61, 97)
    }
    return {};
}

void StyleAttributes::merge(const StyleAttributes &other)
{
    if (other.color != ColorRole::None)
        color = other.color;
    if (other.background != BackgroundRole::None) {
        background = other.background;
        tint = other.tint;
    }
    if (other.weight != FontWeight::Normal)
        weight = other.weight;
    italic = italic || other.italic;
    strike = strike || other.strike;
    underline = underline || other.underline;
    monospace = monospace || other.monospace;
}

QString StyleAttributes::tintColor() const
{
    return background == BackgroundRole::Tint ? markdown::highlightTint(tint) : QString();
}

StyleAttributes styleAttributes(MarkdownStyle style)
{
    StyleAttributes a;
    switch (style) {
    case MarkdownStyle::Heading:
        a.color = ColorRole::Heading;
        a.weight = FontWeight::DemiBold;
        break;
    case MarkdownStyle::Marker: a.color = ColorRole::Marker; break;
    case MarkdownStyle::Link:
    case MarkdownStyle::BlockRef: a.color = ColorRole::Link; break;
    case MarkdownStyle::Url:
        a.color = ColorRole::Url;
        a.underline = true;
        break;
    case MarkdownStyle::Tag: a.color = ColorRole::Tag; break;
    case MarkdownStyle::Code:
        a.color = ColorRole::Code;
        a.background = BackgroundRole::Code;
        a.monospace = true;
        break;
    case MarkdownStyle::Fence:
        a.color = ColorRole::Faint;
        a.monospace = true;
        break;
    case MarkdownStyle::Bold: a.weight = FontWeight::Bold; break;
    case MarkdownStyle::Italic: a.italic = true; break;
    case MarkdownStyle::Strike: a.strike = true; break;
    case MarkdownStyle::Highlight: a.background = BackgroundRole::Highlight; break;
    case MarkdownStyle::Muted:
    case MarkdownStyle::Property: a.color = ColorRole::Muted; break;
    case MarkdownStyle::Faint:
    case MarkdownStyle::BlockId: a.color = ColorRole::Faint; break;
    case MarkdownStyle::Comment:
    case MarkdownStyle::LuauComment:
        a.color = ColorRole::Faint;
        a.italic = true;
        break;
    case MarkdownStyle::Due: a.color = ColorRole::Due; break;
    case MarkdownStyle::Scheduled:
    case MarkdownStyle::Start: a.color = ColorRole::Scheduled; break;
    case MarkdownStyle::Reminder: a.color = ColorRole::Reminder; break;
    case MarkdownStyle::Alarm:
        a.color = ColorRole::Alarm;
        a.weight = FontWeight::DemiBold;
        break;
    case MarkdownStyle::Recurrence: a.color = ColorRole::Recurrence; break;
    case MarkdownStyle::Done: a.color = ColorRole::Done; break;
    case MarkdownStyle::Priority: a.color = ColorRole::Priority; break;
    case MarkdownStyle::LuauKeyword:
        a.color = ColorRole::Keyword;
        a.weight = FontWeight::DemiBold;
        break;
    case MarkdownStyle::LuauConstant: a.color = ColorRole::Constant; break;
    case MarkdownStyle::LuauBuiltin:
    case MarkdownStyle::LuauApi: a.color = ColorRole::Builtin; break;
    case MarkdownStyle::LuauFunction:
        a.color = ColorRole::Function;
        a.weight = FontWeight::DemiBold;
        break;
    case MarkdownStyle::LuauNumber: a.color = ColorRole::Number; break;
    case MarkdownStyle::LuauString: a.color = ColorRole::String; break;
    case MarkdownStyle::LuauInterpolation: a.color = ColorRole::Keyword; break;
    }
    return a;
}

StyleAttributes styleAttributes(const StyleOp &op)
{
    StyleAttributes a = styleAttributes(op.style);
    switch (op.modifier) {
    case StyleModifier::None: break;
    case StyleModifier::DemiBold: a.weight = FontWeight::DemiBold; break;
    case StyleModifier::OnHeading: {
        StyleAttributes heading = styleAttributes(MarkdownStyle::Heading);
        heading.merge(a);
        a = heading;
        break;
    }
    }
    if (op.tint != markdown::HighlightColor::Default) {
        a.background = BackgroundRole::Tint;
        a.tint = op.tint;
    }
    return a;
}

QList<StyleRun> flattenStyles(const QList<StyleOp> &ops, int lineLength)
{
    QList<StyleRun> runs;
    if (lineLength <= 0 || ops.isEmpty())
        return runs;
    struct Cell {
        StyleAttributes attributes;
        MarkdownStyle style = MarkdownStyle::Heading;
        bool styled = false;
    };
    QVarLengthArray<Cell, 256> cells(lineLength);
    for (const StyleOp &op : ops) {
        // (QSyntaxHighlighter::setFormat leaves out what is not inside the line.)
        if (op.length <= 0 || op.from < 0 || op.from >= lineLength)
            continue;
        const StyleAttributes attributes = styleAttributes(op);
        const int end = int(std::min<qint64>(qint64(op.from) + op.length, lineLength));
        for (int i = op.from; i < end; ++i) {
            Cell &cell = cells[i];
            if (op.merge)
                cell.attributes.merge(attributes);
            else
                cell.attributes = attributes;
            cell.style = op.style;
            cell.styled = true;
        }
    }
    for (int i = 0; i < lineLength;) {
        const Cell &first = cells[i];
        if (!first.styled) {
            ++i;
            continue;
        }
        int end = i + 1;
        while (end < lineLength && cells[end].styled && cells[end].style == first.style
               && cells[end].attributes == first.attributes)
            ++end;
        runs.append({i, end - i, first.style, first.attributes});
        i = end;
    }
    return runs;
}

} // namespace brulion::editor
