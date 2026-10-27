// Token edits of a block body (see tokenedit.h).
#include "markdown/tokenedit.h"

#include "dates/local.h"
#include "markdown/blockdocument.h"
#include "markdown/inline.h"
#include "markdown/journal.h"
#include "util/paths.h"

#include <QLocale>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

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

/// The kind a TokenRef names: reminders and priorities in all their spellings as one.
TokenKind refKind(TokenKind kind)
{
    if (isReminderKind(kind))
        return TokenKind::Reminder;
    if (isPriorityKind(kind))
        return TokenKind::Priority;
    return kind;
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
        if (t.hasDate()) {
            if (t.kind == TokenKind::Planning || t.valueFrom < 0)
                continue;
            out.append({t, t.from, t.to});
        } else if (t.kind == TokenKind::Recurrence) {
            const auto rule = recurrencePrefix(masked, t);
            if (!rule)
                continue;
            out.append({t, t.from, rule->second});
        } else if (isPriorityKind(t.kind)) {
            out.append({t, t.from, t.to});
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
    const QString value = refValue(t);
    if (value == ref.value)
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
    QStringList formats;
    const QString own = config.journalFileFormat.isEmpty() ? u"yyyy-MM-dd"_s : config.journalFileFormat;
    formats << own;
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
        return stem.left(stem.size() - written.size()) + next + ext;
    }
    return {};
}

/// Range of the first priority (emoji or Logseq) on the first line, or nullopt.
std::optional<std::pair<qsizetype, qsizetype>> firstLinePriority(const QString &body)
{
    const auto emoji = findFirstLineToken(body, TokenKind::Priority);
    const auto logseq = findFirstLineToken(body, TokenKind::LogseqPriority);
    if (emoji && logseq)
        return emoji->first < logseq->first ? emoji : logseq;
    return emoji ? emoji : logseq;
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
    if (a > 0 && (body[a - 1] == u' ' || body[a - 1] == u'\t'))
        --a;
    else if (b < body.size() && (body[b] == u' ' || body[b] == u'\t'))
        ++b;
    return body.left(a) + body.mid(b);
}

std::optional<std::pair<qsizetype, qsizetype>> findFirstLineToken(const QString &body, TokenKind kind)
{
    const QString masked = maskInlineCode(firstLineOf(body));
    for (const Token &t : findTokens(masked)) {
        if (t.kind == kind)
            return std::pair{t.from, t.to};
    }
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
    const qsizetype nl = body.indexOf(u'\n');
    const QString first = nl < 0 ? body : body.left(nl);
    const QString rest = nl < 0 ? QString() : body.mid(nl);
    qsizetype at = first.size();
    // Before a trailing block id (" ^abc").
    if (const auto id = blockIdAtEnd(first))
        at = id->from;
    // Before trailing whitespace.
    while (at > 0 && (first[at - 1] == u' ' || first[at - 1] == u'\t'))
        --at;
    QString before = first.left(at);
    const QString after = first.mid(at);
    const bool needSpace = !before.isEmpty() && !before.endsWith(u' ') && !before.endsWith(u'\t');
    QString line = before + (needSpace ? u" "_s : QString()) + token;
    if (!after.isEmpty() && !after.startsWith(u' ') && !after.startsWith(u'\t'))
        line += u' ';
    line += after;
    return line + rest;
}

std::optional<Token> dateTokenAt(const QString &body, qsizetype from, qsizetype to)
{
    if (!validRange(body, from, to))
        return std::nullopt;
    for (Token t : findTokens(QStringView(body).sliced(from, to - from))) {
        if (!t.hasDate() || t.kind == TokenKind::Planning || t.valueFrom < 0)
            continue;
        t.from += from;
        t.to += from;
        t.valueFrom += from;
        t.valueTo += from;
        return t;
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
        t.from += from;
        t.to += from;
        if (t.valueFrom >= 0) {
            t.valueFrom += from;
            t.valueTo += from;
        }
        return t;
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
        const QTime time = spec.time.value_or(defaultTime);
        const int total = time.hour() * 60 + time.minute() + minutes;
        const int dayShift = total >= 0 ? total / 1440 : -((-total + 1439) / 1440);
        const int inDay = total - dayShift * 1440;
        out.date = out.date.addDays(dayShift);
        out.time = QTime(inDay / 60, inDay % 60);
    }
    return out;
}

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
    const auto range = findFirstLineToken(body, TokenKind::Recurrence);
    if (!range)
        return {};
    if (const auto t = tokenAt(body, range->first, range->second))
        return t->canonical;
    const QString masked = maskInlineCode(body);
    const QList<Token> all = findTokens(QStringView(masked).first(range->second));
    return all.isEmpty() ? QString() : recurrenceOf(all.last());
}

std::pair<Priority, bool> blockPriority(const QString &body)
{
    for (const Token &t : findTokens(maskInlineCode(firstLineOf(body)))) {
        if (isPriorityKind(t.kind))
            return {t.priority, t.kind == TokenKind::LogseqPriority};
    }
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
    if (name == u"highest")
        return Priority::Highest;
    if (name == u"high")
        return Priority::High;
    if (name == u"medium")
        return Priority::Medium;
    if (name == u"low")
        return Priority::Low;
    if (name == u"lowest")
        return Priority::Lowest;
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
    if (const auto range = firstLinePriority(body))
        return setPriorityAt(body, range->first, range->second, priority);
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
        const auto day = journalDateFromLinkTarget(link.target, config);
        if (!day)
            continue;
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
    for (const MenuToken &m : all) {
        if (m.from != t->from)
            continue;
        TokenRef ref;
        ref.kind = refKind(m.token.kind);
        ref.value = refValue(m.token);
        for (const MenuToken &before : all) {
            if (before.from >= m.from)
                break;
            if (refKind(before.token.kind) == ref.kind && refValue(before.token) == ref.value)
                ++ref.ordinal;
        }
        return ref;
    }
    return std::nullopt;
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
    // The changed run (common prefix and suffix left out): on one line of the body, it is written
    // into that line of the file as it stands — indentation, markers and the line ending untouched.
    qsizetype p = 0;
    while (p < body.size() && p < edited.size() && body[p] == edited[p])
        ++p;
    qsizetype s = 0;
    while (s < body.size() - p && s < edited.size() - p && body[body.size() - 1 - s] == edited[edited.size() - 1 - s])
        ++s;
    const QString oldPart = body.mid(p, body.size() - p - s);
    const QString newPart = edited.mid(p, edited.size() - p - s);
    const qsizetype lineStart = p == 0 ? 0 : body.lastIndexOf(u'\n', p - 1) + 1;
    const qsizetype lineEnd = [&] {
        const qsizetype nl = body.indexOf(u'\n', p);
        return nl < 0 ? body.size() : nl;
    }();
    if (!oldPart.contains(u'\n') && !newPart.contains(u'\n') && p + oldPart.size() <= lineEnd) {
        const QString bodyLine = body.mid(lineStart, lineEnd - lineStart);
        const int docLine = doc.at(i).line + int(body.left(lineStart).count(u'\n'));
        qsizetype ls = 0;
        for (int k = 0; k < docLine && ls >= 0; ++k) {
            ls = content.indexOf(u'\n', ls);
            if (ls >= 0)
                ++ls;
        }
        if (ls >= 0) {
            qsizetype le = content.indexOf(u'\n', ls);
            if (le < 0)
                le = content.size();
            if (le > ls && content[le - 1] == u'\r')
                --le;
            const QStringView fileLine = QStringView(content).sliced(ls, le - ls);
            if (fileLine.endsWith(bodyLine)) {
                const qsizetype at = ls + fileLine.size() - bodyLine.size() + (p - lineStart);
                return content.left(at) + newPart + content.mid(at + oldPart.size());
            }
        }
    }
    // Not one line of the file as it stands (cannot happen for tokens; kept safe): the block's
    // body as the editor writes it.
    doc.setBody(i, edited);
    return doc.toString();
}

} // namespace brulion::markdown
