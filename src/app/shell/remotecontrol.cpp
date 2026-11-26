#include "remotecontrol.h"

#include "appcontroller.h"
#include "appscripting.h"
#include "appsettings.h"
#include "chatcontrol.h"
#include "chatschedules.h"
#include "commandmodel.h"
#include "extensionmodels.h"
#include "navigation.h"
#include "util/unicode.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"
#include "workspaceextensions.h"
#include "workspacesync.h"

#include "extensions/appapi.h"

#include "acp/apptunnel.h"

#include "dates/local.h"
#include "dates/natural.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "notebook/itemref.h"
#include "notebook/pages.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;
namespace E = ipc::ControlError;

struct RemoteControl::Target {
    QPointer<QQuickItem> item; ///< null for a point
    QPointF point;             ///< window coordinates of a point target
    bool isItem = false;
};

struct RemoteControl::Wait {
    QJsonValue route; ///< undefined: no route condition
    QJsonObject selector;
    QString handle;
    bool idle = false;
    bool gone = false;
    int ms = 5000;
    QElapsedTimer since;
    ipc::ControlReply reply;
};

/// An `events` request waiting for an event after `since` (long poll).
struct RemoteControl::EventWait {
    qint64 since = 0;
    ipc::ControlReply reply;
};

struct RemoteControl::ItemLookup {
    QQuickItem *item = nullptr;
    QString code; ///< without an item: not_found or stale_handle
    QString message;
};

/// How a queued input answers: answering also lets the next input start.
struct RemoteControl::Input {
    ipc::ControlReply reply;
    std::function<void()> finished;

    void ok(const QJsonValue &result) const
    {
        reply.ok(result);
        finished();
    }
    void fail(const QString &errorCode, const QString &message, const QJsonObject &details = {}) const
    {
        reply.fail(errorCode, message, details);
        finished();
    }
};

namespace {

QString code(const char *errorCode)
{
    return QString::fromLatin1(errorCode);
}

/// A coordinate in an answer: to a tenth of a pixel.
double rounded(qreal v)
{
    return std::round(v * 10) / 10;
}

/// The text of a label as people read it: rich text without its tags and entities.
QString plainText(const QString &label)
{
    if (!label.contains(u'<') && !label.contains(u'&'))
        return label;
    static const QRegularExpression breaks(u"<br\\s*/?>|</p>|</li>|</h[1-6]>"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(u"<[^>]*>"_s);
    QString out = label;
    out.replace(breaks, u"\n"_s);
    out.remove(tags);
    out.replace(u"&nbsp;"_s, u" "_s)
            .replace(u"&lt;"_s, u"<"_s)
            .replace(u"&gt;"_s, u">"_s)
            .replace(u"&quot;"_s, u"\""_s)
            .replace(u"&#39;"_s, u"'"_s)
            .replace(u"&amp;"_s, u"&"_s);
    return out.trimmed();
}

/// A field that hides what is typed into it (a password: echoMode other than TextInput.Normal).
/// Its text is never shown, matched or logged.
bool secretField(const QQuickItem *item)
{
    // (A field whose text is a secret even while it is shown — the AI chat's secret card with
    // Show: its `secretText` — is one too.)
    const QVariant echoMode = item->property("echoMode");
    return (echoMode.isValid() && echoMode.toInt() != 0) || item->property("secretText").toBool();
}

/// The text an item shows as people read it; "" for a secret field.
QString shownText(const QQuickItem *item)
{
    return secretField(item) ? QString() : plainText(uidriver::labelOf(item));
}

/// Every popup shown in the window is fully open (no enter or exit transition running): a popup
/// gives its field the keyboard once it is (Popup.onOpened).
bool popupsSettled(QQuickWindow *window)
{
    return std::ranges::none_of(uidriver::openPopups(window), [](const QObject *popup) {
        const QVariant opened = popup->property("opened");
        return opened.isValid() && !opened.toBool();
    });
}

/// The item's QML type (or one it is based on) is `name`: "BButton", "TasksRow", "QQuickText" —
/// or a Qt Quick type by its QML name ("Text", "TextEdit", "Flickable").
bool typeMatches(const QQuickItem *item, const QString &name)
{
    for (const QMetaObject *m = item->metaObject(); m; m = m->superClass()) {
        QLatin1StringView cls(m->className());
        for (const QLatin1StringView marker : {"_QMLTYPE_"_L1, "_QML_"_L1}) {
            if (const qsizetype at = cls.indexOf(marker); at > 0)
                cls.truncate(at);
        }
        if (cls == name || (!name.startsWith(u'Q') && cls.startsWith("QQuick"_L1) && cls.sliced(6) == name))
            return true;
    }
    return false;
}

/// An item's rectangle in window coordinates.
QRectF sceneRect(const QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

QJsonObject rect(const QRectF &r)
{
    return {{u"x"_s, rounded(r.x())},
            {u"y"_s, rounded(r.y())},
            {u"width"_s, rounded(r.width())},
            {u"height"_s, rounded(r.height())}};
}

/// What a request's parameters say in the event log: note text only by its length.
QString paramsSummary(const QString &method, const QJsonObject &params)
{
    QJsonObject shown = params;
    for (auto it = shown.begin(); it != shown.end(); ++it) {
        if (!it.value().isString())
            continue;
        const QString s = it.value().toString();
        if ((method == u"type" || method == u"capture") && it.key() == u"text")
            it.value() = u"<%1 characters>"_s.arg(s.size());
        else if (it.key() == u"token") // (a grant's, a webhook's: never in the log)
            it.value() = u"<token>"_s;
        else if (method == u"chatHook" && it.key() == u"text") // (what a webhook sent: data from outside)
            it.value() = u"<%1 characters>"_s.arg(s.size());
        else if (s.size() > 80)
            it.value() = QString(s.left(79) + u'…');
    }
    return shown.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact));
}

QJsonValue toJson(const QVariantMap &map)
{
    return map.isEmpty() ? QJsonValue() : QJsonValue(QJsonObject::fromVariantMap(map));
}

/// The selector fields of a request or a target: text, objectName, type, exact.
QJsonObject selectorOf(const QJsonObject &object)
{
    QJsonObject out;
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s, u"exact"_s}) {
        if (object.contains(key))
            out.insert(key, object.value(key));
    }
    return out;
}

/// A finger rather than the mouse: as the request's `device` says, else with --touch (the phone's
/// UI) where `byDefault` allows it.
bool usesTouch(const QJsonObject &params, bool byDefault)
{
    if (params.contains(u"device"_s))
        return params.value(u"device"_s).toString() == u"touch";
    return byDefault && QCoreApplication::arguments().contains(u"--touch"_s);
}

} // namespace

RemoteControl::RemoteControl(QQmlApplicationEngine *engine, AppController *app, Options options, QObject *parent)
    : QObject(parent), m_engine(engine), m_app(app), m_options(std::move(options))
{
    m_server.setHandler(std::bind_front(&RemoteControl::handle, this));
    connect(&m_server, &ipc::ControlServer::refused, this, [](qint64 uid) {
        qCWarning(lcApp).noquote() << u"Remote control: refused a connection of another user (uid %1)"_s.arg(uid);
    });
    connect(app->settings(), &AppSettings::valueChanged, this, [this](const QString &key) {
        if (key == QLatin1String(SettingKey))
            apply();
    });
    if (QQuickWindow *w = window()) {
        // The toasts the app showed (BToastHost.toastShown), for state.lastToasts.
        if (QObject *host = w->findChild<QObject *>(u"toastHost"_s)) {
            const int signal = host->metaObject()->indexOfSignal("toastShown(int,QString,QString)");
            const int slot = staticMetaObject.indexOfSlot("onToastShown(int,QString,QString)");
            if (signal >= 0 && slot >= 0)
                connect(host, host->metaObject()->method(signal), this, staticMetaObject.method(slot));
        }
        // waitFor idle: no new frame for a while (animations, transitions and scrolling are over).
        m_frameWindow = w;
        connect(w, &QQuickWindow::frameSwapped, this, [this] { m_sinceFrame.restart(); });
    }
    m_sinceFrame.start();
    m_waitTimer.setInterval(50ms);
    connect(&m_waitTimer, &QTimer::timeout, this, &RemoteControl::pollWaits);
    watchEvents();
    apply();
}

RemoteControl::~RemoteControl()
{
    m_server.close();
}

void RemoteControl::apply()
{
    const bool setting = m_app->settings()->value(QString::fromLatin1(SettingKey), false).toBool();
    const bool wanted = m_options.forced || setting;
    // An agent the app started with a grant reaches the socket also while the setting is off.
    // (No socket at all — Android —: only the in-process grants, dispatch().)
    const bool serve = !m_options.key.isEmpty() && (wanted || m_server.grantCount() > 0);
    // (Turned off with grants: the connections without one end now.)
    m_server.setOpen(wanted);
    QString error;
    if (serve && !m_server.isListening()) {
        if (m_server.listen(m_options.key)) {
            qCInfo(lcApp).noquote() << (wanted ? u"Remote control on: %1"_s : u"Remote control for agents only: %1"_s)
                                               .arg(m_server.socketPath());
        } else {
            error = m_server.errorString();
            qCWarning(lcApp).noquote() << u"Remote control could not start: %1"_s.arg(error);
        }
    } else if (!serve && m_server.isListening()) {
        m_server.close(); // its connections too: waits and gestures answer nobody
        if (m_localGrants.isEmpty()) {
            m_waits.clear();
            m_waitTimer.stop();
            m_eventWaits.clear();
            m_events.clear();
            m_lastRoute = QJsonValue();
        }
        qCInfo(lcApp) << "Remote control off";
    }
    m_app->setRemoteControl({{u"available"_s, !m_options.key.isEmpty()},
                             {u"setting"_s, setting},
                             {u"active"_s, isActive()},
                             {u"forced"_s, m_options.forced},
                             {u"socket"_s, socketPath()},
                             {u"grants"_s, m_server.grantCount()},
                             {u"error"_s, error}});
}

QString RemoteControl::chatGrant(const QString &client)
{
    const QString token = m_server.addGrant(client, ipc::GrantScope::Chat);
    qCDebug(lcApp).noquote() << u"Remote control: a chat grant for the agent %1"_s.arg(ipc::cleanClientName(client));
    apply();
    return token;
}

QString RemoteControl::hookGrant()
{
    const QString token = m_server.addGrant(u"webhooks"_s, ipc::GrantScope::Hook);
    qCInfo(lcApp) << "Remote control: a grant for the webhooks' server";
    apply();
    return token;
}

QString RemoteControl::shareGrant()
{
    const QString token = m_server.addGrant(u"agent sharing"_s, ipc::GrantScope::Share);
    qCInfo(lcApp) << "Remote control: a grant for the agent sharing (acp serve, for the phone)";
    // (The chat's control follows the turns from now on: one that ends before the phone first asks
    // is still listed for a while.)
    if (!m_chat)
        m_chat = new ChatControl(m_app->workspace(), this);
    apply();
    return token;
}

QString RemoteControl::grant(const QString &client)
{
    const QString token = m_server.addGrant(client);
    qCInfo(lcApp).noquote() << u"Remote control: a grant for the agent %1"_s.arg(ipc::cleanClientName(client));
    apply();
    return token;
}

void RemoteControl::revokeGrant(const QString &token)
{
    if (m_localGrants.remove(token)) {
        qCDebug(lcApp) << "Remote control: an in-process grant ended";
        if (m_localGrants.isEmpty() && !m_server.isListening()) {
            // (Nobody to tell any more: what was kept for them goes.)
            m_eventWaits.clear();
            m_events.clear();
            m_lastRoute = QJsonValue();
        }
        return;
    }
    if (m_server.revokeGrant(token))
        apply();
}

QString RemoteControl::localGrant(const QString &client, bool chatOnly)
{
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    QString token;
    for (const quint32 w : words)
        token += u"%1"_s.arg(w, 8, 16, u'0');
    m_localGrants.insert(token, LocalGrant{ipc::cleanClientName(client), chatOnly});
    qCInfo(lcApp).noquote() << (chatOnly ? u"Remote control: a chat grant in process for %1"_s
                                         : u"Remote control: a grant in process for %1"_s)
                                       .arg(ipc::cleanClientName(client));
    return token;
}

void RemoteControl::dispatch(const QString &token, const QString &method, const QJsonObject &params,
                             const ipc::ControlReply &reply)
{
    const auto grant = m_localGrants.constFind(token);
    if (grant == m_localGrants.cend())
        return reply.fail(code(E::Unauthorized), u"The grant ended: its agent stopped"_s);
    ipc::ControlRequest request;
    request.id = reply.id();
    request.method = method;
    request.params = params;
    request.client = grant->client;
    request.granted = true;
    request.grant = token;
    request.chatOnly = grant->chatOnly;
    // The checks a socket's connection gets: the method, its parameters, the grant's scope.
    if (const std::optional<ipc::ControlFailure> problem = ipc::checkRequest(request))
        return reply.fail(problem->code, problem->message, problem->details);
    if (grant->chatOnly && method != u"hello") {
        const ipc::MethodSpec *spec = ipc::findControlMethod(method);
        if (!spec || !spec->chat)
            return reply.fail(
                    code(E::Unauthorized),
                    u"This grant covers the AI chat's methods only (chats, chatStart, chatWait …), not %1"_s.arg(
                            method));
    }
    // An agent elsewhere never ends the app, nor names a file of this device.
    if (method == u"quit")
        return reply.fail(code(E::Refused), u"An agent on another computer does not quit the app"_s);
    if (method == u"screenshot") {
        if (params.contains(u"path"_s))
            return reply.fail(code(E::BadParams), u"The picture comes back in the answer: no path on this device"_s,
                              {{u"param"_s, u"path"_s}});
        qCDebug(lcApp).noquote() << u"Remote control (%1, in process): screenshot"_s.arg(grant->client);
        return screenshotData(reply);
    }
    handle(request, reply);
}

QQuickWindow *RemoteControl::window() const
{
    return qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
}

void RemoteControl::prepareInput(QQuickWindow *window)
{
    // Input reaches the items of a shown, active window (as a user's does).
    if (!window->isVisible() || window->visibility() == QWindow::Minimized)
        m_app->showWindow();
    if (!window->isActive())
        window->requestActivate();
}

QString RemoteControl::language() const
{
    return m_options.language ? m_options.language() : m_app->settings()->language();
}

void RemoteControl::onToastShown(int /*toastId*/, const QString &text, const QString &kind)
{
    m_toasts.append({text, kind, QDateTime::currentDateTime()});
    while (m_toasts.size() > 10)
        m_toasts.removeFirst();
    noteEvent(u"toast"_s, {{u"text"_s, text.left(2000)}, {u"kind"_s, kind}});
}

// ---- Requests -----------------------------------------------------------------------------------

void RemoteControl::handle(const ipc::ControlRequest &request, const ipc::ControlReply &reply)
{
    const QString &method = request.method;
    const QJsonObject &p = request.params;
    qCDebug(lcApp).noquote() << u"Remote control: %1 %2"_s.arg(method, paramsSummary(method, p)).trimmed();
    if (method == u"hello") {
        reply.ok(hello(request));
    } else if (method == u"state") {
        reply.ok(state());
    } else if (method == u"navigate") {
        navigate(p, reply);
    } else if (method == u"open") {
        open(p, reply);
    } else if (method == u"commands") {
        reply.ok(QJsonObject{{u"commands"_s, QJsonArray::fromVariantList(m_app->commands()->commands())}});
    } else if (method == u"command") {
        runCommand(p, reply);
    } else if (method == u"capture") {
        capture(p, reply);
    } else if (method == u"screenshot") {
        screenshot(p, reply);
    } else if (method == u"find") {
        find(p, reply);
    } else if (method == u"click" || method == u"longPress" || method == u"doubleClick" || method == u"scroll") {
        pointer(method, p, reply);
    } else if (method == u"drag") {
        drag(p, reply);
    } else if (method == u"type") {
        typeText(p, reply);
    } else if (method == u"key") {
        pressKeys(p, reply);
    } else if (method == u"waitFor") {
        waitFor(p, reply);
    } else if (method == u"quit") {
        reply.ok(QJsonObject{{u"quitting"_s, true}});
        QTimer::singleShot(50, m_app, &AppController::quit); // the answer leaves first
    } else if (method == u"eval") {
        evaluate(p, reply);
    } else if (method == u"extensions") {
        reply.ok(extensionList());
    } else if (method == u"load") {
        loadLive(who(request), p, reply);
    } else if (method == u"unload") {
        unloadLive(p, reply);
    } else if (method == u"reload") {
        m_app->workspace()->extensions()->reloadAll();
        reply.ok(QJsonObject{
                {u"reloaded"_s, true},
                {u"live"_s, QJsonArray::fromVariantList(m_app->workspace()->extensions()->liveExtensions())}});
    } else if (method == u"console") {
        reply.ok(consoleLines(p));
    } else if (method == u"settings") {
        reply.ok(QJsonObject{{u"settings"_s, QJsonArray::fromVariantList(m_app->scripting()->settings())}});
    } else if (method == u"set") {
        setSetting(who(request), p, reply);
    } else if (method == u"theme") {
        theme(who(request), p, reply);
    } else if (method == u"contributions") {
        contributions(p, reply);
    } else if (method == u"inspect") {
        inspect(p, reply);
    } else if (method == u"setProperty") {
        setItemProperty(who(request), p, reply);
    } else if (method == u"events") {
        events(p, reply);
    } else if (method == u"changes") {
        AppScripting *scripting = m_app->scripting();
        reply.ok(QJsonObject{{u"changes"_s, QJsonArray::fromVariantList(scripting->changes())},
                             {u"clients"_s, QJsonArray::fromVariantList(scripting->liveClients())},
                             {u"revision"_s, scripting->liveRevision()}});
    } else if (method == u"revert") {
        const int undone = m_app->scripting()->revert(p.value(u"who"_s).toString().trimmed());
        reply.ok(QJsonObject{{u"undone"_s, undone},
                             {u"changes"_s, QJsonArray::fromVariantList(m_app->scripting()->changes())}});
    } else if (ChatControl::handles(method)) {
        chatRequest(request, reply);
    } else if (method == u"chatHook") {
        hookRequest(request, reply);
    } else {
        reply.fail(code(E::UnknownMethod), u"Unknown method %1"_s.arg(method));
    }
}

void RemoteControl::chatRequest(const ipc::ControlRequest &request, const ipc::ControlReply &reply)
{
    if (!m_chat)
        m_chat = new ChatControl(m_app->workspace(), this);
    ChatControl::Caller caller;
    // (The agent sharing acts for the user's phone: as the user, not as an agent's grant.)
    caller.granted = request.granted && !request.shareOnly;
    caller.client = request.client;
    if (caller.granted)
        caller.conversation = m_app->workspace()->chats()->agents()->grantConversation(request.grant);
    ChatControl::Reply answer;
    answer.ok = [reply](const QJsonObject &result) { reply.ok(result); };
    answer.fail = [reply](const QString &failure, const QString &message, const QJsonObject &details) {
        reply.fail(failure, message, details);
    };
    answer.connected = [reply] { return reply.connected(); };
    m_chat->handle(request.method, request.params, caller, answer);
}

void RemoteControl::hookRequest(const ipc::ControlRequest &request, const ipc::ControlReply &reply)
{
    // (The socket lets only the webhooks' grant call it: ipc::ControlServer.)
    if (!request.hookOnly)
        return reply.fail(code(E::Unauthorized), u"Only the app's webhook server calls chatHook"_s);
    const QJsonObject &p = request.params;
    ChatSchedules::HookRequest hook;
    hook.token = p.value(u"token"_s).toString();
    hook.body.text = p.value(u"text"_s).toString();
    hook.body.kind = webhook::kindFromName(p.value(u"kind"_s).toString())
                             .value_or(hook.body.text.isEmpty() ? webhook::BodyKind::Empty : webhook::BodyKind::Text);
    hook.body.bytes = qsizetype(p.value(u"bytes"_s).toDouble(double(hook.body.text.toUtf8().size())));
    hook.body.cut = p.value(u"cut"_s).toBool();
    // (Cut here too: the server does, a request of another program would not.)
    bool cut = false;
    hook.body.text = webhook::cutUtf8(hook.body.text, webhook::MaxTextBytes, &cut);
    hook.body.cut = hook.body.cut || cut;
    const ChatSchedules::HookOutcome out = m_app->workspace()->chats()->schedules()->runHook(hook);
    QJsonObject result{{u"status"_s, out.status}};
    if (!out.conversation.isEmpty())
        result.insert(u"conversation"_s, out.conversation);
    if (out.retryAfterMs > 0)
        result.insert(u"retryAfterMs"_s, double(out.retryAfterMs));
    reply.ok(result);
}

QJsonObject RemoteControl::hello(const ipc::ControlRequest &request) const
{
    QJsonArray methods;
    for (const ipc::MethodSpec &m : ipc::controlMethods()) {
        // (Each grant's own; the webhooks' method only for its server.)
        if (request.hookOnly ? m.hook : request.shareOnly ? m.share : (!m.hook && (m.chat || !request.chatOnly)))
            methods.append(m.name);
    }
    return {{u"protocol"_s, ipc::ControlProtocolVersion},
            {u"app"_s, u"Brulion"_s},
            {u"version"_s, m_app->version()},
            {u"platform"_s, m_app->platform()},
            {u"methods"_s, methods},
            {u"client"_s, who(request).mid(5)},
            {u"granted"_s, request.granted}};
}

QJsonObject RemoteControl::state()
{
    Workspace *ws = m_app->workspace();
    const QString status = m_app->status();
    QJsonObject out{{u"status"_s, status},
                    {u"ready"_s, status == u"ready" && ws->ready()},
                    {u"indexing"_s, ws->isOpen() && ws->indexing()},
                    {u"currentRoute"_s, toJson(m_app->currentRoute())},
                    {u"sidePanelRoute"_s, toJson(m_app->sidePanelRoute())},
                    {u"sidePanelVisible"_s, m_app->sidePanelVisible()},
                    {u"canGoBack"_s, m_app->canGoBack()},
                    {u"version"_s, m_app->version()}};
    out.insert(u"vault"_s, ws->isOpen() ? QJsonValue(QJsonObject{{u"path"_s, ws->rootPath()},
                                                                 {u"name"_s, ws->name()},
                                                                 {u"documentation"_s, m_app->inDocsVault()}})
                                        : QJsonValue());
    QQuickWindow *w = window();
    QJsonValue focused;
    QJsonValue editing;
    QJsonArray popups;
    if (w) {
        QQuickItem *focus = w->activeFocusItem();
        if (focus && focus != w->contentItem())
            focused = describe(focus);
        // The edited block: the focus inside a page's block editor.
        QQuickItem *block = nullptr;
        bool source = false;
        QObject *controller = nullptr;
        for (QQuickItem *p = focus; p && !controller; p = p->parentItem()) {
            if (!block && uidriver::isA(p, "InlineEditor"))
                block = p;
            if (uidriver::isA(p, "SourceEditor"))
                source = true;
            if (uidriver::isA(p, "BlockEditor"))
                controller = p->property("controller").value<QObject *>();
        }
        if (controller && (block || source)) {
            QJsonObject e{{u"page"_s, controller->property("path").toString()},
                          {u"source"_s, source && !block},
                          {u"conflict"_s, controller->property("conflict").toBool()}};
            QQuickItem *edit = block ? block : focus;
            if (block) {
                e.insert(u"row"_s, block->property("row").toInt());
                e.insert(u"text"_s, block->property("text").toString());
            }
            e.insert(u"cursorPosition"_s, edit->property("cursorPosition").toInt());
            editing = e;
        }
        for (QObject *popup : uidriver::openPopups(w))
            popups.append(QJsonObject{{u"type"_s, uidriver::typeName(popup)}, {u"objectName"_s, popup->objectName()}});
        out.insert(u"window"_s, QJsonObject{{u"width"_s, w->width()},
                                            {u"height"_s, w->height()},
                                            {u"visible"_s, w->isVisible()},
                                            {u"active"_s, w->isActive()},
                                            {u"devicePixelRatio"_s, w->devicePixelRatio()}});
    } else {
        out.insert(u"window"_s, QJsonValue());
    }
    out.insert(u"focusedItem"_s, focused);
    out.insert(u"editing"_s, editing);
    out.insert(u"conflicts"_s, conflicts());
    // Conflict copies sync left to merge (Sync → Conflicts): not pages waiting here.
    out.insert(u"syncConflicts"_s, ws->isOpen() && ws->sync() ? ws->sync()->conflictCount() : 0);
    out.insert(u"openPopups"_s, popups);
    QJsonArray toasts;
    for (const Toast &t : std::as_const(m_toasts))
        toasts.append(QJsonObject{
                {u"text"_s, t.text}, {u"kind"_s, t.kind}, {u"at"_s, t.at.toString(u"yyyy-MM-dd HH:mm:ss"_s)}});
    out.insert(u"lastToasts"_s, toasts);
    auto *theme = m_engine->singletonInstance<QObject *>("Brulion", "Theme");
    const bool dark = theme && theme->property("dark").toBool();
    out.insert(u"theme"_s, dark ? u"dark"_s : u"light"_s);
    out.insert(u"themeSetting"_s, m_app->settings()->themeMode());
    out.insert(u"touchUi"_s, theme && theme->property("touchUi").toBool());
    // What Theme shows now (the settings, or the --accent / --surface / --density overrides).
    if (theme) {
        out.insert(u"appearance"_s,
                   QJsonObject{{u"accent"_s, theme->property("accentName").toString()},
                               {u"accentColor"_s, theme->property("accent").value<QColor>().name()},
                               {u"surface"_s, theme->property(dark ? "darkSurface" : "lightSurface").toString()},
                               {u"background"_s, theme->property("bg").value<QColor>().name()},
                               {u"density"_s, theme->property("dense").toBool() ? u"compact"_s : u"comfortable"_s},
                               {u"fontUi"_s, theme->property("fontUi").toString()},
                               {u"fontEditor"_s, theme->property("fontEditor").toString()},
                               {u"fontMono"_s, theme->property("fontMono").toString()},
                               {u"lineSpacing"_s, theme->property("lineSpacing").toString()}});
    }
    out.insert(u"language"_s, language());
    return out;
}

void RemoteControl::navigate(const QJsonObject &params, const ipc::ControlReply &reply)
{
    if (m_app->status() != u"ready")
        return reply.fail(code(E::NotReady), u"No notebook is open (status %1)"_s.arg(m_app->status()));
    const QJsonValue raw = params.value(u"route"_s);
    const QVariantMap route = raw.isString() ? QVariantMap{{u"view"_s, raw.toString()}} : raw.toObject().toVariantMap();
    const QVariantMap normalized = normalizeRoute(route);
    if (normalized.isEmpty())
        return reply.fail(code(E::BadParams), u"Not a route: an unknown view or a missing parameter"_s,
                          {{u"views"_s, QJsonArray::fromStringList(routeViews())}});
    m_app->navigate(normalized, params.value(u"side"_s).toBool());
    reply.ok(QJsonObject{{u"route"_s, toJson(m_app->currentRoute())},
                         {u"sidePanelRoute"_s, toJson(m_app->sidePanelRoute())}});
}

void RemoteControl::open(const QJsonObject &params, const ipc::ControlReply &reply)
{
    if (m_app->status() != u"ready")
        return reply.fail(code(E::NotReady), u"No notebook is open (status %1)"_s.arg(m_app->status()));
    Workspace *ws = m_app->workspace();
    Vault *vault = ws->vault();
    VaultIndex *index = ws->index();
    if (!vault || !index)
        return reply.fail(code(E::NotReady), u"No notebook is open"_s);
    if (ws->indexing())
        ws->finishIndexing(); // names, aliases and block ids of every note
    const QString ref = params.value(u"ref"_s).toString().trimmed();
    const auto item = notebook::parseItemRef(ref.isEmpty() ? u"today"_s : ref);
    if (!item)
        return reply.fail(
                code(E::BadParams), u"Not a valid reference: %1"_s.arg(ref.left(200)),
                {{u"examples"_s, QJsonArray{u"Projekty/Dom.md"_s, u"Dom"_s, u"today"_s, u"2026-10-15"_s,
                                            u"Dom.md#L12@3f9a01bc"_s, u"Dom.md#^abc123"_s, u"((abc123))"_s}}});
    // The request names a place in the page itself (a line, a block or a heading).
    const bool placeGiven =
            params.contains(u"line"_s) || params.contains(u"blockId"_s) || params.contains(u"heading"_s);
    const auto pageRoute = [&](const VaultPath &path) {
        QVariantMap route{{u"view"_s, u"page"_s}, {u"path"_s, path}};
        if (item->hasBlock()) {
            route.insert(u"blockId"_s, item->blockId);
        } else if (item->hasLine()) {
            const notebook::ResolvedLine line = notebook::resolve(ws->readFile(path), *item, vault->config());
            route.insert(u"line"_s, line.line >= 0 ? line.line : item->line);
        }
        return route;
    };
    const auto dayRoute = [&](QDate day) {
        const QString date = day.toString(Qt::ISODate);
        const QString path = ws->journalFileFor(date);
        const bool wantsPlace = item->hasLine() || item->hasBlock() || placeGiven;
        if (wantsPlace && !path.isEmpty() && vault->exists(path))
            return pageRoute(path);
        QVariantMap route{{u"view"_s, u"journal"_s}, {u"date"_s, date}};
        if (!path.isEmpty())
            route.insert(u"path"_s, path);
        return route;
    };
    QVariantMap route;
    if (item->page.isEmpty()) {
        // ^id / ((id)): the block anywhere in the notebook.
        const auto block = index->blockById(item->blockId);
        if (!block)
            return reply.fail(code(E::NotFound), u"No block with the id %1"_s.arg(item->blockId),
                              {{u"blockId"_s, item->blockId}});
        route = pageRoute(block->path);
    } else {
        const QString &name = item->page;
        const QDateTime now = m_app->now();
        // 1. A page's path (with or without ".md", any letter case).
        QString cleaned = QDir::cleanPath(QString(name).replace(u'\\', u'/'));
        if (QDir::isAbsolutePath(cleaned) && cleaned.startsWith(ws->rootPath() + u'/'))
            cleaned = cleaned.mid(ws->rootPath().size() + 1);
        if (cleaned == u".." || cleaned.startsWith(u"../") || QDir::isAbsolutePath(cleaned))
            return reply.fail(code(E::BadParams), u"%1 is outside the notebook"_s.arg(name));
        const VaultPath path = paths::normalize(cleaned);
        if (!path.isEmpty() && !paths::isIgnored(path)) {
            QStringList tries{path};
            if (!paths::isMarkdown(path))
                tries.append(path + u".md"_s);
            for (const QString &t : std::as_const(tries)) {
                const VaultPath spelled = vault->existingCase(t);
                if (const auto st = vault->stat(spelled); st && !st->isDir && paths::isMarkdown(spelled)) {
                    route = pageRoute(spelled);
                    break;
                }
            }
        }
        // 2. A day named unmistakably: "today" or "2026-10-15".
        if (route.isEmpty() && !name.contains(u'/')) {
            if (const auto iso = dates::parseIsoDate(name.trimmed()))
                route = dayRoute(*iso);
            else if (name.trimmed().compare(u"today", Qt::CaseInsensitive) == 0)
                route = dayRoute(now.date());
        }
        // 3. A page name as [[links]] read it: a title, or a path suffix ("Projekty/Dom").
        if (route.isEmpty()) {
            const auto fold = [](const QString &s) { return unicode::nfc(s).toLower(); };
            const QString key = fold(notebook::withoutMdExtension(name).trimmed());
            const bool bySuffix = key.contains(u'/');
            QStringList candidates;
            for (const FileStat &f : vault->markdownFiles()) {
                if (paths::isConflictCopy(f.path))
                    continue;
                const QString folded =
                        fold(bySuffix ? paths::stripMarkdownExtension(f.path) : paths::pageTitle(f.path));
                if (folded == key || (bySuffix && folded.endsWith(u'/' + key)))
                    candidates.append(f.path);
            }
            if (candidates.size() > 1)
                return reply.fail(code(E::Ambiguous), u"Several pages are called %1: give the path"_s.arg(name),
                                  {{u"candidates"_s, QJsonArray::fromStringList(candidates)}});
            if (candidates.size() == 1)
                route = pageRoute(candidates.first());
        }
        // 4. An alias (frontmatter `aliases:`), as links resolve it.
        if (route.isEmpty()) {
            if (const auto resolved = index->resolveLink(name);
                resolved && paths::isMarkdown(*resolved) && vault->exists(*resolved))
                route = pageRoute(*resolved);
        }
        // 5. A date phrase ("yesterday", "w piątek" — on Friday) when no page has that name.
        if (route.isEmpty() && !name.contains(u'/')) {
            const dates::UiLocale locale = language() == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
            const auto found = dates::parseNaturalDate(name, now, vault->config().weekStartsOn, locale);
            if (found && name.left(found->start).trimmed().isEmpty() && name.mid(found->end()).trimmed().isEmpty())
                route = dayRoute(found->spec.date);
        }
        if (route.isEmpty()) {
            QJsonArray suggestions;
            for (const PageSuggestion &s : index->suggestPages(name, 5))
                suggestions.append(s.path);
            return reply.fail(code(E::NotFound), u"No page %1"_s.arg(name),
                              {{u"ref"_s, name}, {u"suggestions"_s, suggestions}});
        }
    }
    // Explicit places win over the ref's.
    if (placeGiven && route.value(u"view"_s).toString() == u"page") {
        route.remove(u"line"_s);
        route.remove(u"blockId"_s);
        route.remove(u"heading"_s);
        if (params.contains(u"line"_s))
            route.insert(u"line"_s, params.value(u"line"_s).toInt());
        if (params.contains(u"blockId"_s))
            route.insert(u"blockId"_s, params.value(u"blockId"_s).toString().remove(u'^'));
        if (params.contains(u"heading"_s))
            route.insert(u"heading"_s, params.value(u"heading"_s).toString());
    }
    const QVariantMap resolved = ws->resolveRoute(normalizeRoute(route));
    if (resolved.isEmpty())
        return reply.fail(code(E::NotFound), u"Nothing to open for %1"_s.arg(ref));
    m_app->navigate(resolved, params.value(u"side"_s).toBool());
    reply.ok(QJsonObject{{u"route"_s, toJson(resolved)}, {u"currentRoute"_s, toJson(m_app->currentRoute())}});
}

void RemoteControl::runCommand(const QJsonObject &params, const ipc::ControlReply &reply)
{
    const QString id = params.value(u"id"_s).toString();
    CommandModel *commands = m_app->commands();
    if (!commands->ids().contains(id))
        return reply.fail(code(E::NotFound), u"No command %1"_s.arg(id),
                          {{u"commands"_s, QJsonArray::fromStringList(commands->ids())}});
    if (!commands->run(id))
        return reply.fail(code(E::Refused), u"The command %1 cannot run now"_s.arg(id), {{u"id"_s, id}});
    reply.ok(QJsonObject{{u"id"_s, id}, {u"route"_s, toJson(m_app->currentRoute())}});
}

void RemoteControl::capture(const QJsonObject &params, const ipc::ControlReply &reply)
{
    Workspace *ws = m_app->workspace();
    if (m_app->status() != u"ready" || !ws->isOpen())
        return reply.fail(code(E::NotReady), u"No notebook is open (status %1)"_s.arg(m_app->status()));
    if (m_app->inDocsVault())
        return reply.fail(code(E::Refused), u"The documentation notebook takes no notes"_s);
    const QString text = params.value(u"text"_s).toString();
    if (text.trimmed().isEmpty())
        return reply.fail(code(E::BadParams), u"Nothing to capture"_s, {{u"param"_s, u"text"_s}});
    // A date in the text makes it a task with a reminder or a due date (notebook/capture.h).
    const QVariantMap preview = ws->quickCapturePreview(text);
    const QString path = ws->quickCapture(text);
    if (path.isEmpty())
        return reply.fail(code(E::Failed), u"Today's journal could not be written"_s);
    QJsonObject out = QJsonObject::fromVariantMap(preview);
    out.insert(u"path"_s, path);
    reply.ok(out);
}

void RemoteControl::screenshot(const QJsonObject &params, const ipc::ControlReply &reply)
{
    const QString path = params.value(u"path"_s).toString().trimmed();
    if (!path.isEmpty()) {
        // Written only where asked: an absolute .png path in a folder that exists.
        if (!QDir::isAbsolutePath(path))
            return reply.fail(code(E::BadParams), u"The path is absolute"_s, {{u"param"_s, u"path"_s}});
        if (!path.endsWith(u".png"_s, Qt::CaseInsensitive))
            return reply.fail(code(E::BadParams), u"The path ends with .png"_s, {{u"param"_s, u"path"_s}});
        if (const QString folder = QFileInfo(path).absolutePath(); !QFileInfo(folder).isDir())
            return reply.fail(code(E::BadParams), u"The folder %1 does not exist"_s.arg(folder),
                              {{u"param"_s, u"path"_s}});
    }
    enqueueInput(reply, [this, path](const Input &in) {
        QString file = path;
        if (file.isEmpty()) {
            // The user's private runtime folder, a folder of its own — never the shared temp
            // folder, where another user could have made that folder and read the notes shown.
            // Windows has no runtime folder (Qt gives the profile folder): the user's own temp
            // folder (%TEMP%, inside the profile) is private.
#ifdef Q_OS_WIN
            const QString runtime = QDir::tempPath();
#else
            const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
            if (runtime.isEmpty())
                return in.fail(code(E::BadParams),
                               u"There is no private runtime folder ($XDG_RUNTIME_DIR): give a path"_s,
                               {{u"param"_s, u"path"_s}});
            const QString dir = QDir(runtime).filePath(u"brulion-screenshots"_s);
            QDir().mkpath(dir);
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            file = QDir(dir).filePath(
                    u"screenshot-%1.png"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss-zzz"_s)));
        }
        QQuickWindow *w = window();
        const QImage image = w ? w->grabWindow() : QImage();
        if (image.isNull())
            return in.fail(code(E::Failed), u"The window could not be captured"_s);
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || !image.save(&out, "PNG") || !out.commit())
            return in.fail(code(E::Failed), u"Could not write %1"_s.arg(file), {{u"path"_s, file}});
        in.ok(QJsonObject{{u"path"_s, file},
                          {u"width"_s, image.width()},
                          {u"height"_s, image.height()},
                          {u"devicePixelRatio"_s, image.devicePixelRatio()}});
    });
}

void RemoteControl::screenshotData(const ipc::ControlReply &reply)
{
    enqueueInput(reply, [this](const Input &in) {
        QQuickWindow *w = window();
        QImage image = w ? w->grabWindow() : QImage();
        if (image.isNull())
            return in.fail(code(E::Failed), u"The window could not be captured"_s);
        const qreal ratio = image.devicePixelRatio();
        QByteArray png;
        // (A picture too big for the answer: smaller, as the agent sees it anyway.)
        for (int tries = 0; tries < 4; ++tries) {
            png.clear();
            QBuffer buffer(&png);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
                return in.fail(code(E::Failed), u"The window could not be captured"_s);
            if (png.size() <= acp::MaxAppPngBytes)
                break;
            image = image.scaled(image.size() / 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (png.size() > acp::MaxAppPngBytes)
            return in.fail(code(E::Failed), u"The window's picture is too large to send"_s);
        in.ok(QJsonObject{{u"png"_s, QString::fromLatin1(png.toBase64())},
                          {u"width"_s, image.width()},
                          {u"height"_s, image.height()},
                          {u"devicePixelRatio"_s, ratio}});
    });
}

// ---- Items ------------------------------------------------------------------------------------

QString RemoteControl::handleFor(QQuickItem *item)
{
    if (const auto it = m_handles.constFind(item); it != m_handles.cend()) {
        if (m_items.value(*it) == item)
            return u"h%1"_s.arg(*it);
        m_handles.erase(it); // another item at the address of one that is gone
    }
    if (m_items.size() > 4096) {
        // Forget the handles of items that are gone.
        m_items.removeIf([](auto it) { return it.value().isNull(); });
        m_handles.removeIf([this](auto it) { return !m_items.contains(it.value()); });
    }
    const quint64 id = m_nextHandle++;
    m_items.insert(id, item);
    m_handles.insert(item, id);
    return u"h%1"_s.arg(id);
}

RemoteControl::ItemLookup RemoteControl::itemFor(const QString &handle) const
{
    bool ok = false;
    const quint64 id = handle.startsWith(u'h') ? QStringView(handle).sliced(1).toULongLong(&ok) : 0;
    const auto it = ok ? m_items.constFind(id) : m_items.cend();
    if (it == m_items.cend())
        return {nullptr, code(E::NotFound), u"Unknown handle %1 (handles come from find and state)"_s.arg(handle)};
    if (!*it)
        return {nullptr, code(E::StaleHandle), u"The item of %1 is gone: find it again"_s.arg(handle)};
    return {*it, {}, {}};
}

QJsonObject RemoteControl::describe(QQuickItem *item)
{
    QJsonObject o{{u"handle"_s, handleFor(item)},         {u"type"_s, uidriver::typeName(item)},
                  {u"objectName"_s, item->objectName()},  {u"text"_s, shownText(item).left(2000)},
                  {u"geometry"_s, rect(sceneRect(item))}, {u"visible"_s, uidriver::shown(item)},
                  {u"enabled"_s, item->isEnabled()},      {u"focus"_s, item->hasActiveFocus()}};
    if (QQuickWindow *w = item->window())
        o.insert(u"onScreen"_s, uidriver::onScreen(w, item));
    const QVariant checked = item->property("checked");
    if (checked.metaType() == QMetaType::fromType<bool>())
        o.insert(u"checked"_s, checked.toBool());
    if (secretField(item))
        o.insert(u"secret"_s, true); // a password field: its text stays in the app
    return o;
}

QJsonValue RemoteControl::describeFocus()
{
    QQuickWindow *w = window();
    QQuickItem *focus = w ? w->activeFocusItem() : nullptr;
    return focus ? QJsonValue(describe(focus)) : QJsonValue();
}

QList<QQuickItem *> RemoteControl::select(QQuickWindow *window, const QJsonObject &selector, bool shownOnly,
                                          QQuickItem *within) const
{
    const QString text = selector.value(u"text"_s).toString();
    const QString objectName = selector.value(u"objectName"_s).toString();
    const QString type = selector.value(u"type"_s).toString();
    const bool exact = selector.value(u"exact"_s).toBool();
    const QString folded = text.toCaseFolded();
    return uidriver::matching(
            window,
            [&](QQuickItem *item) {
                if (within && item != within && !within->isAncestorOf(item))
                    return false;
                if (!objectName.isEmpty() && item->objectName() != objectName)
                    return false;
                if (!type.isEmpty() && !typeMatches(item, type))
                    return false;
                if (!text.isEmpty()) {
                    const QString label = shownText(item);
                    if (exact ? label != text : !label.toCaseFolded().contains(folded))
                        return false;
                }
                return true;
            },
            shownOnly);
}

void RemoteControl::find(const QJsonObject &params, const ipc::ControlReply &reply)
{
    QQuickWindow *w = window();
    if (!w)
        return reply.fail(code(E::NotReady), u"The window is not there yet"_s);
    QQuickItem *within = nullptr;
    if (params.contains(u"within"_s)) {
        const ItemLookup lookup = itemFor(params.value(u"within"_s).toString());
        if (!lookup.item)
            return reply.fail(lookup.code, lookup.message, {{u"param"_s, u"within"_s}});
        within = lookup.item;
    }
    const bool visibleOnly = params.value(u"visibleOnly"_s).toBool(true);
    const int limit = params.value(u"limit"_s).toInt(50);
    const QList<QQuickItem *> found = select(w, params, visibleOnly, within);
    QJsonArray items;
    for (QQuickItem *item : found) {
        if (items.size() >= limit)
            break;
        items.append(describe(item));
    }
    reply.ok(QJsonObject{{u"items"_s, items}, {u"total"_s, found.size()}});
}

bool RemoteControl::resolveTarget(QQuickWindow *window, const QJsonObject &object, Target *target,
                                  const ipc::ControlReply &reply, const QString &what)
{
    const QString prefix = what.isEmpty() ? QString() : what + u": "_s;
    switch (ipc::targetKind(object)) {
    case ipc::TargetKind::Handle: {
        const ItemLookup lookup = itemFor(object.value(u"handle"_s).toString());
        if (!lookup.item) {
            reply.fail(lookup.code, prefix + lookup.message);
            return false;
        }
        if (!uidriver::shown(lookup.item)) {
            reply.fail(code(E::NotFound), prefix + u"The item is not shown"_s, {{u"item"_s, describe(lookup.item)}});
            return false;
        }
        target->item = lookup.item;
        target->isItem = true;
        return true;
    }
    case ipc::TargetKind::Point: {
        const QPointF p(object.value(u"x"_s).toDouble(), object.value(u"y"_s).toDouble());
        if (!QRectF(0, 0, window->width(), window->height()).contains(p)) {
            reply.fail(
                    code(E::BadParams),
                    prefix + u"The point is outside the window (%1×%2)"_s.arg(window->width()).arg(window->height()));
            return false;
        }
        target->point = p;
        return true;
    }
    case ipc::TargetKind::Selector: {
        const QList<QQuickItem *> found = select(window, object, true);
        if (found.isEmpty()) {
            reply.fail(code(E::NotFound), prefix + u"No shown item matches"_s, {{u"selector"_s, selectorOf(object)}});
            return false;
        }
        target->item = found.first();
        target->isItem = true;
        return true;
    }
    case ipc::TargetKind::None: break;
    }
    reply.fail(code(E::BadParams), prefix + u"A target is needed"_s);
    return false;
}

void RemoteControl::reachTarget(QQuickWindow *window, const Target &target,
                                std::function<void(QPointF, const char *)> then)
{
    if (!target.isItem) {
        then(target.point, nullptr);
        return;
    }
    QQuickItem *item = target.item;
    if (!item) {
        then(QPointF(-1, -1), E::StaleHandle);
        return;
    }
    if (uidriver::onScreen(window, item)) {
        then(uidriver::pointIn(item), nullptr);
        return;
    }
    // Scrolled into view first, as a user scrolls to what they want to touch. What still is not
    // on screen then (beyond the width of a list that only scrolls up and down, in a clipped part
    // nothing scrolls, outside the window) is not touched: the point would be another item's.
    uidriver::scrollIntoView(window, item);
    QTimer::singleShot(150, this, [this, item = target.item, then = std::move(then)] {
        QQuickWindow *w = this->window();
        if (!item || !w)
            then(QPointF(-1, -1), E::StaleHandle);
        else if (!uidriver::onScreen(w, item))
            then(uidriver::pointIn(item), E::OffScreen);
        else
            then(uidriver::pointIn(item), nullptr);
    });
}

namespace {

/// Where the content of each Flickable whose view holds a point is (contentX, contentY).
using ContentPositions = QList<std::pair<QPointer<QQuickItem>, QPointF>>;

/// Where a Flickable's content is: (contentX, contentY).
QPointF contentPosition(const QQuickItem *flickable)
{
    return {flickable->property("contentX").toReal(), flickable->property("contentY").toReal()};
}

ContentPositions contentPositions(QQuickWindow *window, QPointF scenePos)
{
    ContentPositions out;
    const QList<QQuickItem *> items = uidriver::allItems(window->contentItem());
    for (QQuickItem *item : items) {
        if (item->inherits("QQuickFlickable") && sceneRect(item).contains(scenePos))
            out.append({item, contentPosition(item)});
    }
    return out;
}

/// How far the content under the point moved since `before` (contentPositions()): the flickable
/// that moved most.
QJsonObject scrolledSince(const ContentPositions &before)
{
    QPointF most;
    for (const auto &[flickable, was] : before) {
        if (!flickable)
            continue;
        const QPointF moved = contentPosition(flickable) - was;
        if (std::hypot(moved.x(), moved.y()) > std::hypot(most.x(), most.y()))
            most = moved;
    }
    return {{u"dx"_s, rounded(most.x())}, {u"dy"_s, rounded(most.y())}};
}

} // namespace

// ---- Input ------------------------------------------------------------------------------------

void RemoteControl::enqueueInput(const ipc::ControlReply &reply, std::function<void(const Input &)> job)
{
    m_inputQueue.append({reply, std::move(job)});
    if (!m_inputRunning)
        nextInput();
}

void RemoteControl::enqueueSettled(const ipc::ControlReply &reply, const QString &noWindow,
                                   std::function<void(QQuickWindow *, const Input &)> start)
{
    enqueueInput(reply, [this, noWindow, start = std::move(start)](const Input &in) {
        QQuickWindow *w = window();
        if (!w)
            return in.fail(code(E::NotReady), noWindow);
        prepareInput(w);
        // A popup that just opened gives its field the keyboard when its transition is over.
        afterSettled(100, 2000, [this, in, start] {
            if (QQuickWindow *settled = window())
                start(settled, in);
            else
                in.fail(code(E::NotReady), u"The window is gone"_s);
        });
    });
}

void RemoteControl::playThenAnswer(QQuickWindow *window, const uidriver::Gesture &gesture, const Input &in,
                                   std::function<QJsonObject()> answer, std::function<bool()> keepGoing)
{
    uidriver::play(
            this, window, gesture,
            [this, in, answer = std::move(answer)](bool completed) {
                if (!completed)
                    return in.fail(code(E::Failed), u"The window went away"_s);
                afterSettled(100, 2000, [in, answer] { in.ok(answer()); });
            },
            std::move(keepGoing));
}

void RemoteControl::nextInput()
{
    // Requests whose client has gone (a command line that gave up waiting) are dropped: nobody
    // wants that input any more.
    while (!m_inputQueue.isEmpty() && !m_inputQueue.constFirst().reply.connected()) {
        qCDebug(lcApp) << "Remote control: dropped the input of a client that left";
        m_inputQueue.removeFirst();
    }
    m_inputRunning = !m_inputQueue.isEmpty();
    if (!m_inputRunning)
        return;
    const QueuedInput next = m_inputQueue.takeFirst();
    // Its answer lets the next one start (once, from the event loop).
    auto once = std::make_shared<bool>(false);
    next.job(Input{next.reply, [this, once] {
                       if (!std::exchange(*once, true))
                           QTimer::singleShot(0, this, [this] { nextInput(); });
                   }});
}

void RemoteControl::afterSettled(int quietMs, int maxMs, std::function<void()> then)
{
    auto *timer = new QTimer(this);
    timer->setInterval(20ms);
    QElapsedTimer waited;
    waited.start();
    connect(timer, &QTimer::timeout, this, [this, timer, waited, quietMs, maxMs, then = std::move(then)] {
        // No new frame for a while, and no popup in its enter or exit transition (on a slow
        // machine frames of a transition can be further apart than `quietMs`).
        const bool quiet =
                !m_frameWindow
                || ((!m_frameWindow->isExposed() || m_sinceFrame.elapsed() >= quietMs) && popupsSettled(m_frameWindow));
        if (!quiet && waited.elapsed() < maxMs)
            return;
        timer->stop();
        timer->deleteLater();
        then();
    });
    timer->start();
}

void RemoteControl::pointer(const QString &method, const QJsonObject &params, const ipc::ControlReply &reply)
{
    QQuickWindow *current = window();
    if (!current)
        return reply.fail(code(E::NotReady), u"The window is not there yet"_s);
    Target target;
    if (method == u"scroll" && ipc::targetKind(params) == ipc::TargetKind::None)
        target.point = QPointF(current->width() / 2.0, current->height() / 2.0);
    else if (!resolveTarget(current, params, &target, reply))
        return;
    // A finger with --touch (the phone's UI), except for scrolling: the wheel scrolls whatever is
    // under it, a finger where nothing can scroll that way presses what it is on (as a real one).
    const bool touch = usesTouch(params, method != u"scroll");
    const Qt::MouseButton button = ipc::parseMouseButton(params.value(u"button"_s).toString()).value_or(Qt::LeftButton);
    const Qt::KeyboardModifiers modifiers = ipc::parseModifiers(params.value(u"modifiers"_s)).value_or(Qt::NoModifier);
    const int holdMs = params.value(u"ms"_s).toInt(800);
    const QPointF delta(params.value(u"dx"_s).toDouble(), params.value(u"dy"_s).toDouble());
    enqueueSettled(reply, u"The window is gone"_s, [=, this](QQuickWindow *settled, const Input &in) {
        reachTarget(settled, target, [=, this](QPointF point, const char *problem) {
            QQuickWindow *reached = window();
            if (!reached || (problem && qstrcmp(problem, E::StaleHandle) == 0))
                return in.fail(code(E::StaleHandle), u"The item went away before it could be reached"_s);
            if (problem) {
                return in.fail(
                        code(E::OffScreen),
                        u"The item is outside the window, and scrolling cannot bring it in: nothing "
                        u"was touched"_s,
                        {{u"item"_s, describe(target.item)},
                         {u"window"_s, QJsonObject{{u"width"_s, reached->width()}, {u"height"_s, reached->height()}}}});
            }
            const QJsonObject where{{u"x"_s, rounded(point.x())},
                                    {u"y"_s, rounded(point.y())},
                                    {u"device"_s, touch ? u"touch"_s : u"mouse"_s}};
            // The answer: where, the item, whether the window is active.
            const auto answer = [this, target, where] {
                QJsonObject out = where;
                if (target.item)
                    out.insert(u"item"_s, describe(target.item));
                if (QQuickWindow *after = window())
                    out.insert(u"windowActive"_s, after->isActive());
                return out;
            };
            ContentPositions contentBefore;
            // The Flickable a finger would drag: the innermost under it that can move that way.
            QQuickItem *dragged = nullptr;
            if (method == u"scroll") {
                contentBefore = contentPositions(reached, point);
                const QList<QQuickItem *> flickables = uidriver::flickablesAt(reached, point);
                const auto it = std::ranges::find_if(flickables, [&delta](const QQuickItem *flickable) {
                    return uidriver::canScroll(flickable, delta);
                });
                dragged = it == flickables.cend() ? nullptr : *it;
                // A finger moves content only where some Flickable under it can move that way;
                // elsewhere a real one would press what it is on (a switch, a button) — not
                // done: nothing is touched, and the answer says nothing scrolled.
                if (touch && !dragged) {
                    QJsonObject out = answer();
                    out.insert(u"scrolled"_s, QJsonObject{{u"dx"_s, 0}, {u"dy"_s, 0}});
                    out.insert(u"touched"_s, false);
                    return in.ok(out);
                }
            }
            uidriver::Gesture gesture;
            if (method == u"click") {
                gesture = touch ? uidriver::touchTap(reached, point, 60, 200)
                                : uidriver::mouseClick(reached, point, button, modifiers,
                                                       {.afterMove = 20, .afterPress = 60, .afterRelease = 200});
            } else if (method == u"longPress") {
                gesture = touch ? uidriver::touchTap(reached, point, holdMs, 250)
                                : uidriver::mouseLongPress(reached, point, holdMs, button);
            } else if (method == u"doubleClick") {
                gesture = touch ? uidriver::touchDoubleTap(reached, point)
                                : uidriver::mouseDoubleClick(reached, point, button, modifiers);
            } else if (touch) {
                // Scrolling: the content follows the finger past the drag threshold (only where it
                // can move: checked above).
                gesture = uidriver::touchScroll(reached, point, delta, !dragged->property("moving").toBool());
            } else {
                gesture = uidriver::wheel(reached, point, delta, modifiers);
            }
            playThenAnswer(reached, gesture, in, [=] {
                QJsonObject out = answer();
                if (method == u"scroll") {
                    out.insert(u"scrolled"_s, scrolledSince(contentBefore));
                    if (touch)
                        out.insert(u"touched"_s, true);
                }
                return out;
            });
        });
    });
}

void RemoteControl::drag(const QJsonObject &params, const ipc::ControlReply &reply)
{
    QQuickWindow *current = window();
    if (!current)
        return reply.fail(code(E::NotReady), u"The window is not there yet"_s);
    Target from;
    Target to;
    if (!resolveTarget(current, params.value(u"from"_s).toObject(), &from, reply, u"from"_s)
        || !resolveTarget(current, params.value(u"to"_s).toObject(), &to, reply, u"to"_s))
        return;
    const bool touch = usesTouch(params, true);
    const Qt::MouseButton button = ipc::parseMouseButton(params.value(u"button"_s).toString()).value_or(Qt::LeftButton);
    const Qt::KeyboardModifiers modifiers = ipc::parseModifiers(params.value(u"modifiers"_s)).value_or(Qt::NoModifier);
    const int holdMs = params.value(u"holdMs"_s).toInt(0);
    enqueueSettled(reply, u"The window is gone"_s, [=, this](QQuickWindow *settled, const Input &in) {
        reachTarget(settled, from, [=, this](QPointF start, const char *problem) {
            QQuickWindow *reached = window();
            if (!reached || (problem && qstrcmp(problem, E::StaleHandle) == 0) || (to.isItem && !to.item))
                return in.fail(code(E::StaleHandle), u"An item went away before the drag"_s);
            // Both ends on screen (the start was scrolled into view; scrolling to the end
            // now would move the start away).
            if (problem || (to.isItem && !uidriver::onScreen(reached, to.item))) {
                const QString which = problem ? u"from"_s : u"to"_s;
                return in.fail(code(E::OffScreen),
                               u"The %1 item is outside the window, and scrolling cannot bring it in: nothing was "
                               u"touched"_s.arg(which),
                               {{u"param"_s, which}, {u"item"_s, describe(problem ? from.item : to.item)}});
            }
            const QPointF end = to.isItem ? uidriver::pointIn(to.item) : to.point;
            const uidriver::Gesture gesture =
                    touch ? uidriver::touchDrag(reached, start, end - start, 16, 16, holdMs, 300)
                          : uidriver::mouseDrag(reached, start, end, holdMs, false, button, modifiers);
            playThenAnswer(reached, gesture, in, [=] {
                return QJsonObject{{u"from"_s, QJsonObject{{u"x"_s, start.x()}, {u"y"_s, start.y()}}},
                                   {u"to"_s, QJsonObject{{u"x"_s, end.x()}, {u"y"_s, end.y()}}},
                                   {u"device"_s, touch ? u"touch"_s : u"mouse"_s}};
            });
        });
    });
}

void RemoteControl::typeText(const QJsonObject &params, const ipc::ControlReply &reply)
{
    const QString text = params.value(u"text"_s).toString();
    enqueueSettled(reply, u"The window is not there yet"_s, [this, text](QQuickWindow *settled, const Input &in) {
        const QList<uidriver::KeyStroke> strokes = uidriver::strokesFor(text);
        // Typing stops when the client leaves (a command line interrupted).
        playThenAnswer(
                settled, uidriver::keyStrokes(settled, strokes, uidriver::KeyDelivery::System, 8), in,
                [this, keys = strokes.size()] {
                    return QJsonObject{{u"keys"_s, keys}, {u"focusedItem"_s, describeFocus()}};
                },
                [reply = in.reply] { return reply.connected(); });
    });
}

void RemoteControl::pressKeys(const QJsonObject &params, const ipc::ControlReply &reply)
{
    QString error;
    const auto parsed = uidriver::parseKeySequence(params.value(u"sequence"_s).toString(), &error);
    if (!parsed)
        return reply.fail(code(E::BadParams), error, {{u"param"_s, u"sequence"_s}});
    const int count = params.value(u"count"_s).toInt(1);
    if (parsed->size() * count > 1000)
        return reply.fail(code(E::BadParams), u"At most 1000 keys at once"_s, {{u"param"_s, u"sequence"_s}});
    QList<uidriver::KeyStroke> strokes;
    for (int i = 0; i < count; ++i)
        strokes += *parsed;
    enqueueSettled(reply, u"The window is not there yet"_s, [this, strokes](QQuickWindow *settled, const Input &in) {
        auto handled = std::make_shared<QList<bool>>();
        uidriver::Gesture gesture;
        for (const uidriver::KeyStroke &stroke : strokes) {
            gesture.append(uidriver::Step{[settled, stroke, handled] {
                                              handled->append(uidriver::deliverKey(settled, stroke,
                                                                                   uidriver::KeyDelivery::System));
                                          },
                                          30, true});
        }
        playThenAnswer(
                settled, gesture, in,
                [this, handled, keys = strokes.size()] {
                    QJsonArray list;
                    for (const bool h : std::as_const(*handled))
                        list.append(h);
                    return QJsonObject{{u"keys"_s, keys}, {u"handled"_s, list}, {u"focusedItem"_s, describeFocus()}};
                },
                [reply = in.reply] { return reply.connected(); });
    });
}

// ---- waitFor ----------------------------------------------------------------------------------

void RemoteControl::waitFor(const QJsonObject &params, const ipc::ControlReply &reply)
{
    auto wait = std::make_shared<Wait>();
    wait->route = params.value(u"route"_s);
    wait->selector = selectorOf(params);
    wait->handle = params.value(u"handle"_s).toString();
    wait->idle = params.value(u"idle"_s).toBool();
    wait->gone = params.value(u"gone"_s).toBool();
    wait->ms = params.value(u"ms"_s).toInt(5000);
    wait->reply = reply;
    wait->since.start();
    if (!wait->handle.isEmpty()) {
        // An unknown handle never shows up; a gone item only satisfies `gone`.
        if (const ItemLookup lookup = itemFor(wait->handle);
            !lookup.item && (lookup.code == code(E::NotFound) || !wait->gone))
            return reply.fail(lookup.code, lookup.message, {{u"param"_s, u"handle"_s}});
    }
    if (satisfied(*wait))
        return reply.ok(waitAnswer(*wait));
    m_waits.append(wait);
    if (!m_waitTimer.isActive())
        m_waitTimer.start();
}

QJsonObject RemoteControl::waitAnswer(const Wait &wait)
{
    QJsonObject out{{u"waitedMs"_s, wait.since.elapsed()}, {u"route"_s, toJson(m_app->currentRoute())}};
    // The item that showed up (not for `gone`).
    if (!wait.gone) {
        QQuickItem *item = nullptr;
        if (!wait.selector.isEmpty()) {
            if (QQuickWindow *w = window())
                item = select(w, wait.selector, true).value(0);
        } else if (!wait.handle.isEmpty()) {
            item = itemFor(wait.handle).item;
        }
        if (item)
            out.insert(u"item"_s, describe(item));
    }
    return out;
}

bool RemoteControl::satisfied(const Wait &wait) const
{
    if (!wait.route.isUndefined() && !ipc::routeMatches(QJsonObject::fromVariantMap(m_app->currentRoute()), wait.route))
        return false;
    QQuickWindow *w = window();
    if (!wait.selector.isEmpty()) {
        const bool found = w && !select(w, wait.selector, true).isEmpty();
        if (found == wait.gone)
            return false;
    }
    if (!wait.handle.isEmpty()) {
        QQuickItem *item = itemFor(wait.handle).item;
        const bool there = item && uidriver::shown(item);
        if (there == wait.gone)
            return false;
    }
    return !wait.idle || idle();
}

QList<QObject *> RemoteControl::pageControllers() const
{
    QList<QObject *> out;
    QQuickWindow *w = window();
    if (!w)
        return out;
    const QList<QQuickItem *> items = uidriver::allItems(w->contentItem());
    for (QQuickItem *item : items) {
        if (!uidriver::isA(item, "BlockEditor"))
            continue;
        if (auto *controller = item->property("controller").value<QObject *>(); controller && !out.contains(controller))
            out.append(controller);
    }
    return out;
}

QJsonArray RemoteControl::conflicts() const
{
    QStringList pages;
    QJsonArray out;
    const QList<QObject *> controllers = pageControllers();
    for (const QObject *controller : controllers) {
        const QString page = controller->property("path").toString();
        if (controller->property("conflict").toBool() && !pages.contains(page)) {
            pages.append(page);
            out.append(QJsonObject{{u"page"_s, page}});
        }
    }
    return out;
}

bool RemoteControl::idle() const
{
    const QString status = m_app->status();
    if (status == u"boot" || status == u"loading")
        return false;
    if (m_app->workspace()->isOpen() && m_app->workspace()->indexing())
        return false;
    if (m_inputRunning || !m_inputQueue.isEmpty())
        return false;
    // Animations, transitions, scrolling: a frame within the last 300 ms (a text cursor blinks
    // every 500 ms).
    if (m_frameWindow && m_frameWindow->isExposed() && m_sinceFrame.elapsed() < 300)
        return false;
    if (m_frameWindow && !popupsSettled(m_frameWindow))
        return false;
    // Unsaved edits: every block editor's page saved — none with a conflict waiting for the user
    // either (its text is not saved until they decide; another program's change not shown).
    return std::ranges::none_of(pageControllers(), [](const QObject *controller) {
        return controller->property("dirty").toBool() || controller->property("conflict").toBool();
    });
}

void RemoteControl::pollWaits()
{
    const QList<std::shared_ptr<Wait>> waits = m_waits;
    for (const std::shared_ptr<Wait> &wait : waits) {
        bool done = false;
        if (!wait->reply.connected()) {
            done = true; // the client left
        } else if (satisfied(*wait)) {
            wait->reply.ok(waitAnswer(*wait));
            done = true;
        } else if (wait->since.elapsed() > wait->ms) {
            QJsonObject details{{u"route"_s, toJson(m_app->currentRoute())}, {u"idle"_s, idle()}};
            // Why it is not idle when that is a page waiting for the user.
            if (const QJsonArray pending = conflicts(); !pending.isEmpty())
                details.insert(u"conflicts"_s, pending);
            wait->reply.fail(code(E::Timeout), u"The condition did not hold within %1 ms"_s.arg(wait->ms), details);
            done = true;
        }
        if (done)
            m_waits.removeOne(wait);
    }
    if (m_waits.isEmpty())
        m_waitTimer.stop();
}

// ---- Live changes -------------------------------------------------------------------------------

namespace {

/// The properties of a password field that hold what was typed: never read, matched or written.
bool secretProperty(QStringView name)
{
    return name == u"text" || name == u"displayText" || name == u"preeditText" || name == u"selectedText"
           || name == u"getText";
}

/// A property whose name says it holds a secret (a dialog's `password`, `passphrase`, `token` …):
/// neither read nor written, on any item.
bool sensitiveProperty(const QString &name)
{
    static const QRegularExpression pattern(u"password|passphrase|passwd|secret|token|credential|apikey|privatekey"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name).hasMatch();
}

/// An enum's key (a flag's keys) for its value; "" when it has none.
QString enumKey(const QMetaEnum &e, int value)
{
    const QByteArray key = e.isFlag() ? e.valueToKeys(value) : QByteArray(e.valueToKey(value));
    return QString::fromLatin1(key);
}

} // namespace

QString RemoteControl::who(const ipc::ControlRequest &request)
{
    return u"live:"_s + (request.client.isEmpty() ? u"agent"_s : request.client);
}

void RemoteControl::evaluate(const QJsonObject &params, const ipc::ControlReply &reply)
{
    Workspace *ws = m_app->workspace();
    if (!ws->isOpen())
        return reply.fail(code(E::NotReady), u"No notebook is open (status %1)"_s.arg(m_app->status()));
    const QVariantMap result =
            ws->extensions()->evaluateFor(params.value(u"code"_s).toString(), params.value(u"extension"_s).toString());
    reply.ok(QJsonObject::fromVariantMap(result));
}

QJsonObject RemoteControl::extensionList() const
{
    WorkspaceExtensions *extensions = m_app->workspace()->extensions();
    QJsonArray rows;
    for (const ExtensionRow &r : extensions->list()->rows()) {
        QJsonObject row{{u"id"_s, r.id},
                        {u"title"_s, r.title},
                        {u"kind"_s, r.kind},
                        {u"state"_s, r.state},
                        {u"message"_s, r.message},
                        {u"path"_s, r.path},
                        {u"permissions"_s, r.permissions},
                        {u"commands"_s, r.commands},
                        {u"views"_s, r.views},
                        {u"hooks"_s, r.hooks},
                        {u"types"_s, r.types}};
        if (r.errorLine >= 0)
            row.insert(u"line"_s, r.errorLine + 1);
        if (!r.typeMessage.isEmpty())
            row.insert(u"typeMessage"_s, r.typeMessage);
        if (!r.description.isEmpty())
            row.insert(u"description"_s, r.description);
        rows.append(row);
    }
    return {{u"extensions"_s, rows}, {u"live"_s, QJsonArray::fromVariantList(extensions->liveExtensions())}};
}

void RemoteControl::loadLive(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply)
{
    Workspace *ws = m_app->workspace();
    if (!ws->isOpen())
        return reply.fail(code(E::NotReady), u"No notebook is open (status %1)"_s.arg(m_app->status()));
    WorkspaceExtensions *extensions = ws->extensions();
    const QString id = params.value(u"id"_s).toString().trimmed();
    const QString before = extensions->liveOwner(id);
    const QVariantMap result = extensions->loadLive(id, params.value(u"source"_s).toString(),
                                                    ipc::stringList(params.value(u"permissions"_s)),
                                                    params.value(u"title"_s).toString(), who);
    // In the journal once per client: revert takes it out (unless another client's code replaced it).
    if (result.value(u"ok"_s).toBool() && before != who) {
        const QPointer<WorkspaceExtensions> alive(extensions);
        m_app->scripting()->noteLive(who, u"extension"_s, id, [alive, id, who] {
            if (alive && alive->liveOwner(id) == who)
                alive->unloadLive(id);
        });
    }
    reply.ok(QJsonObject::fromVariantMap(result));
}

void RemoteControl::unloadLive(const QJsonObject &params, const ipc::ControlReply &reply)
{
    WorkspaceExtensions *extensions = m_app->workspace()->extensions();
    const QString id = params.value(u"id"_s).toString().trimmed();
    if (!extensions->unloadLive(id)) {
        QJsonArray ids;
        for (const QVariant &row : extensions->liveExtensions())
            ids.append(row.toMap().value(u"id"_s).toString());
        return reply.fail(code(E::NotFound), u"No live extension %1"_s.arg(id), {{u"live"_s, ids}});
    }
    // Nothing left to undo of it.
    m_app->scripting()->forgetLive(u"extension"_s, id);
    reply.ok(QJsonObject{{u"id"_s, id}, {u"unloaded"_s, true}});
}

QJsonObject RemoteControl::consoleLines(const QJsonObject &params) const
{
    const ExtensionConsoleModel *console = m_app->workspace()->extensions()->console();
    const qint64 since = params.contains(u"since"_s) ? params.value(u"since"_s).toInteger() : -1;
    const int limit = params.value(u"limit"_s).toInt(200);
    const QVariantList lines = console->linesAfter(since, limit);
    // `next`: what the next request names as since (the newest line given, else as it was).
    qint64 next = lines.isEmpty() ? std::min(since < 0 ? console->lastNumber() : since, console->lastNumber())
                                  : lines.last().toMap().value(u"n"_s).toLongLong();
    return {{u"lines"_s, QJsonArray::fromVariantList(lines)},
            {u"next"_s, next},
            {u"first"_s, console->firstNumber()},
            {u"last"_s, console->lastNumber()}};
}

void RemoteControl::setSetting(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply)
{
    AppScripting *scripting = m_app->scripting();
    const QString key = params.value(u"key"_s).toString().trimmed();
    QString error;
    if (!scripting->setSetting(who, key, params.value(u"value"_s).toVariant(), &error))
        return reply.fail(code(E::BadParams), error, {{u"param"_s, u"value"_s}, {u"key"_s, key}});
    QString ignored;
    reply.ok(QJsonObject{{u"key"_s, key}, {u"value"_s, QJsonValue::fromVariant(scripting->setting(key, &ignored))}});
}

void RemoteControl::theme(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply)
{
    AppScripting *scripting = m_app->scripting();
    if (params.contains(u"tokens"_s)) {
        extensions::ThemeLayer layer;
        QString error;
        if (!extensions::themeLayerFrom(params.value(u"tokens"_s).toObject().toVariantMap(), &layer, &error))
            return reply.fail(code(E::BadParams), error, {{u"param"_s, u"tokens"_s}});
        scripting->setLiveTheme(who, layer);
    } else if (params.value(u"clear"_s).toBool()) {
        // (Only a layer there is noted as a change.)
        const QVariantList live = scripting->theme().value(u"live"_s).toList();
        if (std::ranges::any_of(live, [&who](const QVariant &v) { return v.toMap().value(u"who"_s) == who.mid(5); }))
            scripting->setLiveTheme(who, extensions::ThemeLayer());
    }
    QJsonObject out = QJsonObject::fromVariantMap(scripting->theme());
    if (params.value(u"catalog"_s).toBool()) {
        QJsonArray catalog;
        for (const extensions::ThemeToken &t : extensions::themeTokens()) {
            QJsonObject token{{u"name"_s, t.name}};
            switch (t.kind) {
            case extensions::ThemeTokenKind::Color: token.insert(u"kind"_s, u"color"_s); break;
            case extensions::ThemeTokenKind::Font: token.insert(u"kind"_s, u"font"_s); break;
            case extensions::ThemeTokenKind::Number:
                token.insert(u"kind"_s, u"number"_s);
                token.insert(u"min"_s, t.min);
                token.insert(u"max"_s, t.max);
                break;
            }
            catalog.append(token);
        }
        out.insert(u"catalog"_s, catalog);
    }
    reply.ok(out);
}

void RemoteControl::contributions(const QJsonObject &params, const ipc::ControlReply &reply)
{
    const QString slot = params.value(u"slot"_s).toString().trimmed();
    if (!slot.isEmpty() && !extensions::isContributionSlot(slot))
        return reply.fail(
                code(E::BadParams), u"Unknown slot %1"_s.arg(slot),
                {{u"param"_s, u"slot"_s}, {u"slots"_s, QJsonArray::fromStringList(extensions::contributionSlots())}});
    const QVariantList items = m_app->workspace()->extensions()->contributions(slot);
    reply.ok(QJsonObject{{u"contributions"_s, QJsonArray::fromVariantList(items)}});
}

void RemoteControl::inspect(const QJsonObject &params, const ipc::ControlReply &reply)
{
    QQuickWindow *w = window();
    if (!w)
        return reply.fail(code(E::NotReady), u"The window is not there yet"_s);
    QQuickItem *root = w->contentItem();
    if (params.contains(u"handle"_s)) {
        const ItemLookup lookup = itemFor(params.value(u"handle"_s).toString());
        if (!lookup.item)
            return reply.fail(lookup.code, lookup.message, {{u"param"_s, u"handle"_s}});
        root = lookup.item;
    }
    int count = 0;
    bool truncated = false;
    const QJsonObject tree =
            inspectItem(root, params.value(u"depth"_s).toInt(3), params.value(u"properties"_s).toBool(),
                        params.value(u"limit"_s).toInt(500), &count, &truncated);
    reply.ok(QJsonObject{{u"root"_s, tree}, {u"items"_s, count}, {u"truncated"_s, truncated}});
}

QJsonObject RemoteControl::inspectItem(QQuickItem *item, int depth, bool properties, int limit, int *count,
                                       bool *truncated)
{
    ++*count;
    QJsonObject o{{u"handle"_s, handleFor(item)},
                  {u"type"_s, uidriver::typeName(item)},
                  {u"objectName"_s, item->objectName()},
                  {u"geometry"_s, rect(sceneRect(item))},
                  {u"visible"_s, uidriver::shown(item)}};
    if (const QString text = shownText(item); !text.isEmpty())
        o.insert(u"text"_s, text.left(500));
    if (secretField(item))
        o.insert(u"secret"_s, true); // a password field: its text stays in the app
    if (properties)
        o.insert(u"properties"_s, propertiesOf(item));
    const QList<QQuickItem *> children = item->childItems();
    if (children.isEmpty())
        return o;
    if (depth <= 0) {
        o.insert(u"childCount"_s, children.size()); // inspect {handle} goes on from here
        return o;
    }
    QJsonArray kids;
    for (QQuickItem *child : children) {
        if (*count >= limit) {
            *truncated = true;
            o.insert(u"childCount"_s, children.size());
            break;
        }
        kids.append(inspectItem(child, depth - 1, properties, limit, count, truncated));
    }
    o.insert(u"children"_s, kids);
    return o;
}

QJsonObject RemoteControl::propertiesOf(QQuickItem *item)
{
    QJsonObject out;
    const bool secret = secretField(item);
    const QMetaObject *meta = item->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        const QString name = QString::fromLatin1(property.name());
        if (!property.isReadable() || (secret && secretProperty(name)) || sensitiveProperty(name))
            continue;
        const QJsonValue value = jsonOf(property.read(item), &property);
        if (!value.isUndefined())
            out.insert(name, value);
    }
    return out;
}

QJsonValue RemoteControl::jsonOf(const QVariant &value, const QMetaProperty *property, int depth)
{
    if (!value.isValid() || value.isNull())
        return QJsonValue::Null;
    const QMetaType type = value.metaType();
    if (property && property->isEnumType()) {
        if (const QString key = enumKey(property->enumerator(), value.toInt()); !key.isEmpty())
            return key;
        return value.toInt();
    }
    switch (type.id()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar: return QJsonValue(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Float16: {
        const double d = value.toDouble();
        return std::isfinite(d) ? QJsonValue(d) : QJsonValue(QJsonValue::Null);
    }
    case QMetaType::QString: return value.toString().left(2000);
    case QMetaType::QUrl: return value.toUrl().toString().left(2000);
    case QMetaType::QColor: return value.value<QColor>().name(QColor::HexArgb);
    case QMetaType::QPoint:
    case QMetaType::QPointF: {
        const QPointF p = value.toPointF();
        return QJsonObject{{u"x"_s, rounded(p.x())}, {u"y"_s, rounded(p.y())}};
    }
    case QMetaType::QSize:
    case QMetaType::QSizeF: {
        const QSizeF size = value.toSizeF();
        return QJsonObject{{u"width"_s, rounded(size.width())}, {u"height"_s, rounded(size.height())}};
    }
    case QMetaType::QRect:
    case QMetaType::QRectF: return rect(value.toRectF());
    case QMetaType::QFont: {
        const QFont font = value.value<QFont>();
        return QJsonObject{{u"family"_s, font.family()},
                           {u"pixelSize"_s, font.pixelSize()},
                           {u"pointSize"_s, font.pointSizeF()},
                           {u"bold"_s, font.bold()},
                           {u"italic"_s, font.italic()}};
    }
    case QMetaType::QStringList: return QJsonArray::fromStringList(value.toStringList().mid(0, 200));
    case QMetaType::QVariantMap:
    case QMetaType::QVariantList: {
        // Plain data only, and only a little of it (a model's rows are no property to read here).
        if (depth > 0)
            return QJsonValue::Undefined;
        const QJsonValue json = QJsonValue::fromVariant(value);
        const qsizetype size = json.isObject() ? QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact).size()
                                               : QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact).size();
        return size <= 4000 ? json : QJsonValue(QJsonValue::Undefined);
    }
    default: break;
    }
    if (type == QMetaType::fromType<QJSValue>()) {
        const QJSValue js = value.value<QJSValue>();
        if (js.isCallable() || depth > 0)
            return QJsonValue::Undefined;
        return jsonOf(js.toVariant(), nullptr, depth + 1);
    }
    if (type.flags() & QMetaType::PointerToQObject) {
        // An item as its handle (parent, contentItem …); other objects are not given.
        if (auto *item = qobject_cast<QQuickItem *>(value.value<QObject *>()))
            return QJsonObject{{u"handle"_s, handleFor(item)}};
        return QJsonValue::Undefined;
    }
    if (type.flags() & QMetaType::IsEnumeration)
        return QJsonValue(value.toLongLong());
    return QJsonValue::Undefined;
}

void RemoteControl::setItemProperty(const QString &who, const QJsonObject &params, const ipc::ControlReply &reply)
{
    const ItemLookup lookup = itemFor(params.value(u"handle"_s).toString());
    if (!lookup.item)
        return reply.fail(lookup.code, lookup.message, {{u"param"_s, u"handle"_s}});
    QQuickItem *item = lookup.item;
    const QString name = params.value(u"name"_s).toString().trimmed();
    if ((secretField(item) && secretProperty(name.section(u'.', 0, 0))) || sensitiveProperty(name))
        return reply.fail(code(E::Refused), u"A password field's text or a secret is neither read nor written"_s,
                          {{u"param"_s, u"name"_s}});
    const QQmlProperty property(item, name, qmlContext(item));
    if (!property.isValid() || property.type() != QQmlProperty::Property)
        return reply.fail(code(E::NotFound), u"%1 has no property %2"_s.arg(uidriver::typeName(item), name),
                          {{u"param"_s, u"name"_s}, {u"item"_s, describe(item)}});
    if (!property.isWritable())
        return reply.fail(code(E::Refused), u"%1 is read-only"_s.arg(name), {{u"param"_s, u"name"_s}});
    const QMetaType type = property.propertyMetaType();
    const QJsonValue json = params.value(u"value"_s);
    QVariant value = json.toVariant();
    const QMetaProperty meta = property.property();
    bool fits = true;
    if (type == QMetaType::fromType<QColor>()) {
        const QColor color(json.toString());
        fits = json.isString() && color.isValid();
        value = color;
    } else if (type == QMetaType::fromType<QUrl>()) {
        fits = json.isString();
        value = QUrl(json.toString());
    } else if (meta.isEnumType() && json.isString()) {
        bool ok = false;
        const QByteArray key = json.toString().toLatin1();
        const int v = meta.isFlagType() ? meta.enumerator().keysToValue(key.constData(), &ok)
                                        : meta.enumerator().keyToValue(key.constData(), &ok);
        fits = ok;
        value = v;
    } else if (type == QMetaType::fromType<QPointF>() || type == QMetaType::fromType<QPoint>()) {
        const QJsonObject o = json.toObject();
        fits = o.value(u"x"_s).isDouble() && o.value(u"y"_s).isDouble();
        value = QPointF(o.value(u"x"_s).toDouble(), o.value(u"y"_s).toDouble());
    } else if (type == QMetaType::fromType<QSizeF>() || type == QMetaType::fromType<QSize>()) {
        const QJsonObject o = json.toObject();
        fits = o.value(u"width"_s).isDouble() && o.value(u"height"_s).isDouble();
        value = QSizeF(o.value(u"width"_s).toDouble(), o.value(u"height"_s).toDouble());
    } else if (type == QMetaType::fromType<QRectF>() || type == QMetaType::fromType<QRect>()) {
        const QJsonObject o = json.toObject();
        fits = o.value(u"x"_s).isDouble() && o.value(u"y"_s).isDouble() && o.value(u"width"_s).isDouble()
               && o.value(u"height"_s).isDouble();
        value = QRectF(o.value(u"x"_s).toDouble(), o.value(u"y"_s).toDouble(), o.value(u"width"_s).toDouble(),
                       o.value(u"height"_s).toDouble());
    } else if (type.flags() & QMetaType::PointerToQObject) {
        fits = false; // (an object is no value an agent can give)
    } else if (type.id() == QMetaType::QString) {
        fits = json.isString();
    } else if (type.id() == QMetaType::Bool) {
        fits = json.isBool();
    } else if (type.flags() & QMetaType::IsEnumeration || type.id() == QMetaType::Int || type.id() == QMetaType::Double
               || type.id() == QMetaType::Float || type.id() == QMetaType::LongLong || type.id() == QMetaType::UInt) {
        fits = json.isDouble() && std::isfinite(json.toDouble());
    }
    if (fits && type.isValid() && type != QMetaType::fromType<QVariant>() && type != QMetaType::fromType<QJSValue>())
        fits = value.canConvert(type);
    if (!fits)
        return reply.fail(code(E::BadParams), u"%1 takes %2"_s.arg(name, QString::fromLatin1(type.name())),
                          {{u"param"_s, u"value"_s}});
    const QVariant before = property.read();
    const QJsonValue beforeJson = jsonOf(before, &meta);
    // A developer's experiment: the write removes the property's binding (as an assignment in QML
    // does); revert writes the value before back, the binding stays gone until the item is made again.
    if (!property.write(value))
        return reply.fail(code(E::BadParams), u"%1 did not take the value"_s.arg(name), {{u"param"_s, u"value"_s}});
    const QPointer<QQuickItem> alive(item);
    m_app->scripting()->noteLive(who, u"property"_s, uidriver::typeName(item) + u'.' + name, [alive, name, before] {
        if (alive)
            QQmlProperty(alive.data(), name, qmlContext(alive.data())).write(before);
    });
    reply.ok(QJsonObject{{u"handle"_s, handleFor(item)},
                         {u"name"_s, name},
                         {u"before"_s, beforeJson.isUndefined() ? QJsonValue() : beforeJson},
                         {u"after"_s, jsonOf(property.read(), &meta)}});
}

// ---- Events -------------------------------------------------------------------------------------

void RemoteControl::watchEvents()
{
    connect(m_app, &AppController::currentRouteChanged, this, [this] {
        // (The same route noted again — a route replaced by itself — is no event.)
        const QJsonValue route = toJson(m_app->currentRoute());
        if (route == m_lastRoute)
            return;
        m_lastRoute = route;
        noteEvent(u"route"_s, {{u"route"_s, route}});
    });
    AppScripting *scripting = m_app->scripting();
    connect(scripting, &AppScripting::liveChanged, this, [this, scripting] {
        noteEvent(u"live"_s, {{u"revision"_s, scripting->liveRevision()},
                              {u"clients"_s, QJsonArray::fromVariantList(scripting->liveClients())}});
    });
    Workspace *ws = m_app->workspace();
    WorkspaceExtensions *extensions = ws->extensions();
    connect(extensions, &WorkspaceExtensions::liveChanged, this, &RemoteControl::noteExtensionsChanged);
    connect(extensions, &WorkspaceExtensions::commandsChanged, this, &RemoteControl::noteExtensionsChanged);
    connect(extensions, &WorkspaceExtensions::appRevisionChanged, this, &RemoteControl::noteExtensionsChanged);
    connect(extensions->console(), &ExtensionConsoleModel::lineAppended, this,
            [this](qint64 number, const QString &kind, const QString &text, const QString &source) {
                noteEvent(u"console"_s,
                          {{u"n"_s, number}, {u"kind"_s, kind}, {u"text"_s, text.left(2000)}, {u"source"_s, source}});
            });
    connect(ws, &Workspace::filesChanged, this, [this](const QStringList &paths) {
        noteEvent(u"notes"_s,
                  {{u"paths"_s, QJsonArray::fromStringList(paths.mid(0, 200))}, {u"count"_s, paths.size()}});
    });
}

void RemoteControl::noteExtensionsChanged()
{
    if (m_extensionsEventDue || (!m_server.isListening() && m_localGrants.isEmpty()))
        return;
    m_extensionsEventDue = true;
    QTimer::singleShot(0, this, [this] {
        m_extensionsEventDue = false;
        noteEvent(u"extensions"_s,
                  {{u"live"_s, QJsonArray::fromVariantList(m_app->workspace()->extensions()->liveExtensions())}});
    });
}

void RemoteControl::noteEvent(const QString &type, QJsonObject data)
{
    if (!m_server.isListening() && m_localGrants.isEmpty())
        return; // (nobody to tell: nothing kept)
    const qint64 id = m_nextEvent++;
    data.insert(u"id"_s, id);
    data.insert(u"type"_s, type);
    data.insert(u"at"_s, QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    m_events.append({id, data});
    while (m_events.size() > MaxEvents)
        m_events.removeFirst();
    // The waits it answers: answered from the event loop (the change may be inside a call).
    if (m_eventWaits.isEmpty())
        return;
    QTimer::singleShot(0, this, [this] {
        const QList<std::shared_ptr<EventWait>> waits = m_eventWaits;
        for (const std::shared_ptr<EventWait> &wait : waits) {
            if (!wait->reply.connected()) {
                m_eventWaits.removeOne(wait);
            } else if (!m_events.isEmpty() && m_events.last().id > wait->since) {
                wait->reply.ok(eventsAfter(wait->since));
                m_eventWaits.removeOne(wait);
            }
        }
    });
}

QJsonObject RemoteControl::eventsAfter(qint64 since) const
{
    QJsonArray out;
    for (const Event &e : m_events) {
        if (e.id > since)
            out.append(e.data);
    }
    const qint64 newest = m_nextEvent - 1;
    return {{u"events"_s, out},
            {u"next"_s, out.isEmpty() ? std::min(since, newest) : out.last().toObject().value(u"id"_s).toInteger()},
            {u"first"_s, m_events.isEmpty() ? m_nextEvent : m_events.first().id}};
}

void RemoteControl::events(const QJsonObject &params, const ipc::ControlReply &reply)
{
    // (An id beyond the newest — from an earlier run of the app — waits for the next event.)
    const qint64 since = std::min(params.value(u"since"_s).toInteger(0), m_nextEvent - 1);
    const int wait = params.value(u"wait"_s).toInt(0);
    if (wait <= 0 || (!m_events.isEmpty() && m_events.last().id > since))
        return reply.ok(eventsAfter(since));
    auto pending = std::make_shared<EventWait>();
    pending->since = since;
    pending->reply = reply;
    m_eventWaits.append(pending);
    QTimer::singleShot(wait, this, [this, pending] {
        if (!m_eventWaits.removeOne(pending))
            return; // (answered)
        pending->reply.ok(eventsAfter(pending->since));
    });
}

} // namespace brulion::shell
