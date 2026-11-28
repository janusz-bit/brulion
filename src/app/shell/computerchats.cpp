#include "computerchats.h"

#include "log/categories.h"

#include "chat/chat.h"
#include "sync/http.h"

#include <QCoreApplication>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

QString baseOf(const QString &url)
{
    return acp::normalizeRemoteBase(url).toString();
}

/// The shortest wait between two long polls (a computer that writes an answer changes ten times a
/// second: this device asks at most four).
constexpr int MinPollGapMs = 250;

} // namespace

struct ComputerChats::Poll {
    Computer computer;
    qint64 revision = -1;
    bool asking = false; ///< the long poll on its way
    bool quick = false;  ///< a request without a wait on its way
    int generation = 0;  ///< answers of an older one are left alone
    bool attached = false;
    bool gone = false; ///< unpaired there: asked no more
    int failures = 0;
    QTimer *retry = nullptr;
    QElapsedTimer lastOk;
    QList<acp::RemoteChat> rows;
    std::optional<acp::RemoteChatDetail> detail;
    struct Page {
        QString text;
        int messages = 0;
    };
    QHash<QString, Page> pages; ///< by the path in lower case
    QSet<QString> fetching;
};

ComputerChats::ComputerChats(QObject *parent) : QObject(parent) { }

ComputerChats::~ComputerChats()
{
    *m_alive = false;
    qDeleteAll(m_polls);
}

void ComputerChats::setRetryDelays(int networkMs, int appMs, int oldMs, int staleMs)
{
    m_networkRetryMs = std::max(1, networkMs);
    m_appRetryMs = std::max(1, appMs);
    m_oldRetryMs = std::max(1, oldMs);
    m_staleMs = std::max(0, staleMs);
}

void ComputerChats::setComputers(const QList<Computer> &computers)
{
    QList<Computer> clean;
    for (const Computer &c : computers) {
        Computer n = c;
        n.url = baseOf(c.url);
        if (n.url.isEmpty() || n.token.isEmpty()
            || std::ranges::any_of(clean, [&n](const Computer &o) { return o.url == n.url; }))
            continue;
        clean.append(n);
    }
    if (clean == this->computers())
        return;
    bool lost = false;
    // (A computer no longer paired, or paired anew: what it said goes.)
    for (qsizetype i = m_polls.size() - 1; i >= 0; --i) {
        Poll *p = m_polls.at(i);
        const auto same = std::ranges::find(clean, p->computer);
        if (same != clean.end())
            continue;
        lost = lost || !p->rows.isEmpty();
        forget(p);
        m_polls.removeAt(i);
        p->retry->deleteLater();
        delete p;
    }
    for (const Computer &c : clean) {
        if (pollOf(c.url))
            continue;
        auto *p = new Poll;
        p->computer = c;
        p->retry = new QTimer(this);
        p->retry->setSingleShot(true);
        connect(p->retry, &QTimer::timeout, this, [this, url = c.url] {
            if (Poll *poll = pollOf(url))
                ask(poll);
        });
        m_polls.append(p);
    }
    // (In the order given: the first computer that lists a conversation runs it.)
    std::ranges::sort(m_polls, {}, [&clean](const Poll *p) {
        return std::ranges::find_if(clean, [p](const Computer &c) { return c.url == p->computer.url; }) - clean.begin();
    });
    if (lost)
        Q_EMIT changed();
    for (Poll *p : std::as_const(m_polls))
        ask(p);
}

QList<ComputerChats::Computer> ComputerChats::computers() const
{
    QList<Computer> out;
    for (const Poll *p : m_polls)
        out.append(p->computer);
    return out;
}

void ComputerChats::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (m_active) {
        for (Poll *p : std::as_const(m_polls)) {
            if (!p->retry->isActive())
                ask(p);
        }
    }
}

void ComputerChats::watch(const QString &path)
{
    if (path.compare(m_watched, Qt::CaseInsensitive) == 0) {
        m_watched = path;
        return;
    }
    m_watched = path;
    if (const std::optional<Chat> listed = find(path))
        m_watched = listed->row.path; // (its own letter case: the computer's page)
    // (Another conversation shown: the detail of the one before is not followed any more.)
    for (Poll *p : std::as_const(m_polls)) {
        if (p->detail && p->detail->path.compare(path, Qt::CaseInsensitive) != 0) {
            const QString was = p->detail->path;
            p->detail.reset();
            Q_EMIT chatChanged(was);
        }
    }
    if (Poll *p = pollListing(path))
        askNow(p);
}

ComputerChats::Poll *ComputerChats::pollOf(const QString &url) const
{
    const QString base = baseOf(url);
    for (Poll *p : m_polls) {
        if (p->computer.url == base)
            return p;
    }
    return nullptr;
}

ComputerChats::Poll *ComputerChats::pollListing(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    for (Poll *p : m_polls) {
        if (std::ranges::any_of(p->rows, [&path](const acp::RemoteChat &c) {
                return c.path.compare(path, Qt::CaseInsensitive) == 0;
            }))
            return p;
    }
    return nullptr;
}

QString ComputerChats::hostOf(const Poll *p) const
{
    return p->computer.host.isEmpty() ? QUrl(p->computer.url).host() : p->computer.host;
}

void ComputerChats::ask(Poll *p)
{
    sync::HttpTransport *t = transport();
    if (!m_active || p->asking || p->gone || p->retry->isActive() || !t)
        return;
    p->asking = true;
    const bool lists = pollListing(m_watched) == p;
    const qint64 since = p->revision;
    const int generation = p->generation;
    const QString url = p->computer.url;
    std::weak_ptr<bool> alive = m_alive;
    acp::fetchRemoteChats(t, QUrl(url), p->computer.token, since, WaitSeconds, lists ? m_watched : QString(),
                          [this, alive, url, generation, since](const acp::RemoteChats &chats,
                                                                const acp::RemoteError &error) {
                              if (alive.expired())
                                  return;
                              Poll *poll = pollOf(url);
                              if (!poll || poll->generation != generation)
                                  return;
                              poll->asking = false;
                              // (A computer started again counts from 0: its answer is taken.)
                              if (!error.failed() && chats.revision < since)
                                  poll->revision = -1;
                              received(poll, chats, error, false, generation);
                          });
}

void ComputerChats::askNow(Poll *p)
{
    sync::HttpTransport *t = transport();
    if (p->quick || p->gone || !t)
        return;
    p->quick = true;
    const int generation = p->generation;
    const QString url = p->computer.url;
    std::weak_ptr<bool> alive = m_alive;
    acp::fetchRemoteChats(t, QUrl(url), p->computer.token, -1, 0, m_watched,
                          [this, alive, url, generation](const acp::RemoteChats &chats, const acp::RemoteError &error) {
                              if (alive.expired())
                                  return;
                              Poll *poll = pollOf(url);
                              if (!poll || poll->generation != generation)
                                  return;
                              poll->quick = false;
                              received(poll, chats, error, true, generation);
                          });
}

void ComputerChats::received(Poll *p, const acp::RemoteChats &chats, const acp::RemoteError &error, bool quick,
                             int generation)
{
    Q_UNUSED(generation)
    if (error.failed()) {
        if (!quick)
            failed(p, error);
        return;
    }
    const bool was = p->attached;
    p->failures = 0;
    p->lastOk.start();
    p->attached = true;
    if (!was)
        qCInfo(logging::lcApp).noquote() << u"The app on %1 shares its conversations"_s.arg(hostOf(p));
    if (chats.revision >= p->revision) {
        apply(p, chats, true);
    }
    if (!quick) {
        // (The next long poll; a short gap: an answer being written changes all the time.)
        p->retry->start(MinPollGapMs);
    }
}

void ComputerChats::failed(Poll *p, const acp::RemoteError &error)
{
    using Kind = acp::RemoteError::Kind;
    int wait = m_networkRetryMs;
    bool drop = false;
    if (error.kind == Kind::Unauthorized) {
        p->gone = true;
        drop = true;
        qCInfo(logging::lcApp).noquote() << u"%1 does not know this device any more"_s.arg(hostOf(p));
    } else if (error.kind == Kind::Refused && error.status == 404) {
        // (An older computer: no such endpoint.)
        drop = true;
        wait = m_oldRetryMs;
    } else if (error.code == u"app_unavailable" || (error.kind == Kind::Server && error.status == 503)) {
        drop = true;
        wait = m_appRetryMs;
    } else {
        ++p->failures;
        wait = std::min(m_networkRetryMs * (1 << std::min(p->failures - 1, 4)), m_networkRetryMs * 12);
        drop = !p->lastOk.isValid() || p->lastOk.elapsed() >= m_staleMs;
    }
    if (drop) {
        if (p->attached)
            qCInfo(logging::lcApp).noquote()
                    << u"The app on %1 does not share its conversations now: %2"_s.arg(hostOf(p), error.detail);
        p->attached = false;
        forget(p);
    }
    if (!p->gone)
        p->retry->start(wait);
}

void ComputerChats::forget(Poll *p)
{
    QStringList paths;
    for (const acp::RemoteChat &c : std::as_const(p->rows))
        paths.append(c.path);
    if (p->detail && !paths.contains(p->detail->path))
        paths.append(p->detail->path);
    p->rows.clear();
    p->detail.reset();
    p->revision = -1;
    if (paths.isEmpty())
        return;
    Q_EMIT changed();
    for (const QString &path : std::as_const(paths))
        Q_EMIT chatChanged(path);
}

void ComputerChats::apply(Poll *p, const acp::RemoteChats &chats, bool withDetail)
{
    p->revision = chats.revision;
    QSet<QString> touched;
    const auto key = [](const QString &path) { return path.toLower(); };
    QHash<QString, acp::RemoteChat> before;
    for (const acp::RemoteChat &c : std::as_const(p->rows))
        before.insert(key(c.path), c);
    bool listChanged = before.size() != chats.chats.size();
    QList<const acp::RemoteChat *> changedRows;
    for (const acp::RemoteChat &c : chats.chats) {
        const auto it = before.constFind(key(c.path));
        if (it == before.cend() || !(*it == c)) {
            touched.insert(c.path);
            listChanged = true;
            changedRows.append(&c);
        }
        before.remove(key(c.path));
    }
    for (const acp::RemoteChat &gone : std::as_const(before))
        touched.insert(gone.path);
    const bool newlyListed = !m_watched.isEmpty() && pollListing(m_watched) != p
                             && std::ranges::any_of(chats.chats, [this](const acp::RemoteChat &c) {
                                    return c.path.compare(m_watched, Qt::CaseInsensitive) == 0;
                                });
    p->rows = chats.chats;
    const int detailMessages = p->detail ? p->detail->messages : -1;
    if (withDetail) {
        std::optional<acp::RemoteChatDetail> detail = chats.detail;
        if (detail && detail->path.compare(m_watched, Qt::CaseInsensitive) != 0)
            detail.reset(); // (asked for one shown before)
        if (detail && detail != p->detail) {
            p->detail = detail;
            touched.insert(detail->path);
        } else if (!detail && p->detail && chats.detail) {
            touched.insert(p->detail->path);
            p->detail.reset();
        }
    }
    // A conversation of the watched path that runs there now: its detail at once.
    if (newlyListed)
        askNow(p);
    // (Pages are read again only when a row changed: an answer being written changes the detail
    // alone, many times a second.)
    for (const acp::RemoteChat *c : std::as_const(changedRows))
        maybeFetchPage(p, c->path, c->messages);
    if (p->detail && p->detail->messages != detailMessages)
        maybeFetchPage(p, p->detail->path, p->detail->messages);
    if (listChanged)
        Q_EMIT changed();
    for (const QString &path : std::as_const(touched))
        Q_EMIT chatChanged(path);
}

void ComputerChats::maybeFetchPage(Poll *p, const QString &path, int messages)
{
    const int local = m_localMessages ? m_localMessages(path) : 0;
    const auto it = p->pages.constFind(path.toLower());
    const int known = it == p->pages.cend() ? 0 : it->messages;
    if (messages > std::max(local, known))
        fetchPage(p, path);
}

void ComputerChats::fetchPage(Poll *p, const QString &path)
{
    sync::HttpTransport *t = transport();
    const QString key = path.toLower();
    if (!t || p->fetching.contains(key))
        return;
    p->fetching.insert(key);
    const int generation = p->generation;
    const QString url = p->computer.url;
    std::weak_ptr<bool> alive = m_alive;
    acp::fetchRemoteChatPage(t, QUrl(url), p->computer.token, path,
                             [this, alive, url, generation, path, key](const QString &text,
                                                                       const acp::RemoteError &error) {
                                 if (alive.expired())
                                     return;
                                 Poll *poll = pollOf(url);
                                 if (!poll || poll->generation != generation)
                                     return;
                                 poll->fetching.remove(key);
                                 if (error.failed()) {
                                     qCDebug(logging::lcApp).noquote()
                                             << u"The page %1 of %2 could not be read: %3"_s.arg(path, hostOf(poll),
                                                                                                error.detail);
                                     return;
                                 }
                                 const int messages = int(chat::parse(text).messages.size());
                                 Poll::Page &page = poll->pages[key];
                                 if (page.text == text)
                                     return;
                                 page.text = text;
                                 page.messages = messages;
                                 Q_EMIT pageChanged(path);
                                 // (More came meanwhile: asked again.)
                                 for (const acp::RemoteChat &c : std::as_const(poll->rows)) {
                                     if (c.path.compare(path, Qt::CaseInsensitive) == 0)
                                         maybeFetchPage(poll, c.path, c.messages);
                                 }
                             });
}

std::optional<ComputerChats::Chat> ComputerChats::find(const QString &path) const
{
    if (path.isEmpty())
        return std::nullopt;
    for (const Poll *p : m_polls) {
        for (const acp::RemoteChat &c : p->rows) {
            if (c.path.compare(path, Qt::CaseInsensitive) == 0)
                return Chat{c, p->computer.url, hostOf(p)};
        }
    }
    return std::nullopt;
}

const acp::RemoteChatDetail *ComputerChats::detail(const QString &path) const
{
    if (path.isEmpty())
        return nullptr;
    if (const Poll *p = pollListing(path); p && p->detail && p->detail->path.compare(path, Qt::CaseInsensitive) == 0)
        return &*p->detail;
    return nullptr;
}

QString ComputerChats::page(const QString &path) const
{
    const QString key = path.toLower();
    const int local = m_localMessages ? m_localMessages(path) : 0;
    for (const Poll *p : m_polls) {
        const auto it = p->pages.constFind(key);
        // (This device's copy caught up: its own.)
        if (it != p->pages.cend() && it->messages > local)
            return it->text;
    }
    return {};
}

int ComputerChats::pageMessages(const QString &path) const
{
    const QString key = path.toLower();
    int n = 0;
    for (const Poll *p : m_polls) {
        if (const auto it = p->pages.constFind(key); it != p->pages.cend())
            n = std::max(n, it->messages);
    }
    return n;
}

QList<ComputerChats::Chat> ComputerChats::chats() const
{
    QList<Chat> out;
    for (const Poll *p : m_polls) {
        for (const acp::RemoteChat &c : p->rows) {
            if (std::ranges::any_of(out, [&c](const Chat &o) { return o.row.path.compare(c.path, Qt::CaseInsensitive) == 0; }))
                continue;
            out.append({c, p->computer.url, hostOf(p)});
        }
    }
    std::ranges::stable_sort(out, {}, [](const Chat &c) { return c.row.active() ? 0 : 1; });
    return out;
}

bool ComputerChats::attached(const QString &url) const
{
    const Poll *p = pollOf(url);
    return p && p->attached;
}

std::optional<ComputerChats::Computer> ComputerChats::computerOf(const QString &path) const
{
    if (const Poll *p = pollListing(path))
        return p->computer;
    return std::nullopt;
}

QString ComputerChats::errorText(const QString &host, const acp::RemoteError &error)
{
    using Kind = acp::RemoteError::Kind;
    switch (error.kind) {
    case Kind::None:
        return {};
    case Kind::Network:
        return tr("%1 cannot be reached: %2").arg(host, error.detail);
    case Kind::Unauthorized:
        return tr("This device is no longer paired with %1: pair it again in Settings → Extensions → AI agents.")
                .arg(host);
    case Kind::Version:
        return tr("%1 runs another version of Brulion: update Brulion there and here.").arg(host);
    case Kind::Malformed:
        return tr("%1 gave an answer this device does not understand (%2).").arg(host, error.detail);
    case Kind::Refused:
    case Kind::Server:
        break;
    }
    if (error.code == u"app_unavailable")
        return tr("Brulion does not run on %1 now, or it does not share its agents with this device.").arg(host);
    if (error.code == u"no_question")
        return tr("The question on %1 was answered already.").arg(host);
    if (error.code == u"busy")
        return tr("Its turn on %1 is still running.").arg(host);
    if (error.code == u"not_found")
        return tr("%1 does not know this conversation (its notebook may not be this one).").arg(host);
    if (error.code == u"not_held")
        return tr("No messages wait on %1 any more.").arg(host);
    return tr("%1 refused it: %2").arg(host, error.detail.isEmpty() ? error.code : error.detail);
}

void ComputerChats::send(const QString &path, const QString &text, const QList<acp::RemotePicture> &pictures,
                         acp::SendMode mode, Done done)
{
    const Poll *p = pollListing(path);
    if (!p)
        return done(tr("This conversation does not run on another computer now."));
    sendTo(p->computer.url, path, text, pictures, mode,
           [done](const acp::RemoteError &, const QString &words) { done(words); });
}

void ComputerChats::sendTo(const QString &url, const QString &path, const QString &text,
                           const QList<acp::RemotePicture> &pictures, acp::SendMode mode,
                           std::function<void(const acp::RemoteError &, const QString &)> done)
{
    Poll *p = pollOf(url);
    sync::HttpTransport *t = transport();
    if (!p || !t) {
        const acp::RemoteError none{acp::RemoteError::Kind::Refused, 0, u"not_paired"_s, QString()};
        return done(none, tr("This device is not paired with that computer."));
    }
    const QString base = p->computer.url;
    const QString host = hostOf(p);
    std::weak_ptr<bool> alive = m_alive;
    acp::sendRemoteChat(t, QUrl(base), p->computer.token, path, text, mode, pictures,
                        [this, alive, base, host, done](const acp::RemoteError &error) {
                            if (alive.expired())
                                return;
                            done(error, errorText(host, error));
                            if (Poll *poll = pollOf(base))
                                askNow(poll);
                        });
}

void ComputerChats::act(const QString &path, const QString &endpoint, const QJsonObject &body, Done done)
{
    Poll *p = pollListing(path);
    sync::HttpTransport *t = transport();
    if (!p || !t)
        return done(tr("This conversation does not run on another computer now."));
    const QString url = p->computer.url;
    const QString host = hostOf(p);
    std::weak_ptr<bool> alive = m_alive;
    acp::postRemoteChat(t, QUrl(url), p->computer.token, endpoint, body,
                        [this, alive, url, host, done](const acp::RemoteError &error) {
                            if (alive.expired())
                                return;
                            done(errorText(host, error));
                            if (Poll *poll = pollOf(url))
                                askNow(poll);
                        });
}

void ComputerChats::stop(const QString &path, Done done)
{
    act(path, u"chats/stop"_s, {{u"path"_s, path}}, std::move(done));
}

void ComputerChats::answer(const QString &path, const QString &option, Done done)
{
    act(path, u"chats/answer"_s, {{u"path"_s, path}, {u"option"_s, option}}, std::move(done));
}

void ComputerChats::setOption(const QString &path, const QString &option, const QString &value, Done done)
{
    act(path, u"chats/option"_s, {{u"path"_s, path}, {u"option"_s, option}, {u"value"_s, value}}, std::move(done));
}

void ComputerChats::resume(const QString &path, Done done)
{
    act(path, u"chats/resume"_s, {{u"path"_s, path}}, std::move(done));
}

void ComputerChats::start(const QString &url, const QString &project, const QString &text, const QString &partner,
                          const QString &worktree, const QString &branch, const QList<acp::RemotePicture> &pictures,
                          std::function<void(const QString &path, const QString &error)> done)
{
    Poll *p = pollOf(url);
    sync::HttpTransport *t = transport();
    if (!p || !t)
        return done(QString(), tr("This device is not paired with that computer."));
    const QString base = p->computer.url;
    const QString host = hostOf(p);
    std::weak_ptr<bool> alive = m_alive;
    acp::startRemoteThread(t, QUrl(base), p->computer.token, project, text, partner, worktree, branch, pictures,
                           [this, alive, base, host, done](const QString &path, const acp::RemoteError &error) {
                               if (alive.expired())
                                   return;
                               done(path, errorText(host, error));
                               if (Poll *poll = pollOf(base))
                                   askNow(poll);
                           });
}

} // namespace brulion::shell
