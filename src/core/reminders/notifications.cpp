// Reminder occurrences -> ScheduledNotification (see notifications.h).
#include "reminders/notifications.h"

#include "dates/format.h"
#include "index/vaultindex.h"
#include "reminders/reminders_p.h"
#include "text/text.h"

#include <QHash>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <optional>

namespace brulion {

using namespace Qt::StringLiterals;
using namespace reminders_detail;

namespace {

/// Largest horizon accepted by upcomingNotifications() (ten years).
constexpr int MaxHorizonDays = 3660;

/// " · termin: dziś 17:00" / " · due today 17:00" for the notification body.
QString dueInfo(const DateSpec &due, dates::UiLocale locale, QDate day)
{
    const QString when = dates::formatHuman(due, locale, day);
    if (when.isEmpty())
        return {};
    return (locale == dates::UiLocale::Polish ? u" · termin: "_s : u" · due "_s) + when;
}

/// An occurrence with its time as milliseconds since the epoch: comparing QDateTimes in local
/// time converts them on every comparison.
struct Timed {
    qint64 ms = 0;
    const ScheduledNotification *n = nullptr;
};

/// By time, then id (the order of every list here).
bool earlier(const Timed &a, const Timed &b)
{
    if (a.ms != b.ms)
        return a.ms < b.ms;
    return a.n->id < b.n->id;
}

/// The first `max` of `items` in time order (all when `max` < 0), copied.
QList<ScheduledNotification> firstInTime(QList<Timed> &items, qsizetype max)
{
    const qsizetype keep = max < 0 ? items.size() : std::min(max, items.size());
    if (keep < items.size())
        std::partial_sort(items.begin(), items.begin() + keep, items.end(), earlier);
    else
        std::sort(items.begin(), items.end(), earlier);
    QList<ScheduledNotification> out;
    out.reserve(keep);
    for (qsizetype i = 0; i < keep; ++i)
        out.append(*items[i].n);
    return out;
}

/// notificationId() with the text already hashed (text::contentHash()).
QString idOf(const VaultPath &path, const QDateTime &at, QStringView textHash)
{
    QString key = path;
    key += u'|';
    key += at.date().toString(u"yyyy-MM-dd"_s);
    key += u'T';
    key += at.time().toString(u"HH:mm"_s);
    key += u'|';
    key += textHash;
    return text::fnv1a64Hex(key.toUtf8());
}

/// What the reminders of one block share: computed once per block, so a huge block with many
/// reminders costs its size once, not once per reminder occurrence.
class BlockInfo {
public:
    /// `note`: the reminder's note when at hand (else looked up).
    BlockInfo(const VaultIndex &index, const ParsedNote *note, const ReminderRef &ref)
        : m_index(index)
        , m_path(ref.path)
        , m_block(ref.blockIndex)
        , m_src(note ? sourceOf(note, ref) : sourceOf(index, ref))
    { }

    bool isFor(const ReminderRef &ref) const { return ref.blockIndex == m_block && ref.path == m_path; }
    const ReminderSource &source() const { return m_src; }

    /// text::contentHash() of the source text (for ids).
    const QString &textHash()
    {
        if (m_textHash.isNull())
            m_textHash = text::contentHash(m_src.text);
        return m_textHash;
    }

    /// Plain block text, else the page title; elided.
    const QString &title()
    {
        if (m_title.isNull()) {
            const QString plain = plainText(m_src.text, &m_index);
            m_title = elide(plain.isEmpty() ? m_src.pageTitle : plain, ScheduledNotification::MaxTitleLength);
        }
        return m_title;
    }

private:
    const VaultIndex &m_index;
    VaultPath m_path;
    int m_block;
    ReminderSource m_src;
    QString m_textHash;
    QString m_title;
};

/// The occurrences of note `path` within [from, to] (local times), merged by id (ids contain the
/// path: no other note has them), unsorted, nativeIds not assigned. `capped`: a series gave
/// MaxOccurrences dates (a longer window might differ further on).
void collectRefs(const VaultIndex &index, const ParsedNote *note, const QList<ReminderRef> &refs, const QDateTime &from,
                 const QDateTime &to, dates::UiLocale locale, QList<ScheduledNotification> &out, bool *capped = nullptr)
{
    QHash<QString, qsizetype> byId;
    std::optional<BlockInfo> block; // remindersOf() lists the reminders of a block together
    for (const ReminderRef &ref : refs) {
        if (!block || !block->isFor(ref))
            block.emplace(index, note, ref);
        const ReminderSource &src = block->source();
        if (isClosed(src.task))
            continue;
        const QList<DateSpec> specs = reminderDates(ref, true, from.date(), to.date());
        if (capped && specs.size() >= MaxOccurrences)
            *capped = true;
        for (const DateSpec &spec : specs) {
            const QDateTime at = spec.toLocalDateTime();
            if (at < from || at > to)
                continue;
            const QString id = idOf(ref.path, at, block->textHash());
            if (const auto it = byId.constFind(id); it != byId.cend()) {
                out[*it].alarm = out[*it].alarm || ref.reminder.alarm;
                continue;
            }
            ScheduledNotification n;
            n.id = id;
            n.title = block->title();
            n.body = src.pageTitle;
            if (src.task && src.task->due)
                n.body += dueInfo(*src.task->due, locale, at.date());
            n.at = at;
            n.alarm = ref.reminder.alarm;
            n.path = ref.path;
            n.line = ref.reminder.line;
            n.taskLine = src.task ? src.task->line : -1;
            byId.insert(id, out.size());
            out.append(std::move(n));
        }
    }
}

/// collectRefs() of one note (its reminders that may occur within [from, to]).
void collectNote(const VaultIndex &index, const VaultPath &path, const ParsedNote &note, const QDateTime &from,
                 const QDateTime &to, dates::UiLocale locale, QList<ScheduledNotification> &out, bool *capped = nullptr)
{
    const QList<ReminderRef> refs = remindersOf(path, note, from.date(), to.date());
    if (!refs.isEmpty())
        collectRefs(index, &note, refs, from, to, locale, out, capped);
}

/// The local time zone as part of the kept days' key: the kept occurrences' instants (`ms`) hold
/// only while it stays — after a change (travel, an automatic time zone) every note is computed
/// again, so reminders keep their local wall-clock times. The zone's id (also a TZ rule string)
/// and the offsets at both ends of the kept days (local time goes through the C library, which
/// may see a change the id does not).
QString zoneKey(const QDateTime &dayStart, const QDateTime &dayEnd)
{
    QString key = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    key += u'|';
    key += QString::number(dayStart.offsetFromUtc());
    key += u'|';
    key += QString::number(dayEnd.offsetFromUtc());
    return key;
}

/// The occurrences of one note for the kept days.
struct NoteOccurrences {
    QList<ScheduledNotification> list;
    QList<qint64> ms;    ///< list's times since the epoch (ms), in the local zone of the key
    bool capped = false; ///< a series was cut at MaxOccurrences: requests compute the note directly
};

} // namespace

QString notificationId(const VaultPath &path, const QDateTime &at, QStringView text)
{
    return idOf(path, at.toLocalTime(), text::contentHash(text));
}

int nativeIdFor(QStringView id)
{
    const quint64 hash = text::fnv1a64(id.toUtf8());
    const int value = static_cast<int>(hash & 0x7FFF'FFFFULL);
    return value == 0 ? 1 : value;
}

void assignNativeIds(QList<ScheduledNotification> &notifications)
{
    QSet<int> used;
    used.reserve(notifications.size());
    for (ScheduledNotification &n : notifications) {
        n.nativeId = probeNativeId(nativeIdFor(n.id), used);
        used.insert(n.nativeId);
    }
}

// The free functions are a NotificationCache without days: every note computed for the request
// (collectNote(), as for the cache's days) — one code path.
QList<ScheduledNotification> notificationsBetween(const VaultIndex &index, const QDateTime &from, const QDateTime &to,
                                                  dates::UiLocale locale)
{
    return NotificationCache().between(index, from, to, locale);
}

QList<ScheduledNotification> upcomingNotifications(const VaultIndex &index, const QDateTime &now, int horizonDays,
                                                   int max, int *dropped, dates::UiLocale locale)
{
    return NotificationCache().upcoming(index, now, horizonDays, max, dropped, locale);
}

// ---- NotificationCache ----------------------------------------------------------------------------

struct NotificationCache::Private {
    QDate first;
    QDate last;
    NoteCache<NoteOccurrences> notes; ///< notes with occurrences in [first, last]
    NoteCacheSync lastSync;
    int computed = 0;
};

NotificationCache::NotificationCache() : d(std::make_unique<Private>()) { }
NotificationCache::~NotificationCache() = default;
NotificationCache::NotificationCache(NotificationCache &&) noexcept = default;
NotificationCache &NotificationCache::operator=(NotificationCache &&) noexcept = default;

void NotificationCache::setDays(QDate first, QDate last)
{
    d->first = first;
    d->last = last;
}

QDate NotificationCache::firstDay() const
{
    return d->first;
}

QDate NotificationCache::lastDay() const
{
    return d->last;
}

QList<ScheduledNotification> NotificationCache::occurrences(const VaultIndex &index, const QDateTime &from,
                                                            const QDateTime &to, dates::UiLocale locale, qsizetype max,
                                                            qsizetype *total)
{
    // `from` and `to` are local. Reminders are local wall-clock times: the window's dates are
    // local dates, whatever time spec the caller used (e.g. a UTC "now").
    QList<ScheduledNotification> direct; // computed for this request (all notes, or capped ones)
    QList<Timed> timed;
    const bool kept = d->first.isValid() && d->last.isValid() && d->first <= d->last && from.date() >= d->first
                      && to.date() <= d->last;
    if (!kept) {
        d->lastSync = NoteCacheSync();
        forEachNoteReminders(index, from.date(), to.date(), [&](const VaultPath &, const QList<ReminderRef> &refs) {
            collectRefs(index, nullptr, refs, from, to, locale, direct);
        });
    } else {
        const QDateTime dayStart(d->first, QTime(0, 0));
        const QDateTime dayEnd(d->last, QTime(23, 59, 59, 999));
        const QString key = d->first.toString(Qt::ISODate) + u'|' + d->last.toString(Qt::ISODate) + u'|'
                            + QString::number(int(locale)) + u'|' + zoneKey(dayStart, dayEnd);
        d->lastSync = d->notes.sync(
                index, key, false,
                [&](const VaultPath &path, const ParsedNote &note) {
                    NoteOccurrences occurrences;
                    ++d->computed;
                    collectNote(index, path, note, dayStart, dayEnd, locale, occurrences.list, &occurrences.capped);
                    occurrences.ms.reserve(occurrences.list.size());
                    for (const ScheduledNotification &n : std::as_const(occurrences.list))
                        occurrences.ms.append(n.at.toMSecsSinceEpoch());
                    return occurrences;
                },
                [](const NoteOccurrences &o) { return !o.list.isEmpty() || o.capped; },
                [&](std::map<VaultPath, NoteOccurrences> &values) {
                    // Everything: from the index's reminder list (notes by path) — contiguous.
                    forEachNoteReminders(index, d->first, d->last,
                                         [&](const VaultPath &path, const QList<ReminderRef> &refs) {
                                             NoteOccurrences occurrences;
                                             collectRefs(index, nullptr, refs, dayStart, dayEnd, locale,
                                                         occurrences.list, &occurrences.capped);
                                             occurrences.ms.reserve(occurrences.list.size());
                                             for (const ScheduledNotification &n : std::as_const(occurrences.list))
                                                 occurrences.ms.append(n.at.toMSecsSinceEpoch());
                                             if (!occurrences.list.isEmpty() || occurrences.capped)
                                                 values.emplace_hint(values.end(), path, std::move(occurrences));
                                         });
                    d->computed += int(index.paths().size()); // as many notes as a sync note by note
                });
        const qint64 fromMs = from.toMSecsSinceEpoch();
        const qint64 toMs = to.toMSecsSinceEpoch();
        for (const auto &[path, occurrences] : d->notes.values()) {
            if (occurrences.capped) { // the series' dates depend on the window: as asked
                if (const ParsedNote *note = index.note(path))
                    collectNote(index, path, *note, from, to, locale, direct);
                continue;
            }
            for (qsizetype i = 0; i < occurrences.list.size(); ++i) {
                if (occurrences.ms[i] >= fromMs && occurrences.ms[i] <= toMs)
                    timed.append(Timed{occurrences.ms[i], &occurrences.list[i]});
            }
        }
    }
    for (const ScheduledNotification &n : std::as_const(direct)) // `direct` is complete: pointers stay
        timed.append(Timed{n.at.toMSecsSinceEpoch(), &n});
    if (total)
        *total = timed.size();
    return firstInTime(timed, max);
}

QList<ScheduledNotification> NotificationCache::between(const VaultIndex &index, const QDateTime &from,
                                                        const QDateTime &to, dates::UiLocale locale)
{
    if (!from.isValid() || !to.isValid() || from > to)
        return {};
    QList<ScheduledNotification> out = occurrences(index, from.toLocalTime(), to.toLocalTime(), locale, -1, nullptr);
    assignNativeIds(out);
    return out;
}

QList<ScheduledNotification> NotificationCache::upcoming(const VaultIndex &index, const QDateTime &now, int horizonDays,
                                                         int max, int *dropped, dates::UiLocale locale)
{
    if (dropped)
        *dropped = 0;
    if (!now.isValid())
        return {};
    // Days of the horizon are local calendar days (also across a DST change).
    const QDateTime local = now.toLocalTime();
    const QDateTime from = local.addSecs(-60);
    const QDateTime to = local.addDays(std::clamp(horizonDays, 0, MaxHorizonDays));
    qsizetype total = 0;
    QList<ScheduledNotification> out = occurrences(index, from, to, locale, std::max(max, 0), &total);
    if (dropped)
        *dropped = int(total - out.size());
    assignNativeIds(out);
    return out;
}

const NoteCacheSync &NotificationCache::lastSync() const
{
    return d->lastSync;
}

int NotificationCache::computedNotes() const
{
    return d->computed;
}

void NotificationCache::clear()
{
    d->notes.clear();
    d->lastSync = NoteCacheSync();
}

} // namespace brulion
