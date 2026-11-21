// RemoteControl — AI agents and scripts drive the running app (desktop; docs/CLI.md "Remote
// control"): the control socket of src/ipc (ControlServer, protocol in ipc/controlprotocol.h)
// answered here with the app's state and real input. `brulion-cli app state | navigate | open |
// command | capture | screenshot | find | click | type | key | wait | quit …` and the MCP tools of
// `brulion-cli mcp --app-control` are its clients.
//
// Off unless the app runs with --remote-control or the device setting devtools/remoteControl is on
// (Settings → Application → "For developers"; switching it takes effect at once). The socket is
// owner-only and refuses other users, but while it is on any program of this user can drive the
// UI and read the notes it shows: nothing secret is in `state` (no passwords or tokens; a password
// field's text is never given or matched), a screenshot is written only where asked (else into
// the runtime folder), every request is logged at debug level in the event log.
//
// Nothing runs nested: input is played from timers (uidriver::play), one gesture at a time (later
// ones queue; those of a client that left are dropped, its typing stops), started and answered
// once the window stopped changing (no new frame for 100 ms and no popup in a transition, at most
// 2 s: a popup that opened has given its field the keyboard); waitFor is answered when its
// condition holds or its time is up, polled on the event loop. Item handles ("h12", from find,
// state, click) are stable while their item lives. An item target is scrolled into view first and
// not touched when it cannot be (off_screen); a finger scroll is put down only where a Flickable
// under it can move that way (uidriver::canScroll()), so it never turns into a tap. `idle` means
// every shown page saved, none with a conflict waiting for the user (`state.conflicts`).
//
// Live changes (docs/CLI.md "Live changes"): eval, load / unload / reload, console, settings / set,
// theme, contributions, inspect / setProperty, events, changes / revert change the running app
// for this run through AppScripting and WorkspaceExtensions' live code; each change is noted in
// AppScripting's journal under "live:<client>" (hello's client, a grant's), which the top bar's
// live indicator shows and revert() undoes. Grants: an AI agent the app starts with "Let it change
// the app" gets one (grant(); WorkspaceAgents hands its token to the agent's tools in their
// environment) — while one exists the socket listens even with the setting off, and then serves
// only connections that showed a grant's token (ipc::ControlServer). `events` is a ring buffer of
// the app's last 500 events (routes, toasts, console lines, live changes, extensions, notes
// changed), recorded only while the socket listens; a request waits for a newer one (long poll).
//
// main() creates it for the real app (not the dev tools) once the QML is loaded; it reports its
// state to AppController::setRemoteControl() for the settings page.
#pragma once

#include "controlserver.h"
#include "uidriver.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

class QMetaProperty;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;

namespace brulion::shell {

class AppController;

class RemoteControl : public QObject {
    Q_OBJECT
public:
    /// The device setting that turns it on (AppSettings::value()).
    static constexpr auto SettingKey = "devtools/remoteControl";

    struct Options {
        QString key;                       ///< the control socket: ipc::controlKeyFor(single-instance key)
        bool forced = false;               ///< --remote-control: on for this run whatever the setting
        std::function<QString()> language; ///< the UI language in use ("pl" | "en")
    };

    RemoteControl(QQmlApplicationEngine *engine, AppController *app, Options options, QObject *parent = nullptr);
    ~RemoteControl() override;

    /// Listening for everyone of this user (the setting, --remote-control), not only for grants.
    bool isActive() const { return m_server.isListening() && m_server.isOpen(); }
    QString socketPath() const { return m_server.socketPath(); }

    /// A grant for the AI agent `client` the app starts (its name: the journal's "live:<client>"):
    /// the token its tools show in hello (BRULION_APP_TOKEN). The socket listens while one exists.
    QString grant(const QString &client);
    /// Ends the grant of `token` (the agent stopped) and drops its connections.
    void revokeGrant(const QString &token);

private Q_SLOTS:
    void onToastShown(int toastId, const QString &text, const QString &kind);

private:
    struct Target;
    struct Wait;
    struct ItemLookup;
    struct Input;

    /// Listen or stop, as the option and the setting say; report it to AppController.
    void apply();
    void handle(const ipc::ControlRequest &request, const ipc::ControlReply &reply);
    QQuickWindow *window() const;
    /// Bring the window up for input (shown, asked to be active).
    void prepareInput(QQuickWindow *window);
    /// The UI language in use ("pl" | "en").
    QString language() const;

    QJsonObject hello(const ipc::ControlRequest &request) const;
    QJsonObject state();
    void navigate(const QJsonObject &params, const ipc::ControlReply &reply);
    void open(const QJsonObject &params, const ipc::ControlReply &reply);
    void runCommand(const QJsonObject &params, const ipc::ControlReply &reply);
    void capture(const QJsonObject &params, const ipc::ControlReply &reply);
    void screenshot(const QJsonObject &params, const ipc::ControlReply &reply);
    void find(const QJsonObject &params, const ipc::ControlReply &reply);
    void pointer(const QString &method, const QJsonObject &params, const ipc::ControlReply &reply);
    void drag(const QJsonObject &params, const ipc::ControlReply &reply);
    void typeText(const QJsonObject &params, const ipc::ControlReply &reply);
    void pressKeys(const QJsonObject &params, const ipc::ControlReply &reply);
    void waitFor(const QJsonObject &params, const ipc::ControlReply &reply);

    // ---- Items and targets
    QString handleFor(QQuickItem *item);
    /// The item of a handle, or why there is none (not_found / stale_handle).
    ItemLookup itemFor(const QString &handle) const;
    QJsonObject describe(QQuickItem *item);
    /// describe() of the item with the keyboard focus; null without one.
    QJsonValue describeFocus();
    /// Items matching a selector ({text, objectName, type, exact}), topmost first.
    QList<QQuickItem *> select(QQuickWindow *window, const QJsonObject &selector, bool shownOnly,
                               QQuickItem *within = nullptr) const;
    /// Resolve a target object; false (and the reply failed) when it names nothing usable.
    bool resolveTarget(QQuickWindow *window, const QJsonObject &object, Target *target, const ipc::ControlReply &reply,
                       const QString &what = QString());
    /// Scroll a target's item into view, then call `then` with its point (from the event loop),
    /// or with the reason it cannot be touched (ipc::ControlError: stale_handle — gone meanwhile —
    /// or off_screen — its middle still outside the window or the view of a list around it).
    void reachTarget(QQuickWindow *window, const Target &target,
                     std::function<void(QPointF point, const char *problem)> then);

    // ---- Input queue: one gesture at a time
    /// Queue `job`: it runs once the inputs before it are answered, and answers through its Input.
    void enqueueInput(const ipc::ControlReply &reply, std::function<void(const Input &input)> job);
    /// Queue `start`: it runs with the window shown, active and settled (afterSettled()); without
    /// a window the request fails with `noWindow`.
    void enqueueSettled(const ipc::ControlReply &reply, const QString &noWindow,
                        std::function<void(QQuickWindow *window, const Input &input)> start);
    /// Play `gesture` (uidriver::play(), with its `keepGoing`), then answer with `answer()` once
    /// the window settled.
    void playThenAnswer(QQuickWindow *window, const uidriver::Gesture &gesture, const Input &input,
                        std::function<QJsonObject()> answer, std::function<bool()> keepGoing = {});
    void nextInput();
    /// Call `then` once the window has shown no new frame for `quietMs` (transitions and
    /// animations are over: a popup that opened has given its field the keyboard), at most after
    /// `maxMs`. Input starts and is answered that way, so the next request sees a settled UI.
    void afterSettled(int quietMs, int maxMs, std::function<void()> then);

    /// The page controllers of the block editors in the window (the main view, the side panel,
    /// every day of the journal stream).
    QList<QObject *> pageControllers() const;
    /// The pages shown with a conflict waiting for the user (changed here and elsewhere, not
    /// merged): [{page}], each page once.
    QJsonArray conflicts() const;

    // ---- Live changes (the client "live:<name>" of the request)
    static QString who(const ipc::ControlRequest &request);
    void evaluate(const QJsonObject &params, const ipc::ControlReply &reply);
    QJsonObject extensionList() const;
    void loadLive(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply);
    void unloadLive(const QJsonObject &params, const ipc::ControlReply &reply);
    QJsonObject consoleLines(const QJsonObject &params) const;
    void setSetting(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply);
    void theme(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply);
    void contributions(const QJsonObject &params, const ipc::ControlReply &reply);
    void inspect(const QJsonObject &params, const ipc::ControlReply &reply);
    /// An item of inspect's tree, its children `depth` levels down; `count` items so far of `limit`.
    QJsonObject inspectItem(QQuickItem *item, int depth, bool properties, int limit, int *count, bool *truncated);
    QJsonObject propertiesOf(QQuickItem *item);
    /// A property's value as JSON (undefined: not given — a list, an object, a function, too big).
    QJsonValue jsonOf(const QVariant &value, const QMetaProperty *property = nullptr, int depth = 0);
    void setItemProperty(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply);

    // ---- events
    struct EventWait;
    /// Notes an event of `type` (while the socket listens) and answers the waits it satisfies.
    void noteEvent(const QString &type, QJsonObject data);
    /// "extensions": once per turn of the event loop (commands and lists change in bursts).
    void noteExtensionsChanged();
    void events(const QJsonObject &params, const ipc::ControlReply &reply);
    QJsonObject eventsAfter(qint64 since) const;
    /// Connects the events' sources (once the controller's workspace is there).
    void watchEvents();

    // ---- waitFor
    bool satisfied(const Wait &wait) const;
    /// waitFor's answer: {waitedMs, route, item?}.
    QJsonObject waitAnswer(const Wait &wait);
    bool idle() const;
    void pollWaits();

    QQmlApplicationEngine *m_engine;
    AppController *m_app;
    Options m_options;
    ipc::ControlServer m_server;

    QHash<quint64, QPointer<QQuickItem>> m_items;
    QHash<QQuickItem *, quint64> m_handles;
    quint64 m_nextHandle = 1;

    struct Toast {
        QString text;
        QString kind;
        QDateTime at;
    };
    QList<Toast> m_toasts;

    struct QueuedInput {
        ipc::ControlReply reply; ///< not connected any more: dropped
        std::function<void(const Input &)> job;
    };
    QList<QueuedInput> m_inputQueue;
    bool m_inputRunning = false;

    QList<std::shared_ptr<Wait>> m_waits;
    QTimer m_waitTimer;

    struct Event {
        qint64 id = 0;
        QJsonObject data; ///< {id, type, at, …}
    };
    static constexpr int MaxEvents = 500;
    QList<Event> m_events;
    qint64 m_nextEvent = 1;
    QList<std::shared_ptr<EventWait>> m_eventWaits;
    bool m_extensionsEventDue = false;
    QJsonValue m_lastRoute; ///< the route of the last `route` event

    QElapsedTimer m_sinceFrame; ///< since the window last showed a new frame
    QPointer<QQuickWindow> m_frameWindow;
};

} // namespace brulion::shell
