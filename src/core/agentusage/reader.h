// UsageReader — the usage history of agents' session logs (agentusage/history.h) read off the
// calling thread, at most every RefreshMs per log source: a source is a provider and its log
// folders, so two agents of one account (Claude Code with two models) share one reading.
// refresh() reads when the last reading started longer ago than that, refresh(force) at once
// unless one is on its way. One reading at a time (a pool of one thread), each cancellable: a
// reader destroyed cancels what runs and waits for it.
//
//   UsageReader reader(cacheDir);
//   connect(&reader, &UsageReader::changed, …);
//   reader.refresh(sourceOf(Provider::Claude, command, environment, home));
//   report(reader.history(source), lastDays(today, 30)) …
//
// Owned by one thread (the GUI's); the readings run on the reader's pool.
#pragma once

#include "agentusage/history.h"

#include <QHash>
#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

namespace brulion::agentusage {

class UsageReader : public QObject {
    Q_OBJECT
public:
    /// The shortest time between two readings of one source (but a forced refresh).
    static constexpr qint64 RefreshMs = 5 * 60 * 1000;

    /// `cacheDir`: the app's cache folder (the cache files go into its agent-usage/; "" none).
    explicit UsageReader(const QString &cacheDir, QObject *parent = nullptr);
    ~UsageReader() override;

    /// The time (default: now) and the days' time zone (default: the computer's) — tests.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    void setTimeZone(const QTimeZone &zone) { m_zone = zone; }

    /// The last history of `source` (readAt invalid: never read).
    History history(const Source &source) const;
    /// A reading of it is on its way.
    bool busy(const Source &source) const;
    /// When the last reading of it started (invalid: never).
    QDateTime requestedAt(const Source &source) const;
    /// Reads it when the last reading is older than RefreshMs; `force`: now (Refresh) unless one is
    /// on its way. True when a reading started.
    bool refresh(const Source &source, bool force = false);
    /// Waits until every reading has ended (tests, brulion-cli).
    void waitForDone();

Q_SIGNALS:
    /// history() or busy() of the source with `key` changed.
    void changed(const QString &key);

private:
    struct Entry {
        History history;
        qint64 requestedAt = -1;
        bool busy = false;
        std::shared_ptr<std::atomic_bool> cancel;
    };
    QDateTime now() const;
    void finished(const QString &key, const History &history);

    QString m_cacheDir;
    std::function<QDateTime()> m_clock;
    QTimeZone m_zone = QTimeZone::LocalTime;
    QHash<QString, Entry> m_entries;
    QThreadPool m_pool;
};

} // namespace brulion::agentusage
