#include "workspaceextensions.h"

#include "extensionviewmodel.h"

#include "extensions/builtin.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "markdown/parse.h"
#include "notebook/pages.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;

// ---- ExtensionFormModel ------------------------------------------------------------------------

int ExtensionFormModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_fields.size()); }

QVariant ExtensionFormModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_fields.size())
        return {};
    const Field &f = m_fields[index.row()];
    switch (role) {
    case KeyRole: return f.key;
    case LabelRole: return f.label;
    case KindRole: return f.kind;
    case ValueRole: return f.value;
    case StepRole: return f.step;
    case UnitRole: return f.unit;
    case TextRole: return f.text;
    case DisplayRole: return formatExtensionNumber(f.value, m_comma);
    }
    return {};
}

QHash<int, QByteArray> ExtensionFormModel::roleNames() const
{
    return {{KeyRole, "fieldKey"}, {LabelRole, "fieldLabel"}, {KindRole, "fieldKind"}, {ValueRole, "fieldValue"},
            {StepRole, "fieldStep"}, {UnitRole, "fieldUnit"}, {TextRole, "fieldText"}, {DisplayRole, "fieldDisplay"}};
}

void ExtensionFormModel::reset(const QVariantList &fields)
{
    beginResetModel();
    m_fields.clear();
    for (const QVariant &v : fields) {
        const QVariantMap map = v.toMap();
        Field f;
        f.key = map.value(u"key"_s).toString();
        f.label = map.value(u"label"_s).toString();
        f.kind = map.value(u"kind"_s, u"number"_s).toString();
        f.unit = map.value(u"unit"_s).toString();
        f.value = map.value(u"value"_s).toDouble();
        f.step = map.value(u"step"_s, 1).toDouble();
        if (!std::isfinite(f.step) || f.step <= 0)
            f.step = 1;
        f.text = f.kind == u"number" ? QString() : map.value(u"value"_s).toString();
        if (!f.key.isEmpty())
            m_fields << f;
    }
    endResetModel();
}

QVariantMap ExtensionFormModel::values() const
{
    QVariantMap out;
    for (const Field &f : m_fields)
        out.insert(f.key, f.kind == u"number" ? QVariant(f.value) : QVariant(f.text));
    return out;
}

void ExtensionFormModel::setNumber(int row, double value)
{
    if (row < 0 || row >= m_fields.size() || !std::isfinite(value))
        return;
    m_fields[row].value = std::max(0.0, value);
    Q_EMIT dataChanged(index(row), index(row), {ValueRole, DisplayRole});
}

void ExtensionFormModel::setText(int row, const QString &text)
{
    if (row < 0 || row >= m_fields.size())
        return;
    m_fields[row].text = text;
    Q_EMIT dataChanged(index(row), index(row), {TextRole});
}

void ExtensionFormModel::setNumberText(int row, const QString &text)
{
    QString t = text.trimmed();
    t.replace(u',', u'.');
    t.remove(u' ');
    bool ok = false;
    const double value = t.toDouble(&ok);
    if (ok)
        setNumber(row, value);
}

void ExtensionFormModel::step(int row, int direction)
{
    if (row < 0 || row >= m_fields.size())
        return;
    const Field &f = m_fields[row];
    // Back onto the step's grid first ("82,5" +2,5 → 85), then one step.
    const double base = std::round(f.value / f.step) * f.step;
    setNumber(row, base + (direction < 0 ? -f.step : f.step));
}

// ---- WorkspaceExtensions -----------------------------------------------------------------------

namespace {

const QString kConsole = u"*console*"_s; // the console's environment (no page or built-in has a '*')
constexpr int kMaxApproved = 300;

QByteArray contentKey(const QString &content)
{
    return QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha1);
}

/// What an extension was loaded from: loading again only when this changes.
QString loadKey(const extensions::Extension &e)
{
    return QString::fromLatin1(contentKey(e.name + u'\n' + e.permissions.join(u',') + u'\n' + e.source).toHex());
}

/// A value of the console as Luau would write it ("text" quoted inside tables, raw on its own).
QString formatValue(const QVariant &v, int depth = 0)
{
    if (!v.isValid() || v.isNull())
        return u"nil"_s;
    switch (v.metaType().id()) {
    case QMetaType::Bool: return v.toBool() ? u"true"_s : u"false"_s;
    case QMetaType::LongLong: case QMetaType::Int: return QString::number(v.toLongLong());
    case QMetaType::Double: return QString::number(v.toDouble(), 'g', 15);
    case QMetaType::QVariantList: case QMetaType::QStringList: {
        if (depth > 4)
            return u"{…}"_s;
        QStringList parts;
        const QVariantList list = v.toList();
        for (qsizetype i = 0; i < list.size() && i < 100; ++i)
            parts << formatValue(list[i], depth + 1);
        if (list.size() > 100)
            parts << u"…"_s;
        return u"{"_s + parts.join(u", "_s) + u"}"_s;
    }
    case QMetaType::QVariantMap: {
        if (depth > 4)
            return u"{…}"_s;
        QStringList parts;
        const QVariantMap map = v.toMap();
        static const QRegularExpression name(u"^[A-Za-z_][A-Za-z0-9_]*$"_s);
        for (auto it = map.cbegin(); it != map.cend() && parts.size() < 100; ++it) {
            const QString key = name.match(it.key()).hasMatch() ? it.key() : u"[\""_s + it.key() + u"\"]"_s;
            parts << key + u" = "_s + formatValue(it.value(), depth + 1);
        }
        return u"{"_s + parts.join(u", "_s) + u"}"_s;
    }
    default: {
        const QString text = v.toString();
        return depth == 0 ? text : u"\""_s + text + u"\""_s;
    }
    }
}

} // namespace

WorkspaceExtensions::WorkspaceExtensions(Clock clock, QObject *parent)
    : QObject(parent),
      m_clock(std::move(clock)),
      m_fields(new ExtensionFormModel(this)),
      m_list(new ExtensionListModel(this)),
      m_console(new ExtensionConsoleModel(this))
{
    m_fields->setComma(true);
    m_restTimer.setInterval(250ms);
    connect(&m_restTimer, &QTimer::timeout, this, &WorkspaceExtensions::tickRest);
    // "change" hooks once a burst of changes is over (a sync writes many files one by one).
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(600ms);
    connect(&m_changeTimer, &QTimer::timeout, this, &WorkspaceExtensions::flushChanged);
    // Changed notes are looked at after the change, never inside it: an extension's own write
    // reaches notesChanged() while its call still runs, and nothing may load in the engine then.
    m_pagesTimer.setSingleShot(true);
    m_pagesTimer.setInterval(0);
    connect(&m_pagesTimer, &QTimer::timeout, this, &WorkspaceExtensions::processPages);
}

WorkspaceExtensions::~WorkspaceExtensions() = default;

void WorkspaceExtensions::attach(Vault *vault, VaultIndex *index)
{
    m_vault = vault;
    m_index = index;
    // Views show the notebook: a note changed anywhere (the editor, a sync) draws them again.
    connect(m_index, &VaultIndex::changed, this, &WorkspaceExtensions::changed);
    m_pages.clear();
    m_scanned = false;
    loadTrust();
    reload();
}

void WorkspaceExtensions::detach()
{
    if (m_index)
        disconnect(m_index, nullptr, this, nullptr);
    cancelQuestion();
    m_changeTimer.stop();
    m_engine.reset();
    m_host.reset();
    m_vault = nullptr;
    m_index = nullptr;
    m_pages.clear();
    m_scanned = false;
    m_pendingPaths.clear();
    m_pagesTimer.stop();
    m_calls.clear();
    m_questionCall = 0;
    ++m_generation;
    m_approved.clear();
    m_approvedOrder.clear();
    m_off.clear();
    m_editorSaves.clear();
    m_extensionWrites.clear();
    m_loadedKeys.clear();
    m_lastGood.clear();
    m_loadErrors.clear();
    m_hookFailures.clear();
    m_changedPages.clear();
    refreshList();
    Q_EMIT commandsChanged();
    bumpRevision();
}

void WorkspaceExtensions::setLanguage(const QString &language)
{
    const QString lang = language == u"en" ? u"en"_s : u"pl"_s;
    if (lang == m_language)
        return;
    m_language = lang;
    m_fields->setComma(lang == u"pl");
    reload(); // the extensions pick their words when they load
}

void WorkspaceExtensions::setEnabled(const QStringList &ids)
{
    if (ids == m_enabled)
        return;
    m_enabled = ids;
    Q_EMIT enabledChanged();
    reconcile();
}

void WorkspaceExtensions::setNotifier(std::function<void(const QString &, const QString &)> notify)
{
    m_notify = std::move(notify);
}

void WorkspaceExtensions::reload()
{
    cancelQuestion();
    m_engine.reset();
    m_host.reset();
    m_calls.clear();
    m_questionCall = 0;
    ++m_generation; // AI answers still on their way find no call
    m_loadedKeys.clear();
    m_loadErrors.clear();
    m_hookFailures.clear();
    reconcile();
    if (!m_engine) { // nothing runs: no VM (reconcile() said so already when something did)
        Q_EMIT commandsChanged();
        Q_EMIT changed();
    }
}

void WorkspaceExtensions::reloadAll()
{
    reload();
    m_console->append(u"info"_s, tr("Extensions loaded again."));
}

bool WorkspaceExtensions::ensureEngine()
{
    if (m_engine)
        return true;
    if (!m_vault || !m_index)
        return false;
    extensions::NotebookHost::Options options;
    options.now = m_clock;
    options.language = m_language;
    options.uiLanguage = m_language;
    m_host = std::make_unique<extensions::NotebookHost>(*m_vault, *m_index, options);
    m_host->onTimer = [this](const QString &, int seconds, const QString &label) { startRest(seconds, label); };
    m_host->onLog = [this](const QString &extension, const QString &text) {
        qCInfo(lcApp, "extension %s: %s", qUtf8Printable(extension), qUtf8Printable(text));
        m_console->append(u"print"_s, text, extension == kConsole ? QString() : title(extension));
    };
    m_host->onWritten = [this](const QString &, const QString &path) { m_extensionWrites.insert(path); };
    m_engine = std::make_unique<extensions::Engine>();
    QString error;
    if (!m_engine->start(m_host.get(), extensions::Limits{}, &error)) {
        qCWarning(lcApp, "extensions: %s", qUtf8Printable(error));
        m_engine.reset();
        m_host.reset();
        return false;
    }
    m_loadedKeys.clear();
    m_hookFailures.clear();
    return true;
}

// ---- the trust of this device --------------------------------------------------------------------

void WorkspaceExtensions::loadTrust()
{
    m_approved.clear();
    m_approvedOrder.clear();
    m_off.clear();
    LocalStore *store = m_vault ? m_vault->store() : nullptr;
    if (!store)
        return;
    for (const QJsonValue v : store->get(extensions::approvedStoreKey()).toArray()) {
        const QString hash = v.toString();
        if (hash.size() == 64 && !m_approved.contains(hash)) {
            m_approved.insert(hash);
            m_approvedOrder << hash;
        }
    }
    for (const QJsonValue v : store->get(extensions::offStoreKey()).toArray())
        m_off.insert(v.toString());
}

void WorkspaceExtensions::storeTrust()
{
    LocalStore *store = m_vault ? m_vault->store() : nullptr;
    if (!store)
        return;
    store->set(extensions::approvedStoreKey(), QJsonArray::fromStringList(m_approvedOrder));
    QStringList off(m_off.cbegin(), m_off.cend());
    off.sort();
    store->set(extensions::offStoreKey(), QJsonArray::fromStringList(off));
}

void WorkspaceExtensions::addApproved(const QString &hash)
{
    if (hash.isEmpty())
        return;
    m_approvedOrder.removeAll(hash);
    m_approvedOrder << hash;
    m_approved.insert(hash);
    while (m_approvedOrder.size() > kMaxApproved)
        m_approved.remove(m_approvedOrder.takeFirst());
    storeTrust();
}

// ---- extension pages -------------------------------------------------------------------------------

void WorkspaceExtensions::indexReady()
{
    m_scanned = true;
    scanPages();
    reconcile();
}

void WorkspaceExtensions::scanPages()
{
    m_pages.clear();
    if (!m_index)
        return;
    QStringList found;
    m_index->forEachNote([&](const VaultPath &path, const ParsedNote &note) {
        if (extensions::isExtensionPage(note))
            found << path;
    });
    for (const QString &path : std::as_const(found))
        updatePage(path);
}

bool WorkspaceExtensions::updatePage(const QString &path)
{
    const ParsedNote *note = m_index && paths::isMarkdown(path) ? m_index->note(path) : nullptr;
    const auto old = m_pages.constFind(path);
    if (!note || !extensions::isExtensionPage(*note)) {
        if (old == m_pages.constEnd())
            return false;
        m_pages.erase(old);
        return true;
    }
    const QString content = m_index->content(path);
    PageEntry entry;
    entry.page = extensions::readPageExtension(path, *note, content);
    entry.builtin = extensions::builtinFor(entry.page.overrides.isEmpty() ? entry.page.id : entry.page.overrides);
    entry.runId = entry.builtin.isEmpty() ? entry.page.id : entry.builtin;
    if (old != m_pages.constEnd()) {
        // Trust follows this device's own editing: an approved extension saved here by the editor
        // stays approved (unless it now asks for more). Code that arrives from elsewhere (a sync,
        // an import, another program, an extension's write) waits for approval.
        const auto saved = m_editorSaves.constFind(path);
        if (old->page.hash != entry.page.hash && isApproved(old->page.hash) && saved != m_editorSaves.constEnd()
            && *saved == contentKey(content)
            && std::ranges::all_of(entry.page.permissions, [&](const QString &p) { return old->page.permissions.contains(p); }))
            addApproved(entry.page.hash);
        if (old->page.hash == entry.page.hash && old->page.source == entry.page.source && old->page.title == entry.page.title
            && old->page.description == entry.page.description && old->page.problems == entry.page.problems
            && old->runId == entry.runId)
            return false;
        entry.state = old->state;
    }
    m_pages.insert(path, entry);
    return true;
}

void WorkspaceExtensions::notesChanged(const QStringList &changedPaths)
{
    if (!m_index)
        return;
    if (m_scanned) {
        for (const QString &path : changedPaths)
            if (!m_pendingPaths.contains(path))
                m_pendingPaths << path;
        m_pagesTimer.start();
    }
    // "change" hooks: what changed, except what extensions wrote themselves (no echoes, no loops).
    const bool hooked = m_engine && !m_engine->hooks(u"change"_s).isEmpty();
    for (const QString &path : changedPaths) {
        if (m_extensionWrites.remove(path) || !hooked || !paths::isMarkdown(path))
            continue;
        const ParsedNote *note = m_index->note(path);
        const QString name = note ? note->title : paths::pageTitle(path);
        if (!m_changedPages.contains(name))
            m_changedPages << name;
    }
    if (!m_changedPages.isEmpty())
        m_changeTimer.start();
}

void WorkspaceExtensions::processPages()
{
    if (m_engine && m_engine->isRunning()) { // (never from the event loop; a guard all the same)
        m_pagesTimer.start();
        return;
    }
    bool pagesChanged = false;
    const QStringList paths = std::exchange(m_pendingPaths, {});
    for (const QString &path : paths) {
        pagesChanged = updatePage(path) || pagesChanged;
        m_editorSaves.remove(path);
    }
    if (pagesChanged)
        reconcile();
}

void WorkspaceExtensions::savedByEditor(const QString &path, const QString &content)
{
    // Only an extension page's own text matters (its trust): every other save costs a lookup.
    if (m_pages.contains(path))
        m_editorSaves.insert(path, contentKey(content));
    if (!m_engine || m_engine->hooks(u"save"_s).isEmpty() || !paths::isMarkdown(path))
        return;
    QVariantMap event{{u"page"_s, paths::pageTitle(path)}, {u"path"_s, path}};
    // After the save, not inside it (the editor is still writing).
    QTimer::singleShot(0, this, [this, event] { runHooks(u"save"_s, event); });
}

void WorkspaceExtensions::flushChanged()
{
    const QStringList pages = std::exchange(m_changedPages, {});
    if (!pages.isEmpty())
        runHooks(u"change"_s, {{u"pages"_s, pages}});
}

void WorkspaceExtensions::runHooks(const QString &event, const QVariantMap &arg)
{
    if (!m_engine)
        return;
    if (m_engine->isRunning()) { // never inside another call
        QTimer::singleShot(0, this, [this, event, arg] { runHooks(event, arg); });
        return;
    }
    const QList<extensions::HookInfo> hooks = m_engine->hooks(event);
    for (const extensions::HookInfo &h : hooks) {
        m_current = CallContext{};
        m_current.hook = true;
        m_current.hookId = h.id;
        m_current.hookEvent = event;
        m_current.extension = h.extension;
        handleResult(m_engine->runHook(h.id, arg));
    }
    m_current = CallContext{};
}

void WorkspaceExtensions::hookResult(const CallContext &ctx, const extensions::Result &r)
{
    if (r.status == extensions::Status::NotFound || !m_engine)
        return;
    if (r.ok()) {
        m_hookFailures.remove(ctx.hookId);
        if (r.value.isValid() && !r.value.isNull())
            m_console->append(u"result"_s, formatValue(r.value), title(ctx.extension));
        return;
    }
    qCWarning(lcApp, "extension %s, hook %s: %s", qUtf8Printable(ctx.extension), qUtf8Printable(ctx.hookEvent),
              qUtf8Printable(r.error));
    m_console->append(u"error"_s, r.error, title(ctx.extension));
    // Emacs' way: a hook that keeps failing is taken off (until the extension loads again).
    if (++m_hookFailures[ctx.hookId] >= 3) {
        m_engine->removeHook(ctx.hookId);
        m_hookFailures.remove(ctx.hookId);
        Q_EMIT message(tr("%1: its reaction to “%2” failed three times and is off until the extension is loaded again.")
                               .arg(title(ctx.extension), ctx.hookEvent),
                       u"error"_s);
    }
}

void WorkspaceExtensions::handleResult(const extensions::Result &r)
{
    const auto known = r.call ? m_calls.constFind(r.call) : m_calls.constEnd();
    const CallContext ctx = known != m_calls.constEnd() ? *known : m_current;
    if (r.status == extensions::Status::Suspended) {
        m_calls.insert(r.call, ctx);
        const QVariantMap request = r.value.toMap();
        if (request.value(u"kind"_s).toString() == u"ai") {
            dispatchAi(r.call, request.value(u"request"_s).toMap());
            return;
        }
        if (ctx.hook) {
            m_engine->cancel(r.call);
            m_calls.remove(r.call);
            hookResult(ctx, {extensions::Status::Error, {}, u"b.on(\"%1\"): a hook cannot ask a question"_s.arg(ctx.hookEvent)});
            return;
        }
        cancelQuestion(); // one question at a time: an older one is answered "cancelled"
        m_questionCall = r.call;
        setQuestion(request);
        return;
    }
    if (r.call)
        m_calls.remove(r.call);
    if (ctx.console)
        consoleResult(r, ctx.toast);
    else if (ctx.hook)
        hookResult(ctx, r);
    else
        handle(r, ctx.fromAction);
}

void WorkspaceExtensions::setAi(const ai::Settings &settings) { m_aiSettings = settings; }

void WorkspaceExtensions::dispatchAi(int call, const QVariantMap &request)
{
    const CallContext ctx = m_calls.value(call);
    m_calls[call].askedAi = true;
    ++m_aiPending;
    Q_EMIT aiPendingChanged();
    if (ctx.askedAi) {
        // (A further turn of the same call: said once; the top bar's ✨ spins meanwhile.)
    } else if (ctx.console) {
        m_console->append(u"info"_s, tr("Asking AI…"));
    } else if (!ctx.hook) {
        Q_EMIT message(tr("Asking AI…"), u"info"_s);
    }
    QString error;
    const std::optional<ai::Request> r = ai::requestFrom(request, &error);
    const quint64 generation = m_generation;
    if (!r) {
        QTimer::singleShot(0, this, [this, call, error, generation] { finishAi(call, {{u"error"_s, error}}, generation); });
        return;
    }
    if (!m_aiClient) {
        m_transport = std::make_unique<sync::NetworkHttpTransport>();
        m_aiClient = std::make_unique<ai::Client>(m_transport.get());
    }
    const QPointer<WorkspaceExtensions> self(this);
    m_aiClient->send(m_aiSettings, *r, [self, call, generation](const ai::Reply &reply) {
        if (self)
            self->finishAi(call, reply.toVariant(), generation);
    });
}

void WorkspaceExtensions::finishAi(int call, const QVariantMap &reply, quint64 generation)
{
    m_aiPending = std::max(0, m_aiPending - 1);
    Q_EMIT aiPendingChanged();
    if (generation != m_generation || !m_engine || !m_engine->waitingCalls().contains(call)) {
        m_calls.remove(call); // its extension was loaded again, or the notebook closed
        return;
    }
    if (m_engine->isRunning()) {
        ++m_aiPending;
        QTimer::singleShot(0, this, [this, call, reply, generation] { finishAi(call, reply, generation); });
        return;
    }
    handleResult(m_engine->resume(call, reply));
}

void WorkspaceExtensions::testAi()
{
    ai::Request request;
    request.messages << ai::textMessage(u"user"_s, u"Say OK."_s);
    request.maxTokens = 1024;
    if (m_aiSettings.provider == u"anthropic")
        request.effort = u"low"_s;
    if (!m_aiClient) {
        m_transport = std::make_unique<sync::NetworkHttpTransport>();
        m_aiClient = std::make_unique<ai::Client>(m_transport.get());
    }
    ++m_aiPending;
    Q_EMIT aiPendingChanged();
    const QPointer<WorkspaceExtensions> self(this);
    m_aiClient->send(m_aiSettings, request, [self](const ai::Reply &reply) {
        if (!self)
            return;
        self->m_aiPending = std::max(0, self->m_aiPending - 1);
        Q_EMIT self->aiPendingChanged();
        if (reply.ok)
            Q_EMIT self->message(tr("AI answers (%1): %2").arg(reply.model, reply.text.simplified().left(80)), u"success"_s);
        else
            Q_EMIT self->message(tr("AI: %1").arg(reply.error), u"error"_s);
    });
}

QString WorkspaceExtensions::pageStateOf(const PageEntry &entry) const { return entry.state; }

void WorkspaceExtensions::reconcile()
{
    if (isWaiting()) {
        // Loading would abandon the call waiting for an answer: after the answer.
        m_reconcilePending = true;
        return;
    }
    m_reconcilePending = false;
    // What should run: the built-ins switched on — each replaced by an approved page that
    // overrides it — and the approved pages that are on. A page's state says why it does not run.
    QHash<QString, extensions::Extension> want;
    for (const extensions::BuiltinExtension &b : extensions::builtinExtensions())
        if (m_enabled.contains(b.extension.id))
            want.insert(b.extension.id, b.extension);
    QStringList pagePaths = m_pages.keys();
    pagePaths.sort();
    QSet<QString> taken;
    for (const QString &path : std::as_const(pagePaths)) {
        PageEntry &e = m_pages[path];
        e.message = e.page.problems.join(u"; "_s);
        e.errorLine = -1;
        if (e.page.blocks == 0) {
            e.state = u"problem"_s;
            continue;
        }
        if (taken.contains(e.runId)) {
            e.state = u"duplicate"_s;
            continue;
        }
        taken.insert(e.runId);
        if (!e.builtin.isEmpty() && !m_enabled.contains(e.builtin))
            e.state = u"builtinOff"_s;
        else if (m_off.contains(e.runId))
            e.state = u"off"_s;
        else if (!isApproved(e.page.hash))
            e.state = u"approval"_s;
        else {
            extensions::Extension x = e.page.extension();
            x.id = e.runId;
            if (!e.builtin.isEmpty())
                x.data = extensions::builtinExtension(e.builtin).data;
            want.insert(e.runId, x);
            e.state = u"running"_s; // or "error" once loaded
        }
    }
    bool loaded = false;
    if (!want.isEmpty() && ensureEngine()) {
        for (const QString &id : m_loadedKeys.keys()) {
            if (!want.contains(id)) {
                m_engine->unload(id);
                m_loadedKeys.remove(id);
                m_loadErrors.remove(id);
                loaded = true;
            }
        }
        for (auto it = want.cbegin(); it != want.cend(); ++it) {
            const QString key = loadKey(it.value());
            if (m_loadedKeys.value(it.key()) == key)
                continue;
            loaded = true;
            m_loadedKeys.insert(it.key(), key);
            m_hookFailures.clear();
            const extensions::Result r = m_engine->load(it.value());
            if (r.ok()) {
                m_lastGood.insert(it.key(), it.value());
                m_loadErrors.remove(it.key());
                continue;
            }
            m_loadErrors.insert(it.key(), r.error);
            qCWarning(lcApp, "extension %s: %s", qUtf8Printable(it.key()), qUtf8Printable(r.error));
            m_console->append(u"error"_s, r.error, title(it.key()));
            // The last version that loaded keeps running: a half-written edit never takes a
            // working extension away (Emacs keeps the definitions an eval-buffer error left).
            const auto good = m_lastGood.constFind(it.key());
            if (good != m_lastGood.constEnd() && loadKey(*good) != key && !m_engine->load(*good).ok())
                m_lastGood.remove(it.key());
        }
    } else if (m_engine) {
        for (const QString &id : m_loadedKeys.keys()) {
            m_engine->unload(id);
            loaded = true;
        }
        m_loadedKeys.clear();
        m_loadErrors.clear();
    }
    for (PageEntry &e : m_pages) {
        if (e.state != u"running")
            continue;
        const auto error = m_loadErrors.constFind(e.runId);
        if (error == m_loadErrors.constEnd())
            continue;
        e.state = u"error"_s;
        e.message = error->section(u'\n', 0, 0);
        e.errorLine = extensions::errorLine(e.page.title, e.message); // the code keeps the page's lines
    }
    refreshList();
    if (loaded) {
        Q_EMIT commandsChanged();
        Q_EMIT changed();
    }
    bumpRevision();
}

void WorkspaceExtensions::refreshList()
{
    const auto counts = [&](const QString &id, ExtensionRow &row) {
        if (!m_engine || !m_engine->isLoaded(id))
            return;
        for (const extensions::CommandInfo &c : m_engine->commands())
            row.commands += c.extension == id;
        for (const extensions::ViewInfo &v : m_engine->views())
            row.views += v.extension == id;
        for (const extensions::HookInfo &h : m_engine->hooks())
            row.hooks += h.extension == id;
    };
    const auto permissionsText = [](const QStringList &permissions) {
        QStringList out;
        for (const QString &p : permissions)
            out << (p == u"write" ? tr("change notes") : p == u"ai" ? tr("use AI") : p);
        return out.join(u", "_s);
    };
    QList<ExtensionRow> rows;
    for (const extensions::BuiltinExtension &b : extensions::builtinExtensions()) {
        ExtensionRow row;
        row.id = b.extension.id;
        row.title = title(b.extension.id);
        row.kind = u"builtin"_s;
        row.permissions = permissionsText(b.extension.permissions);
        const bool on = m_enabled.contains(b.extension.id);
        row.state = on ? u"running"_s : u"builtinOff"_s;
        for (const PageEntry &e : std::as_const(m_pages)) {
            if (e.builtin != b.extension.id || e.state == u"duplicate")
                continue;
            row.path = e.page.path;
            if (on && (e.state == u"running" || e.state == u"error"))
                row.state = u"overridden"_s;
        }
        if (on)
            counts(b.extension.id, row);
        rows << row;
    }
    QList<const PageEntry *> pages;
    for (const PageEntry &e : std::as_const(m_pages))
        pages << &e;
    std::ranges::sort(pages, [](const PageEntry *a, const PageEntry *b) {
        return a->page.title.compare(b->page.title, Qt::CaseInsensitive) < 0
               || (a->page.title.compare(b->page.title, Qt::CaseInsensitive) == 0 && a->page.path < b->page.path);
    });
    for (const PageEntry *e : std::as_const(pages)) {
        ExtensionRow row;
        row.id = e->runId;
        row.title = e->page.title;
        row.kind = u"page"_s;
        row.state = e->state;
        row.message = e->message;
        row.path = e->page.path;
        row.permissions = permissionsText(e->page.permissions);
        row.description = e->page.description;
        row.errorLine = e->errorLine;
        if (e->state == u"running" || e->state == u"error")
            counts(e->runId, row);
        rows << row;
    }
    m_list->setRows(rows);
}

void WorkspaceExtensions::bumpRevision()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

const WorkspaceExtensions::PageEntry *WorkspaceExtensions::pageAt(const QString &path) const
{
    const auto it = m_pages.constFind(paths::normalize(path));
    return it == m_pages.constEnd() ? nullptr : &*it;
}

QString WorkspaceExtensions::pageState(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? pageStateOf(*e) : QString();
}

QString WorkspaceExtensions::pageMessage(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? e->message : QString();
}

int WorkspaceExtensions::pageErrorLine(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? e->errorLine : -1;
}

int WorkspaceExtensions::waitingApproval() const
{
    return int(std::ranges::count_if(m_pages, [](const PageEntry &e) { return e.state == u"approval"; }));
}

void WorkspaceExtensions::approve(const QString &path)
{
    const PageEntry *e = pageAt(path);
    if (!e)
        return;
    addApproved(e->page.hash);
    m_off.remove(e->runId);
    storeTrust();
    m_console->append(u"info"_s, tr("Approved on this device: %1").arg(e->page.title));
    reconcile();
}

void WorkspaceExtensions::setPageOn(const QString &path, bool on)
{
    const PageEntry *e = pageAt(path);
    if (!e || on == !m_off.contains(e->runId))
        return;
    if (on)
        m_off.remove(e->runId);
    else
        m_off.insert(e->runId);
    storeTrust();
    reconcile();
}

QString WorkspaceExtensions::createExtension()
{
    if (!m_vault)
        return {};
    const bool en = m_language == u"en";
    const QString content = extensions::newExtensionPage(m_language);
    // Extension pages together in a folder of their own (TiddlyWiki's "$:/" corner).
    const notebook::CreatedPage created = notebook::createPage(*m_vault, en ? u"My extension"_s : u"Moje rozszerzenie"_s,
                                                               en ? u"Extensions"_s : u"Rozszerzenia"_s, std::nullopt, content);
    if (created.path.isEmpty())
        return {};
    if (!created.existing) {
        // Written here, by Brulion: approved on this device.
        const ParsedNote note = markdown::parseNote(created.path, content, m_vault->config());
        addApproved(extensions::readPageExtension(created.path, note, content).hash);
        reconcile();
    }
    return created.path;
}

QString WorkspaceExtensions::editBuiltinSource(const QString &builtin)
{
    if (!m_vault)
        return {};
    for (const PageEntry &e : std::as_const(m_pages))
        if (e.builtin == builtin)
            return e.page.path;
    const extensions::Extension b = extensions::builtinExtension(builtin);
    if (b.id.isEmpty())
        return {};
    QString name = builtin;
    for (const extensions::BuiltinExtension &x : extensions::builtinExtensions())
        if (x.extension.id == builtin)
            name = m_language == u"en" ? x.titleEn : x.titlePl;
    const QString folder = m_language == u"en" ? u"Extensions"_s : u"Rozszerzenia"_s;
    const QString content = extensions::builtinSourcePage(b, m_language);
    const notebook::CreatedPage created = notebook::createPage(*m_vault, name, folder, std::nullopt, content);
    if (created.path.isEmpty())
        return {};
    if (!created.existing) {
        // The built-in code, written by Brulion: approved on this device.
        const ParsedNote note = markdown::parseNote(created.path, content, m_vault->config());
        addApproved(extensions::readPageExtension(created.path, note, content).hash);
        reconcile();
    }
    return created.path;
}

QString WorkspaceExtensions::overridePage(const QString &builtin) const
{
    QString found;
    for (const PageEntry &e : m_pages)
        if (e.builtin == builtin && e.state != u"duplicate" && (found.isEmpty() || e.page.path < found))
            found = e.page.path;
    return found;
}

bool WorkspaceExtensions::restoreBuiltin(const QString &builtin)
{
    if (!m_vault)
        return false;
    QStringList pagesOf;
    for (const PageEntry &e : std::as_const(m_pages))
        if (e.builtin == builtin)
            pagesOf << e.page.path;
    bool ok = !pagesOf.isEmpty();
    for (const QString &path : std::as_const(pagesOf))
        ok = notebook::deletePath(*m_vault, path, ChangeSource::User).problem.kind == notebook::PathProblem::None && ok;
    return ok;
}

QList<extensions::CommandInfo> WorkspaceExtensions::commands() const
{
    return m_engine ? m_engine->commands() : QList<extensions::CommandInfo>();
}

QList<extensions::ViewInfo> WorkspaceExtensions::views() const
{
    return m_engine ? m_engine->views() : QList<extensions::ViewInfo>();
}

QString WorkspaceExtensions::title(const QString &extension) const
{
    if (extension == kConsole)
        return tr("Luau console");
    for (const extensions::BuiltinExtension &builtin : extensions::builtinExtensions())
        if (builtin.extension.id == extension)
            return m_language == u"en" ? builtin.titleEn : builtin.titlePl;
    for (const PageEntry &e : m_pages)
        if (e.runId == extension && e.state != u"duplicate")
            return e.page.title;
    return extension;
}

QString WorkspaceExtensions::viewTitle(const QString &extension, const QString &view) const
{
    for (const extensions::ViewInfo &v : views())
        if (v.extension == extension && v.name == view)
            return v.label.isEmpty() ? view : v.label;
    return view;
}

bool WorkspaceExtensions::isWaiting() const { return !m_questionKind.isEmpty(); }

void WorkspaceExtensions::runCommand(const QString &extension, const QString &name, const QString &path, int line)
{
    if (!m_engine)
        return;
    QVariantMap ctx{{u"page"_s, path.isEmpty() ? QString() : paths::pageTitle(path)}, {u"selection"_s, QString()}};
    if (!path.isEmpty() && line >= 0) {
        const QVariantMap block = m_host->blockAt(path, line);
        if (!block.isEmpty())
            ctx.insert(u"block"_s, block);
    }
    cancelQuestion();
    m_current = CallContext{};
    handleResult(m_engine->runCommand(extension, name, ctx));
    m_current = CallContext{};
}

void WorkspaceExtensions::runBlockCommand(const QString &command, const QString &path, int line)
{
    if (command == u"*/run") {
        runCodeAt(path, line);
        return;
    }
    const qsizetype slash = command.indexOf(u'/');
    if (slash > 0)
        runCommand(command.left(slash), command.mid(slash + 1), path, line);
}

QVariantMap WorkspaceExtensions::render(const QString &extension, const QString &view, const QVariantMap &params, QString *error)
{
    if (!m_engine || isWaiting())
        return {};
    const extensions::Result r = m_engine->render(extension, view, params);
    if (r.status == extensions::Status::Suspended) {
        m_engine->cancel(r.call); // a view never waits (b.ask, b.ai)
        if (error)
            *error = tr("A view cannot wait for a question or for AI: ask in a command or an action.");
        return {};
    }
    if (r.status == extensions::Status::NotFound)
        return {}; // its extension is off or not loaded (yet): nothing to draw, nothing wrong
    if (!r.ok()) {
        qCWarning(lcApp, "extension view %s/%s: %s", qUtf8Printable(extension), qUtf8Printable(view), qUtf8Printable(r.error));
        if (error)
            *error = r.error.section(u'\n', 0, 0);
        return {};
    }
    return r.value.toMap();
}

bool WorkspaceExtensions::act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args)
{
    if (!m_engine)
        return false;
    cancelQuestion();
    m_current = CallContext{};
    m_current.fromAction = true;
    const extensions::Result r = m_engine->act(extension, view, action, args);
    handleResult(r);
    m_current = CallContext{};
    return r.ok() || r.status == extensions::Status::Suspended;
}

void WorkspaceExtensions::handle(const extensions::Result &result, bool fromAction)
{
    switch (result.status) {
    case extensions::Status::Suspended:
        setQuestion(result.value.toMap());
        return;
    case extensions::Status::Ok:
        if (result.value.metaType().id() == QMetaType::QString && !result.value.toString().isEmpty())
            Q_EMIT message(result.value.toString(), u"info"_s);
        break;
    case extensions::Status::NotFound:
        break;
    default: {
        qCWarning(lcApp, "extension: %s", qUtf8Printable(result.error));
        m_console->append(u"error"_s, result.error);
        // A line changed elsewhere (a sync, another pane) since the view showed it: the view shows
        // it again, nothing was written.
        if (fromAction && result.error.contains(u"stale:"_s))
            Q_EMIT message(tr("The line changed in the meantime. The view shows it again; nothing was written."), u"info"_s);
        else if (result.status == extensions::Status::Timeout)
            Q_EMIT message(tr("The extension took too long and was stopped."), u"error"_s);
        else if (result.status == extensions::Status::OutOfMemory)
            Q_EMIT message(tr("The extension needed too much memory and was stopped."), u"error"_s);
        else
            Q_EMIT message(tr("The extension failed: %1").arg(result.error.section(u'\n', 0, 0)), u"error"_s);
    }
    }
    Q_EMIT changed();
}

// ---- the console -----------------------------------------------------------------------------------

bool WorkspaceExtensions::ensureConsole()
{
    if (!ensureEngine())
        return false;
    if (m_engine->isLoaded(kConsole))
        return true;
    extensions::Extension console;
    console.id = kConsole;
    console.name = u"console"_s;
    console.permissions = {u"ai"_s, u"write"_s}; // what the user types is theirs
    const extensions::Result r = m_engine->load(console);
    if (!r.ok())
        m_console->append(u"error"_s, r.error);
    return r.ok();
}

void WorkspaceExtensions::evaluate(const QString &code)
{
    if (code.trimmed().isEmpty())
        return;
    evaluateIn(code, u"console"_s, code.trimmed(), false);
}

void WorkspaceExtensions::runCodeAt(const QString &path, int line)
{
    const QString content = m_index ? m_index->content(paths::normalize(path)) : QString();
    const QString code = extensions::codeBlockAt(content, line);
    if (code.trimmed().isEmpty()) {
        Q_EMIT message(tr("Put the cursor in a ```luau block to run it."), u"info"_s);
        return;
    }
    const ParsedNote *note = m_index->note(paths::normalize(path));
    evaluateIn(code, note ? note->title : paths::pageTitle(path), code.trimmed(), true);
}

void WorkspaceExtensions::evaluateIn(const QString &code, const QString &name, const QString &echo, bool toast)
{
    m_console->append(u"input"_s, echo);
    if (!ensureConsole()) {
        m_console->append(u"error"_s, tr("The Luau engine is not available: open a notebook first."));
        return;
    }
    cancelQuestion();
    m_current = CallContext{};
    m_current.console = true;
    m_current.toast = toast;
    handleResult(m_engine->evaluate(kConsole, code, name));
    m_current = CallContext{};
}

void WorkspaceExtensions::consoleResult(const extensions::Result &result, bool toast)
{
    QString shown;
    if (result.ok()) {
        if (result.value.isValid() && !result.value.isNull()) {
            shown = formatValue(result.value);
            m_console->append(u"result"_s, shown);
        }
    } else {
        shown = result.error.section(u'\n', 0, 0);
        // A traceback of one frame says nothing the message did not ("console:1").
        const QStringList lines = result.error.split(u'\n');
        const bool oneFrame = lines.size() == 3 && lines[1] == u"stack traceback:";
        m_console->append(u"error"_s, oneFrame ? shown : result.error);
    }
    if (toast && !shown.isEmpty())
        Q_EMIT message(result.ok() ? u"Luau: "_s + shown.left(300) : tr("Luau: %1").arg(shown.left(300)),
                       result.ok() ? u"info"_s : u"error"_s);
    // The console may have registered commands, views or hooks (b.command typed in it).
    refreshList();
    Q_EMIT commandsChanged();
    Q_EMIT changed();
    bumpRevision();
}

void WorkspaceExtensions::setQuestion(const QVariantMap &question)
{
    m_questionKind = question.value(u"kind"_s).toString();
    m_questionTitle = question.value(m_questionKind == u"form" ? u"title"_s : u"prompt"_s).toString();
    m_questionOptions = question.value(u"options"_s).toStringList();
    m_fields->reset(question.value(u"fields"_s).toList());
    ++m_questionSerial;
    Q_EMIT questionChanged();
}

void WorkspaceExtensions::answer(const QVariant &value)
{
    if (!m_engine || m_questionKind.isEmpty())
        return;
    const int call = std::exchange(m_questionCall, 0);
    m_questionKind.clear();
    m_questionTitle.clear();
    m_questionOptions.clear();
    Q_EMIT questionChanged();
    handleResult(m_engine->resume(call, value));
    if (m_reconcilePending && !isWaiting())
        reconcile();
}

void WorkspaceExtensions::answerText(const QString &text) { answer(text); }

void WorkspaceExtensions::answerChoice(const QString &option)
{
    if (m_questionKind == u"choose" && !m_questionOptions.contains(option))
        return;
    answer(option);
}

void WorkspaceExtensions::answerForm() { answer(m_fields->values()); }

void WorkspaceExtensions::cancelQuestion()
{
    if (m_questionKind.isEmpty())
        return;
    const QString kind = m_questionKind;
    // ask gets "" (nothing typed), choose and form nil: the extension ends its call itself
    answer(kind == u"ask" ? QVariant(QString()) : QVariant());
}

void WorkspaceExtensions::startRest(int seconds, const QString &label)
{
    m_restTotal = std::max(1, seconds);
    m_restRemaining = m_restTotal;
    m_restLabel = label.isEmpty() ? tr("Rest") : label;
    m_restEnd = QDateTime::currentDateTimeUtc().addSecs(m_restTotal);
    m_restTimer.start();
    Q_EMIT restChanged();
}

QString WorkspaceExtensions::restText() const
{
    return u"%1:%2"_s.arg(m_restRemaining / 60).arg(m_restRemaining % 60, 2, 10, QChar(u'0'));
}

void WorkspaceExtensions::stopRest()
{
    if (!m_restRemaining && !m_restTimer.isActive())
        return;
    m_restTimer.stop();
    m_restRemaining = 0;
    Q_EMIT restChanged();
}

void WorkspaceExtensions::tickRest()
{
    const qint64 left = (QDateTime::currentDateTimeUtc().msecsTo(m_restEnd) + 999) / 1000;
    if (left > 0) {
        if (left != m_restRemaining) {
            m_restRemaining = int(left);
            Q_EMIT restChanged();
        }
        return;
    }
    m_restTimer.stop();
    m_restRemaining = 0;
    Q_EMIT restChanged();
    if (m_notify)
        m_notify(tr("%1 is over").arg(m_restLabel), tr("Time for the next set."));
    Q_EMIT message(tr("%1 is over: time for the next set.").arg(m_restLabel), u"info"_s);
}

} // namespace brulion::shell
