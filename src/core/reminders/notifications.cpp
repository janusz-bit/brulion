// Reminder occurrences -> ScheduledNotification (see notifications.h).
#include "reminders/notifications.h"

#include "dates/format.h"
#include "index/vaultindex.h"
#include "reminders/reminders_p.h"
#include "text/text.h"

#include <QHash>
#include <QSet>

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

bool earlier(const ScheduledNotification &a, const ScheduledNotification &b)
{
    if (a.at != b.at)
        return a.at < b.at;
    return a.id < b.id;
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
    BlockInfo(const VaultIndex &index, const ReminderRef &ref)
        : m_index(index), m_path(ref.path), m_block(ref.blockIndex), m_src(sourceOf(index, ref))
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

/// All occurrences within [from, to], deduplicated by id and sorted; nativeIds not assigned.
QList<ScheduledNotification> collect(const VaultIndex &index, const QDateTime &start, const QDateTime &end,
                                     dates::UiLocale locale)
{
    QList<ScheduledNotification> out;
    if (!start.isValid() || !end.isValid() || start > end)
        return out;
    // Reminders are local wall-clock times: the window's dates are local dates, whatever time
    // spec the caller used (e.g. a UTC "now").
    const QDateTime from = start.toLocalTime();
    const QDateTime to = end.toLocalTime();
    QHash<QString, qsizetype> byId;
    std::optional<BlockInfo> block; // index.reminders() lists the reminders of a block together
    const QList<ReminderRef> refs = index.reminders();
    for (const ReminderRef &ref : refs) {
        if (!block || !block->isFor(ref))
            block.emplace(index, ref);
        const ReminderSource &src = block->source();
        if (isClosed(src.task))
            continue;
        const QList<DateSpec> specs = reminderDates(ref, true, from.date(), to.date());
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
            byId.insert(id, out.size());
            out.append(std::move(n));
        }
    }
    std::sort(out.begin(), out.end(), earlier);
    return out;
}

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

QList<ScheduledNotification> notificationsBetween(const VaultIndex &index, const QDateTime &from, const QDateTime &to,
                                                  dates::UiLocale locale)
{
    QList<ScheduledNotification> out = collect(index, from, to, locale);
    assignNativeIds(out);
    return out;
}

QList<ScheduledNotification> upcomingNotifications(const VaultIndex &index, const QDateTime &now, int horizonDays,
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
    QList<ScheduledNotification> out = collect(index, from, to, locale);
    const qsizetype keep = std::clamp<qsizetype>(max, 0, out.size());
    if (dropped)
        *dropped = int(out.size() - keep);
    out.resize(keep);
    assignNativeIds(out);
    return out;
}

} // namespace brulion
