// LimitsReader — the subscription limits of agents' accounts read over the network
// (agentlimits/limits.h), at most every RefreshMs per account: an account is a provider and its
// sign-in file, so two agents of one account (Claude Code with two models) share one reading.
// refresh() reads them when the last reading that reached the provider is older than that (a
// reading without a request — no sign-in file, an expired token — is tried again at once: it costs
// nothing), refresh(force) at once unless one is on its way. The sign-in file is read for each
// request; its token lives only in that request (and is registered with the event log's
// redaction while it is on its way), never kept in the reader, never logged.
//
//   LimitsReader reader(&transport);
//   connect(&reader, &LimitsReader::changed, …);
//   reader.refresh({Provider::Claude, u"/home/ola/.claude/.credentials.json"_s});
//   reader.limits(account).windows …
//
// Single-threaded (the thread that owns the transport).
#pragma once

#include "agentlimits/limits.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>

namespace brulion::agentlimits {

/// An account whose limits are read: its provider and sign-in file.
struct Account {
    Provider provider = Provider::None;
    QString file;
    /// "claude:/home/ola/.claude/.credentials.json".
    QString key() const { return providerName(provider) + u':' + file; }
    bool valid() const { return provider != Provider::None && !file.isEmpty(); }
};

class LimitsReader : public QObject {
    Q_OBJECT
public:
    /// The shortest time between two requests for one account (but a forced refresh).
    static constexpr qint64 RefreshMs = 5 * 60 * 1000;

    /// `transport` is not owned and must outlive the reader.
    explicit LimitsReader(sync::HttpTransport *transport, QObject *parent = nullptr);
    ~LimitsReader() override;

    /// The time (default: now) — tests.
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    /// How a sign-in file is read (default: readCredentials()) — tests.
    using CredentialsReader = std::function<Credentials(Provider, const QString &file)>;
    void setCredentialsReader(CredentialsReader read) { m_read = std::move(read); }

    /// The last limits of `account` (checkedAt invalid: never read).
    Limits limits(const Account &account) const;
    /// A request for it is on its way.
    bool busy(const Account &account) const;
    /// Reads them when they are older than RefreshMs (or never reached the provider); `force`: now
    /// (a manual refresh) unless a request is on its way. True when a reading started.
    bool refresh(const Account &account, bool force = false);

Q_SIGNALS:
    /// limits() or busy() of the account with `key` changed.
    void changed(const QString &key);

private:
    struct Entry {
        Limits limits;
        qint64 requestedAt = -1; ///< ms since the epoch of the last request that went out
        bool busy = false;
    };
    QDateTime now() const;
    void store(const QString &key, const Limits &limits);

    sync::HttpTransport *m_transport;
    std::function<QDateTime()> m_clock;
    CredentialsReader m_read;
    QHash<QString, Entry> m_entries;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::agentlimits
