#pragma once
// The conversations the desktop app runs, followed and steered from another device (docs/CODE.md
// "The computer's own turns on the phone", docs/CLI.md "AI agents for other devices" → "The app's own
// conversations"): an AI agent the user started in the app on the computer — a thread of Code mode,
// any conversation with an agent — is seen on the phone as it works (its answer as it is written,
// its tool calls, its plan, the question it asks) and steered from there: the question answered,
// the turn stopped, the next message sent, queued or steered in.
//
//   app (desktop)  ── ChatControl over the app's control socket (a grant of its own: the agent
//                     sharing's, ipc GrantScope::Share) ──  brulion-cli acp serve  ── HTTPS ──  phone
//
//   GET  chats?since=R&wait=S&path=P → RemoteChats: {revision, chats: [RemoteChat], detail?}
//        at once when the app's revision is past R, else when it changes (S seconds at most: a long
//        poll); `detail` is the conversation P's (its answer being written, tools, plan, question)
//   GET  chats/page?path=P           → {path, text} the conversation's page as the computer has it (the
//                                      phone's copy may lag behind the notebook's sync)
//   POST chats/send   {path, text, mode: "" | "queue" | "steer", pictures?} → {path}
//   POST chats/start  {project, text, partner?, worktree?, branch?, pictures?} → {path} a new thread
//                                    in the app
//   POST chats/stop   {path}         → {stopped}
//   POST chats/answer {path, option} → {answered} — one of its question's options
//   POST chats/option {path, option, value} → {path} — one of its agent's options (detail's `options`)
//   POST chats/resume {path}         → {resumed} — the messages held after its turn stopped or failed
//                                      go on (detail's `held`)
//
// `pictures`: [{name, data}] — each a picture's file name and its bytes (base64), as the device
// prepared it for the model (at most MaxRemotePictures, MaxRemotePictureBytes each,
// MaxRemotePicturesBytes in all); the app stores them in its notebook and sends them with the text.
//
// The host offers them (GET info's features: ChatsFeature) only while the app that started it
// answers (its agent sharing: `acp serve` with the app's grant in its environment). Errors are the
// host's (RemoteError): 503 "app_unavailable" when the app does not answer, 404 "not_found", 409
// "busy" (a turn runs: send with mode "queue"), 409 "no_question", 409 "not_held", 400 "bad_option",
// 400 "bad_pictures".
//
// Pure (QtCore): the types, their JSON (ChatControl writes it, acp serve passes it on, the phone reads
// it, each bounding what it takes), the device's requests over sync::HttpTransport as remote.h's.

#include "acp/codehost.h"
#include "acp/remote.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace brulion::acp {

/// The feature a host names (GET info's `features`) while the app on that computer shares its own
/// conversations.
inline constexpr QLatin1StringView ChatsFeature{"chats"};
/// The conversations one answer lists at most, the text of a live answer, a question's details and
/// a file's text in it, the messages waiting, the tool calls and steps of a plan.
inline constexpr int MaxRemoteChats = 50;
inline constexpr qsizetype MaxLiveTextChars = 64 * 1024;
inline constexpr qsizetype MaxLiveThoughtsChars = 2000;
inline constexpr qsizetype MaxQuestionChars = 16 * 1024;
inline constexpr int MaxLiveWaiting = 20;
inline constexpr int MaxLiveTools = 100;
inline constexpr int MaxLiveSteps = 50;
inline constexpr int MaxQuestionOptions = 8;
/// A conversation that ended stays listed for this long (the device hears how it ended).
inline constexpr int RecentChatMinutes = 10;
/// The longest page chats/page gives (a longer one: 413).
inline constexpr qsizetype MaxRemotePageBytes = 4 * 1024 * 1024;
/// The agent's options a detail lists at most, and the choices of one.
inline constexpr int MaxLiveOptions = 20;
inline constexpr int MaxOptionChoices = 100;
/// The pictures one message takes at most: how many, the bytes of one, of all (a device sends them
/// as it prepared them for the model: ai::Image, at most 1568 pixels a side).
inline constexpr int MaxRemotePictures = 8;
inline constexpr qsizetype MaxRemotePictureBytes = 4 * 1024 * 1024;
inline constexpr qsizetype MaxRemotePicturesBytes = 8 * 1024 * 1024;

/// A conversation of the app (its turn runs, waits, asks — or it ended lately).
struct RemoteChat {
    QString path;        ///< its page in the notebook ("AI/2026-11-26 Popraw testy.md")
    QString name;        ///< without its date
    QString partner;     ///< "acp:claude"
    QString partnerName; ///< "Claude Code"
    /// "running", "queued" (waits for the model on the computer), "asking" (waits for the user),
    /// "failed", "idle" (ended lately: `ended`).
    QString state;
    QString status;   ///< what it does now ("Reading „Dom”…"; "" none)
    QString question; ///< asking: the question's first line
    QString failure;  ///< failed: why
    QString project;  ///< a thread of Code mode: the project's id ("" none), its worktree and branch
    QString worktree;
    QString branch;
    int messages = 0; ///< the messages of its page there
    int waiting = 0;  ///< messages written while it runs, waiting for its turn to end
    bool held = false; ///< they wait for chats/resume: the turn before them stopped or failed
    QString ended;    ///< when its turn ended, ISO ("" one that runs)
    bool active() const { return state == u"running" || state == u"queued" || state == u"asking"; }
    friend bool operator==(const RemoteChat &, const RemoteChat &) = default;
};

/// What the agent asks the user (an AI agent's session/request_permission, or an extension's choice).
struct RemoteChatQuestion {
    QString text;        ///< its title (first line)
    QString details;     ///< the rest
    QStringList options; ///< the answers, as the app words them (one goes back: answerRemoteChat())
    QString plan;        ///< a plan it asks to approve (Markdown; "" none)
    QString file;        ///< the file a change it asks for is about ("" none), its texts before and after
    QString oldText;
    QString newText;
    bool isEmpty() const { return options.isEmpty(); }
    friend bool operator==(const RemoteChatQuestion &, const RemoteChatQuestion &) = default;
};

/// A tool call of the running turn, a step of its plan.
struct RemoteChatTool {
    QString title;
    QString status; ///< "pending", "in_progress", "completed", "failed"
    QString kind;   ///< ACP's ("read", "edit", "execute" …; "" none)
    friend bool operator==(const RemoteChatTool &, const RemoteChatTool &) = default;
};
struct RemoteChatStep {
    QString content;
    QString status; ///< "pending", "in_progress", "completed"
    friend bool operator==(const RemoteChatStep &, const RemoteChatStep &) = default;
};

/// One of the agent's options for the conversation (a model, a mode, a reasoning effort …: ACP's
/// session config options), as the app on the computer has it now.
struct RemoteChatChoice {
    QString value;
    QString name;
    QString group; ///< "" none
    friend bool operator==(const RemoteChatChoice &, const RemoteChatChoice &) = default;
};
struct RemoteChatOption {
    QString id;
    QString name;
    QString description;
    bool boolean = false; ///< on or off ("true" / "false"), no choices
    QString current;      ///< its value now
    QString currentName;  ///< … in words
    QList<RemoteChatChoice> choices;
    friend bool operator==(const RemoteChatOption &, const RemoteChatOption &) = default;
};

/// A picture sent with a message: its file name ("photo.jpg") and bytes.
struct RemotePicture {
    QString name;
    QByteArray data;
    friend bool operator==(const RemotePicture &, const RemotePicture &) = default;
};

/// One conversation as it is now, in full (GET chats with `path`).
struct RemoteChatDetail {
    QString path;
    QString state; ///< as RemoteChat::state ("idle" for one that is not running)
    QString status;
    QString partial;  ///< the answer being written (Markdown; its end when longer than MaxLiveTextChars)
    bool partialCut = false;
    QString thoughts; ///< the end of what the model thinks meanwhile
    QString failure;
    QList<RemoteChatTool> tools;
    QList<RemoteChatStep> plan;
    RemoteChatQuestion question;
    QStringList waiting; ///< the messages waiting for its turn to end, oldest first
    bool held = false;   ///< … held: they go on with chats/resume (its turn stopped or failed)
    QList<RemoteChatOption> options; ///< its agent's options ([] none: no agent, or none offered)
    int messages = 0;
    friend bool operator==(const RemoteChatDetail &, const RemoteChatDetail &) = default;
};

/// GET chats: the app's conversations now.
struct RemoteChats {
    /// Changes whenever anything of them changes (a long poll waits for a newer one).
    qint64 revision = 0;
    QList<RemoteChat> chats;
    std::optional<RemoteChatDetail> detail;
    /// Bounded, odd entries dropped (no path, a path twice, not a conversation's page); nullopt for
    /// no revision.
    static std::optional<RemoteChats> fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
    friend bool operator==(const RemoteChats &, const RemoteChats &) = default;
};

QJsonObject toJson(const RemoteChat &chat);
QJsonObject toJson(const RemoteChatDetail &detail);
std::optional<RemoteChat> remoteChatFromJson(const QJsonObject &o);
std::optional<RemoteChatDetail> remoteChatDetailFromJson(const QJsonObject &o);
QJsonObject toJson(const RemoteChatOption &option);
std::optional<RemoteChatOption> remoteChatOptionFromJson(const QJsonObject &o);
/// chats/send's and chats/start's `pictures`.
QJsonArray toJson(const QList<RemotePicture> &pictures);
/// … read where they come in: nullopt (`*why` says why) for anything but a list of at most
/// MaxRemotePictures pictures — each a file name of a picture's extension (its folders dropped) and
/// bytes in base64, MaxRemotePictureBytes at most, MaxRemotePicturesBytes in all. Undefined or null:
/// none.
std::optional<QList<RemotePicture>> remotePicturesFromJson(const QJsonValue &value, QString *why = nullptr);
/// `text` cut to its last `max` characters (a surrogate pair kept whole): an answer's end.
QString keepEnd(const QString &text, qsizetype max, bool *cut = nullptr);
/// A path a device may name: a page of the notebook's AI folder ("AI/…​.md"), relative, no "..".
bool isRemoteChatPath(const QString &path);

/// How a message goes to a conversation whose turn runs: refused ("" — 409 busy), after it
/// ("queue"), ahead of what waits, now ("steer": as T3 Code's steer).
enum class SendMode { Now, Queue, Steer };

/// GET chats?since=&wait=&path=: an answer at once when the host's revision is past `since` (-1:
/// always at once), else when it changes, `waitSeconds` (≤ RemoteWaitSeconds) at most.
void fetchRemoteChats(sync::HttpTransport *transport, const QUrl &base, const QString &token, qint64 since,
                      int waitSeconds, const QString &path,
                      std::function<void(const RemoteChats &chats, const RemoteError &error)> done);
/// GET chats/page?path=: the conversation's page there.
void fetchRemoteChatPage(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                         std::function<void(const QString &text, const RemoteError &error)> done);
/// POST chats/send (with `pictures`, none: []).
void sendRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                    const QString &text, SendMode mode, const QList<RemotePicture> &pictures,
                    std::function<void(const RemoteError &error)> done);
/// POST chats/start: a new thread of `project` in the app there ("" worktree: the project's folder);
/// `done` gets its page's path.
void startRemoteThread(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                       const QString &text, const QString &partner, const QString &worktree, const QString &branch,
                       const QList<RemotePicture> &pictures,
                       std::function<void(const QString &path, const RemoteError &error)> done);
/// POST chats/stop.
void stopRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                    std::function<void(const RemoteError &error)> done);
/// POST chats/answer.
void answerRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                      const QString &option, std::function<void(const RemoteError &error)> done);
/// POST chats/option: the agent's option `option` of the conversation set to `value` (a choice's
/// value; "true" / "false" for one that is boolean).
void setRemoteChatOption(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                         const QString &option, const QString &value,
                         std::function<void(const RemoteError &error)> done);
/// POST `endpoint` (chats/stop, chats/answer, chats/option, chats/resume) with `body`, as the functions
/// above do: `done` gets the host's error (none: it was done).
void postRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &endpoint,
                    const QJsonObject &body, std::function<void(const RemoteError &error)> done);
/// POST chats/resume: its held messages go on.
void resumeRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                      std::function<void(const RemoteError &error)> done);

} // namespace brulion::acp
