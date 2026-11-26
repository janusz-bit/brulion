#pragma once
// ComputerChats — the conversations the app on the user's other computers runs, followed and steered
// from this device (docs/CODE.md "The computer's own turns on the phone"; core acp/appchats.h): an AI
// agent the desktop app started there — a thread of Code mode, a conversation with an agent — is seen
// here as it works and steered from here, through that computer's agent sharing (`brulion-cli acp
// serve`, the desktop app's "Share agents with the phone", which hands the app's own conversations on
// while the app runs: GET info's feature acp::ChatsFeature).
//
// Each computer paired with this device (setComputers(): an address and a token, as RemoteProjects')
// is asked GET chats in a long poll while this device follows them (setActive(): the app in front,
// or a conversation of theirs followed while it runs): its conversations that run, wait or ask there
// and those that ended lately (acp::RemoteChat), and in full (acp::RemoteChatDetail: the answer being
// written, tool calls, plan, the question with its options, the messages waiting) the one this
// device shows (watch()). The page of a conversation whose messages there outnumber this device's
// copy (the notebook's sync is slower) is fetched too (page()), as is the page of one whose turn ended
// there. WorkspaceChats mirrors all of it into the conversation's ChatTurn (`remoteHost`) and routes
// what the user does with it — answer, Stop, the next message (queued, steered) — here (send(), stop(),
// answer()).
//
// A computer whose app does not run answers 503 (asked again every RetryAppMs), an older one has no
// such endpoint (asked again every RetryOldMs); a network gone keeps the last state for StaleMs
// before its conversations are taken as gone; a device unpaired there asks no more.
//
//   chats->setComputers({{u"http://nixos.tail1ea20e.ts.net:8790"_s, token, u"nixos"_s}});
//   chats->setActive(true);
//   chats->watch(u"AI/2026-11-26 Popraw testy.md"_s);          // → chatChanged(path) as it works
//   chats->answer(path, u"Allow once"_s, [](const QString &error) { … });
//
// Single-threaded (the GUI thread).

#include "acp/appchats.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace brulion::sync {
class HttpTransport;
}

namespace brulion::shell {

class ComputerChats : public QObject {
    Q_OBJECT
public:
    explicit ComputerChats(QObject *parent = nullptr);
    ~ComputerChats() override;

    /// A computer this device is paired with.
    struct Computer {
        QString url; ///< its agents' address (acp::normalizeRemoteBase())
        QString token;
        QString host; ///< its name ("nixos"; "" the address's host)
        friend bool operator==(const Computer &, const Computer &) = default;
    };
    /// A conversation of a computer's app as it said last.
    struct Chat {
        acp::RemoteChat row;
        QString url;  ///< the computer's address
        QString host; ///< its name
    };

    /// How long the long poll waits on the computer (seconds), and the waits after a failure.
    static constexpr int WaitSeconds = 25;
    static constexpr int RetryAppMs = 20000;
    static constexpr int RetryOldMs = 10 * 60 * 1000;
    static constexpr int StaleMs = 2 * 60 * 1000;

    void setTransport(std::function<sync::HttpTransport *()> transport) { m_transport = std::move(transport); }
    /// The computers (one per address, the first token of each); changed: asked anew.
    void setComputers(const QList<Computer> &computers);
    QList<Computer> computers() const;
    /// Asking them goes on (the app in front, or a conversation of theirs followed) or rests after the
    /// requests on their way.
    void setActive(bool active);
    bool active() const { return m_active; }
    /// The conversation this device shows ("" none): its detail is asked for with the computer that
    /// runs it, at once and in every long poll after.
    void watch(const QString &path);
    QString watched() const { return m_watched; }
    /// Tests: the waits after failures (the network's grows from `networkMs` to 12 times as long).
    void setRetryDelays(int networkMs, int appMs, int oldMs, int staleMs);

    /// The conversation `path` as a computer's app said last (the first computer that lists it).
    std::optional<Chat> find(const QString &path) const;
    /// Its detail (nullptr: not the watched one, or not asked yet).
    const acp::RemoteChatDetail *detail(const QString &path) const;
    /// Its page as that computer has it ("" not fetched), and how many messages it has.
    QString page(const QString &path) const;
    int pageMessages(const QString &path) const;
    /// Every conversation the computers' apps list now, the running ones first.
    QList<Chat> chats() const;
    /// The computer at `url` shares its app's conversations (its app runs; the last poll answered).
    bool attached(const QString &url) const;
    /// The computer that lists `path` ({} none).
    std::optional<Computer> computerOf(const QString &path) const;

    /// How many messages this device's copy of a conversation has (WorkspaceChats: the page read
    /// here) — a computer's page with more is fetched.
    void setLocalMessages(std::function<int(const QString &path)> count) { m_localMessages = std::move(count); }

    using Done = std::function<void(const QString &error)>;
    /// The next message of `path` (its computer's: the one that lists it), now, queued or steered in.
    void send(const QString &path, const QString &text, acp::SendMode mode, Done done);
    /// Stops its turn there.
    void stop(const QString &path, Done done);
    /// Answers its question with `option` (one of its detail's options).
    void answer(const QString &path, const QString &option, Done done);
    /// A new thread of `project` started by the app at `url` (its path, or the error in words).
    void start(const QString &url, const QString &project, const QString &text, const QString &partner,
               const QString &worktree, const QString &branch,
               std::function<void(const QString &path, const QString &error)> done);

    /// `error` in the user's words for the computer `host`.
    static QString errorText(const QString &host, const acp::RemoteError &error);

Q_SIGNALS:
    /// The list of conversations changed (one came, went, changed its state).
    void changed();
    /// `path`'s row or detail changed.
    void chatChanged(const QString &path);
    /// `path`'s page from the computer came (page()).
    void pageChanged(const QString &path);

private:
    struct Poll;
    Poll *pollOf(const QString &url) const;
    Poll *pollListing(const QString &path) const;
    void ask(Poll *p);
    void askNow(Poll *p);
    void received(Poll *p, const acp::RemoteChats &chats, const acp::RemoteError &error, bool quick, int generation);
    void failed(Poll *p, const acp::RemoteError &error);
    void apply(Poll *p, const acp::RemoteChats &chats, bool withDetail);
    void forget(Poll *p);
    void fetchPage(Poll *p, const QString &path);
    void maybeFetchPage(Poll *p, const QString &path, int messages);
    QString hostOf(const Poll *p) const;
    sync::HttpTransport *transport() const { return m_transport ? m_transport() : nullptr; }

    std::function<sync::HttpTransport *()> m_transport;
    std::function<int(const QString &)> m_localMessages;
    QList<Poll *> m_polls;
    bool m_active = false;
    QString m_watched;
    int m_networkRetryMs = 3000;
    int m_appRetryMs = RetryAppMs;
    int m_oldRetryMs = RetryOldMs;
    int m_staleMs = StaleMs;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::shell
