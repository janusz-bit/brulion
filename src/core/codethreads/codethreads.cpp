// A Code mode thread's place (codethreads.h): its page's frontmatter read and edited.
#include "codethreads/codethreads.h"

#include "chat/chat.h"
#include "dates/natural.h"
#include "markdown/frontmatter.h"

using namespace Qt::StringLiterals;

namespace brulion::codethreads {

namespace {

/// The value of `key` in the frontmatter (any letter case), "" when it has none.
QString valueOf(const QVariantMap &data, QStringView key)
{
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        if (QStringView(it.key()).compare(key, Qt::CaseInsensitive) == 0)
            return it.value().toString().trimmed();
    return {};
}

/// `content` with `key` set to `value` ("" takes it out) — chat::setField's edit, byte for byte.
QString withField(const QString &content, QStringView key, const QString &value)
{
    return chat::setField(content, key, value);
}

/// 9:00 of `date`.
QDateTime morningOf(QDate date) { return QDateTime(date, QTime(9, 0)); }

/// A fixed preset's end ("" not one).
std::optional<QDateTime> presetEnd(QStringView preset, const QDateTime &now)
{
    if (preset == u"1h")
        return now.addSecs(3600);
    if (preset == u"3h")
        return now.addSecs(3 * 3600);
    if (preset == u"evening") {
        const QDateTime today(now.date(), QTime(18, 0));
        return today > now ? today : today.addDays(1);
    }
    if (preset == u"tomorrow")
        return morningOf(now.date().addDays(1));
    if (preset == u"monday")
        return morningOf(now.date().addDays(8 - now.date().dayOfWeek()));
    return std::nullopt;
}

} // namespace

QString formatTime(const QDateTime &time) { return time.toString(u"yyyy-MM-dd HH:mm"_s); }

QDateTime parseTime(QStringView value)
{
    QString text = value.trimmed().toString();
    if (text.size() >= 2 && (text.front() == u'"' || text.front() == u'\'') && text.back() == text.front())
        text = text.mid(1, text.size() - 2).trimmed();
    for (const QString &format : {u"yyyy-MM-dd HH:mm"_s, u"yyyy-MM-ddTHH:mm"_s, u"yyyy-MM-ddTHH:mm:ss"_s})
        if (const QDateTime t = QDateTime::fromString(text, format); t.isValid())
            return QDateTime(t.date(), QTime(t.time().hour(), t.time().minute()));
    const QDate day = QDate::fromString(text, u"yyyy-MM-dd"_s);
    return day.isValid() ? QDateTime(day, QTime(0, 0)) : QDateTime();
}

State readState(QStringView content)
{
    // (The frontmatter's YAML reads a bare date as text too: every value is taken as written.)
    const QVariantMap data = markdown::parseFrontmatter(content).data;
    State state;
    state.settled = parseTime(valueOf(data, SettledKey));
    state.snoozedUntil = parseTime(valueOf(data, SnoozedKey));
    state.neverAutoSettle = valueOf(data, AutoSettleKey).compare(u"never"_s, Qt::CaseInsensitive) == 0;
    return state;
}

QString settle(const QString &content, const QDateTime &at)
{
    return withField(withField(content, SnoozedKey, {}), SettledKey, formatTime(at));
}

QString unsettle(const QString &content) { return withField(content, SettledKey, {}); }

QString snooze(const QString &content, const QDateTime &until)
{
    if (!until.isValid())
        return wake(content);
    return withField(withField(content, SettledKey, {}), SnoozedKey, formatTime(until));
}

QString wake(const QString &content) { return withField(content, SnoozedKey, {}); }

QString setNeverAutoSettle(const QString &content, bool never)
{
    return withField(content, AutoSettleKey, never ? u"never"_s : QString());
}

QString reactivated(const QString &content) { return wake(unsettle(content)); }

Section sectionOf(const State &state, const Facts &facts, const QDateTime &now)
{
    if (facts.archived)
        return Section::Archived;
    if (facts.needsUser)
        return facts.pinned ? Section::Pinned : Section::Active;
    if (state.snoozedUntil.isValid() && state.snoozedUntil > now)
        return Section::Snoozed;
    if (state.settled.isValid())
        return Section::Settled;
    return facts.pinned ? Section::Pinned : Section::Active;
}

QString sectionName(Section section)
{
    switch (section) {
    case Section::Pinned: return u"pinned"_s;
    case Section::Active: return u"active"_s;
    case Section::Snoozed: return u"snoozed"_s;
    case Section::Settled: return u"settled"_s;
    case Section::Archived: return u"archived"_s;
    }
    return {};
}

std::optional<QDateTime> snoozeEnd(QStringView preset, const QDateTime &now)
{
    const QStringView text = preset.trimmed();
    if (text.isEmpty() || !now.isValid())
        return std::nullopt;
    if (const std::optional<QDateTime> fixed = presetEnd(text, now))
        return fixed;
    // Words or digits, in either language (the parser reads both).
    const std::optional<dates::NaturalDate> found = dates::parseNaturalDate(text, now);
    if (!found)
        return std::nullopt;
    const QDateTime end = found->spec.hasTime() ? found->spec.toLocalDateTime() : morningOf(found->spec.date);
    if (end <= now)
        return std::nullopt;
    return end;
}

bool settlesFromInactivity(const State &state, const Facts &facts, const QDateTime &lastActivity,
                           const QDateTime &now, int days, bool busy)
{
    if (days <= 0 || busy || !lastActivity.isValid() || facts.archived)
        return false;
    if (state.settled.isValid() || state.neverAutoSettle)
        return false;
    return lastActivity.addDays(days) <= now;
}

PullRequest pullRequestOf(QStringView state)
{
    const QString s = state.trimmed().toString().toLower();
    if (s == u"open" || s == u"draft")
        return PullRequest::Open;
    if (s == u"merged")
        return PullRequest::Merged;
    return s == u"closed" ? PullRequest::Closed : PullRequest::None;
}

bool settlesFromPullRequest(const State &state, const Facts &facts, PullRequest pr, bool busy)
{
    if (pr != PullRequest::Merged && pr != PullRequest::Closed)
        return false;
    return !busy && !facts.archived && !state.settled.isValid() && !state.neverAutoSettle;
}

Cleanup cleanupOf(QStringView setting)
{
    if (setting == u"merged")
        return {Cleanup::When::Merged};
    bool ok = false;
    const int days = setting.toInt(&ok);
    return ok && days > 0 ? Cleanup{Cleanup::When::Days, days} : Cleanup{};
}

bool removesWorktree(const State &state, PullRequest pr, const Cleanup &cleanup, const QDateTime &now, int changes,
                     bool othersActive)
{
    if (cleanup.when == Cleanup::When::Never || !state.settled.isValid() || changes != 0 || othersActive)
        return false;
    if (pr == PullRequest::Merged)
        return true;
    return cleanup.when == Cleanup::When::Days && state.settled.addDays(cleanup.days) <= now;
}

} // namespace brulion::codethreads
