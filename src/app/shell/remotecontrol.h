// RemoteControl — AI agents and scripts drive the running app (desktop; docs/CLI.md "Remote
// control"): the control socket of src/ipc (ControlServer, protocol in ipc/controlprotocol.h)
// answered here with the app's state and real input. `brulion-cli app state | navigate | open |
// command | capture | screenshot | find | click | type | key | wait | quit …` and the MCP tools of
// `brulion-cli mcp --app-control` are its clients.
//
// Off unless the app runs with --remote-control or the device setting devtools/remoteControl is on
// (Settings → Application → "For developers"; switching it takes effect at once). The socket is
// owner-only and refuses other users, but while it is on any program of this user can drive the
// UI and read the notes it shows: nothing secret is in `state` (no passwords or tokens), a
// screenshot is written only where asked (else into the runtime folder), every request is logged
// at debug level in the event log.
//
// Nothing runs nested: input is played from timers (uidriver::play), one gesture at a time (later
// ones queue), started and answered once the window stopped changing (no new frame for 100 ms, at
// most 2 s: a popup that opened has given its field the keyboard); waitFor is answered when its
// condition holds or its time is up, polled on the event loop. Item handles ("h12", from find,
// state, click) are stable while their item lives.
//
// main() creates it for the real app (not the dev tools) once the QML is loaded; it reports its
// state to AppController::setRemoteControl() for the settings page.
#pragma once

#include "controlserver.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

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

    bool isActive() const { return m_server.isListening(); }
    QString socketPath() const { return m_server.socketPath(); }

private Q_SLOTS:
    void onToastShown(int toastId, const QString &text, const QString &kind);

private:
    struct Target;
    struct Wait;

    /// Listen or stop, as the option and the setting say; report it to AppController.
    void apply();
    void handle(const ipc::ControlRequest &request, ipc::ControlReply reply);
    QQuickWindow *window() const;
    /// Bring the window up for input (shown, asked to be active).
    void prepareInput(QQuickWindow *window);

    QJsonObject hello() const;
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
    /// The item of a handle; nullptr with `problem` (not_found / stale_handle) set.
    QQuickItem *itemFor(const QString &handle, QString *code, QString *message);
    QJsonObject describe(QQuickItem *item);
    /// Items matching a selector ({text, objectName, type, exact}), topmost first.
    QList<QQuickItem *> select(QQuickWindow *window, const QJsonObject &selector, bool shownOnly,
                               QQuickItem *within = nullptr);
    /// Resolve a target object; false (and the reply failed) when it names nothing usable.
    bool resolveTarget(QQuickWindow *window, const QJsonObject &object, Target *target, const ipc::ControlReply &reply,
                       const QString &what = QString());
    /// Scroll a target's item into view, then call `then` with its point (from the event loop).
    void reachTarget(QQuickWindow *window, const Target &target, std::function<void(QPointF)> then);

    // ---- Input queue: one gesture at a time
    void enqueueInput(const ipc::ControlReply &reply, std::function<void(std::function<void()> finished)> job);
    void nextInput();
    /// Call `then` once the window has shown no new frame for `quietMs` (transitions and
    /// animations are over: a popup that opened has given its field the keyboard), at most after
    /// `maxMs`. Input starts and is answered that way, so the next request sees a settled UI.
    void afterSettled(int quietMs, int maxMs, std::function<void()> then);

    // ---- waitFor
    bool satisfied(const Wait &wait);
    /// waitFor's answer: {waitedMs, route, item?}.
    QJsonObject waitAnswer(const Wait &wait);
    bool idle();
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
        std::function<void(std::function<void()>)> job;
    };
    QList<QueuedInput> m_inputQueue;
    bool m_inputRunning = false;

    QList<std::shared_ptr<Wait>> m_waits;
    QTimer m_waitTimer;
    QElapsedTimer m_sinceFrame; ///< since the window last showed a new frame
    QPointer<QQuickWindow> m_frameWindow;
};

} // namespace brulion::shell
