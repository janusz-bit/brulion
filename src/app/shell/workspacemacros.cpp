// WorkspaceExtensions' macros, template variables and panels (docs/EXTENSIONS.md "Macros",
// "Panels", "View blocks"; docs/TEMPLATES.md "Variables of extensions"): what a macro shows comes
// from the cache (extensions/macros.h) — rendering never runs Luau —, the stale macros asked for
// are computed after the frame that asked, a slice of time at a time, each within its own short
// time limit and every note's together within a budget per round of changes, and the editors are
// told when what one shows changed (macrosChanged()).
#include "workspaceextensions.h"

#include "extensions/builtin.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "markdown/journal.h"
#include "text/text.h"
#include "util/paths.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;

namespace {

/// Macros asked for within this time are kept and computed again when stale; older ones go.
constexpr qint64 kMacroKeepMs = 10 * 60 * 1000;
/// One slice of macro calls between two frames (a call has MacroTimeoutMs at most).
constexpr qint64 kMacroSliceMs = 25;
/// A template variable is filled once, when the template is: more time than a macro's.
constexpr int kVariableTimeoutMs = 1000;

} // namespace

markdown::MacroView WorkspaceExtensions::macroView(const markdown::MacroSpan &macro, const QString &path)
{
    using State = markdown::MacroView::State;
    markdown::MacroView view;
    const QString id = macro.extension;
    if (!m_engine || !m_engine->isLoaded(id)) {
        // Why it does not run: its page's state, a built-in switched off, or no such extension.
        view.state = State::Unknown;
        for (const PageEntry &e : std::as_const(m_pages)) {
            if (e.runId != id || e.state == u"duplicate")
                continue;
            if (e.state == u"approval")
                view.state = State::Unapproved;
            else if (e.state == u"off" || e.state == u"builtinOff")
                view.state = State::Off;
            else if (e.state == u"error" || e.state == u"problem") {
                view.state = State::Error;
                view.message = e.message;
            } else if (e.state == u"running")
                view.state = State::Pending; // loads in a moment
            break;
        }
        if (view.state == State::Unknown && !extensions::builtinExtension(id).id.isEmpty())
            view.state = m_enabled.contains(id) ? State::Pending : State::Off;
        return view;
    }
    if (!m_macroNames.contains(id + u'/' + macro.name)) {
        view.state = State::Unknown;
        return view;
    }
    const extensions::MacroRequest request{id, macro.name, macro.arguments, path};
    const QString key = extensions::MacroCache::keyOf(request);
    if (!m_macros.ask(key, request, m_macroClock.elapsed())) {
        view.state = State::TooMany;
        view.message = tr("more than %1 macros on this page").arg(extensions::MaxMacrosPerPage);
        return view;
    }
    if (m_macros.needsRun(key))
        scheduleMacros();
    if (const auto known = m_macros.view(key))
        return *known; // also while stale: its new value replaces it when computed
    view.state = State::Pending;
    return view;
}

void WorkspaceExtensions::scheduleMacros()
{
    if (!m_macroTimer.isActive())
        m_macroTimer.start(0);
}

QVariantMap WorkspaceExtensions::macroContext(const QString &arguments, const QString &path) const
{
    QVariantList args;
    for (const QString &arg : markdown::macroArguments(arguments))
        args << arg;
    QVariantMap ctx{{u"args"_s, args}, {u"text"_s, arguments}, {u"path"_s, path}};
    if (path.isEmpty())
        return ctx;
    const ParsedNote *note = m_index ? m_index->note(path) : nullptr;
    ctx.insert(u"page"_s, note && !note->title.isEmpty() ? note->title : paths::pageTitle(path));
    if (m_index) {
        if (const auto day = markdown::journalDateFromPath(path, m_index->config()))
            ctx.insert(u"date"_s, day->toString(Qt::ISODate));
    }
    return ctx;
}

void WorkspaceExtensions::runMacros()
{
    if (!m_engine)
        return;
    // Never inside another call, nor while a question waits (as views: after the answer).
    if (m_engine->isRunning() || isWaiting()) {
        m_macroTimer.start(200);
        return;
    }
    const qint64 now = m_macroClock.elapsed();
    const QStringList keys = m_macros.pending(now - kMacroKeepMs);
    const bool comma = m_language == u"pl";
    QElapsedTimer slice;
    slice.start();
    for (const QString &key : keys) {
        if (slice.elapsed() >= kMacroSliceMs) {
            m_macroTimer.start(16); // the rest after the next frame
            return;
        }
        const std::optional<extensions::MacroRequest> request = m_macros.request(key);
        if (!request || !m_engine->isLoaded(request->extension))
            continue;
        markdown::MacroView view;
        extensions::Reads reads;
        bool failed = false;
        qint64 &spent = m_macroSpentMs[request->path];
        if (spent >= extensions::MacroBudgetMs) {
            // The note's macros had their time in this round: the rest wait for a change.
            view.state = markdown::MacroView::State::TooMany;
            view.message = tr("the page's macros took too long");
            reads.everything = true;
            failed = true;
        } else {
            QElapsedTimer clock;
            clock.start();
            m_engine->startRecording();
            const extensions::Result r = m_engine->runMacro(request->extension, request->name,
                                                            macroContext(request->arguments, request->path),
                                                            extensions::MacroTimeoutMs);
            reads = m_engine->stopRecording();
            spent += clock.elapsed();
            if (r.ok()) {
                view = extensions::macroViewFrom(r.value, comma);
                failed = view.state != markdown::MacroView::State::Ok;
            } else {
                failed = true;
                view.state = markdown::MacroView::State::Error;
                view.message = r.error.section(u'\n', 0, 0);
                if (r.status == extensions::Status::Timeout) {
                    m_macros.countTimeout(key);
                    view.message = tr("took longer than %1 ms").arg(extensions::MacroTimeoutMs);
                    reads.everything = true;
                }
            }
        }
        const bool changed = m_macros.store(key, view, reads, failed, now);
        if (changed && view.state == markdown::MacroView::State::Error) {
            const QString macro = markdown::macroSource(request->extension, request->name, request->arguments);
            m_console->append(u"error"_s, macro + u": "_s + view.message, title(request->extension));
            qCInfo(lcApp, "macro %s failed: %s", qUtf8Printable(macro), qUtf8Printable(view.message));
        }
        if (changed)
            noteMacrosChanged();
    }
    // The round is over: the notes' budgets start again, macros nobody looked at lately go.
    m_macroSpentMs.clear();
    m_macros.prune(now - kMacroKeepMs);
}

void WorkspaceExtensions::noteMacrosChanged()
{
    if (!m_macrosChangedTimer.isActive())
        m_macrosChangedTimer.start();
}

void WorkspaceExtensions::macrosReset()
{
    // Only when what runs changed (a page approved, switched, loaded again, an error): every
    // reconcile() of a scan would compute every macro again otherwise.
    QStringList signature;
    for (auto it = m_loadedKeys.cbegin(); it != m_loadedKeys.cend(); ++it)
        signature << it.key() + u'=' + it.value() + (m_loadErrors.contains(it.key()) ? u"!"_s : QString());
    for (const PageEntry &e : std::as_const(m_pages))
        signature << e.runId + u':' + e.state;
    signature.sort();
    const QString joined = signature.join(u'\n');
    if (joined == m_macroSignature)
        return;
    m_macroSignature = joined;
    m_macroNames.clear();
    if (m_engine) {
        for (const extensions::MacroInfo &m : m_engine->macros())
            m_macroNames.insert(m.extension + u'/' + m.name);
    }
    m_macros.markAllStale();
    m_macroSpentMs.clear();
    noteMacrosChanged(); // placeholders become values, values placeholders
    scheduleMacros();
}

QVariantList WorkspaceExtensions::macroSuggestions(const QString &filter, int limit) const
{
    QVariantList out;
    if (!m_engine)
        return out;
    const QString wanted = text::foldKey(filter).simplified();
    QList<std::pair<int, QVariantMap>> found;
    for (const extensions::MacroInfo &m : m_engine->macros()) {
        const QString name = m.extension + u'/' + m.name;
        const QString folded = text::foldKey(name);
        int rank = 0;
        if (!wanted.isEmpty()) {
            if (text::foldKey(m.name).startsWith(wanted) || folded.startsWith(wanted))
                rank = 0;
            else if (folded.contains(wanted))
                rank = 1;
            else if (text::foldKey(m.description).contains(wanted) || text::foldKey(title(m.extension)).contains(wanted))
                rank = 2;
            else
                continue;
        }
        const QString subtitle = m.description.isEmpty() ? title(m.extension) : m.description + u" · "_s + title(m.extension);
        found.append({rank,
                      QVariantMap{{u"title"_s, name},
                                  {u"subtitle"_s, subtitle},
                                  {u"insertText"_s, QString(u"ext:"_s + name + (m.example.isEmpty() ? QString() : u' ' + m.example))},
                                  {u"hint"_s, m.arguments.join(u", "_s)},
                                  {u"icon"_s, u"puzzle"_s}}});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &f : std::as_const(found)) {
        if (out.size() >= limit)
            break;
        out << f.second;
    }
    return out;
}

std::optional<QString> WorkspaceExtensions::templateVariable(const QString &extension, const QString &name,
                                                             const QString &arguments, const QVariantMap &context)
{
    if (!m_engine || !m_engine->isLoaded(extension) || m_engine->isRunning())
        return std::nullopt;
    QVariantMap ctx = macroContext(arguments, context.value(u"path"_s).toString());
    for (const QString &key : {u"page"_s, u"date"_s, u"template"_s}) {
        if (!context.value(key).toString().isEmpty())
            ctx.insert(key, context.value(key).toString());
    }
    const extensions::Result r = m_engine->runVariable(extension, name, ctx, kVariableTimeoutMs);
    const QString variable = u"{{"_s + extension + u'/' + name + (arguments.isEmpty() ? QString() : u' ' + arguments) + u"}}"_s;
    if (r.status == extensions::Status::NotFound)
        return std::nullopt;
    if (!r.ok()) {
        m_console->append(u"error"_s, variable + u": "_s + r.error.section(u'\n', 0, 0), title(extension));
        Q_EMIT message(tr("The template variable %1 failed: %2").arg(variable, r.error.section(u'\n', 0, 0)), u"error"_s);
        return std::nullopt;
    }
    const std::optional<QString> text = extensions::variableTextFrom(r.value, m_language == u"pl");
    if (!text) {
        const QString why = tr("a template variable returns a text");
        m_console->append(u"error"_s, variable + u": "_s + why, title(extension));
        Q_EMIT message(tr("The template variable %1 failed: %2").arg(variable, why), u"error"_s);
    }
    return text;
}

QVariantMap WorkspaceExtensions::macroExtension(const QString &extension) const
{
    QVariantMap out{{u"state"_s, u"unknown"_s}, {u"title"_s, extension}, {u"path"_s, QString()},
                    {u"line"_s, -1},          {u"message"_s, QString()}, {u"builtin"_s, false}};
    for (const PageEntry &e : std::as_const(m_pages)) {
        if (e.runId != extension || e.state == u"duplicate")
            continue;
        out.insert(u"title"_s, e.page.title);
        out.insert(u"path"_s, e.page.path);
        out.insert(u"line"_s, e.errorLine);
        out.insert(u"message"_s, e.message);
        out.insert(u"state"_s, e.state == u"approval"                      ? u"approval"_s
                               : e.state == u"off" || e.state == u"builtinOff" ? u"off"_s
                               : e.state == u"error" || e.state == u"problem"  ? u"error"_s
                                                                               : u"running"_s);
        return out;
    }
    if (!extensions::builtinExtension(extension).id.isEmpty()) {
        out.insert(u"title"_s, title(extension));
        out.insert(u"builtin"_s, true);
        out.insert(u"state"_s, m_enabled.contains(extension) ? u"running"_s : u"off"_s);
    }
    return out;
}

QVariantMap WorkspaceExtensions::noteParams(const QString &path) const
{
    QVariantMap params = macroContext(QString(), path);
    params.remove(u"args"_s);
    params.remove(u"text"_s);
    return params;
}

void WorkspaceExtensions::showMacro(const QString &extension, const QString &name, const QString &arguments)
{
    Q_EMIT macroHelpRequested(extension, markdown::macroSource(extension, name, arguments));
}

void WorkspaceExtensions::showView(const QString &extension, const QString &view)
{
    Q_EMIT macroHelpRequested(extension, u"```view "_s + extension + u'/' + view);
}

QVariantList WorkspaceExtensions::panels(const QString &kind) const
{
    QList<std::pair<QString, QVariantMap>> found;
    if (m_engine) {
        for (const extensions::ViewInfo &v : m_engine->views()) {
            if (v.panel.isEmpty() || (v.panel != u"all" && v.panel != kind))
                continue;
            const QString shown = v.label.isEmpty() ? title(v.extension) : v.label;
            found.append({shown.toLower(), QVariantMap{{u"extension"_s, v.extension},
                                                       {u"view"_s, v.name},
                                                       {u"title"_s, shown},
                                                       {u"icon"_s, v.icon.isEmpty() ? u"puzzle"_s : v.icon}}});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QVariantList out;
    for (const auto &f : std::as_const(found))
        out << f.second;
    return out;
}

QVariantList WorkspaceExtensions::embeddableViews() const
{
    QList<std::pair<QString, QVariantMap>> found;
    if (m_engine) {
        for (const extensions::ViewInfo &v : m_engine->views()) {
            if (v.label.isEmpty())
                continue;
            found.append({v.label.toLower(), QVariantMap{{u"extension"_s, v.extension},
                                                         {u"view"_s, v.name},
                                                         {u"title"_s, v.label},
                                                         {u"icon"_s, v.icon.isEmpty() ? u"puzzle"_s : v.icon},
                                                         {u"source"_s, QString(v.extension + u'/' + v.name)}}});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QVariantList out;
    for (const auto &f : std::as_const(found))
        out << f.second;
    return out;
}

} // namespace brulion::shell
