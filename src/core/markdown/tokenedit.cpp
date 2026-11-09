// Token edits of a block body (see tokenedit.h).
#include "markdown/tokenedit.h"

#include "dates/local.h"
#include "markdown/blockdocument.h"
#include "markdown/inline.h"
#include "markdown/journal.h"
#include "markdown/lines_p.h"
#include "util/paths.h"

#include <QLocale>
#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

using detail::isSpaceOrTab;

namespace {

QStringView firstLineOf(QStringView body)
{
    const qsizetype nl = body.indexOf(u'\n');
    return nl < 0 ? body : body.first(nl);
}

bool validRange(const QString &body, qsizetype from, qsizetype to)
{
    return from >= 0 && to > from && to <= body.size();
}

bool isReminderKind(TokenKind kind)
{
    return kind == TokenKind::Reminder || kind == TokenKind::ReminderCompat;
}

bool isPriorityKind(TokenKind kind)
{
    return kind == TokenKind::Priority || kind == TokenKind::LogseqPriority;
}

/// A date token with a menu (the dates of Logseq planning lines have none).
bool isMenuDate(const Token &t)
{
    return t.hasDate() && t.kind != TokenKind::Planning && t.valueFrom >= 0;
}

/// The kind a TokenRef names: reminders and priorities in all their spellings as one.
TokenKind refKind(TokenKind kind)
{
    if (isReminderKind(kind))
        return TokenKind::Reminder;
    if (isPriorityKind(kind))
        return TokenKind::Priority;
    return kind;
}

/// `t` (found in a slice of the body) with its positions moved by `by`.
Token movedBy(Token t, qsizetype by)
{
    t.from += by;
    t.to += by;
    if (t.valueFrom >= 0) {
        t.valueFrom += by;
        t.valueTo += by;
    }
    return t;
}

/// The first token on the body's first line whose kind `accept` takes (tokens inside inline
/// code are ignored), or nullopt.
template<typename Accept>
std::optional<Token> firstLineToken(const QString &body, Accept accept)
{
    for (const Token &t : findTokens(maskInlineCode(firstLineOf(body)))) {
        if (accept(t.kind))
            return t;
    }
    return std::nullopt;
}

/// A token with a menu and its chip range.
struct MenuToken {
    Token token; ///< as findTokens() reads it in the whole body
    qsizetype from = 0;
    qsizetype to = 0; ///< a 🔁 rule's chip ends with the rule
};

/// The tokens of `body` that have a menu, in order (masked: none inside inline code).
QList<MenuToken> menuTokens(const QString &body)
{
    QList<MenuToken> out;
    const QString masked = maskInlineCode(body);
    for (const Token &t : findTokens(masked)) {
        if (isMenuDate(t) || isPriorityKind(t.kind)) {
            out.append({t, t.from, t.to});
        } else if (t.kind == TokenKind::Recurrence) {
            if (const auto rule = recurrencePrefix(masked, t))
                out.append({t, t.from, rule->second});
        }
    }
    return out;
}

QString refValue(const Token &t)
{
    if (t.hasDate())
        return t.spec().toString();
    if (t.kind == TokenKind::Recurrence)
        return recurrenceOf(t);
    return priorityName(t.priority);
}

bool refMatches(const TokenRef &ref, const Token &t)
{
    if (refKind(t.kind) != ref.kind)
        return false;
    if (refValue(t) == ref.value)
        return true;
    // The index gives a reminder without a time the default reminder time.
    return isReminderKind(t.kind) && !t.time && ref.defaultTime.isValid()
           && DateSpec{t.date, ref.defaultTime}.toString() == ref.value;
}

std::optional<DateSpec> specFrom(const QVariant &value)
{
    return dates::parseDateSpec(value.toString().trimmed());
}

/// The rule as written into a note: canonical English when it parses, else as given.
QString writtenRule(const QString &rule)
{
    const QString canonical = dates::canonicalRecurrence(rule);
    return canonical.isEmpty() ? rule.trimmed() : canonical;
}

/// `target` (a link target naming the daily note of `from`) naming `to` instead, in the format
/// it was written in; "" when that format is not recognised.
QString retargetedDay(const QString &target, QDate from, QDate to, const VaultConfig &config)
{
    QString stem = target;
    QString ext;
    if (stem.endsWith(u".md"_s, Qt::CaseInsensitive)) {
        ext = stem.right(3);
        stem.chop(3);
    }
    const QString own = config.journalFileFormat.isEmpty() ? u"yyyy-MM-dd"_s : config.journalFileFormat;
    QStringList formats{own};
    if (own.contains(u'/'))
        formats << own.section(u'/', -1);
    formats << u"yyyy-MM-dd"_s << u"yyyy_MM_dd"_s;
    const QLocale c = QLocale::c();
    for (const QString &format : std::as_const(formats)) {
        const QString written = c.toString(from, format);
        if (written.isEmpty() || !stem.endsWith(written, Qt::CaseInsensitive))
            continue;
        const QString next = c.toString(to, format);
        if (c.toDate(next, format) != to)
            continue; // a format that does not round-trip
        return stem.chopped(written.size()) + next + ext;
    }
    return {};
}

/// [start, end) of line `line` (0-based) of `content` without its line ending, when that line
/// ends with `tail`; nullopt otherwise.
std::optional<std::pair<qsizetype, qsizetype>> lineEndingWith(const QString &content, int line, QStringView tail)
{
    qsizetype from = 0;
    for (int n = 0; n < line; ++n) {
        from = content.indexOf(u'\n', from) + 1;
        if (from == 0)
            return std::nullopt;
    }
    qsizetype to = content.indexOf(u'\n', from);
    if (to < 0)
        to = content.size();
    if (to > from && content[to - 1] == u'\r')
        --to;
    if (!QStringView(content).sliced(from, to - from).endsWith(tail))
        return std::nullopt;
    return std::pair{from, to};
}

} // namespace

QString replaceTokenRange(const QString &body, qsizetype from, qsizetype to, const QString &token)
{
    if (from < 0 || to < from || to > body.size())
        return body;
    if (!token.isEmpty())
        return body.left(from) + token + body.mid(to);
    // Remove with one adjacent space.
    qsizetype a = from;
    qsizetype b = to;
    if (a > 0 && isSpaceOrTab(body[a - 1]))
        --a;
    else if (b < body.size() && isSpaceOrTab(body[b]))
        ++b;
    QString out = body.left(a) + body.mid(b);
    // Alone on a line below the first one: the line goes too — a whitespace-only line left behind
    // would be a blank line (a paragraph break in the item, a loose list in CommonMark and Obsidian).
    const qsizetype lineStart = a == 0 ? 0 : out.lastIndexOf(u'\n', a - 1) + 1;
    if (lineStart > 0) {
        qsizetype lineEnd = out.indexOf(u'\n', a);
        if (lineEnd < 0)
            lineEnd = out.size();
        if (QStringView(out).sliced(lineStart, lineEnd - lineStart).trimmed().isEmpty())
            out.remove(lineStart - 1, lineEnd - lineStart + 1);
    }
    return out;
}

std::optional<std::pair<qsizetype, qsizetype>> findFirstLineToken(const QString &body, TokenKind kind)
{
    if (const auto t = firstLineToken(body, [kind](TokenKind k) { return k == kind; }))
        return std::pair{t->from, t->to};
    return std::nullopt;
}

QString setFirstLineToken(const QString &body, TokenKind kind, const QString &token, bool append)
{
    if (!append) {
        if (const auto range = findFirstLineToken(body, kind))
            return replaceTokenRange(body, range->first, range->second, token);
    }
    if (token.isEmpty())
        return body;
    const QStringView first = firstLineOf(body);
    qsizetype at = first.size();
    // Before a trailing block id (" ^abc").
    if (const auto id = blockIdAtEnd(first))
        at = id->from;
    // Before trailing whitespace: no blank before `at`, so the token gets a space before it (not
    // at the line's start) and after it when the text after `at` does not start with a blank.
    while (at > 0 && isSpaceOrTab(first[at - 1]))
        --at;
    QString out = body.left(at);
    if (at > 0)
        out += u' ';
    out += token;
    if (at < first.size() && !isSpaceOrTab(first[at]))
        out += u' ';
    out += QStringView(body).sliced(at);
    return out;
}

std::optional<Token> dateTokenAt(const QString &body, qsizetype from, qsizetype to)
{
    if (!validRange(body, from, to))
        return std::nullopt;
    for (const Token &t : findTokens(QStringView(body).sliced(from, to - from))) {
        if (isMenuDate(t))
            return movedBy(t, from);
    }
    return std::nullopt;
}

std::optional<Token> tokenAt(const QString &body, qsizetype from, qsizetype to)
{
    if (auto t = dateTokenAt(body, from, to))
        return t;
    if (!validRange(body, from, to))
        return std::nullopt;
    const QStringView slice = QStringView(body).sliced(from, to - from);
    for (Token t : findTokens(slice)) {
        if (t.kind == TokenKind::Recurrence) {
            const auto rule = recurrencePrefix(slice, t);
            if (!rule)
                continue;
            t.to = rule->second;
            t.valueTo = rule->second;
            t.value = slice.sliced(t.valueFrom, t.valueTo - t.valueFrom).toString();
            t.canonical = dates::formatRecurrence(rule->first, dates::UiLocale::English);
        } else if (!isPriorityKind(t.kind)) {
            continue;
        }
        return movedBy(std::move(t), from);
    }
    return std::nullopt;
}

QString setTokenDate(const QString &body, qsizetype from, qsizetype to, const DateSpec &spec)
{
    const auto t = dateTokenAt(body, from, to);
    if (!t || !spec.isValid())
        return body;
    return body.left(t->valueFrom) + spec.toString() + body.mid(t->valueTo);
}

QString setReminderAlarm(const QString &body, qsizetype from, qsizetype to, bool alarm)
{
    const auto t = dateTokenAt(body, from, to);
    if (!t || !isReminderKind(t->kind) || (t->kind == TokenKind::Reminder && t->alarm == alarm))
        return body;
    return replaceTokenRange(body, t->from, t->to, formatReminderToken(t->spec(), alarm));
}

DateSpec shiftedSpec(const DateSpec &spec, int days, int minutes, QTime defaultTime)
{
    DateSpec out = spec;
    out.date = spec.date.addDays(days);
    if (minutes != 0) {
        constexpr int MinutesPerDay = 24 * 60;
        const QTime time = spec.time.value_or(defaultTime);
        // In 64 bits (no overflow for any `minutes`): the time wraps into the day, whole days carry.
        const qint64 total = time.hour() * 60 + time.minute() + qint64(minutes);
        const int inDay = int((total % MinutesPerDay + MinutesPerDay) % MinutesPerDay);
        out.date = out.date.addDays((total - inDay) / MinutesPerDay);
        out.time = QTime(inDay / 60, inDay % 60);
    }
    return out;
}

std::optional<std::pair<dates::RecurrenceRule, qsizetype>> recurrencePrefix(QStringView masked, const Token &tok)
{
    if (tok.valueFrom < 0 || tok.valueTo <= tok.valueFrom || tok.valueTo > masked.size())
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
        while (!candidate.isEmpty()
               && (candidate.back() == u',' || candidate.back() == u';' || candidate.back() == u':'))
            candidate.chop(1);
        if (const auto rule = dates::parseRecurrence(candidate))
            return std::pair{*rule, tok.valueFrom + candidate.size()};
    }
    return std::nullopt;
}

QString setRecurrenceAt(const QString &body, qsizetype from, qsizetype to, const QString &rule)
{
    const auto t = tokenAt(body, from, to);
    if (!t || t->kind != TokenKind::Recurrence)
        return body;
    if (rule.trimmed().isEmpty())
        return replaceTokenRange(body, t->from, t->to, QString());
    // The emoji (and its spacing) stay as written; only the rule's text changes.
    return body.left(t->valueFrom) + writtenRule(rule) + body.mid(t->valueTo);
}

QString setBlockRecurrence(const QString &body, const QString &rule)
{
    if (const auto range = findFirstLineToken(body, TokenKind::Recurrence)) {
        // The rule at the start of the token's text (text after it stays), else the whole token.
        if (const auto t = tokenAt(body, range->first, range->second)) {
            // The same rule (in any phrasing: "co tydzień" is "every week"): written as it was.
            const QString canonical = dates::canonicalRecurrence(rule);
            if (!canonical.isEmpty() && canonical == t->canonical)
                return body;
            return setRecurrenceAt(body, t->from, t->to, rule);
        }
        return replaceTokenRange(body, range->first, range->second,
                                 rule.trimmed().isEmpty() ? QString() : u"🔁 "_s + writtenRule(rule));
    }
    if (rule.trimmed().isEmpty())
        return body;
    return setFirstLineToken(body, TokenKind::Recurrence, u"🔁 "_s + writtenRule(rule));
}

QString blockRecurrence(const QString &body)
{
    const auto token = firstLineToken(body, [](TokenKind kind) { return kind == TokenKind::Recurrence; });
    if (!token)
        return {};
    if (const auto t = tokenAt(body, token->from, token->to))
        return t->canonical;
    return recurrenceOf(*token);
}

std::pair<Priority, bool> blockPriority(const QString &body)
{
    if (const auto t = firstLineToken(body, isPriorityKind))
        return {t->priority, t->kind == TokenKind::LogseqPriority};
    return {Priority::None, false};
}

QString priorityName(Priority priority)
{
    switch (priority) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::None: break;
    }
    return {};
}

Priority priorityFromName(QStringView name)
{
    for (const Priority p : {Priority::Highest, Priority::High, Priority::Medium, Priority::Low, Priority::Lowest}) {
        if (name == priorityName(p))
            return p;
    }
    return Priority::None;
}

QString setPriorityAt(const QString &body, qsizetype from, qsizetype to, Priority priority)
{
    const auto t = tokenAt(body, from, to);
    if (!t || !isPriorityKind(t->kind))
        return body;
    if (priority == Priority::None)
        return replaceTokenRange(body, t->from, t->to, QString());
    if (t->kind == TokenKind::LogseqPriority) {
        const QChar letter = priority >= Priority::High ? u'A' : priority == Priority::Medium ? u'B' : u'C';
        return body.left(t->from) + u"[#"_s + letter + u']' + body.mid(t->to);
    }
    return body.left(t->from) + tokens::priorityEmoji(priority).toString() + body.mid(t->to);
}

QString setBlockPriority(const QString &body, Priority priority)
{
    if (const auto t = firstLineToken(body, isPriorityKind))
        return setPriorityAt(body, t->from, t->to, priority);
    if (priority == Priority::None)
        return body;
    return setFirstLineToken(body, TokenKind::Priority, tokens::priorityEmoji(priority).toString());
}

std::optional<JournalLinkMatch> journalLinkAt(const QString &body, qsizetype from, qsizetype to,
                                              const VaultConfig &config)
{
    if (!validRange(body, from, to))
        return std::nullopt;
    const InlineScan scan = extractInline(body);
    for (const Link &link : scan.links) {
        if ((link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed) || link.from < from || link.to > to)
            continue;
        if (const auto day = journalDateFromLinkTarget(link.target, config))
            return JournalLinkMatch{*day, link.from, link.to, link.target};
    }
    return std::nullopt;
}

QString setJournalLinkDate(const QString &body, qsizetype from, qsizetype to, QDate date, const VaultConfig &config)
{
    const auto link = journalLinkAt(body, from, to, config);
    if (!link || !date.isValid() || link->target.isEmpty())
        return body;
    const qsizetype open = body.indexOf(u"[["_s, link->from);
    if (open < 0 || open >= link->to)
        return body;
    const qsizetype at = body.indexOf(link->target, open + 2);
    if (at < 0 || at + link->target.size() > link->to)
        return body;
    QString target = retargetedDay(link->target, link->date, date, config);
    if (target.isEmpty())
        target = paths::pageTitle(journalPathForDate(date, config));
    return body.left(at) + target + body.mid(at + link->target.size());
}

QString applyTokenAction(const QString &body, qsizetype from, qsizetype to, const QString &action, const QVariant &a,
                         const QVariant &b, const TokenEditContext &context)
{
    if (action == u"blockRecurrence")
        return setBlockRecurrence(body, a.toString());
    if (action == u"blockPriority")
        return setBlockPriority(body, priorityFromName(a.toString()));
    const auto t = tokenAt(body, from, to);
    if (!t) {
        // A link to a daily note: the date menu of `[[2026-10-20]]`.
        const auto link = journalLinkAt(body, from, to, context.config);
        if (!link)
            return body;
        QDate day;
        if (action == u"set") {
            const auto spec = specFrom(a);
            day = spec ? spec->date : QDate();
        } else if (action == u"move") {
            day = context.today.addDays(a.toInt());
        } else if (action == u"shift") {
            day = link->date.addDays(a.toInt());
        }
        return day.isValid() ? setJournalLinkDate(body, from, to, day, context.config) : body;
    }
    if (t->hasDate()) {
        const bool reminder = isReminderKind(t->kind);
        if (action == u"remove" || (action == u"set" && a.toString().trimmed().isEmpty()))
            return replaceTokenRange(body, t->from, t->to, QString());
        if (action == u"set") {
            const auto spec = specFrom(a);
            return spec ? setTokenDate(body, from, to, *spec) : body;
        }
        if (action == u"move")
            return setTokenDate(body, from, to, DateSpec{context.today.addDays(a.toInt()), t->time});
        if (action == u"shift")
            return setTokenDate(body, from, to,
                                shiftedSpec(t->spec(), a.toInt(), b.toInt(), context.defaultReminderTime));
        if (action == u"alarm" && reminder)
            return setReminderAlarm(body, from, to, a.toBool());
        if (action == u"reminder" && reminder) {
            const auto spec = specFrom(a);
            if (!spec)
                return body;
            const QVariantMap options = b.toMap();
            QString out = replaceTokenRange(body, t->from, t->to,
                                            formatReminderToken(*spec, options.value(u"alarm"_s).toBool()));
            if (options.contains(u"recurrence"_s))
                out = setBlockRecurrence(out, options.value(u"recurrence"_s).toString());
            return out;
        }
        return body;
    }
    if (t->kind == TokenKind::Recurrence) {
        if (action == u"recurrence")
            return setRecurrenceAt(body, from, to, a.toString());
        if (action == u"remove")
            return setRecurrenceAt(body, from, to, QString());
        return body;
    }
    if (action == u"priority")
        return setPriorityAt(body, from, to, priorityFromName(a.toString()));
    if (action == u"remove")
        return setPriorityAt(body, from, to, Priority::None);
    return body;
}

std::optional<TokenRef> tokenRefAt(const QString &body, qsizetype from, qsizetype to)
{
    const auto t = tokenAt(body, from, to);
    if (!t)
        return std::nullopt;
    const QList<MenuToken> all = menuTokens(body);
    const auto m = std::ranges::find(all, t->from, &MenuToken::from);
    if (m == all.end())
        return std::nullopt;
    TokenRef ref;
    ref.kind = refKind(m->token.kind);
    ref.value = refValue(m->token);
    // Which of the equal ones it is: the menu tokens are in order.
    ref.ordinal = int(std::count_if(all.begin(), m, [&ref](const MenuToken &before) {
        return refKind(before.token.kind) == ref.kind && refValue(before.token) == ref.value;
    }));
    return ref;
}

std::optional<std::pair<qsizetype, qsizetype>> findTokenRef(const QString &body, const TokenRef &ref)
{
    int seen = 0;
    for (const MenuToken &m : menuTokens(body)) {
        if (!refMatches(ref, m.token))
            continue;
        if (seen++ == ref.ordinal)
            return std::pair{m.from, m.to};
    }
    return std::nullopt;
}

QString editBlockToken(const QString &content, int line, const TokenRef &ref, const QString &action, const QVariant &a,
                       const QVariant &b, const TokenEditContext &context)
{
    BlockDocument doc(content);
    const int i = doc.indexAtLine(line);
    if (i < 0)
        return content;
    // Tokens are text of list items, paragraphs, headings, quotes and callouts (not of code,
    // tables, HTML or frontmatter).
    switch (doc.at(i).kind) {
    case BlockKind::ListItem:
    case BlockKind::Paragraph:
    case BlockKind::Heading:
    case BlockKind::Quote:
    case BlockKind::Callout: break;
    default: return content;
    }
    if (!doc.at(i).lang.isEmpty())
        return content; // a fenced block inside a list item
    const QString body = doc.at(i).body;
    const auto range = findTokenRef(body, ref);
    if (!range)
        return content;
    const QString edited = applyTokenAction(body, range->first, range->second, action, a, b, context);
    if (edited == body)
        return content;
    // Written into the file line by line as it stands — indentation, markers and line endings
    // untouched: one changed line of the body (only its changed run replaced), or one line below
    // the first removed (a token alone on it, replaceTokenRange()) — the file's line goes with
    // its line ending.
    const QStringList oldLines = body.split(u'\n');
    const QStringList newLines = edited.split(u'\n');
    qsizetype k = 0; // the body's first changed line
    while (k < oldLines.size() && k < newLines.size() && oldLines.at(k) == newLines.at(k))
        ++k;
    const bool lineChanged =
            oldLines.size() == newLines.size() && k < oldLines.size() && oldLines.mid(k + 1) == newLines.mid(k + 1);
    const bool lineRemoved = newLines.size() == oldLines.size() - 1 && k > 0 && oldLines.mid(k + 1) == newLines.mid(k);
    // The file's line of that body line: it ends with it (cannot fail for tokens; kept safe).
    const auto span = (lineChanged || lineRemoved) ? lineEndingWith(content, doc.at(i).line + int(k), oldLines.at(k))
                                                   : std::nullopt;
    if (span && lineChanged) {
        const QString &before = oldLines.at(k);
        const QString &after = newLines.at(k);
        qsizetype p = 0;
        while (p < before.size() && p < after.size() && before[p] == after[p])
            ++p;
        qsizetype q = 0;
        while (q < before.size() - p && q < after.size() - p
               && before[before.size() - 1 - q] == after[after.size() - 1 - q]) {
            ++q;
        }
        const qsizetype at = span->second - before.size() + p;
        return content.left(at) + after.mid(p, after.size() - p - q) + content.mid(at + before.size() - p - q);
    }
    if (span) { // lineRemoved
        if (const qsizetype nl = content.indexOf(u'\n', span->second); nl >= 0)
            return content.left(span->first) + content.mid(nl + 1);
        // The file's last line, without a line ending: the one before it goes instead.
        qsizetype prev = span->first - 1; // the previous line's '\n'
        if (prev > 0 && content[prev - 1] == u'\r')
            --prev;
        return content.left(prev);
    }
    // Not one line of the file as it stands (cannot happen for tokens; kept safe): the block's
    // body as the editor writes it.
    doc.setBody(i, edited);
    return doc.toString();
}

} // namespace brulion::markdown
