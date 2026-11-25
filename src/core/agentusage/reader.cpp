#include "agentusage/reader.h"

#include "log/log.h"

#include <QMetaObject>

using namespace Qt::StringLiterals;

namespace brulion::agentusage {

UsageReader::UsageReader(const QString &cacheDir, QObject *parent) : QObject(parent), m_cacheDir(cacheDir)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(u"agent-usage"_s);
}

UsageReader::~UsageReader()
{
    for (const Entry &e : std::as_const(m_entries)) {
        if (e.cancel)
            e.cancel->store(true);
    }
    m_pool.waitForDone();
}

QDateTime UsageReader::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

History UsageReader::history(const Source &source) const
{
    const auto it = m_entries.constFind(source.key());
    if (it == m_entries.constEnd()) {
        History none;
        none.provider = source.provider;
        return none;
    }
    return it->history;
}

bool UsageReader::busy(const Source &source) const
{
    const auto it = m_entries.constFind(source.key());
    return it != m_entries.constEnd() && it->busy;
}

QDateTime UsageReader::requestedAt(const Source &source) const
{
    const auto it = m_entries.constFind(source.key());
    if (it == m_entries.constEnd() || it->requestedAt < 0)
        return {};
    return QDateTime::fromMSecsSinceEpoch(it->requestedAt, QTimeZone::UTC);
}

bool UsageReader::refresh(const Source &source, bool force)
{
    if (!source.valid())
        return false;
    const QString key = source.key();
    Entry &e = m_entries[key];
    if (e.busy)
        return false;
    const qint64 at = now().toMSecsSinceEpoch();
    if (!force && e.requestedAt >= 0 && at - e.requestedAt < RefreshMs)
        return false;
    e.requestedAt = at;
    e.busy = true;
    e.cancel = std::make_shared<std::atomic_bool>(false);
    Q_EMIT changed(key);
    ReadOptions options;
    options.cacheFile = m_cacheDir.isEmpty() ? QString() : cacheFileFor(m_cacheDir, source);
    options.timeZone = m_zone;
    options.now = now();
    // (The destructor cancels the readings and waits for them: `self` outlives every job; a result
    // queued to a reader destroyed meanwhile is dropped.)
    UsageReader *self = this;
    m_pool.start([self, source, key, options, cancel = e.cancel]() mutable {
        options.cancel = cancel.get();
        ReadStats stats;
        const History history = readHistory(source, options, &stats);
        QMetaObject::invokeMethod(
                self,
                [self, key, history, stats] {
                    if (!stats.cancelled)
                        qCInfo(logging::lcApp).noquote()
                                << u"%1 usage read: %2 files, %3 changed, %4 requests counted, %5 repeats left out%6"_s
                                           .arg(agentlimits::providerName(history.provider))
                                           .arg(history.files)
                                           .arg(stats.filesRead)
                                           .arg(stats.events)
                                           .arg(stats.duplicates)
                                           .arg(stats.cacheRebuilt ? u" (cache rebuilt)"_s : QString());
                    self->finished(key, history);
                },
                Qt::QueuedConnection);
    });
    return true;
}

void UsageReader::finished(const QString &key, const History &history)
{
    Entry &e = m_entries[key];
    e.busy = false;
    e.cancel.reset();
    // (A cancelled reading keeps the last history.)
    if (history.error != u"cancelled")
        e.history = history;
    Q_EMIT changed(key);
}

void UsageReader::waitForDone()
{
    m_pool.waitForDone();
}

} // namespace brulion::agentusage
