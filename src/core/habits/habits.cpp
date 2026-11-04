// Habit names, definitions and checks of a note, statistics and the month grid (see habits.h).
#include "habits/habits.h"

#include "markdown/inline.h"

#include <QHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::habits {

namespace {

using Kind = Schedule::Kind;
using Period = Schedule::Period;

/// The longest month grid daysOf() fills.
constexpr int MaxGridDays = 400;

/// A property value as text: a list (of links or tags) joined with ", ", a date as YYYY-MM-DD.
QString propertyText(const QVariant &value)
{
    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().join(u", "_s);
    if (value.typeId() == QMetaType::QVariantList) {
        QStringList parts;
        for (const QVariant &v : value.toList())
            parts.append(propertyText(v));
        return parts.join(u", "_s);
    }
    if (value.typeId() == QMetaType::QDate)
        return value.toDate().toString(Qt::ISODate);
    if (value.typeId() == QMetaType::QDateTime)
        return value.toDateTime().date().toString(Qt::ISODate);
    return value.toString();
}

/// `start::` as written: 2026-10-01, [[2026-10-01]], a YAML date.
QDate startDate(const QVariant &value)
{
    if (!value.isValid())
        return {};
    if (value.typeId() == QMetaType::QDate)
        return value.toDate();
    if (value.typeId() == QMetaType::QDateTime)
        return value.toDateTime().date();
    QString s = propertyText(value).trimmed();
    if (s.startsWith(u"[["_s) && s.endsWith(u"]]"_s))
        s = s.sliced(2, s.size() - 4).trimmed();
    return QDate::fromString(s, Qt::ISODate);
}

/// The value of the first of `keys` among `props` (QVariant() when none).
QVariant firstOf(const Properties &props, std::initializer_list<QStringView> keys)
{
    for (const QStringView key : keys) {
        const auto it = props.constFind(key.toString());
        if (it != props.constEnd())
            return *it;
    }
    return {};
}

bool definesHabit(const Properties &props)
{
    return props.contains(u"habit"_s) || props.contains(u"nawyk"_s);
}

Habit makeHabit(const VaultPath &path, const QString &name, const Properties &props)
{
    Habit h;
    h.name = name;
    h.key = habitKey(name);
    h.scheduleText = propertyText(firstOf(props, {u"habit", u"nawyk"})).trimmed();
    h.schedule = parseSchedule(h.scheduleText);
    h.start = startDate(firstOf(props, {u"start", u"od"}));
    h.path = path;
    return h;
}

Mark markOf(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Done: return Mark::Done;
    case TaskStatus::Cancelled: return Mark::Skipped;
    default: return Mark::Open;
    }
}

bool isDaysKind(const Schedule &s)
{
    return s.kind != Kind::Times;
}

/// The Times block (`interval` periods) holding `day`, for a habit anchored at `start`: [first, last].
struct Span {
    QDate first;
    QDate last;
};

QDate addPeriods(Period period, QDate day, int n)
{
    switch (period) {
    case Period::Week: return day.addDays(qint64(n) * 7);
    case Period::Month: return day.addMonths(n);
    case Period::Year: return day.addYears(n);
    }
    return day;
}

int periodsBetween(Period period, QDate a, QDate b)
{
    switch (period) {
    case Period::Week: return int(a.daysTo(b) / 7);
    case Period::Month: return (b.year() - a.year()) * 12 + (b.month() - a.month());
    case Period::Year: return b.year() - a.year();
    }
    return 0;
}

Span blockOf(const Schedule &s, QDate start, QDate day, Qt::DayOfWeek weekStart)
{
    const int interval = std::max(1, s.interval);
    const QDate anchor = periodStart(s.period, start, weekStart);
    const int n = periodsBetween(s.period, anchor, periodStart(s.period, day, weekStart));
    // Floor division: a day before the anchor belongs to an earlier block.
    const int index = n >= 0 ? n / interval : -((-n + interval - 1) / interval);
    const QDate first = addPeriods(s.period, anchor, index * interval);
    return {first, addPeriods(s.period, first, interval).addDays(-1)};
}

/// Done and skipped days of [from, to].
struct Counts {
    int done = 0;
    int skipped = 0;
};

Counts countIn(const DayMarks &days, QDate from, QDate to)
{
    Counts c;
    if (from > to)
        return c;
    for (auto it = days.lowerBound(from); it != days.constEnd() && it.key() <= to; ++it) {
        if (*it == Mark::Done)
            ++c.done;
        else if (*it == Mark::Skipped)
            ++c.skipped;
    }
    return c;
}

/// What a Times block amounts to.
struct BlockResult {
    int done = 0;
    int target = 0;
    bool met = false;
    bool excused = false;
};

BlockResult evaluate(const Schedule &s, const DayMarks &days, const Span &b, QDate from, QDate today)
{
    const Counts c = countIn(days, std::max(b.first, from), std::min(b.last, today));
    BlockResult r;
    r.done = c.done;
    r.target = std::max(0, s.times - c.skipped);
    r.met = c.done > 0 && c.done >= r.target;
    r.excused = !r.met && r.target == 0;
    return r;
}

StreakUnit unitOf(const Schedule &s)
{
    if (s.kind == Kind::Days)
        return s.interval == 1 ? StreakUnit::Days : StreakUnit::Times;
    if (s.kind != Kind::Times)
        return StreakUnit::Times;
    switch (s.period) {
    case Period::Week: return StreakUnit::Weeks;
    case Period::Month: return StreakUnit::Months;
    case Period::Year: return StreakUnit::Years;
    }
    return StreakUnit::Times;
}

} // namespace

// ---- Names ------------------------------------------------------------------------------------------

QString habitName(QStringView text)
{
    const qsizetype eol = text.indexOf(u'\n');
    const QStringView line = eol < 0 ? text : text.first(eol);
    struct Cut {
        qsizetype from;
        qsizetype to;
        QString with;
    };
    QList<Cut> cuts;
    for (const markdown::TagSpan &tag : markdown::findTags(line))
        cuts.append({tag.from, tag.to, QString()});
    const markdown::InlineScan scan = markdown::extractInline(line);
    for (const Link &link : scan.links) {
        if (link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed)
            continue;
        QString label = link.display.trimmed();
        if (label.isEmpty()) {
            label = link.target;
            const qsizetype hash = label.indexOf(u'#');
            if (hash >= 0)
                label.truncate(hash);
            const qsizetype slash = label.lastIndexOf(u'/');
            if (slash >= 0)
                label = label.sliced(slash + 1);
            if (label.endsWith(u".md"_s, Qt::CaseInsensitive))
                label.chop(3);
        }
        cuts.append({link.from, link.to, label});
    }
    std::sort(cuts.begin(), cuts.end(), [](const Cut &a, const Cut &b) { return a.from < b.from; });
    QString out;
    qsizetype at = 0;
    for (const Cut &cut : std::as_const(cuts)) {
        if (cut.from < at || cut.to > line.size())
            continue; // inside an earlier cut (the link of a #[[tag]])
        out += line.sliced(at, cut.from - at);
        out += u' ' + cut.with + u' ';
        at = cut.to;
    }
    out += line.sliced(at);
    return out.simplified();
}

QString habitKey(QStringView name)
{
    return name.toString().normalized(QString::NormalizationForm_C).toCaseFolded().simplified();
}

bool isCheckTag(QStringView tag)
{
    return tag == u"nawyk" || tag == u"habit";
}

QString checkTag(dates::UiLocale locale)
{
    return locale == dates::UiLocale::English ? u"habit"_s : u"nawyk"_s;
}

// ---- Definitions and checks -----------------------------------------------------------------------

QList<Habit> definitionsOf(const VaultPath &path, const ParsedNote &note)
{
    QList<Habit> out;
    if (definesHabit(note.properties) && !note.title.trimmed().isEmpty()) {
        Habit h = makeHabit(path, note.title.trimmed(), note.properties);
        if (!h.key.isEmpty())
            out.append(std::move(h));
    }
    for (qsizetype i = 0; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        if (b.kind == BlockKind::Frontmatter || b.properties.isEmpty() || !definesHabit(b.properties))
            continue;
        const QString name = habitName(b.text);
        if (name.isEmpty())
            continue; // leading `habit::` lines of a page: the page is the habit
        Habit h = makeHabit(path, name, b.properties);
        h.blockIndex = int(i);
        h.line = b.line;
        out.append(std::move(h));
    }
    return out;
}

QList<Check> checksOf(const ParsedNote &note)
{
    QList<Check> out;
    if (!note.journalDate || !note.journalDate->isValid())
        return out;
    for (qsizetype i = 0; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        if (!b.task || b.tags.isEmpty() || definesHabit(b.properties))
            continue;
        if (std::none_of(b.tags.cbegin(), b.tags.cend(), [](const QString &t) { return isCheckTag(t); }))
            continue;
        Check c;
        c.name = habitName(b.task->text);
        c.key = habitKey(c.name);
        if (c.key.isEmpty())
            continue;
        c.date = *note.journalDate;
        c.mark = markOf(b.task->status);
        c.blockIndex = int(i);
        c.line = b.line;
        out.append(std::move(c));
    }
    return out;
}

Mark strongerMark(Mark a, Mark b)
{
    return int(a) >= int(b) ? a : b;
}

// ---- Statistics -------------------------------------------------------------------------------------

QDate effectiveStart(const Habit &habit, const DayMarks &days, QDate today)
{
    if (habit.start.isValid())
        return habit.start;
    if (!days.isEmpty() && days.firstKey() <= today)
        return days.firstKey();
    return today;
}

Stats statsOf(const Habit &habit, const DayMarks &days, QDate today, Qt::DayOfWeek weekStart)
{
    Stats st;
    if (!habit.schedule || !today.isValid())
        return st;
    const Schedule &s = *habit.schedule;
    st.unit = unitOf(s);
    st.start = effectiveStart(habit, days, today);
    const auto todayIt = days.constFind(today);
    st.todayRecorded = todayIt != days.constEnd();
    st.todayMark = st.todayRecorded ? *todayIt : Mark::Open;
    for (auto it = days.constBegin(); it != days.constEnd() && it.key() <= today; ++it) {
        if (*it == Mark::Done)
            st.lastDone = it.key();
    }
    if (st.start > today)
        return st;
    const QDate from = std::max(st.start, today.addDays(-MaxCountedDays));
    st.done = countIn(days, from, today).done;

    int run = 0;
    if (isDaysKind(s)) {
        for (QDate d = from; d <= today; d = d.addDays(1)) {
            if (!isScheduled(s, st.start, d, weekStart))
                continue;
            const auto it = days.constFind(d);
            const Mark mark = it == days.constEnd() ? Mark::Open : *it;
            if (mark == Mark::Done) {
                ++run;
                ++st.met;
                ++st.expected;
                st.longestStreak = std::max(st.longestStreak, run);
            } else if (mark == Mark::Skipped) {
                continue;
            } else if (d < today) {
                run = 0;
                ++st.expected;
            } else {
                st.dueToday = true; // today, not done yet: pending
            }
        }
        st.currentStreak = run;
        return st;
    }

    for (Span b = blockOf(s, st.start, from, weekStart); b.first <= today;
         b = blockOf(s, st.start, b.last.addDays(1), weekStart)) {
        const BlockResult r = evaluate(s, days, b, from, today);
        const bool current = b.last >= today;
        const bool partial = b.first < from;
        if (current) {
            st.periodDone = r.done;
            st.periodTarget = r.target;
            st.dueToday = !r.met && !r.excused;
        }
        if (r.met) {
            ++run;
            ++st.met;
            ++st.expected;
            st.longestStreak = std::max(st.longestStreak, run);
        } else if (r.excused || current || partial) {
            continue;
        } else {
            run = 0;
            ++st.expected;
        }
    }
    st.currentStreak = run;
    return st;
}

QList<DayState> daysOf(const Habit &habit, const DayMarks &days, QDate from, QDate to, QDate today,
                       Qt::DayOfWeek weekStart)
{
    QList<DayState> out;
    if (!from.isValid() || !to.isValid() || from > to || !today.isValid())
        return out;
    if (from.daysTo(to) >= MaxGridDays)
        to = from.addDays(MaxGridDays - 1);
    const QDate start = effectiveStart(habit, days, today);
    // A Times habit's today is due while its period still needs a day.
    bool periodOpen = false;
    if (habit.schedule && habit.schedule->kind == Kind::Times && today >= from && today <= to && start <= today) {
        const Span b = blockOf(*habit.schedule, start, today, weekStart);
        const BlockResult r =
                evaluate(*habit.schedule, days, b, std::max(start, today.addDays(-MaxCountedDays)), today);
        periodOpen = !r.met && !r.excused;
    }
    out.reserve(from.daysTo(to) + 1);
    for (QDate d = from; d <= to; d = d.addDays(1)) {
        const auto it = days.constFind(d);
        const Mark mark = it == days.constEnd() ? Mark::Open : *it;
        if (d > today)
            out.append(DayState::Future);
        else if (mark == Mark::Done)
            out.append(DayState::Done);
        else if (mark == Mark::Skipped)
            out.append(DayState::Skipped);
        else if (d < start || !habit.schedule)
            out.append(DayState::Outside);
        else if (habit.schedule->kind == Kind::Times)
            out.append(d == today && periodOpen ? DayState::Due : DayState::Rest);
        else if (isScheduled(*habit.schedule, start, d, weekStart))
            out.append(d == today ? DayState::Due : DayState::Missed);
        else
            out.append(DayState::Rest);
    }
    return out;
}

// ---- Names for JSON and QML ---------------------------------------------------------------------------

QString markName(Mark mark)
{
    switch (mark) {
    case Mark::Open: return u"open"_s;
    case Mark::Skipped: return u"skipped"_s;
    case Mark::Done: return u"done"_s;
    }
    return {};
}

std::optional<Mark> markFromName(QStringView name)
{
    const QString n = name.trimmed().toString().toCaseFolded();
    static const QHash<QString, Mark> names = {
            {u"done"_s, Mark::Done},         {u"x"_s, Mark::Done},
            {u"zrobione"_s, Mark::Done},     {u"zrobiony"_s, Mark::Done},
            {u"zrobiono"_s, Mark::Done},     {u"tak"_s, Mark::Done},
            {u"yes"_s, Mark::Done},          {u"skipped"_s, Mark::Skipped},
            {u"skip"_s, Mark::Skipped},      {u"cancelled"_s, Mark::Skipped},
            {u"pominięte"_s, Mark::Skipped}, {u"pominiete"_s, Mark::Skipped},
            {u"pominięty"_s, Mark::Skipped}, {u"pominiety"_s, Mark::Skipped},
            {u"pomiń"_s, Mark::Skipped},     {u"pomin"_s, Mark::Skipped},
            {u"open"_s, Mark::Open},         {u"none"_s, Mark::Open},
            {u"todo"_s, Mark::Open},         {u"undo"_s, Mark::Open},
            {u"brak"_s, Mark::Open},         {u"nie"_s, Mark::Open},
            {u"no"_s, Mark::Open},           {u"otwarte"_s, Mark::Open},
    };
    const auto it = names.constFind(n);
    if (it == names.constEnd())
        return std::nullopt;
    return *it;
}

QString dayStateName(DayState state)
{
    switch (state) {
    case DayState::Outside: return u"outside"_s;
    case DayState::Future: return u"future"_s;
    case DayState::Rest: return u"rest"_s;
    case DayState::Due: return u"due"_s;
    case DayState::Done: return u"done"_s;
    case DayState::Skipped: return u"skipped"_s;
    case DayState::Missed: return u"missed"_s;
    }
    return {};
}

QString streakUnitName(StreakUnit unit)
{
    switch (unit) {
    case StreakUnit::Days: return u"days"_s;
    case StreakUnit::Times: return u"times"_s;
    case StreakUnit::Weeks: return u"weeks"_s;
    case StreakUnit::Months: return u"months"_s;
    case StreakUnit::Years: return u"years"_s;
    }
    return {};
}

} // namespace brulion::habits
