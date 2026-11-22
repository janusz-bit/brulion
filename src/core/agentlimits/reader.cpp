#include "agentlimits/reader.h"

#include "log/categories.h"
#include "log/redact.h"

using namespace Qt::StringLiterals;

namespace brulion::agentlimits {

using logging::lcApp;

LimitsReader::LimitsReader(sync::HttpTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport), m_read(&readCredentials)
{
}

LimitsReader::~LimitsReader()
{
    *m_alive = false;
}

QDateTime LimitsReader::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

Limits LimitsReader::limits(const Account &account) const
{
    if (const auto it = m_entries.constFind(account.key()); it != m_entries.cend())
        return it->limits;
    Limits none;
    none.provider = account.provider;
    return none;
}

bool LimitsReader::busy(const Account &account) const
{
    const auto it = m_entries.constFind(account.key());
    return it != m_entries.cend() && it->busy;
}

void LimitsReader::store(const QString &key, const Limits &limits)
{
    Entry &e = m_entries[key];
    e.limits = limits;
    e.busy = false;
    Q_EMIT changed(key);
}

bool LimitsReader::refresh(const Account &account, bool force)
{
    if (!account.valid() || !m_transport)
        return false;
    const QString key = account.key();
    Entry &entry = m_entries[key];
    if (entry.busy)
        return false;
    const QDateTime at = now();
    const qint64 nowMs = at.toMSecsSinceEpoch();
    if (!force && entry.requestedAt >= 0 && nowMs - entry.requestedAt < RefreshMs)
        return false;

    // The sign-in, read now: a token is never kept beyond this request.
    const Credentials credentials = m_read(account.provider, account.file);
    if (!credentials.ok() || (credentials.expiresAt.isValid() && credentials.expiresAt <= at)) {
        Limits failed;
        failed.provider = account.provider;
        failed.checkedAt = at;
        failed.failure = credentials.ok() ? Failure::Expired
                         : credentials.failure == Failure::None ? Failure::NoCredentials
                                                                : credentials.failure;
        entry.requestedAt = -1; // (nothing went out: tried again the next time)
        qCInfo(lcApp).noquote() << u"%1 limits not read: %2"_s.arg(providerName(account.provider),
                                                                     failureCode(failed.failure));
        store(key, failed);
        return false;
    }
    entry.busy = true;
    entry.requestedAt = nowMs;
    Q_EMIT changed(key);
    const sync::HttpRequest request = usageRequest(account.provider, credentials);
    // (The token in the log's redaction while the request is on its way.)
    auto secret = std::make_shared<logging::ScopedSecret>(credentials.token);
    const Provider provider = account.provider;
    m_transport->send(request, [this, alive = m_alive, key, provider, secret](const sync::HttpResponse &response) {
        if (!*alive)
            return;
        const Limits read = usageFrom(provider, response, now());
        if (read.ok())
            qCInfo(lcApp).noquote() << u"%1 limits read: %2 windows"_s.arg(providerName(provider))
                                                  .arg(read.windows.size());
        else
            qCInfo(lcApp).noquote() << u"%1 limits not read: %2 (HTTP %3)"_s.arg(
                    providerName(provider), failureCode(read.failure)).arg(response.status);
        store(key, read);
    });
    return true;
}

} // namespace brulion::agentlimits
