// Internal helpers of the reminders module (see reminders_p.h).
#include "reminders/reminders_p.h"

#include "dates/local.h"
#include "markdown/inline.h"
#include "reminders/notifications.h"

#include <QHash>

#include <algorithm>
#include <array>

namespace brulion::reminders_detail {

using namespace Qt::StringLiterals;

ReminderSource sourceOf(const VaultIndex &index, const ReminderRef &ref)
{
    return sourceOf(index.note(ref.path), ref);
}

QList<ReminderRef> remindersOf(const VaultPath &path, const ParsedNote &note, QDate from, QDate to)
{
    QList<ReminderRef> out;
    // Most reminders of a vault are one-off ones on other days (skipped cheaply), and most notes
    // have no other.
    const auto mayOccur = [&](const Reminder &r) { return mayOccurWithin(r, from, to); };
    if (std::none_of(note.reminders.cbegin(), note.reminders.cend(), mayOccur))
        return out;
    for (qsizetype i = 0; i < note.blocks.size(); ++i) {
        const Block &block = note.blocks.at(i);
        for (const Reminder &r : block.reminders) {
            if (mayOccur(r))
                out.append(ReminderRef{path, int(i), r, block.task});
        }
    }
    return out;
}

ReminderSource sourceOf(const ParsedNote *note, const ReminderRef &ref)
{
    ReminderSource src;
    src.text = ref.reminder.text;
    src.task = ref.task;
    if (!note)
        return src;
    src.pageTitle = note->title;
    // A bare "- ⏰ …" bullet (e.g. a snoozed occurrence) belongs to its parent item — through
    // further bare bullets (a snoozed occurrence of a recurring bare bullet) — but not to a
    // quote or callout that merely contains the list.
    qsizetype b = ref.blockIndex;
    while (!src.task && src.text.trimmed().isEmpty() && b >= 0 && b < note->blocks.size()) {
        const Block &block = note->blocks.at(b);
        const qsizetype p = block.parent;
        if (block.kind != BlockKind::ListItem || p < 0 || p >= b) // parents precede children
            break;
        const Block &parent = note->blocks.at(p);
        if (parent.kind != BlockKind::ListItem)
            break;
        src.text = parent.text;
        src.task = parent.task;
        b = p;
    }
    return src;
}

bool isClosed(const Task &task)
{
    return task.status == TaskStatus::Done || task.status == TaskStatus::Cancelled;
}

bool isClosed(const std::optional<Task> &task)
{
    return task && isClosed(*task);
}

std::optional<dates::RecurrenceRule> ruleOf(const QString &recurrence)
{
    if (recurrence.isEmpty() || recurrence.trimmed().isEmpty())
        return std::nullopt;
    return dates::parseRecurrence(recurrence);
}

std::optional<DateSpec> seriesReference(const Task &task)
{
    for (const std::optional<DateSpec> &date : {task.due, task.scheduled, task.start}) {
        if (date && date->isValid())
            return date;
    }
    return std::nullopt;
}

QList<DateSpec> datesWithin(const DateSpec &date, const std::optional<dates::RecurrenceRule> &rule, QDate from,
                            QDate to, const std::optional<DateSpec> &reference)
{
    if (!date.isValid() || !from.isValid() || !to.isValid() || from > to)
        return {};
    if (!rule) {
        if (date.date >= from && date.date <= to)
            return {date};
        return {};
    }
    // The series of the reference date, shifted by the distance of `date` to it.
    const QDate base = reference && reference->isValid() ? reference->date : date.date;
    const qint64 offset = base.daysTo(date.date);
    const QDate baseFrom = from.addDays(-offset);
    const QDate baseTo = to.addDays(-offset);
    if (!baseFrom.isValid() || !baseTo.isValid())
        return {};
    const QList<DateSpec> bases =
            dates::occurrencesBetween(*rule, DateSpec{base, date.time}, baseFrom, baseTo, MaxOccurrences);
    QList<DateSpec> out;
    out.reserve(bases.size());
    for (const DateSpec &occurrence : bases)
        out.append(DateSpec{occurrence.date.addDays(offset), date.time});
    return out;
}

QList<DateSpec> reminderDates(const ReminderRef &ref, bool expand, QDate from, QDate to)
{
    const std::optional<dates::RecurrenceRule> rule = expand ? ruleOf(ref.reminder.recurrence) : std::nullopt;
    // The block's own task recurs (its rule is the reminder's): the reminder moves with it.
    std::optional<DateSpec> reference;
    if (rule && ref.task && !ref.task->recurrence.trimmed().isEmpty())
        reference = seriesReference(*ref.task);
    return datesWithin(ref.reminder.at, rule, from, to, reference);
}

namespace {

/// Most characters of a referenced block's Markdown read for its text in a title.
constexpr qsizetype MaxRefSource = 1000;

/// What a character of inline Markdown becomes in plain text.
enum class CharRole : char {
    Text,     ///< kept; may be markup
    Verbatim, ///< kept as is: code span content, link texts
    Drop,     ///< markup: code span backticks, emphasis markers of a pair
};

/// A range [from, to) of a text.
struct Span {
    qsizetype from = 0;
    qsizetype to = 0;
};

/// Mark the code spans of `text` (outside verbatim parts): a backtick run is closed by the next
/// run of the same length (CommonMark); unmatched runs stay literal. Linear.
void markCodeSpans(QStringView text, QList<CharRole> &roles)
{
    QList<Span> runs;
    for (qsizetype i = 0; i < text.size();) {
        if (text.at(i) != u'`' || roles.at(i) != CharRole::Text) {
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < text.size() && text.at(j) == u'`' && roles.at(j) == CharRole::Text)
            ++j;
        runs.append({i, j});
        i = j;
    }
    // The next run of the same length (one backwards pass).
    QList<qsizetype> next(runs.size(), -1);
    QHash<qsizetype, qsizetype> laterByLength;
    for (qsizetype r = runs.size() - 1; r >= 0; --r) {
        const qsizetype length = runs.at(r).to - runs.at(r).from;
        next[r] = laterByLength.value(length, -1);
        laterByLength.insert(length, r);
    }
    for (qsizetype r = 0; r < runs.size();) {
        if (next.at(r) < 0) {
            ++r;
            continue;
        }
        const Span &open = runs.at(r);
        const Span &close = runs.at(next.at(r));
        std::fill(roles.begin() + open.from, roles.begin() + open.to, CharRole::Drop);
        std::fill(roles.begin() + open.to, roles.begin() + close.from, CharRole::Verbatim);
        std::fill(roles.begin() + close.from, roles.begin() + close.to, CharRole::Drop);
        r = next.at(r) + 1;
    }
}

/// Mark the markers of `**bold**`, `__bold__`, `~~strike~~` and `==highlight==` pairs outside
/// verbatim parts: an opener followed and a closer preceded by a non-space, at least one
/// character between them, on one line; `__` not inside a word. Each kind pairs its earliest
/// open marker with the next possible closer (different kinds nest: `**==x==**`). Linear.
void markEmphasis(QStringView text, QList<CharRole> &roles)
{
    static constexpr std::array<char16_t, 4> Markers = {u'*', u'_', u'~', u'='};
    std::array<qsizetype, Markers.size()> open;
    open.fill(-1);
    const auto spaceAt = [&text](qsizetype i) { return i < 0 || i >= text.size() || text.at(i).isSpace(); };
    const auto wordAt = [&text](qsizetype i) { return i >= 0 && i < text.size() && text.at(i).isLetterOrNumber(); };
    for (qsizetype i = 0; i < text.size();) {
        const QChar c = text.at(i);
        if (c == u'\n' || c == u'\r') {
            open.fill(-1);
            ++i;
            continue;
        }
        const auto kind = std::find(Markers.cbegin(), Markers.cend(), c.unicode()) - Markers.cbegin();
        if (kind == qsizetype(Markers.size()) || i + 1 >= text.size() || text.at(i + 1) != c
            || roles.at(i) != CharRole::Text || roles.at(i + 1) != CharRole::Text) {
            ++i;
            continue;
        }
        const bool underscore = c == u'_';
        qsizetype &opener = open[size_t(kind)];
        if (opener >= 0 && !spaceAt(i - 1) && i > opener + 2 && !(underscore && wordAt(i + 2))) {
            roles[opener] = roles[opener + 1] = roles[i] = roles[i + 1] = CharRole::Drop;
            opener = -1;
        } else if (opener < 0 && !spaceAt(i + 2) && !(underscore && wordAt(i - 1))) {
            opener = i;
        }
        i += 2;
    }
}

/// `text` without code span backticks and emphasis markers; `verbatim` ranges are kept as is.
QString stripInlineMarkup(QStringView text, const QList<Span> &verbatim = {})
{
    QList<CharRole> roles(text.size(), CharRole::Text);
    for (const Span &span : verbatim)
        std::fill(roles.begin() + span.from, roles.begin() + span.to, CharRole::Verbatim);
    markCodeSpans(text, roles);
    markEmphasis(text, roles);
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (roles.at(i) != CharRole::Drop)
            out += text.at(i);
    }
    return out;
}

/// The text a link shows in a plain-text title (final: inserted verbatim). Display texts lose
/// their markup; page names, URLs and unknown references are kept as written.
QString linkText(const Link &link, QStringView source, const VaultIndex *index)
{
    switch (link.kind) {
    case LinkKind::Wiki:
    case LinkKind::Embed:
        if (!link.display.isEmpty())
            return stripInlineMarkup(link.display);
        if (!link.target.isEmpty())
            return link.heading.isEmpty() ? link.target : link.target + u" > "_s + link.heading;
        return link.heading;
    case LinkKind::Markdown: return link.display.isEmpty() ? link.target : stripInlineMarkup(link.display);
    case LinkKind::BlockRef:
    case LinkKind::BlockEmbed:
        if (index) {
            if (const auto ref = index->blockById(link.blockId)) {
                const ParsedNote *note = index->note(ref->path);
                if (note && ref->blockIndex >= 0 && ref->blockIndex < note->blocks.size()) {
                    // One level only, and a title's worth: many references to a huge block must
                    // not multiply its text.
                    const QStringView text = note->blocks.at(ref->blockIndex).text;
                    return elide(plainText(text.first(std::min(text.size(), MaxRefSource))),
                                 ScheduledNotification::MaxTitleLength);
                }
            }
        }
        break;
    case LinkKind::Url: return link.target;
    }
    return source.sliced(link.from, link.to - link.from).toString();
}

} // namespace

QString plainText(QStringView markdown, const VaultIndex *index)
{
    const markdown::InlineScan scan = markdown::extractInline(markdown);
    QString out;
    out.reserve(markdown.size());
    QList<Span> links; // link texts in `out`: no further markup processing
    qsizetype pos = 0;
    for (const Link &link : scan.links) {
        if (link.from < pos || link.to > markdown.size() || link.from >= link.to)
            continue;
        out += markdown.sliced(pos, link.from - pos);
        const qsizetype from = out.size();
        out += linkText(link, markdown, index);
        links.append({from, out.size()});
        pos = link.to;
    }
    out += markdown.sliced(pos);
    return stripInlineMarkup(out, links).simplified();
}

QString elide(const QString &text, int max)
{
    if (max <= 0)
        return {};
    if (text.size() <= max)
        return text;
    qsizetype cut = max - 1;
    if (cut > 0 && text.at(cut - 1).isHighSurrogate())
        --cut;
    while (cut > 0 && text.at(cut - 1).isSpace())
        --cut;
    return text.first(cut) + u'…';
}

QString formatStamp(const QDateTime &dt)
{
    if (!dt.isValid())
        return {};
    const QDateTime local = dt.toLocalTime();
    return local.date().toString(u"yyyy-MM-dd"_s) + u'T' + local.time().toString(u"HH:mm:ss.zzz"_s);
}

QDateTime parseStamp(QStringView text)
{
    const qsizetype t = text.indexOf(u'T');
    if (t < 0)
        return {};
    const auto date = dates::parseIsoDate(text.first(t));
    const QTime time = QTime::fromString(text.sliced(t + 1).toString(), Qt::ISODateWithMs);
    if (!date || !time.isValid())
        return {};
    return QDateTime(*date, time);
}

int probeNativeId(int candidate, const QSet<int> &used)
{
    constexpr int Max = 0x7FFF'FFFF;
    int value = candidate < 1 ? 1 : candidate;
    while (used.contains(value))
        value = value == Max ? 1 : value + 1;
    return value;
}

} // namespace brulion::reminders_detail
