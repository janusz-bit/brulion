#include "remotecontrol.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "navigation.h"
#include "uidriver.h"
#include "workspace.h"
#include "workspacesync.h"

#include "dates/local.h"
#include "dates/natural.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "notebook/itemref.h"
#include "notebook/pages.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

using namespace Qt::StringLiterals;

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

namespace {

QString code(const char *errorCode)
{
    return QString::fromLatin1(errorCode);
}

/// The text of a label as people read it: rich text without its tags and entities.
QString plainText(const QString &label)
{
    if (!label.contains(u'<') && !label.contains(u'&'))
        return label;
    static const QRegularExpression tags(u"<[^>]*>"_s);
    QString out = label;
    out.replace(QRegularExpression(u"<br\\s*/?>|</p>|</li>|</h[1-6]>"_s, QRegularExpression::CaseInsensitiveOption),
                u"\n"_s);
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
    const QVariant echoMode = item->property("echoMode");
    return echoMode.isValid() && echoMode.toInt() != 0;
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
    const QList<QObject *> popups = uidriver::openPopups(window);
    for (QObject *popup : popups) {
        const QVariant opened = popup->property("opened");
        if (opened.isValid() && !opened.toBool())
            return false;
    }
    return true;
}

/// The item's QML type (or one it is based on) is `name`: "BButton", "TasksRow", "QQuickText" —
/// or a Qt Quick type by its QML name ("Text", "TextEdit", "Flickable").
bool typeMatches(const QQuickItem *item, const QString &name)
{
    for (const QMetaObject *m = item->metaObject(); m; m = m->superClass()) {
        QString cls = QString::fromLatin1(m->className());
        for (const QString &marker : {u"_QMLTYPE_"_s, u"_QML_"_s}) {
            if (const qsizetype at = cls.indexOf(marker); at > 0)
                cls.truncate(at);
        }
        if (cls == name || (!name.startsWith(u'Q') && cls == u"QQuick"_s + name))
            return true;
    }
    return false;
}

QJsonObject rect(const QRectF &r)
{
    const auto round = [](qreal v) { return std::round(v * 10) / 10; };
    return {{u"x"_s, round(r.x())},
            {u"y"_s, round(r.y())},
            {u"width"_s, round(r.width())},
            {u"height"_s, round(r.height())}};
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
        else if (s.size() > 80)
            it.value() = QString(s.left(79) + u'…');
    }
    return shown.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact));
}

QJsonValue toJson(const QVariantMap &map)
{
    return map.isEmpty() ? QJsonValue() : QJsonValue(QJsonObject::fromVariantMap(map));
}

} // namespace

RemoteControl::RemoteControl(QQmlApplicationEngine *engine, AppController *app, Options options, QObject *parent)
    : QObject(parent), m_engine(engine), m_app(app), m_options(std::move(options))
{
    m_server.setHandler(
            [this](const ipc::ControlRequest &request, const ipc::ControlReply &reply) { handle(request, reply); });
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
    m_waitTimer.setInterval(50);
    connect(&m_waitTimer, &QTimer::timeout, this, &RemoteControl::pollWaits);
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
    QString error;
    if (wanted && !m_server.isListening()) {
        if (m_server.listen(m_options.key)) {
            qCInfo(lcApp).noquote() << u"Remote control on: %1"_s.arg(m_server.socketPath());
        } else {
            error = m_server.errorString();
            qCWarning(lcApp).noquote() << u"Remote control could not start: %1"_s.arg(error);
        }
    } else if (!wanted && m_server.isListening()) {
        m_server.close(); // its connections too: waits and gestures answer nobody
        m_waits.clear();
        m_waitTimer.stop();
        qCInfo(lcApp) << "Remote control off";
    }
    m_app->setRemoteControl({{u"available"_s, true},
                             {u"setting"_s, setting},
                             {u"active"_s, isActive()},
                             {u"forced"_s, m_options.forced},
                             {u"socket"_s, socketPath()},
                             {u"error"_s, error}});
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

void RemoteControl::onToastShown(int toastId, const QString &text, const QString &kind)
{
    Q_UNUSED(toastId);
    m_toasts.append({text, kind, QDateTime::currentDateTime()});
    while (m_toasts.size() > 10)
        m_toasts.removeFirst();
}

// ---- Requests -----------------------------------------------------------------------------------

void RemoteControl::handle(const ipc::ControlRequest &request, ipc::ControlReply reply)
{
    const QString &method = request.method;
    const QJsonObject &p = request.params;
    qCDebug(lcApp).noquote() << u"Remote control: %1 %2"_s.arg(method, paramsSummary(method, p)).trimmed();
    if (method == u"hello") {
        reply.ok(hello());
    } else if (method == u"state") {
        reply.ok(state());
    } else if (method == u"navigate") {
        navigate(p, reply);
    } else if (method == u"open") {
        open(p, reply);
    } else if (method == u"commands") {
        QJsonArray list;
        for (const QVariant &c : m_app->commands()->commands())
            list.append(QJsonObject::fromVariantMap(c.toMap()));
        reply.ok(QJsonObject{{u"commands"_s, list}});
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
    } else {
        reply.fail(code(E::UnknownMethod), u"Unknown method %1"_s.arg(method));
    }
}

QJsonObject RemoteControl::hello() const
{
    QJsonArray methods;
    for (const ipc::MethodSpec &m : ipc::controlMethods())
        methods.append(m.name);
    return {{u"protocol"_s, ipc::ControlProtocolVersion},
            {u"app"_s, u"Brulion"_s},
            {u"version"_s, m_app->version()},
            {u"platform"_s, m_app->platform()},
            {u"methods"_s, methods}};
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
    // Conflict copies sync left to merge (Synchronizacja → Konflikty): not pages waiting here.
    out.insert(u"syncConflicts"_s, ws->isOpen() && ws->sync() ? ws->sync()->conflictCount() : 0);
    out.insert(u"openPopups"_s, popups);
    QJsonArray toasts;
    for (const Toast &t : std::as_const(m_toasts))
        toasts.append(QJsonObject{
                {u"text"_s, t.text}, {u"kind"_s, t.kind}, {u"at"_s, t.at.toString(u"yyyy-MM-dd HH:mm:ss"_s)}});
    out.insert(u"lastToasts"_s, toasts);
    auto *theme = m_engine->singletonInstance<QObject *>("Brulion", "Theme");
    out.insert(u"theme"_s, theme && theme->property("dark").toBool() ? u"dark"_s : u"light"_s);
    out.insert(u"themeSetting"_s, m_app->settings()->themeMode());
    out.insert(u"touchUi"_s, theme && theme->property("touchUi").toBool());
    out.insert(u"language"_s, m_options.language ? m_options.language() : m_app->settings()->language());
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
        const bool wantsPlace = item->hasLine() || item->hasBlock() || params.contains(u"line"_s)
                                || params.contains(u"blockId"_s) || params.contains(u"heading"_s);
        if (wantsPlace && !path.isEmpty() && vault->exists(path))
            return pageRoute(path);
        QVariantMap route{{u"view"_s, u"journal"_s}, {u"date"_s, date}};
        if (!path.isEmpty())
            route.insert(u"path"_s, path);
        return route;
    };
    QVariantMap route;
    QStringList candidates;
    if (item->page.isEmpty()) {
        // ^id / ((id)): the block anywhere in the notebook.
        const auto block = index->blockById(item->blockId);
        if (!block)
            return reply.fail(code(E::NotFound), u"No block with the id %1"_s.arg(item->blockId),
                              {{u"blockId"_s, item->blockId}});
        route = pageRoute(block->path);
    } else {
        const QString name = item->page;
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
            const auto fold = [](const QString &s) { return s.normalized(QString::NormalizationForm_C).toLower(); };
            const QString key = fold(notebook::withoutMdExtension(name).trimmed());
            for (const FileStat &f : vault->markdownFiles()) {
                if (paths::isConflictCopy(f.path))
                    continue;
                const QString stem = fold(paths::stripMarkdownExtension(f.path));
                if (key.contains(u'/') ? (stem == key || stem.endsWith(u'/' + key))
                                       : fold(paths::pageTitle(f.path)) == key)
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
        // 5. A date phrase ("wczoraj", "w piątek") when no page has that name.
        if (route.isEmpty() && !name.contains(u'/')) {
            const dates::UiLocale locale =
                    (m_options.language ? m_options.language() : m_app->settings()->language()) == u"en"
                            ? dates::UiLocale::English
                            : dates::UiLocale::Polish;
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
    if (route.value(u"view"_s).toString() == u"page") {
        if (params.contains(u"line"_s) || params.contains(u"blockId"_s) || params.contains(u"heading"_s)) {
            route.remove(u"line"_s);
            route.remove(u"blockId"_s);
            route.remove(u"heading"_s);
        }
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
    QString path = params.value(u"path"_s).toString().trimmed();
    if (!path.isEmpty()) {
        // Written only where asked: an absolute .png path in a folder that exists.
        if (!QDir::isAbsolutePath(path))
            return reply.fail(code(E::BadParams), u"The path is absolute"_s, {{u"param"_s, u"path"_s}});
        if (!path.endsWith(u".png"_s, Qt::CaseInsensitive))
            return reply.fail(code(E::BadParams), u"The path ends with .png"_s, {{u"param"_s, u"path"_s}});
        if (!QFileInfo(QFileInfo(path).absolutePath()).isDir())
            return reply.fail(code(E::BadParams), u"The folder %1 does not exist"_s.arg(QFileInfo(path).absolutePath()),
                              {{u"param"_s, u"path"_s}});
    }
    enqueueInput(reply, [this, path, reply](const std::function<void()> &finished) {
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
            if (runtime.isEmpty()) {
                reply.fail(code(E::BadParams), u"There is no private runtime folder ($XDG_RUNTIME_DIR): give a path"_s,
                           {{u"param"_s, u"path"_s}});
                finished();
                return;
            }
            const QString dir = QDir(runtime).filePath(u"brulion-screenshots"_s);
            QDir().mkpath(dir);
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            file = QDir(dir).filePath(
                    u"screenshot-%1.png"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss-zzz"_s)));
        }
        QQuickWindow *w = window();
        const QImage image = w ? w->grabWindow() : QImage();
        if (image.isNull()) {
            reply.fail(code(E::Failed), u"The window could not be captured"_s);
            finished();
            return;
        }
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || !image.save(&out, "PNG") || !out.commit()) {
            reply.fail(code(E::Failed), u"Could not write %1"_s.arg(file), {{u"path"_s, file}});
            finished();
            return;
        }
        reply.ok(QJsonObject{{u"path"_s, file},
                             {u"width"_s, image.width()},
                             {u"height"_s, image.height()},
                             {u"devicePixelRatio"_s, image.devicePixelRatio()}});
        finished();
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
        for (auto it = m_items.begin(); it != m_items.end();) {
            it = it.value() ? std::next(it) : m_items.erase(it);
        }
        for (auto it = m_handles.begin(); it != m_handles.end();) {
            it = m_items.contains(it.value()) ? std::next(it) : m_handles.erase(it);
        }
    }
    const quint64 id = m_nextHandle++;
    m_items.insert(id, item);
    m_handles.insert(item, id);
    return u"h%1"_s.arg(id);
}

QQuickItem *RemoteControl::itemFor(const QString &handle, QString *errorCode, QString *message)
{
    bool ok = false;
    const quint64 id = handle.startsWith(u'h') ? handle.mid(1).toULongLong(&ok) : 0;
    if (!ok || !m_items.contains(id)) {
        *errorCode = code(E::NotFound);
        *message = u"Unknown handle %1 (handles come from find and state)"_s.arg(handle);
        return nullptr;
    }
    QQuickItem *item = m_items.value(id);
    if (!item) {
        *errorCode = code(E::StaleHandle);
        *message = u"The item of %1 is gone: find it again"_s.arg(handle);
        return nullptr;
    }
    return item;
}

QJsonObject RemoteControl::describe(QQuickItem *item)
{
    QJsonObject o{{u"handle"_s, handleFor(item)},
                  {u"type"_s, uidriver::typeName(item)},
                  {u"objectName"_s, item->objectName()},
                  {u"text"_s, shownText(item).left(2000)},
                  {u"geometry"_s, rect(item->mapRectToScene(QRectF(0, 0, item->width(), item->height())))},
                  {u"visible"_s, uidriver::shown(item)},
                  {u"enabled"_s, item->isEnabled()},
                  {u"focus"_s, item->hasActiveFocus()}};
    if (QQuickWindow *w = item->window())
        o.insert(u"onScreen"_s, uidriver::onScreen(w, item));
    const QVariant checked = item->property("checked");
    if (checked.metaType() == QMetaType::fromType<bool>())
        o.insert(u"checked"_s, checked.toBool());
    if (secretField(item))
        o.insert(u"secret"_s, true); // a password field: its text stays in the app
    return o;
}

QList<QQuickItem *> RemoteControl::select(QQuickWindow *window, const QJsonObject &selector, bool shownOnly,
                                          QQuickItem *within)
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
        QString errorCode, message;
        within = itemFor(params.value(u"within"_s).toString(), &errorCode, &message);
        if (!within)
            return reply.fail(errorCode, message, {{u"param"_s, u"within"_s}});
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
        QString errorCode, message;
        QQuickItem *item = itemFor(object.value(u"handle"_s).toString(), &errorCode, &message);
        if (!item) {
            reply.fail(errorCode, prefix + message);
            return false;
        }
        if (!uidriver::shown(item)) {
            reply.fail(code(E::NotFound), prefix + u"The item is not shown"_s, {{u"item"_s, describe(item)}});
            return false;
        }
        target->item = item;
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
            QJsonObject selector;
            for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s, u"exact"_s}) {
                if (object.contains(key))
                    selector.insert(key, object.value(key));
            }
            reply.fail(code(E::NotFound), prefix + u"No shown item matches"_s, {{u"selector"_s, selector}});
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

ContentPositions contentPositions(QQuickWindow *window, QPointF scenePos)
{
    ContentPositions out;
    for (QQuickItem *item : uidriver::allItems(window->contentItem())) {
        if (!item->inherits("QQuickFlickable"))
            continue;
        if (item->mapRectToScene(QRectF(0, 0, item->width(), item->height())).contains(scenePos))
            out.append({item, QPointF(item->property("contentX").toReal(), item->property("contentY").toReal())});
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
        const QPointF moved =
                QPointF(flickable->property("contentX").toReal(), flickable->property("contentY").toReal()) - was;
        if (std::hypot(moved.x(), moved.y()) > std::hypot(most.x(), most.y()))
            most = moved;
    }
    return {{u"dx"_s, std::round(most.x() * 10) / 10}, {u"dy"_s, std::round(most.y() * 10) / 10}};
}

} // namespace

// ---- Input ------------------------------------------------------------------------------------

void RemoteControl::enqueueInput(const ipc::ControlReply &reply, std::function<void(std::function<void()>)> job)
{
    m_inputQueue.append({reply, std::move(job)});
    if (!m_inputRunning)
        nextInput();
}

void RemoteControl::nextInput()
{
    // Requests whose client has gone (a command line that gave up waiting) are dropped: nobody
    // wants that input any more.
    while (!m_inputQueue.isEmpty() && !m_inputQueue.first().reply.connected()) {
        qCDebug(lcApp) << "Remote control: dropped the input of a client that left";
        m_inputQueue.removeFirst();
    }
    if (m_inputQueue.isEmpty()) {
        m_inputRunning = false;
        return;
    }
    m_inputRunning = true;
    const auto job = m_inputQueue.takeFirst().job;
    auto once = std::make_shared<bool>(false);
    job([this, once] {
        if (*once)
            return;
        *once = true;
        QTimer::singleShot(0, this, [this] { nextInput(); });
    });
}

void RemoteControl::afterSettled(int quietMs, int maxMs, std::function<void()> then)
{
    auto *timer = new QTimer(this);
    timer->setInterval(20);
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
    const bool touchDefault = method != u"scroll" && QCoreApplication::arguments().contains(u"--touch"_s);
    const bool touch = params.contains(u"device"_s) ? params.value(u"device"_s).toString() == u"touch" : touchDefault;
    const Qt::MouseButton button = ipc::parseMouseButton(params.value(u"button"_s).toString()).value_or(Qt::LeftButton);
    const Qt::KeyboardModifiers modifiers = ipc::parseModifiers(params.value(u"modifiers"_s)).value_or(Qt::NoModifier);
    const int holdMs = params.value(u"ms"_s).toInt(800);
    const QPointF delta(params.value(u"dx"_s).toDouble(), params.value(u"dy"_s).toDouble());
    enqueueInput(reply, [=, this](const std::function<void()> &finished) {
        QQuickWindow *w = window();
        if (!w) {
            reply.fail(code(E::NotReady), u"The window is gone"_s);
            finished();
            return;
        }
        prepareInput(w);
        afterSettled(100, 2000, [=, this] {
            QQuickWindow *settled = window();
            if (!settled) {
                reply.fail(code(E::NotReady), u"The window is gone"_s);
                finished();
                return;
            }
            reachTarget(settled, target, [=, this](QPointF point, const char *problem) {
                QQuickWindow *reached = window();
                if (!reached || (problem && qstrcmp(problem, E::StaleHandle) == 0)) {
                    reply.fail(code(E::StaleHandle), u"The item went away before it could be reached"_s);
                    finished();
                    return;
                }
                if (problem) {
                    reply.fail(code(E::OffScreen),
                               u"The item is outside the window, and scrolling cannot bring it in: nothing "
                               "was touched"_s,
                               {{u"item"_s, describe(target.item)},
                                {u"window"_s,
                                 QJsonObject{{u"width"_s, reached->width()}, {u"height"_s, reached->height()}}}});
                    finished();
                    return;
                }
                const QJsonObject where{{u"x"_s, std::round(point.x() * 10) / 10},
                                        {u"y"_s, std::round(point.y() * 10) / 10},
                                        {u"device"_s, touch ? u"touch"_s : u"mouse"_s}};
                ContentPositions contentBefore;
                // The Flickable a finger would drag: the innermost under it that can move that way.
                QQuickItem *dragged = nullptr;
                if (method == u"scroll") {
                    contentBefore = contentPositions(reached, point);
                    for (QQuickItem *flickable : uidriver::flickablesAt(reached, point)) {
                        if (!dragged && uidriver::canScroll(flickable, delta))
                            dragged = flickable;
                    }
                    // A finger moves content only where some Flickable under it can move that way;
                    // elsewhere a real one would press what it is on (a switch, a button) — not
                    // done: nothing is touched, and the answer says nothing scrolled.
                    if (touch && !dragged) {
                        QJsonObject out = where;
                        if (target.item)
                            out.insert(u"item"_s, describe(target.item));
                        out.insert(u"windowActive"_s, reached->isActive());
                        out.insert(u"scrolled"_s, QJsonObject{{u"dx"_s, 0}, {u"dy"_s, 0}});
                        out.insert(u"touched"_s, false);
                        reply.ok(out);
                        finished();
                        return;
                    }
                }
                uidriver::Gesture gesture;
                if (method == u"click") {
                    if (touch) {
                        gesture = uidriver::touchTap(reached, point, 60, 200);
                    } else {
                        uidriver::ClickTiming timing;
                        timing.afterMove = 20;
                        timing.afterPress = 60;
                        timing.afterRelease = 200;
                        gesture = uidriver::mouseClick(reached, point, button, modifiers, timing);
                    }
                } else if (method == u"longPress") {
                    gesture = touch ? uidriver::touchTap(reached, point, holdMs, 250)
                                    : uidriver::mouseLongPress(reached, point, holdMs, button);
                } else if (method == u"doubleClick") {
                    gesture = touch ? uidriver::touchDoubleTap(reached, point)
                                    : uidriver::mouseDoubleClick(reached, point, button, modifiers);
                } else { // scroll
                    if (touch) {
                        // The content follows the finger past the drag threshold (only where it
                        // can move: checked above).
                        gesture = uidriver::touchScroll(reached, point, delta, !dragged->property("moving").toBool());
                    } else {
                        gesture = uidriver::wheel(reached, point, delta, modifiers);
                    }
                }
                uidriver::play(this, reached, gesture, [=, this](bool completed) {
                    if (!completed) {
                        reply.fail(code(E::Failed), u"The window went away"_s);
                        finished();
                        return;
                    }
                    afterSettled(100, 2000, [=, this] {
                        QJsonObject out = where;
                        if (target.item)
                            out.insert(u"item"_s, describe(target.item));
                        if (QQuickWindow *after = window())
                            out.insert(u"windowActive"_s, after->isActive());
                        if (method == u"scroll") {
                            out.insert(u"scrolled"_s, scrolledSince(contentBefore));
                            if (touch)
                                out.insert(u"touched"_s, true);
                        }
                        reply.ok(out);
                        finished();
                    });
                });
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
    const bool touchDefault = QCoreApplication::arguments().contains(u"--touch"_s);
    const bool touch = params.contains(u"device"_s) ? params.value(u"device"_s).toString() == u"touch" : touchDefault;
    const Qt::MouseButton button = ipc::parseMouseButton(params.value(u"button"_s).toString()).value_or(Qt::LeftButton);
    const Qt::KeyboardModifiers modifiers = ipc::parseModifiers(params.value(u"modifiers"_s)).value_or(Qt::NoModifier);
    const int holdMs = params.value(u"holdMs"_s).toInt(0);
    enqueueInput(reply, [=, this](const std::function<void()> &finished) {
        QQuickWindow *w = window();
        if (!w) {
            reply.fail(code(E::NotReady), u"The window is gone"_s);
            finished();
            return;
        }
        prepareInput(w);
        afterSettled(100, 2000, [=, this] {
            QQuickWindow *settled = window();
            if (!settled) {
                reply.fail(code(E::NotReady), u"The window is gone"_s);
                finished();
                return;
            }
            reachTarget(settled, from, [=, this](QPointF start, const char *problem) {
                QQuickWindow *reached = window();
                const QPointF end = to.isItem ? (to.item ? uidriver::pointIn(to.item) : QPointF(-1, -1)) : to.point;
                if (!reached || (problem && qstrcmp(problem, E::StaleHandle) == 0) || end.x() < 0) {
                    reply.fail(code(E::StaleHandle), u"An item went away before the drag"_s);
                    finished();
                    return;
                }
                // Both ends on screen (the start was scrolled into view; scrolling to the end
                // now would move the start away).
                if (problem || (to.isItem && !uidriver::onScreen(reached, to.item))) {
                    QQuickItem *off = problem ? from.item.data() : to.item.data();
                    reply.fail(code(E::OffScreen),
                               u"The %1 item is outside the window, and scrolling cannot bring it in: nothing was "
                               "touched"_s.arg(problem ? u"from"_s : u"to"_s),
                               {{u"param"_s, problem ? u"from"_s : u"to"_s}, {u"item"_s, describe(off)}});
                    finished();
                    return;
                }
                const uidriver::Gesture gesture =
                        touch ? uidriver::touchDrag(reached, start, end - start, 16, 16, holdMs, 300)
                              : uidriver::mouseDrag(reached, start, end, holdMs, false, button, modifiers);
                uidriver::play(this, reached, gesture, [=, this](bool completed) {
                    if (!completed) {
                        reply.fail(code(E::Failed), u"The window went away"_s);
                        finished();
                        return;
                    }
                    afterSettled(100, 2000, [=] {
                        reply.ok(QJsonObject{{u"from"_s, QJsonObject{{u"x"_s, start.x()}, {u"y"_s, start.y()}}},
                                             {u"to"_s, QJsonObject{{u"x"_s, end.x()}, {u"y"_s, end.y()}}},
                                             {u"device"_s, touch ? u"touch"_s : u"mouse"_s}});
                        finished();
                    });
                });
            });
        });
    });
}

void RemoteControl::typeText(const QJsonObject &params, const ipc::ControlReply &reply)
{
    const QString text = params.value(u"text"_s).toString();
    enqueueInput(reply, [=, this](const std::function<void()> &finished) {
        QQuickWindow *w = window();
        if (!w) {
            reply.fail(code(E::NotReady), u"The window is not there yet"_s);
            finished();
            return;
        }
        prepareInput(w);
        // A popup that just opened gives its field the keyboard when its transition is over.
        afterSettled(100, 2000, [=, this] {
            QQuickWindow *settled = window();
            if (!settled) {
                reply.fail(code(E::NotReady), u"The window is gone"_s);
                finished();
                return;
            }
            const QList<uidriver::KeyStroke> strokes = uidriver::strokesFor(text);
            uidriver::Gesture gesture = uidriver::keyStrokes(settled, strokes, uidriver::KeyDelivery::System, 8);
            // Typing stops when the client leaves (a command line interrupted).
            uidriver::play(
                    this, settled, gesture,
                    [=, this](bool completed) {
                        if (!completed) {
                            reply.fail(code(E::Failed), u"The window went away"_s);
                            finished();
                            return;
                        }
                        afterSettled(100, 2000, [=, this] {
                            QQuickWindow *after = window();
                            QQuickItem *focus = after ? after->activeFocusItem() : nullptr;
                            reply.ok(QJsonObject{
                                    {u"keys"_s, strokes.size()},
                                    {u"focusedItem"_s, focus ? QJsonValue(describe(focus)) : QJsonValue()}});
                            finished();
                        });
                    },
                    [reply] { return reply.connected(); });
        });
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
    enqueueInput(reply, [=, this](const std::function<void()> &finished) {
        QQuickWindow *w = window();
        if (!w) {
            reply.fail(code(E::NotReady), u"The window is not there yet"_s);
            finished();
            return;
        }
        prepareInput(w);
        afterSettled(100, 2000, [=, this] {
            QQuickWindow *settled = window();
            if (!settled) {
                reply.fail(code(E::NotReady), u"The window is gone"_s);
                finished();
                return;
            }
            auto handled = std::make_shared<QList<bool>>();
            uidriver::Gesture gesture;
            for (const uidriver::KeyStroke &stroke : strokes) {
                gesture.append(uidriver::Step{[settled, stroke, handled] {
                                                  handled->append(uidriver::deliverKey(settled, stroke,
                                                                                       uidriver::KeyDelivery::System));
                                              },
                                              30, true});
            }
            uidriver::play(
                    this, settled, gesture,
                    [=, this](bool completed) {
                        if (!completed) {
                            reply.fail(code(E::Failed), u"The window went away"_s);
                            finished();
                            return;
                        }
                        afterSettled(100, 2000, [=, this] {
                            QJsonArray list;
                            for (const bool h : std::as_const(*handled))
                                list.append(h);
                            QQuickWindow *after = window();
                            QQuickItem *focus = after ? after->activeFocusItem() : nullptr;
                            reply.ok(QJsonObject{
                                    {u"keys"_s, strokes.size()},
                                    {u"handled"_s, list},
                                    {u"focusedItem"_s, focus ? QJsonValue(describe(focus)) : QJsonValue()}});
                            finished();
                        });
                    },
                    [reply] { return reply.connected(); });
        });
    });
}

// ---- waitFor ----------------------------------------------------------------------------------

void RemoteControl::waitFor(const QJsonObject &params, const ipc::ControlReply &reply)
{
    auto wait = std::make_shared<Wait>();
    wait->route = params.value(u"route"_s);
    for (const QString &key : {u"text"_s, u"objectName"_s, u"type"_s, u"exact"_s}) {
        if (params.contains(key))
            wait->selector.insert(key, params.value(key));
    }
    wait->handle = params.value(u"handle"_s).toString();
    wait->idle = params.value(u"idle"_s).toBool();
    wait->gone = params.value(u"gone"_s).toBool();
    wait->ms = params.value(u"ms"_s).toInt(5000);
    wait->reply = reply;
    wait->since.start();
    if (!wait->handle.isEmpty()) {
        // An unknown handle never shows up; a gone item only satisfies `gone`.
        QString errorCode, message;
        if (!itemFor(wait->handle, &errorCode, &message)
            && (errorCode == code(E::NotFound) || (errorCode == code(E::StaleHandle) && !wait->gone)))
            return reply.fail(errorCode, message, {{u"param"_s, u"handle"_s}});
    }
    if (satisfied(*wait)) {
        reply.ok(waitAnswer(*wait));
        return;
    }
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
            QString errorCode, message;
            item = itemFor(wait.handle, &errorCode, &message);
        }
        if (item)
            out.insert(u"item"_s, describe(item));
    }
    return out;
}

bool RemoteControl::satisfied(const Wait &wait)
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
        QString errorCode, message;
        QQuickItem *item = itemFor(wait.handle, &errorCode, &message);
        const bool there = item && uidriver::shown(item);
        if (there == wait.gone)
            return false;
    }
    if (wait.idle && !idle())
        return false;
    return true;
}

QList<QObject *> RemoteControl::pageControllers()
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

QJsonArray RemoteControl::conflicts()
{
    QStringList pages;
    for (const QObject *controller : pageControllers()) {
        const QString page = controller->property("path").toString();
        if (controller->property("conflict").toBool() && !pages.contains(page))
            pages.append(page);
    }
    QJsonArray out;
    for (const QString &page : std::as_const(pages))
        out.append(QJsonObject{{u"page"_s, page}});
    return out;
}

bool RemoteControl::idle()
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
    for (const QObject *controller : pageControllers()) {
        if (controller->property("dirty").toBool() || controller->property("conflict").toBool())
            return false;
    }
    return true;
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

} // namespace brulion::shell
