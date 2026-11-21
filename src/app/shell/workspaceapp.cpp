// WorkspaceExtensions' part of b.app (docs/EXTENSIONS.md "The app (`b.app`)", "Live code"): the
// items the extensions put into the UI and their themes as the shell shows them, the "route"
// hooks, and live code — extensions a live client (an AI agent over the remote control) loads for
// this run only, and its evaluations in the console's environment.
#include "workspaceextensions.h"

#include "extensions/builtin.h"
#include "extensions/pageextension.h"
#include "log/categories.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>

#include <optional>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;

namespace {
const QString kConsoleId = u"*console*"_s; // workspaceextensions.cpp's kConsole
}

void WorkspaceExtensions::scheduleAppChanged()
{
    // Inside a call (b.app.contribute in a hook): the shell redraws after it, never within it.
    if (m_appChangePending)
        return;
    m_appChangePending = true;
    QTimer::singleShot(0, this, [this] {
        m_appChangePending = false;
        ++m_appRevision;
        Q_EMIT appRevisionChanged();
    });
}

QVariantList WorkspaceExtensions::contributions(const QString &slot, int revision) const
{
    Q_UNUSED(revision);
    QVariantList out;
    if (!m_engine)
        return out;
    for (const extensions::Contribution &c : m_engine->contributions(slot)) {
        QVariantMap row = extensions::contributionTable(c);
        // Every key there for QML (compiled QML reads a missing one as undefined).
        for (const QString &key :
             {u"icon"_s, u"tooltip"_s, u"tone"_s, u"command"_s, u"view"_s, u"page"_s, u"run"_s, u"insert"_s})
            if (!row.contains(key))
                row.insert(key, QString());
        if (!row.contains(u"params"_s))
            row.insert(u"params"_s, QVariantMap());
        row.insert(u"title"_s, title(c.extension));
        row.insert(u"key"_s, QString(c.extension + u'/' + c.id));
        row.insert(u"live"_s, m_live.contains(c.extension));
        if (row.value(u"tooltip"_s).toString().isEmpty())
            row.insert(u"tooltip"_s, c.label);
        out << row;
    }
    return out;
}

QVariantMap WorkspaceExtensions::contribution(const QString &slot, const QString &extension, const QString &id) const
{
    for (const QVariant &v : contributions(slot)) {
        const QVariantMap row = v.toMap();
        if (row.value(u"extension"_s).toString() == extension && row.value(u"id"_s).toString() == id)
            return row;
    }
    return {};
}

QString WorkspaceExtensions::contributionsKey(const QString &slot, int revision) const
{
    return QString::fromUtf8(
            QJsonDocument(QJsonArray::fromVariantList(contributions(slot, revision))).toJson(QJsonDocument::Compact));
}

QList<extensions::ThemeInfo> WorkspaceExtensions::themes() const
{
    return m_engine ? m_engine->themes() : QList<extensions::ThemeInfo>();
}

void WorkspaceExtensions::routeChanged(const QVariantMap &route)
{
    // A burst (a route replaced while a view settles, back and forth) is one event, once it settled.
    const QString view = route.value(u"view"_s).toString();
    if (view.isEmpty())
        return;
    QVariantMap event{{u"view"_s, view}, {u"previous"_s, m_lastRoute.value(u"view"_s).toString()}};
    const QString path = route.value(u"path"_s).toString();
    if (!path.isEmpty()) {
        event.insert(u"path"_s, path);
        event.insert(u"page"_s, noteParams(path).value(u"page"_s));
    }
    const QString date = route.value(u"date"_s).toString();
    if (!date.isEmpty())
        event.insert(u"date"_s, date);
    if (view == u"extension") {
        event.insert(u"extension"_s, route.value(u"extension"_s).toString());
        event.insert(u"name"_s, route.value(u"name"_s).toString());
    }
    m_lastRoute = route;
    m_routeEvent = event;
    if (!m_routeTimer) {
        m_routeTimer = new QTimer(this);
        m_routeTimer->setSingleShot(true);
        m_routeTimer->setInterval(150ms);
        connect(m_routeTimer, &QTimer::timeout, this, &WorkspaceExtensions::runRouteHooks);
    }
    m_routeTimer->start();
}

void WorkspaceExtensions::runRouteHooks()
{
    if (!m_engine || m_engine->hooks(u"route"_s).isEmpty() || m_routeEvent.isEmpty())
        return;
    const QVariantMap event = m_routeEvent;
    m_routeEvent.clear();
    runHooks(u"route"_s, event);
}

// ---- live code ------------------------------------------------------------------------------------

QVariantMap WorkspaceExtensions::loadLive(const QString &id, const QString &source, const QStringList &permissions,
                                          const QString &title, const QString &who)
{
    static const QRegularExpression idPattern(u"^[a-z0-9][a-z0-9_-]{0,63}$"_s);
    if (!idPattern.match(id).hasMatch())
        return {{u"ok"_s, false},
                {u"error"_s, u"an id is lower-case letters, digits, - and _ (at most 64), as a page's id"_s}};
    if (!m_vault || !m_index)
        return {{u"ok"_s, false}, {u"error"_s, u"no notebook is open"_s}};
    QStringList unknown;
    const QStringList canonical = extensions::permissionsFrom(permissions, &unknown);
    if (!unknown.isEmpty())
        return {{u"ok"_s, false}, {u"error"_s, QString(u"unknown permission: "_s + unknown.join(u", "_s))}};
    const bool replaced = m_live.contains(id);
    const std::optional<LiveEntry> previous = replaced ? std::optional<LiveEntry>(m_live.value(id)) : std::nullopt;
    m_live.insert(id, LiveEntry{source, canonical, title.trimmed(), who, QString(), -1});
    m_lastGood.remove(id); // a failing live version never falls back to another client's code
    reconcile();
    const LiveEntry &entry = m_live[id];
    const QString name = entry.title.isEmpty() ? id : entry.title;
    QVariantMap out{{u"ok"_s, entry.error.isEmpty()}, {u"id"_s, id}, {u"replaced"_s, replaced}};
    if (!entry.error.isEmpty()) {
        out.insert(u"error"_s, entry.error);
        if (entry.errorLine >= 0)
            out.insert(u"line"_s, entry.errorLine + 1);
        // Nothing of it runs: the live version before it (if any) runs again, else the page's.
        if (previous)
            m_live.insert(id, *previous);
        else
            m_live.remove(id);
        reconcile();
        m_console->append(u"error"_s,
                          tr("%1 sent live code that failed: %2").arg(who.mid(5), out.value(u"error"_s).toString()),
                          name);
        Q_EMIT liveChanged();
        return out;
    }
    QVariantList commands, views, contributions;
    QStringList hooks;
    if (m_engine) {
        for (const extensions::CommandInfo &c : m_engine->commands())
            if (c.extension == id)
                commands << QVariantMap{{u"name"_s, c.name}, {u"label"_s, c.label}};
        for (const extensions::ViewInfo &v : m_engine->views())
            if (v.extension == id)
                views << QVariantMap{{u"name"_s, v.name}, {u"label"_s, v.label}, {u"panel"_s, v.panel}};
        for (const extensions::HookInfo &h : m_engine->hooks())
            if (h.extension == id)
                hooks << h.event;
        for (const extensions::Contribution &c : m_engine->contributions())
            if (c.extension == id)
                contributions << extensions::contributionTable(c);
        for (const extensions::ThemeInfo &t : m_engine->themes())
            if (t.extension == id)
                out.insert(u"theme"_s, extensions::themeLayerTable(t.layer));
    }
    out.insert(u"commands"_s, commands);
    out.insert(u"views"_s, views);
    out.insert(u"hooks"_s, hooks);
    out.insert(u"contributions"_s, contributions);
    out.insert(u"permissions"_s, canonical);
    m_console->append(u"info"_s,
                      replaced ? tr("%1 loaded new live code.").arg(who.mid(5))
                               : tr("%1 loaded live code.").arg(who.mid(5)),
                      name);
    qCInfo(lcApp, "extensions: live code %s from %s", qUtf8Printable(id), qUtf8Printable(who));
    Q_EMIT liveChanged();
    return out;
}

bool WorkspaceExtensions::unloadLive(const QString &id)
{
    const auto it = m_live.constFind(id);
    if (it == m_live.constEnd())
        return false;
    const QString name = it->title.isEmpty() ? id : it->title;
    m_live.erase(it);
    m_lastGood.remove(id);
    reconcile();
    m_console->append(u"info"_s, tr("Live code taken out."), name);
    Q_EMIT liveChanged();
    return true;
}

QVariantList WorkspaceExtensions::liveExtensions() const
{
    QStringList ids = m_live.keys();
    ids.sort();
    QVariantList out;
    for (const QString &id : std::as_const(ids)) {
        const LiveEntry &l = m_live[id];
        QVariantMap row{{u"id"_s, id},
                        {u"title"_s, l.title.isEmpty() ? id : l.title},
                        {u"who"_s, l.who},
                        {u"permissions"_s, l.permissions},
                        {u"state"_s, l.error.isEmpty() ? u"running"_s : u"error"_s}};
        if (!l.error.isEmpty()) {
            row.insert(u"error"_s, l.error);
            if (l.errorLine >= 0)
                row.insert(u"line"_s, l.errorLine + 1);
        }
        out << row;
    }
    return out;
}

QString WorkspaceExtensions::liveOwner(const QString &extension) const
{
    const auto it = m_live.constFind(extension);
    return it == m_live.constEnd() ? QString() : it->who;
}

QVariantMap WorkspaceExtensions::evaluateFor(const QString &code, const QString &extension)
{
    if (code.trimmed().isEmpty())
        return {{u"ok"_s, false}, {u"error"_s, u"no code"_s}};
    if (!ensureConsole())
        return {{u"ok"_s, false}, {u"error"_s, u"no notebook is open"_s}};
    const QString env = extension.isEmpty() ? kConsoleId : extension;
    if (!m_engine->isLoaded(env))
        return {{u"ok"_s, false}, {u"error"_s, u"no extension “%1” runs"_s.arg(extension)}};
    if (m_engine->isRunning())
        return {{u"ok"_s, false}, {u"error"_s, u"an extension's call runs: try again"_s}};
    m_console->append(u"input"_s, code.trimmed(), extension.isEmpty() ? tr("live") : title(extension));
    QStringList printed;
    m_printCapture = &printed;
    m_current = CallContext{};
    m_current.console = true;
    extensions::Result r = m_engine->evaluate(env, code, u"live"_s);
    m_current = CallContext{};
    m_printCapture = nullptr;
    if (r.status == extensions::Status::Suspended) {
        m_engine->cancel(r.call);
        r = {extensions::Status::Error,
             {},
             u"live:1: the code waits for a question or a model; a live evaluation "
             "cannot (run a command that does instead)"_s};
    }
    QVariantMap out{{u"ok"_s, r.ok()}, {u"printed"_s, printed}};
    if (r.ok()) {
        if (r.value.isValid() && !r.value.isNull())
            out.insert(u"value"_s, r.value);
        m_console->append(u"result"_s, r.value.isValid() ? r.value.toString() : QString());
    } else {
        const QString message =
                r.error.isEmpty()
                        ? (r.status == extensions::Status::Timeout ? u"the code took too long"_s : u"the code failed"_s)
                        : r.error;
        out.insert(u"error"_s, message.section(u'\n', 0, 0));
        if (message.contains(u'\n'))
            out.insert(u"traceback"_s, message);
        const int line = extensions::errorLine(u"live"_s, message);
        if (line >= 0)
            out.insert(u"line"_s, line + 1);
        m_console->append(u"error"_s, message.section(u'\n', 0, 0));
    }
    // It may have registered commands, views, hooks or UI items (b.command in the code).
    refreshList();
    Q_EMIT commandsChanged();
    Q_EMIT changed();
    bumpRevision();
    return out;
}

} // namespace brulion::shell
