#include "workspaceextensions.h"

#include "extensionviewmodel.h"

#include "ai/local.h"
#include "extensions/builtin.h"
#include "extensions/notebookhost.h"
#include "extensions/share.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "markdown/parse.h"
#include "notebook/pages.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QPointer>
#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#endif
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <utility>

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
    case PlaceholderRole: return f.placeholder;
    }
    return {};
}

QHash<int, QByteArray> ExtensionFormModel::roleNames() const
{
    return {{KeyRole, "fieldKey"}, {LabelRole, "fieldLabel"}, {KindRole, "fieldKind"}, {ValueRole, "fieldValue"},
            {StepRole, "fieldStep"}, {UnitRole, "fieldUnit"}, {TextRole, "fieldText"}, {DisplayRole, "fieldDisplay"},
            {PlaceholderRole, "fieldPlaceholder"}};
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
        f.placeholder = map.value(u"placeholder"_s).toString();
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
    if (row < 0 || row >= m_fields.size())
        return;
    if (const std::optional<double> value = parseExtensionNumber(text, m_fields[row].unit))
        setNumber(row, *value);
}

int ExtensionFormModel::firstTextField() const
{
    for (qsizetype i = 0; i < m_fields.size(); ++i)
        if (m_fields[i].kind == u"text")
            return int(i);
    return -1;
}

void ExtensionFormModel::step(int row, int direction)
{
    if (row < 0 || row >= m_fields.size())
        return;
    const Field &f = m_fields[row];
    // Onto the step's grid ("31" −2,5 → 30, +2,5 → 32,5; "82,5" +2,5 → 85).
    setNumber(row, stepExtensionNumber(f.value, f.step, direction));
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
    // The flashcards' due count: once the notes stop changing for a moment, and every five
    // minutes (a learning card's step ends, a new day begins).
    m_dueTimer.setSingleShot(true);
    m_dueTimer.setInterval(1500ms);
    connect(&m_dueTimer, &QTimer::timeout, this, &WorkspaceExtensions::refreshFlashcardsDue);
    m_dueClock.setInterval(5min);
    connect(&m_dueClock, &QTimer::timeout, this, &WorkspaceExtensions::refreshFlashcardsDue);
    // Macros are computed after the frame that asked for them, a slice at a time (workspacemacros.cpp).
    m_macroTimer.setSingleShot(true);
    m_macroTimer.setInterval(0);
    connect(&m_macroTimer, &QTimer::timeout, this, &WorkspaceExtensions::runMacros);
    m_macrosChangedTimer.setSingleShot(true);
    m_macrosChangedTimer.setInterval(30ms);
    connect(&m_macrosChangedTimer, &QTimer::timeout, this, &WorkspaceExtensions::macrosChanged);
    m_macroClock.start();
    // "day" hooks: midnight passed while the app ran (or the device woke up on a later day).
    m_dayTimer.setInterval(10s);
    connect(&m_dayTimer, &QTimer::timeout, this, &WorkspaceExtensions::checkDay);
}

QVariantMap WorkspaceExtensions::withSettings(const QString &extension, const QVariantMap &values) const
{
    const auto it = m_settingsFor.constFind(extension);
    if (it == m_settingsFor.constEnd())
        return values;
    QVariantMap out = *it;
    for (auto v = values.cbegin(); v != values.cend(); ++v)
        out.insert(v.key(), v.value());
    return out;
}

void WorkspaceExtensions::setFlashcardSettings(const QVariantMap &settings)
{
    if (m_settingsFor.value(u"fiszki"_s) == settings)
        return;
    m_settingsFor.insert(u"fiszki"_s, settings);
    Q_EMIT changed(); // its views draw again with them
    m_dueTimer.start();
}

void WorkspaceExtensions::refreshFlashcardsDue()
{
    m_dueTimer.stop();
    int due = 0;
    if (m_engine && flashcardsEnabled()) {
        if (m_engine->isRunning() || isWaiting()) {
            m_dueTimer.start(); // after the call, or the question's answer
            return;
        }
        const QVariantMap ctx{{u"page"_s, QString()},
                              {u"selection"_s, QString()},
                              {u"args"_s, withSettings(u"fiszki"_s, {})}};
        const extensions::Result r = m_engine->runCommand(u"fiszki"_s, u"count"_s, ctx);
        if (r.ok()) {
            const QVariantMap n = r.value.toMap();
            due = n.value(u"due"_s).toInt() + n.value(u"new"_s).toInt();
        } else if (r.status != extensions::Status::NotFound) {
            qCWarning(lcApp, "flashcards' count: %s", qUtf8Printable(r.error.section(u'\n', 0, 0)));
        }
    }
    if (flashcardsEnabled() && m_engine)
        m_dueClock.start();
    else
        m_dueClock.stop();
    if (due != m_flashcardsDue) {
        m_flashcardsDue = due;
        Q_EMIT flashcardsDueChanged();
    }
}

WorkspaceExtensions::~WorkspaceExtensions() = default;

QDate WorkspaceExtensions::today() const { return m_clock ? m_clock().date() : QDate::currentDate(); }

void WorkspaceExtensions::attach(Vault *vault, VaultIndex *index)
{
    m_vault = vault;
    m_index = index;
    // Views show the notebook: a note changed anywhere (the editor, a sync) draws them again.
    connect(m_index, &VaultIndex::changed, this, &WorkspaceExtensions::changed);
    connect(m_index, &VaultIndex::changed, this, [this] { m_dueTimer.start(); });
    m_pages.clear();
    m_scanned = false;
    m_opened = false;
    m_day = m_clock ? m_clock().date() : QDate::currentDate();
    m_dayTimer.start();
    loadTrust();
    loadTimers();
    reload();
}

void WorkspaceExtensions::detach()
{
    if (m_index)
        disconnect(m_index, nullptr, this, nullptr);
    m_waitingQuestions.clear(); // (their calls go with the engine)
    cancelQuestion();
    m_changeTimer.stop();
    m_dueTimer.stop();
    m_dueClock.stop();
    if (m_flashcardsDue != 0) {
        m_flashcardsDue = 0;
        Q_EMIT flashcardsDueChanged();
    }
    m_engine.reset();
    m_host.reset();
    m_live.clear();       // live code lives with its notebook
    scheduleAppChanged(); // the UI items and themes went with the engine
    m_vault = nullptr;
    m_index = nullptr;
    m_pages.clear();
    m_scanned = false;
    m_pendingPaths.clear();
    m_pagesTimer.stop();
    m_calls.clear();
    stopAiRequests();
    m_questionCall = 0;
    ++m_generation;
    m_approved.clear();
    m_approvedOrder.clear();
    m_off.clear();
    m_editorSaves.clear();
    m_loadedKeys.clear();
    m_lastGood.clear();
    m_loadErrors.clear();
    m_hookFailures.clear();
    m_macros.clear();
    m_macroTimer.stop();
    m_batch.clear();
    m_changing.clear();
    m_dayTimer.stop();
    m_opened = false;
    clearSelection();
    // (The timers stay in the notebook's device state and their platform alarms armed: their
    // ends still come, and the notebook opened again shows them — loadTimers().)
    m_lateTimers.clear();
    if (!m_timers.isEmpty()) {
        m_timers.clear();
        m_restTimer.stop();
        m_restRemaining = 0;
        Q_EMIT restChanged();
        Q_EMIT timersChanged();
    }
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
    m_dueTimer.start();
}

void WorkspaceExtensions::setNotifier(std::function<void(const QString &, const QString &)> notify)
{
    m_notify = std::move(notify);
}

void WorkspaceExtensions::setTimerAlarms(std::function<bool(const ScheduledNotification &)> arm,
                                         std::function<void(const QString &)> cancel)
{
    m_timerArm = std::move(arm);
    m_timerCancel = std::move(cancel);
}

void WorkspaceExtensions::reload()
{
    if (!m_waitingQuestions.isEmpty()) {
        m_waitingQuestions.clear(); // (their calls go with the engine)
        ++m_chatQuestionSerial;
        Q_EMIT chatQuestionsChanged();
    }
    cancelQuestion();
    // A turn of the AI chat on its way ends here: the chat says so.
    QStringList chats;
    for (const CallContext &ctx : std::as_const(m_calls)) {
        if (!ctx.chat.isEmpty())
            chats << ctx.chat;
    }
    m_engine.reset();
    m_host.reset();
    m_calls.clear();
    stopAiRequests(); // (a model on the device stops writing for nobody)
    for (const QString &chat : std::as_const(chats))
        Q_EMIT chatFailed(chat, tr("The AI assistant was loaded again meanwhile: try again."));
    m_questionCall = 0;
    ++m_generation; // AI answers still on their way find no call
    m_loadedKeys.clear();
    m_loadErrors.clear();
    m_hookFailures.clear();
    scheduleAppChanged(); // the old VM's UI items and themes are gone, the new one's follow
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
    // b.require takes the code of a page only as this device runs pages: approved, not off.
    options.moduleAllowed = [this](const QString &path, const QString &hash) -> QString {
        if (!isApproved(hash))
            return u"waits for approval on this device (Settings → Extensions, or Run on its page)"_s;
        const PageEntry *e = pageAt(path);
        if (e && m_off.contains(e->runId))
            return u"is switched off on this device"_s;
        return {};
    };
    m_host = std::make_unique<extensions::NotebookHost>(*m_vault, *m_index, options);
    m_host->onExtensionTimer = [this](const QString &extension, const extensions::TimerSpec &spec) {
        if (spec.seconds > 0)
            startTimerFor(extension, spec);
        else
            stopTimer(extension);
    };
    // A setting changed (its dialog, b.setSetting): the extension's macros are computed again.
    m_host->onSettingChanged = [this](const QString &extension) {
        if (!m_macros.extensionChanged(extension).isEmpty())
            scheduleMacros();
    };
    // b.app: the app answers (AppScripting through AppController); the UI items and themes the
    // engine keeps are shown after the call.
    m_host->onApp = [this](const QString &extension, const QString &operation, const QVariantMap &args,
                           QString *error) -> QVariant {
        if (!m_appHandler) {
            *error = u"the app is not here"_s;
            return {};
        }
        return m_appHandler(extension, operation, args, error);
    };
    m_host->onAppChanged = [this](const QString &) { scheduleAppChanged(); };
    // b.notify: the app's notification center shows it, titled with the extension's name.
    m_host->onNotify = [this](const QString &extension, const extensions::NotifySpec &spec) -> QString {
        if (!m_notifyHandler)
            return u"not available here"_s;
        return m_notifyHandler(extension, title(extension), spec.title, spec.text, spec.page);
    };
    m_host->onLog = [this](const QString &extension, const QString &text) {
        if (m_printCapture && m_printCapture->size() < 1000)
            m_printCapture->append(text);
        qCInfo(lcApp, "extension %s: %s", qUtf8Printable(extension), qUtf8Printable(text));
        m_console->append(u"print"_s, text, extension == kConsole ? QString() : title(extension));
    };
    // b.aiModel: the model this device's b.ai asks (the assistant gives a small one on the device
    // fewer tools) — of the provider on the network, or the model on the device with its facts.
    m_host->onAiModel = [this]() -> QVariantMap {
        if (m_aiSettings.provider != u"local")
            return ai::modelInfo(m_aiSettings, nullptr);
        std::optional<ai::LocalModel> model;
        if (m_localAi)
            model = m_localAi->model(m_aiSettings.model);
        else if (const ai::LocalModel *known = ai::localModel(m_aiSettings.model))
            model = *known;
        return ai::modelInfo(m_aiSettings, model ? &*model : nullptr,
                             model && m_localAi ? m_localAi->contextTokens(*model) : 0);
    };
    // b.status: the chat shows what its turn does; other calls say nothing that way (yet).
    m_host->onStatus = [this](const QString &, const QString &text) {
        if (!m_statusChat.isEmpty())
            Q_EMIT chatStatus(m_statusChat, text);
    };
    m_engine = std::make_unique<extensions::Engine>();
    QString error;
    if (!m_engine->start(m_host.get(), m_limits, &error)) {
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
    // "open": once per notebook opened, after every extension that runs here has loaded.
    if (!m_opened) {
        m_opened = true;
        QTimer::singleShot(0, this, [this] {
            if (!m_vault)
                return;
            runHooks(u"open"_s,
                     {{u"date"_s, (m_clock ? m_clock().date() : QDate::currentDate()).toString(Qt::ISODate)}});
            // Timers that ended while the app was closed: their extensions' "timer" hooks, late.
            const auto late = std::exchange(m_lateTimers, {});
            for (const auto &[extension, spec] : late)
                runHooks(u"timer"_s, {{u"label"_s, spec.label}, {u"seconds"_s, spec.seconds}, {u"late"_s, true}},
                         extension);
        });
    }
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
        // an import, another program, an extension's write) waits for approval — and so does a
        // save the editor made over other code than the approved: a change that reached the file
        // a moment before the save is merged into it (PageController::save), and code that was
        // merged in is not code somebody typed here.
        const auto saved = m_editorSaves.constFind(path);
        if (old->page.hash != entry.page.hash && isApproved(old->page.hash) && saved != m_editorSaves.constEnd()
            && saved->text == contentKey(content) && saved->baseHash == old->page.hash
            && std::ranges::all_of(entry.page.permissions,
                                   [&](const QString &p) { return old->page.permissions.contains(p); })) {
            addApproved(entry.page.hash);
            rememberApproved(entry);
        }
        if (old->page.hash == entry.page.hash && old->page.source == entry.page.source && old->page.title == entry.page.title
            && old->page.description == entry.page.description && old->page.problems == entry.page.problems
            && old->runId == entry.runId)
            return false;
        entry.state = old->state;
        if (old->typedHash == entry.page.hash) { // (prose changed: the code's warnings stay)
            entry.types = old->types;
            entry.typedHash = old->typedHash;
        }
    }
    const bool typed = entry.typedHash == entry.page.hash;
    m_pages.insert(path, entry);
    if (!typed)
        checkTypes(path);
    return true;
}

void WorkspaceExtensions::checkTypes(const QString &path)
{
    const PageEntry *e = pageAt(path);
    if (!e || e->page.blocks == 0 || !extensions::typeCheckerAvailable())
        return;
    const QString source = e->page.source, hash = e->page.hash, title = e->page.title;
    const quint64 generation = m_generation;
    const QPointer<WorkspaceExtensions> self(this);
    // What the checker found, back on the GUI thread: for the code it read, if it is still there.
    const auto apply = [self, path, hash, title, generation](const QList<extensions::TypeProblem> &problems) {
        if (!self || generation != self->m_generation)
            return;
        const auto it = self->m_pages.find(paths::normalize(path));
        if (it == self->m_pages.end() || it->page.hash != hash)
            return; // (changed again meanwhile: its own check follows)
        it->types = problems;
        it->typedHash = hash;
        if (!problems.isEmpty())
            self->m_console->append(u"info"_s, extensions::describeTypeProblems(problems, title), title);
        self->refreshList();
        self->bumpRevision();
    };
#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
    // A worker of its own (a long page takes the checker some 100 ms, never the GUI thread's; each
    // thread keeps a checker: one is enough).
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.start([source, apply] {
        const QList<extensions::TypeProblem> problems = extensions::typeCheck(source);
        // (To the application's thread: `apply` asks there whether its extensions are still alive.)
        if (QCoreApplication *app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, [apply, problems] { apply(problems); }, Qt::QueuedConnection);
    });
#else
    QTimer::singleShot(0, this, [source, apply] { apply(extensions::typeCheck(source)); });
#endif
}

void WorkspaceExtensions::notesChanging(const QList<VaultChange> &changes)
{
    if (!m_index || !m_engine)
        return;
    const QList<extensions::HookInfo> hooks = m_engine->hooks();
    const bool listening = std::ranges::any_of(hooks, [](const extensions::HookInfo &h) {
        return h.event == u"change" || h.event == u"task" || h.event == u"journal";
    });
    if (!listening)
        return;
    const bool tasks = std::ranges::any_of(hooks, [](const extensions::HookInfo &h) { return h.event == u"task"; });
    // A change that arrives while a call executes is that call's write (the vault reports a write
    // while it is made; a sync, the editor and other programs change notes from the event loop).
    const QString writer = m_engine->runningExtension();
    for (const VaultChange &c : changes) {
        for (const QString &path : {c.path, c.oldPath}) {
            if (path.isEmpty() || !paths::isMarkdown(path) || m_changing.contains(path))
                continue;
            Changing changing;
            if (!writer.isEmpty()) {
                changing.origin = extensions::Origin::Extension;
                changing.by = writer;
                changing.depth = m_runningDepth + 1;
            } else {
                changing.origin = c.source == ChangeSource::Sync   ? extensions::Origin::Sync
                                  : c.source == ChangeSource::User ? extensions::Origin::App
                                                                   : extensions::Origin::Other;
            }
            const ParsedNote *note = m_index->note(path);
            changing.existed = note != nullptr;
            if (tasks && note) {
                changing.tasks = extensions::taskStates(*note, m_index->content(path));
                changing.tasksKnown = true;
            }
            m_changing.insert(path, changing);
        }
    }
}

void WorkspaceExtensions::notesChanged(const QStringList &changedPaths)
{
    if (!m_index)
        return;
    // Macros that read a changed note are computed again (their old value shows meanwhile).
    if (!m_macros.notesChanged(changedPaths).isEmpty())
        scheduleMacros();
    if (m_scanned) {
        for (const QString &path : changedPaths)
            if (!m_pendingPaths.contains(path))
                m_pendingPaths << path;
        m_pagesTimer.start();
    }
    // The hooks hear of it once the burst is over (events.h): every change but what an extension
    // wrote itself, which only its own hooks never hear — no echoes; a chain of hooks answering
    // hooks ends after ChangeBatch::kMaxDepth rounds.
    const bool hooked = m_engine && !m_engine->hooks().isEmpty();
    const QString writer = m_engine ? m_engine->runningExtension() : QString();
    for (const QString &path : changedPaths) {
        const Changing changing = m_changing.take(path);
        if (!hooked || !paths::isMarkdown(path))
            continue;
        const ParsedNote *note = m_index->note(path);
        extensions::NoteChange change;
        change.path = path;
        change.page = note ? note->title : paths::pageTitle(path);
        change.origin = changing.origin;
        change.by = changing.by;
        change.depth = changing.depth;
        if (change.by.isEmpty() && !writer.isEmpty()) { // (told without notesChanging(): a test's vault)
            change.origin = extensions::Origin::Extension;
            change.by = writer;
            change.depth = m_runningDepth + 1;
        }
        change.created = !changing.existed && note;
        change.deleted = !note;
        if (note && note->journalDate)
            change.date = note->journalDate->toString(Qt::ISODate);
        if (!m_batch.add(change))
            continue;
        if (changing.tasksKnown && note)
            m_batch.addTasks(change, extensions::changedTasks(changing.tasks,
                                                              extensions::taskStates(*note, m_index->content(path))));
    }
    m_changing.clear(); // (notesChanging() and notesChanged() come in pairs: nothing is left for later)
    if (!m_batch.isEmpty() || m_batch.dropped() > 0)
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

void WorkspaceExtensions::savedByEditor(const QString &path, const QString &content, const std::optional<QString> &base)
{
    // Only an extension page's own text matters (its trust): every other save costs a lookup.
    if (m_pages.contains(path)) {
        // The code the save is made over: the extension as the text before it reads. Over the
        // editor's own save before this one, which processPages() has not looked at yet, it is
        // the code that one was made over.
        QString baseHash;
        if (const auto before = m_editorSaves.constFind(path);
            base && before != m_editorSaves.constEnd() && before->text == contentKey(*base))
            baseHash = before->baseHash;
        else if (base)
            baseHash =
                    extensions::readPageExtension(
                            path, markdown::parseNote(path, *base, m_index ? m_index->config() : VaultConfig()), *base)
                            .hash;
        m_editorSaves.insert(path, EditorSave{contentKey(content), baseHash});
    }
    if (!m_engine || m_engine->hooks(u"save"_s).isEmpty() || !paths::isMarkdown(path))
        return;
    QVariantMap event{{u"page"_s, paths::pageTitle(path)}, {u"path"_s, path}};
    // After the save, not inside it (the editor is still writing).
    QTimer::singleShot(0, this, [this, event] { runHooks(u"save"_s, event); });
}

void WorkspaceExtensions::flushChanged()
{
    if (m_engine && m_engine->isRunning()) { // never inside another call
        m_changeTimer.start();
        return;
    }
    const extensions::ChangeBatch batch = std::exchange(m_batch, {});
    if (batch.dropped() > 0)
        m_console->append(
                u"info"_s,
                tr("Extensions answering each other's changes stopped after %1 rounds: %n change(s) not passed on.", "",
                   batch.dropped())
                        .arg(extensions::ChangeBatch::kMaxDepth));
    if (!m_engine || batch.isEmpty())
        return;
    // In the order of registration; each extension's hooks hear what the others changed.
    for (const extensions::HookInfo &h : m_engine->hooks()) {
        QList<QVariantMap> events;
        if (h.event == u"change") {
            if (const QVariantMap e = batch.changeEvent(h.extension); !e.isEmpty())
                events << e;
        } else if (h.event == u"task") {
            events = batch.taskEvents(h.extension);
        } else if (h.event == u"journal") {
            events = batch.journalEvents(h.extension);
        }
        for (const QVariantMap &e : std::as_const(events)) {
            if (!m_engine || m_engine->hooks(h.event).isEmpty())
                break;
            runHook(h, e, batch.depthFor(h.extension));
        }
    }
}

void WorkspaceExtensions::runHooks(const QString &event, const QVariantMap &arg, const QString &onlyExtension)
{
    if (!m_engine)
        return;
    if (m_engine->isRunning()) { // never inside another call
        QTimer::singleShot(0, this, [this, event, arg, onlyExtension] { runHooks(event, arg, onlyExtension); });
        return;
    }
    const QList<extensions::HookInfo> hooks = m_engine->hooks(event);
    for (const extensions::HookInfo &h : hooks)
        if (onlyExtension.isEmpty() || h.extension == onlyExtension)
            runHook(h, arg, 0);
}

void WorkspaceExtensions::runHook(const extensions::HookInfo &h, const QVariantMap &arg, int depth)
{
    if (!m_engine)
        return;
    m_current = CallContext{};
    m_current.hook = true;
    m_current.hookId = h.id;
    m_current.hookEvent = h.event;
    m_current.extension = h.extension;
    m_current.depth = depth;
    const int before = std::exchange(m_runningDepth, depth);
    handleResult(m_engine->runHook(h.id, arg));
    m_runningDepth = before;
    m_current = CallContext{};
}

void WorkspaceExtensions::checkDay()
{
    const QDate today = m_clock ? m_clock().date() : QDate::currentDate();
    if (!today.isValid() || today == m_day)
        return;
    const QDate previous = std::exchange(m_day, today);
    runHooks(u"day"_s, {{u"date"_s, today.toString(Qt::ISODate)}, {u"previous"_s, previous.toString(Qt::ISODate)}});
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
        ask(r.call, ctx.chat, request);
        return;
    }
    if (r.call)
        m_calls.remove(r.call);
    if (!ctx.chat.isEmpty())
        chatResult(ctx.chat, r);
    else if (ctx.console)
        consoleResult(r, ctx.toast);
    else if (ctx.hook)
        hookResult(ctx, r);
    else
        handle(r, ctx);
}

void WorkspaceExtensions::setAi(const ai::Settings &settings)
{
    const bool changed = settings.provider != m_aiSettings.provider || settings.model != m_aiSettings.model
                         || settings.baseUrl != m_aiSettings.baseUrl || settings.apiKey != m_aiSettings.apiKey;
    m_aiSettings = settings;
    if (changed)
        Q_EMIT aiSettingsChanged();
}

void WorkspaceExtensions::setLocalAi(ai::LocalBackend *backend)
{
    m_localAi = backend;
    if (m_aiClient)
        m_aiClient->setLocal(backend);
}

void WorkspaceExtensions::setAiPictures(ai::ImageLoader loader)
{
    m_aiPictures = std::move(loader);
}

void WorkspaceExtensions::setLimits(const extensions::Limits &limits)
{
    m_limits = limits;
    if (m_engine)
        m_engine->setLimits(limits);
}

/// The client of b.ai's requests: the network for the providers there, the app's model runner
/// for the model on the device.
ai::Client *WorkspaceExtensions::aiClient()
{
    if (!m_aiClient) {
        m_transport = std::make_unique<sync::NetworkHttpTransport>();
        m_aiClient = std::make_unique<ai::Client>(m_transport.get());
        m_aiClient->setLocal(m_localAi);
    }
    return m_aiClient.get();
}

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
    } else if (!ctx.hook && ctx.chat.isEmpty()) { // (the chat shows its own dots)
        Q_EMIT message(tr("Asking AI…"), u"info"_s);
    }
    QString error;
    const std::optional<ai::Request> r = ai::requestFrom(request, &error, m_aiPictures);
    const quint64 generation = m_generation;
    if (!r) {
        QTimer::singleShot(0, this, [this, call, error, generation] { finishAi(call, {{u"error"_s, error}}, generation); });
        return;
    }
    const QPointer<WorkspaceExtensions> self(this);
    // A chat's turn shows the answer while it is written: each request of its loop anew.
    ai::PartialCallback partial;
    if (!ctx.chat.isEmpty()) {
        Q_EMIT chatPartial(ctx.chat, QString(), QString(), 0, 0);
        partial = [self, chat = ctx.chat](const ai::Partial &p) {
            if (self)
                Q_EMIT self->chatPartial(chat, p.text, p.thoughts, p.tokens, p.writingMs);
        };
    }
    m_aiRequests.insert(call, aiClient()->send(
                                      m_aiSettings, *r,
                                      [self, call, generation](const ai::Reply &reply) {
                                          if (self)
                                              self->finishAi(call, reply.toVariant(), generation);
                                      },
                                      partial));
}

void WorkspaceExtensions::stopAiRequests()
{
    const QHash<int, ai::Pending> requests = std::exchange(m_aiRequests, {});
    for (ai::Pending request : requests)
        request.stop();
}

void WorkspaceExtensions::finishAi(int call, const QVariantMap &reply, quint64 generation)
{
    if (generation == m_generation)
        m_aiRequests.remove(call);
    m_aiPending = std::max(0, m_aiPending - 1);
    Q_EMIT aiPendingChanged();
    if (generation != m_generation || !m_engine || !m_engine->waitingCalls().contains(call)) {
        // Its extension was loaded again, or the notebook closed. A chat's turn says so (a
        // stopped one is no longer among the calls: nothing).
        // (A new engine said so in reload().)
        const CallContext ctx = m_calls.take(call);
        if (!ctx.chat.isEmpty() && generation == m_generation)
            Q_EMIT chatFailed(ctx.chat, tr("The AI assistant was loaded again meanwhile: try again."));
        return;
    }
    if (m_engine->isRunning()) {
        ++m_aiPending;
        QTimer::singleShot(0, this, [this, call, reply, generation] { finishAi(call, reply, generation); });
        return;
    }
    handleResult(resumeCall(call, reply));
}

extensions::Result WorkspaceExtensions::resumeCall(int call, const QVariant &value)
{
    const CallContext ctx = m_calls.value(call);
    const QString before = std::exchange(m_statusChat, ctx.chat);
    const int depth = std::exchange(m_runningDepth, ctx.depth);
    const extensions::Result r = m_engine->resume(call, value);
    m_runningDepth = depth;
    m_statusChat = before;
    return r;
}

// ---- the AI chat ---------------------------------------------------------------------------------

QVariantList WorkspaceExtensions::chatPartners() const
{
    QVariantList out{QVariantMap{
            {u"id"_s, QString()}, {u"label"_s, title(u"asystent"_s)}, {u"extension"_s, title(u"asystent"_s)}}};
    for (const extensions::CommandInfo &c : commands()) {
        if (!c.chat || (c.extension == u"asystent" && c.name == u"chat"))
            continue;
        out.append(QVariantMap{{u"id"_s, QString(c.extension + u'/' + c.name)},
                               {u"label"_s, c.label},
                               {u"extension"_s, title(c.extension)}});
    }
    return out;
}

QString WorkspaceExtensions::partnerLabel(const QString &partner) const
{
    if (partner.trimmed().isEmpty())
        return title(u"asystent"_s);
    const QString extension = partner.section(u'/', 0, 0).trimmed();
    const QString name = partner.section(u'/', 1).trimmed();
    for (const extensions::CommandInfo &c : commands()) {
        if (c.chat && c.extension == extension && (name.isEmpty() || c.name == name))
            return c.label;
    }
    return {};
}

bool WorkspaceExtensions::runChat(const QString &path, const QVariantList &messages, const QString &page,
                                  const QString &partner, int omitted)
{
    // Who answers: the assistant's "chat", or an extension's chat command ("x/y", "x": its first).
    QString extension = u"asystent"_s, command = u"chat"_s;
    if (!partner.trimmed().isEmpty()) {
        extension = partner.section(u'/', 0, 0).trimmed();
        command = partner.section(u'/', 1).trimmed();
        if (command.isEmpty() && m_engine) {
            for (const extensions::CommandInfo &c : m_engine->commands()) {
                if (c.chat && c.extension == extension) {
                    command = c.name;
                    break;
                }
            }
        }
    }
    const bool loaded = m_engine && m_engine->isLoaded(extension)
                        && std::ranges::any_of(m_engine->commands(), [&](const extensions::CommandInfo &c) {
                               return c.extension == extension && c.name == command;
                           });
    if (!loaded) {
        if (extension == u"asystent")
            Q_EMIT chatFailed(path, tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        else
            Q_EMIT chatFailed(
                    path,
                    tr("The chat partner “%1” is not running: switch its extension on, or choose another partner.")
                            .arg(partner));
        return false;
    }
    // (The conversation's own page by its name, as b.read names pages: its tools leave it out.)
    const QString conversation = path.endsWith(u".md"_s, Qt::CaseInsensitive) ? path.chopped(3) : path;
    // ctx.appTools: the app is here (b.app works) and the model is on the network — the built-in
    // assistant then offers its tools for the app itself; never to the model on the device, which
    // gets the tools it was trained on (docs/FINETUNE.md).
    const QVariantMap ctx{{u"page"_s, page},         {u"selection"_s, QString()},
                          {u"messages"_s, messages}, {u"conversation"_s, conversation},
                          {u"omitted"_s, omitted},   {u"appTools"_s, m_aiSettings.provider != u"local"}};
    // (Another conversation's question, shown or waiting, stays: turns run side by side.)
    m_current = CallContext{};
    m_current.chat = path;
    m_current.extension = extension;
    const QString before = std::exchange(m_statusChat, path);
    const extensions::Result r = m_engine->runCommand(extension, command, ctx);
    m_statusChat = before;
    handleResult(r);
    m_current = CallContext{};
    return true;
}

void WorkspaceExtensions::stopChat(const QString &path)
{
    if (!m_engine || path.isEmpty())
        return;
    const qsizetype waiting = m_waitingQuestions.size();
    m_waitingQuestions.removeIf([&path](const WaitingQuestion &q) { return q.chat == path; });
    if (m_waitingQuestions.size() != waiting) {
        ++m_chatQuestionSerial;
        Q_EMIT chatQuestionsChanged();
    }
    for (auto it = m_calls.begin(); it != m_calls.end();) {
        if (it->chat != path) {
            ++it;
            continue;
        }
        const int call = it.key();
        if (call == m_questionCall) { // (its question goes with it, unanswered)
            m_questionCall = 0;
            m_questionKind.clear();
            m_questionTitle.clear();
            m_questionOptions.clear();
            m_questionChat.clear();
            m_questionRequest.clear();
            ++m_questionSerial;
            Q_EMIT questionChanged();
        }
        m_engine->cancel(call); // (an answer on its way finds no call: finishAi drops it)
        if (auto request = m_aiRequests.find(call); request != m_aiRequests.end()) {
            request->stop(); // (the model stops writing, the provider's connection closes)
            m_aiRequests.erase(request);
        }
        it = m_calls.erase(it);
    }
    showWaitingQuestion();
    if (m_reconcilePending && !isWaiting())
        reconcile();
}

void WorkspaceExtensions::chatResult(const QString &path, const extensions::Result &r)
{
    switch (r.status) {
    case extensions::Status::Ok: {
        const QString text = r.value.metaType().id() == QMetaType::QString ? r.value.toString().trimmed() : QString();
        if (text.isEmpty())
            Q_EMIT chatFailed(path, tr("The AI assistant gave no answer."));
        else
            Q_EMIT chatAnswered(path, text);
        break;
    }
    case extensions::Status::NotFound:
        Q_EMIT chatFailed(path, tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        break;
    case extensions::Status::Timeout:
        Q_EMIT chatFailed(path, tr("The extension took too long and was stopped."));
        break;
    case extensions::Status::OutOfMemory:
        Q_EMIT chatFailed(path, tr("The extension needed too much memory and was stopped."));
        break;
    default: {
        qCWarning(lcApp, "AI chat: %s", qUtf8Printable(r.error));
        m_console->append(u"error"_s, r.error);
        // The model's or the provider's own words ("b.ai: no connection: …") without the script's
        // line before them.
        const QString first = r.error.section(u'\n', 0, 0);
        const qsizetype ai = first.indexOf(u"b.ai: "_s);
        Q_EMIT chatFailed(path, ai >= 0 ? tr("AI: %1").arg(first.mid(ai + 6)) : tr("The extension failed: %1").arg(first));
    }
    }
    Q_EMIT changed(); // (its tools may have written pages: views show them)
}

ai::Pending WorkspaceExtensions::sendAi(const ai::Request &request, std::function<void(const ai::Reply &)> done,
                                        ai::PartialCallback partial)
{
    ++m_aiPending;
    Q_EMIT aiPendingChanged();
    const QPointer<WorkspaceExtensions> self(this);
    return aiClient()->send(
            m_aiSettings, request,
            [self, done = std::move(done)](const ai::Reply &reply) {
                if (self) {
                    self->m_aiPending = std::max(0, self->m_aiPending - 1);
                    Q_EMIT self->aiPendingChanged();
                }
                if (done)
                    done(reply);
            },
            std::move(partial));
}

void WorkspaceExtensions::testAi()
{
    ai::Request request;
    request.messages << ai::textMessage(u"user"_s, u"Say OK."_s);
    request.maxTokens = 1024;
    // (Claude's effort; the model on the device answers without thinking first.)
    if (m_aiSettings.provider == u"anthropic" || m_aiSettings.provider == u"local")
        request.effort = u"low"_s;
    ++m_aiPending;
    Q_EMIT aiPendingChanged();
    const QPointer<WorkspaceExtensions> self(this);
    aiClient()->send(m_aiSettings, request, [self](const ai::Reply &reply) {
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
        if (m_live.contains(e.runId)) { // live code from a client runs instead (loadLive)
            e.state = u"live"_s;
            continue;
        }
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
    // Live code (loadLive) for this run only: over a page or a built-in of the same id.
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        extensions::Extension x;
        x.id = it.key();
        x.source = it->source;
        x.permissions = it->permissions;
        x.name = it->title.isEmpty() ? it.key() : it->title;
        if (extensions::builtinFor(it.key()) == it.key())
            x.data = extensions::builtinExtension(it.key()).data;
        want.insert(it.key(), x);
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
            // Loaded again when its code changed, or a page it requires (b.require) did; one that
            // failed to load is tried again whenever extension pages changed (a page it requires
            // may be there now).
            const QString key = loadKey(it.value()) + u'#' + moduleKey(it.key());
            if (m_loadedKeys.value(it.key()) == key && !m_loadErrors.contains(it.key()))
                continue;
            loaded = true;
            m_hookFailures.clear();
            const extensions::Result r = m_engine->load(it.value());
            m_loadedKeys.insert(it.key(), loadKey(it.value()) + u'#' + moduleKey(it.key()));
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
    for (auto it = m_live.begin(); it != m_live.end(); ++it) {
        const QString error = m_loadErrors.value(it.key());
        it->error = error.section(u'\n', 0, 0);
        it->errorLine =
                error.isEmpty() ? -1 : extensions::errorLine(it->title.isEmpty() ? it.key() : it->title, it->error);
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
    macrosReset(); // a page approved, switched or changed: what its macros show
    bumpRevision();
}

QString WorkspaceExtensions::moduleKey(const QString &id) const
{
    if (!m_engine)
        return {};
    QStringList parts;
    for (const QString &path : m_engine->modulesOf(id)) {
        const PageEntry *e = pageAt(path);
        if (!e) {
            parts << path + u"=gone"_s;
            continue;
        }
        parts << path + u'=' + e->page.hash + (isApproved(e->page.hash) ? u"+"_s : u"-"_s)
                         + (m_off.contains(e->runId) ? u"off"_s : QString());
    }
    return parts.join(u'|');
}

void WorkspaceExtensions::refreshList()
{
    const auto counts = [&](const QString &id, ExtensionRow &row) {
        if (!m_engine || !m_engine->isLoaded(id))
            return;
        row.settings = int(m_engine->settingsOf(id).size());
        for (const QString &module : m_engine->modulesOf(id)) {
            const PageEntry *e = pageAt(module);
            row.modules << (e ? e->page.title : paths::pageTitle(module));
        }
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
            out << (p == u"write" ? tr("change notes")
                    : p == u"ai"  ? tr("use AI")
                    : p == u"app" ? tr("change the app")
                                  : p);
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
        if (on && m_live.contains(b.extension.id))
            row.state = u"live"_s;
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
        row.types = int(e->types.size());
        if (!e->types.isEmpty())
            row.typeMessage = tr("line %1: %2").arg(e->types.first().line).arg(e->types.first().message);
        if (e->state == u"running" || e->state == u"error")
            counts(e->runId, row);
        rows << row;
    }
    // Live code (loadLive): a row each, after the pages; "live" in the rows it shadows.
    QStringList liveIds = m_live.keys();
    liveIds.sort();
    for (const QString &id : std::as_const(liveIds)) {
        const LiveEntry &l = m_live[id];
        ExtensionRow row;
        row.id = id;
        row.title = l.title.isEmpty() ? id : l.title;
        row.kind = u"live"_s;
        row.state = l.error.isEmpty() ? u"running"_s : u"error"_s;
        row.message = l.error;
        row.errorLine = l.errorLine;
        row.permissions = permissionsText(l.permissions);
        row.description = l.who.mid(5); // "live:" left out: who sent it
        counts(id, row);
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

int WorkspaceExtensions::pageTypeCount(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? int(e->types.size()) : 0;
}

QString WorkspaceExtensions::pageTypeMessage(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    if (!e || e->types.isEmpty())
        return {};
    return tr("line %1: %2").arg(e->types.first().line).arg(e->types.first().message);
}

int WorkspaceExtensions::pageTypeLine(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e && !e->types.isEmpty() ? e->types.first().line - 1 : -1;
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
    approveVersion(path, e->page.hash);
}

QString WorkspaceExtensions::approveVersion(const QString &path, const QString &hash)
{
    const PageEntry *e = pageAt(path);
    if (!e)
        return u"gone"_s;
    // The version shown, or none: code a sync replaced while it was read is not approved unread.
    if (e->page.hash != hash)
        return u"changed"_s;
    addApproved(e->page.hash);
    rememberApproved(*e);
    m_off.remove(e->runId);
    storeTrust();
    m_console->append(u"info"_s, tr("Approved on this device: %1").arg(e->page.title));
    reconcile();
    return {};
}

void WorkspaceExtensions::rememberApproved(const PageEntry &entry)
{
    LocalStore *store = m_vault ? m_vault->store() : nullptr;
    if (!store)
        return;
    QJsonObject code{{u"hash"_s, entry.page.hash},
                     {u"permissions"_s, QJsonArray::fromStringList(entry.page.permissions)},
                     {u"title"_s, entry.page.title}};
    if (entry.page.code.size() <= extensions::kMaxApprovedCode)
        code.insert(u"code"_s, entry.page.code);
    store->set(extensions::approvedCodeStoreKey(entry.runId), code);
}

QVariantMap WorkspaceExtensions::approvalReview(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    if (!e)
        return {};
    const QVariantMap before = extensions::approvedCode(m_vault ? m_vault->store() : nullptr, e->runId);
    const QStringList earlier = before.value(u"permissions"_s).toStringList();
    const auto permissionText = [](const QString &p) {
        return p == u"write" ? tr("change notes") : p == u"ai" ? tr("use AI") : p == u"app" ? tr("change the app") : p;
    };
    QStringList permissions, added;
    for (const QString &p : e->page.permissions) {
        permissions << permissionText(p);
        if (!before.isEmpty() && !earlier.contains(p))
            added << permissionText(p);
    }
    // Against the code approved before (a built-in's own code when the page overrides one and was
    // never approved here); all of it when there is none.
    QString old = before.value(u"code"_s).toString();
    bool compared = before.contains(u"code"_s);
    if (!compared && !e->builtin.isEmpty() && before.isEmpty()) {
        // (The built-in's source as "Edit the code" writes it: only what was changed shows.)
        const QString page = extensions::builtinSourcePage(extensions::builtinExtension(e->builtin), m_language);
        old = extensions::readPageExtension(e->page.path, markdown::parseNote(e->page.path, page), page).code;
        compared = true;
    }
    QVariantList lines;
    int addedLines = 0, removedLines = 0;
    for (const extensions::CodeDiffLine &l : extensions::codeDiff(compared ? old : QString(), e->page.code)) {
        static const QStringList kinds{u"same"_s, u"added"_s, u"removed"_s, u"skipped"_s};
        addedLines += l.kind == extensions::CodeDiffLine::Added;
        removedLines += l.kind == extensions::CodeDiffLine::Removed;
        lines << QVariantMap{
                {u"kind"_s, kinds.value(int(l.kind))}, {u"text"_s, l.text}, {u"line"_s, l.line}, {u"count"_s, l.count}};
    }
    return {{u"path"_s, e->page.path},
            {u"title"_s, e->page.title},
            {u"hash"_s, e->page.hash},
            {u"approved"_s, isApproved(e->page.hash)},
            {u"permissions"_s, permissions},
            {u"newPermissions"_s, added},
            {u"before"_s, compared},
            {u"builtin"_s, !e->builtin.isEmpty() && before.isEmpty() ? title(e->builtin) : QString()},
            {u"lines"_s, lines},
            {u"added"_s, addedLines},
            {u"removed"_s, removedLines},
            {u"oldCode"_s, compared ? old : QString()},
            {u"code"_s, e->page.code},
            {u"description"_s, e->page.description}};
}

QString WorkspaceExtensions::pageHash(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? e->page.hash : QString();
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
        PageEntry entry;
        entry.page = extensions::readPageExtension(created.path, note, content);
        entry.runId = extensions::runIdOf(entry.page);
        addApproved(entry.page.hash);
        rememberApproved(entry);
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
        PageEntry entry;
        entry.page = extensions::readPageExtension(created.path, note, content);
        entry.runId = extensions::runIdOf(entry.page);
        addApproved(entry.page.hash);
        rememberApproved(entry);
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
    if (const auto live = m_live.constFind(extension); live != m_live.constEnd() && !live->title.isEmpty())
        return live->title; // live code, as its client named it
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

void WorkspaceExtensions::runCommand(const QString &extension, const QString &name, const QString &path, int line,
                                     const QVariantMap &args, const QVariantMap &context)
{
    if (!m_engine)
        return;
    // The editor's selection, when it offered one for this page (offerSelection()).
    // (The journal's stream is no page on screen: the day the editor is in offers it.)
    const QString selection =
            !m_selection.isEmpty() && (path.isEmpty() || paths::normalize(m_selectionPath) == paths::normalize(path))
                    ? m_selection
                    : QString();
    clearSelection();
    QVariantMap ctx{{u"page"_s, path.isEmpty() ? QString() : paths::pageTitle(path)},
                    {u"selection"_s, selection},
                    {u"args"_s, withSettings(extension, args)}};
    if (!path.isEmpty() && line >= 0) {
        const QVariantMap block = m_host->blockAt(path, line);
        if (!block.isEmpty())
            ctx.insert(u"block"_s, block);
    }
    // A slot's own fields (the folder tree's row, what is shared), never one of the above.
    for (const QString &key : {u"folder"_s, u"path"_s, u"shared"_s}) {
        if (context.contains(key))
            ctx.insert(key, context.value(key));
    }
    cancelCommandQuestion();
    m_current = CallContext{};
    m_current.extension = extension;
    handleResult(m_engine->runCommand(extension, name, ctx));
    m_current = CallContext{};
}

void WorkspaceExtensions::runBlockCommand(const QString &command, const QString &path, int line,
                                          const QString &selection)
{
    if (command == u"*/run") {
        runCodeAt(path, line);
        return;
    }
    const qsizetype slash = command.indexOf(u'/');
    if (slash <= 0)
        return;
    offerSelection(path, selection);
    runCommand(command.left(slash), command.mid(slash + 1), path, line);
}

void WorkspaceExtensions::offerSelection(const QString &path, const QString &text)
{
    // (What a command gets of a selection of a whole book: its beginning.)
    constexpr qsizetype kMaxSelection = 200000;
    m_selectionPath = path;
    qsizetype end = std::min(text.size(), kMaxSelection);
    if (end < text.size() && end > 0 && text[end - 1].isHighSurrogate())
        --end; // (never half a letter)
    m_selection = text.left(end);
}

void WorkspaceExtensions::clearSelection()
{
    m_selectionPath.clear();
    m_selection.clear();
}

QVariantMap WorkspaceExtensions::render(const QString &extension, const QString &view, const QVariantMap &params, QString *error)
{
    if (!m_engine || isWaiting())
        return {};
    const extensions::Result r = m_engine->render(extension, view, withSettings(extension, params));
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
    cancelCommandQuestion();
    m_current = CallContext{};
    m_current.fromAction = true;
    m_current.extension = extension;
    m_current.view = view;
    const extensions::Result r = m_engine->act(extension, view, action, withSettings(extension, args));
    handleResult(r);
    m_current = CallContext{};
    return r.ok() || r.status == extensions::Status::Suspended;
}

void WorkspaceExtensions::handle(const extensions::Result &result, const CallContext &ctx)
{
    const bool fromAction = ctx.fromAction;
    switch (result.status) {
    case extensions::Status::Suspended:
        setQuestion(result.value.toMap());
        return;
    case extensions::Status::Ok:
        showResult(result.value, ctx);
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

void WorkspaceExtensions::showResult(const QVariant &value, const CallContext &ctx)
{
    if (value.metaType().id() == QMetaType::QString) {
        if (!value.toString().isEmpty())
            Q_EMIT message(value.toString(), u"info"_s);
        return;
    }
    if (value.metaType().id() != QMetaType::QVariantMap)
        return; // anything else is silent (the CLI's JSON of a command run for it)
    const QVariantMap map = value.toMap();
    const QString text = map.value(u"message"_s).toString();
    if (text.isEmpty())
        return;
    const QString tone = map.value(u"tone"_s).toString();
    const QString kind = tone == u"success" || tone == u"error" || tone == u"warning" ? tone : u"info"_s;
    const QVariantMap b = map.value(u"button"_s).toMap();
    Button button;
    button.extension = ctx.extension;
    button.args = b.value(u"args"_s).toMap();
    const QString label = b.value(u"label"_s).toString();
    if (!b.value(u"action"_s).toString().isEmpty() && !ctx.view.isEmpty()) {
        button.view = ctx.view;
        button.action = b.value(u"action"_s).toString();
    } else if (!b.value(u"command"_s).toString().isEmpty()) {
        button.command = b.value(u"command"_s).toString();
    } else if (!b.value(u"page"_s).toString().isEmpty()) {
        button.page = b.value(u"page"_s).toString();
    }
    if (label.isEmpty() || (button.action.isEmpty() && button.command.isEmpty() && button.page.isEmpty())) {
        Q_EMIT message(text, kind);
        return;
    }
    const int id = ++m_lastButton;
    m_buttons.insert(id, button);
    // (A burst of results keeps at most the last 64 buttons: toasts go long before that.)
    if (m_buttons.size() > 64)
        m_buttons.remove(id - 64);
    Q_EMIT messageWithButton(text, kind, label, id);
}

void WorkspaceExtensions::pressButton(int id)
{
    const Button button = m_buttons.take(id);
    if (!button.page.isEmpty())
        Q_EMIT pageRequested(button.page);
    else if (!button.action.isEmpty())
        act(button.extension, button.view, button.action, button.args);
    else if (!button.command.isEmpty())
        runCommand(button.extension, button.command, QString(), -1, button.args);
}

void WorkspaceExtensions::forgetButton(int id) { m_buttons.remove(id); }

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
    console.permissions = {u"ai"_s, u"app"_s, u"write"_s}; // what the user types is theirs
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
    cancelCommandQuestion();
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

void WorkspaceExtensions::ask(int call, const QString &chat, const QVariantMap &request)
{
    if (!m_questionKind.isEmpty()) {
        if (!chat.isEmpty()) {
            // A turn's question waits behind the one shown: never cancelled by it.
            m_waitingQuestions.append({call, chat, request});
            ++m_chatQuestionSerial;
            Q_EMIT chatQuestionsChanged();
            return;
        }
        if (!m_questionChat.isEmpty()) {
            // A command asks while a turn's question is shown: that one waits (first) meanwhile.
            m_waitingQuestions.prepend({m_questionCall, m_questionChat, m_questionRequest});
            ++m_chatQuestionSerial;
            Q_EMIT chatQuestionsChanged();
        } else {
            cancelQuestion(); // one command's question at a time: an older one is answered "cancelled"
        }
    }
    m_questionCall = call;
    m_questionChat = chat;
    m_questionRequest = request;
    setQuestion(request);
}

void WorkspaceExtensions::showWaitingQuestion()
{
    if (!m_questionKind.isEmpty() || m_waitingQuestions.isEmpty() || !m_engine)
        return;
    const WaitingQuestion next = m_waitingQuestions.takeFirst();
    ++m_chatQuestionSerial;
    Q_EMIT chatQuestionsChanged();
    m_questionCall = next.call;
    m_questionChat = next.chat;
    m_questionRequest = next.request;
    setQuestion(next.request);
}

void WorkspaceExtensions::cancelCommandQuestion()
{
    if (!m_questionKind.isEmpty() && m_questionChat.isEmpty())
        cancelQuestion();
}

QVariantMap WorkspaceExtensions::chatQuestion(const QString &path, int) const
{
    if (path.isEmpty())
        return {};
    const auto shape = [](const QVariantMap &request) {
        const QString kind = request.value(u"kind"_s).toString();
        return QVariantMap{{u"kind"_s, kind},
                           {u"title"_s, request.value(kind == u"form" ? u"title"_s : u"prompt"_s).toString()},
                           {u"options"_s, request.value(u"options"_s).toStringList()}};
    };
    if (!m_questionKind.isEmpty() && m_questionChat == path)
        return shape(m_questionRequest);
    for (const WaitingQuestion &q : m_waitingQuestions) {
        if (q.chat == path)
            return shape(q.request);
    }
    return {};
}

QString WorkspaceExtensions::chatQuestionKind(const QString &path, int serial) const
{
    return chatQuestion(path, serial).value(u"kind"_s).toString();
}

QString WorkspaceExtensions::chatQuestionTitle(const QString &path, int serial) const
{
    return chatQuestion(path, serial).value(u"title"_s).toString();
}

QStringList WorkspaceExtensions::chatQuestionOptions(const QString &path, int serial) const
{
    return chatQuestion(path, serial).value(u"options"_s).toStringList();
}

void WorkspaceExtensions::answerChat(const QString &path, const QString &option)
{
    if (!m_engine || path.isEmpty())
        return;
    if (!m_questionKind.isEmpty() && m_questionChat == path) {
        answerChoice(option);
        return;
    }
    for (qsizetype i = 0; i < m_waitingQuestions.size(); ++i) {
        const WaitingQuestion q = m_waitingQuestions.at(i);
        if (q.chat != path)
            continue;
        if (q.request.value(u"kind"_s).toString() == u"choose"
            && !q.request.value(u"options"_s).toStringList().contains(option))
            return;
        m_waitingQuestions.removeAt(i);
        ++m_chatQuestionSerial;
        Q_EMIT chatQuestionsChanged();
        handleResult(resumeCall(q.call, option));
        showWaitingQuestion();
        if (m_reconcilePending && !isWaiting())
            reconcile();
        return;
    }
}

void WorkspaceExtensions::answer(const QVariant &value)
{
    if (!m_engine || m_questionKind.isEmpty())
        return;
    const int call = std::exchange(m_questionCall, 0);
    m_questionKind.clear();
    m_questionTitle.clear();
    m_questionOptions.clear();
    m_questionChat.clear();
    m_questionRequest.clear();
    Q_EMIT questionChanged();
    handleResult(resumeCall(call, value));
    showWaitingQuestion();
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
    extensions::TimerSpec spec;
    spec.seconds = seconds;
    spec.label = label;
    startTimerFor(u"silownia"_s, spec);
}

void WorkspaceExtensions::startTimerFor(const QString &extension, const extensions::TimerSpec &spec)
{
    RunningTimer t;
    t.spec = spec;
    t.spec.seconds = std::max(1, spec.seconds);
    t.remaining = t.spec.seconds;
    t.end = QDateTime::currentDateTimeUtc().addSecs(t.spec.seconds);
    // Its end armed on the platform now (the same id: a running one's moves): with the app closed
    // it still comes where the platform can (Android).
    t.native = m_timerArm && m_vault ? m_timerArm(timerNotification(extension, t)) : false;
    m_timers.insert(extension, t); // (one per extension: a new one replaces it)
    storeTimers();
    if (extension == u"silownia") {
        m_restTotal = t.spec.seconds;
        m_restRemaining = m_restTotal;
        m_restLabel = spec.label.isEmpty() ? tr("Rest") : spec.label;
        Q_EMIT restChanged();
    }
    m_restTimer.start();
    Q_EMIT timersChanged();
}

QString WorkspaceExtensions::restText() const
{
    return u"%1:%2"_s.arg(m_restRemaining / 60).arg(m_restRemaining % 60, 2, 10, QChar(u'0'));
}

void WorkspaceExtensions::stopRest()
{
    stopTimer(u"silownia"_s);
}

void WorkspaceExtensions::stopTimer(const QString &extension)
{
    if (!m_timers.remove(extension))
        return;
    if (m_timerCancel && m_vault)
        m_timerCancel(timerNotificationId(extension));
    storeTimers();
    if (extension == u"silownia") {
        m_restRemaining = 0;
        Q_EMIT restChanged();
    }
    if (m_timers.isEmpty())
        m_restTimer.stop();
    Q_EMIT timersChanged();
}

QVariantList WorkspaceExtensions::timers() const
{
    QList<std::pair<QString, const RunningTimer *>> running;
    for (auto it = m_timers.cbegin(); it != m_timers.cend(); ++it)
        running.append({it.key(), &it.value()});
    std::ranges::sort(running, [](const auto &a, const auto &b) {
        return a.second->end != b.second->end ? a.second->end < b.second->end : a.first < b.first;
    });
    QVariantList out;
    for (const auto &[extension, t] : std::as_const(running)) {
        const QString name = title(extension);
        out << QVariantMap{{u"extension"_s, extension},
                           {u"title"_s, name},
                           {u"label"_s, t->spec.label.isEmpty() ? name : t->spec.label},
                           {u"remaining"_s, t->remaining},
                           {u"total"_s, t->spec.seconds},
                           {u"text"_s, u"%1:%2"_s.arg(t->remaining / 60).arg(t->remaining % 60, 2, 10, QChar(u'0'))}};
    }
    return out;
}

void WorkspaceExtensions::tickRest()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool changedAny = false;
    QStringList ended;
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
        const qint64 left = (now.msecsTo(it->end) + 999) / 1000;
        if (left <= 0) {
            ended << it.key();
            continue;
        }
        if (left != it->remaining) {
            it->remaining = int(left);
            changedAny = true;
            if (it.key() == u"silownia") {
                m_restRemaining = int(left);
                Q_EMIT restChanged();
            }
        }
    }
    ended.sort();
    for (const QString &extension : std::as_const(ended))
        timerEnded(extension);
    if (m_timers.isEmpty())
        m_restTimer.stop();
    if (changedAny && ended.isEmpty())
        Q_EMIT timersChanged();
}

void WorkspaceExtensions::timerEnded(const QString &extension)
{
    const RunningTimer t = m_timers.take(extension);
    storeTimers();
    if (extension == u"silownia") {
        m_restRemaining = 0;
        Q_EMIT restChanged();
    }
    Q_EMIT timersChanged();
    // Its own words: the label and the message the extension gave, else the app's.
    const QString label = t.spec.label.isEmpty() ? title(extension) : t.spec.label;
    const auto [heading, body] = timerWords(extension, t.spec);
    // The platform shows an end it armed exactly by itself (Android); else the app shows it now
    // — and takes back what was armed late (an inexact alarm without the permission).
    if (!t.native) {
        if (m_timerCancel && m_vault)
            m_timerCancel(timerNotificationId(extension));
        if (m_notify)
            m_notify(heading, body);
    }
    Q_EMIT message(!t.spec.message.isEmpty() ? t.spec.message : tr("%1 is over: %2").arg(label, body), u"info"_s);
    runHooks(u"timer"_s, {{u"label"_s, t.spec.label}, {u"seconds"_s, t.spec.seconds}, {u"late"_s, false}}, extension);
}

QString WorkspaceExtensions::timerNotificationId(const QString &extension) const
{
    return u"brulion-timer:"_s + (m_vault ? m_vault->id() : QString()) + u':' + extension;
}

std::pair<QString, QString> WorkspaceExtensions::timerWords(const QString &extension,
                                                            const extensions::TimerSpec &spec) const
{
    const QString label = spec.label.isEmpty() ? title(extension) : spec.label;
    const QString body = !spec.message.isEmpty()    ? spec.message
                         : extension == u"silownia" ? tr("Time for the next set.")
                                                    : tr("The timer of %1 has ended.").arg(title(extension));
    return {tr("%1 is over").arg(label), body};
}

ScheduledNotification WorkspaceExtensions::timerNotification(const QString &extension, const RunningTimer &timer) const
{
    ScheduledNotification n;
    n.id = timerNotificationId(extension);
    n.nativeId = nativeIdFor(n.id);
    const auto [heading, body] = timerWords(extension, timer.spec);
    n.title = heading.left(ScheduledNotification::MaxTitleLength);
    n.body = body;
    n.at = timer.end.toLocalTime(); // a moment (seconds and all), not a reminder's minute
    n.vault = m_vault ? m_vault->id() : QString();
    return n;
}

namespace {
const QString kTimersKey = u"extensions/timers"_s;
constexpr qint64 kLateTimerMs = 24ll * 60 * 60 * 1000; // older ends run no hooks
} // namespace

void WorkspaceExtensions::storeTimers()
{
    LocalStore *store = m_vault ? m_vault->store() : nullptr;
    if (!store)
        return;
    QJsonObject timers;
    for (auto it = m_timers.cbegin(); it != m_timers.cend(); ++it) {
        timers.insert(it.key(), QJsonObject{{u"label"_s, it->spec.label},
                                            {u"message"_s, it->spec.message},
                                            {u"seconds"_s, it->spec.seconds},
                                            {u"end"_s, it->end.toMSecsSinceEpoch()},
                                            {u"native"_s, it->native}});
    }
    if (timers.isEmpty())
        store->remove(kTimersKey);
    else
        store->set(kTimersKey, timers);
}

void WorkspaceExtensions::loadTimers()
{
    m_lateTimers.clear();
    LocalStore *store = m_vault ? m_vault->store() : nullptr;
    if (!store)
        return;
    const QJsonObject stored = store->get(kTimersKey).toObject();
    if (stored.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        RunningTimer t;
        t.spec.label = o.value(u"label"_s).toString().left(100);
        t.spec.message = o.value(u"message"_s).toString().left(300);
        t.spec.seconds = std::clamp(o.value(u"seconds"_s).toInt(), 1, 86400);
        const qint64 end = o.value(u"end"_s).toInteger();
        // (A hand-edited state: an end further away than a timer can last is no timer.)
        if (end <= 0 || end > now + 86'400'000ll + 60'000)
            continue;
        if (end <= now) {
            if (now - end <= kLateTimerMs)
                m_lateTimers.append({it.key(), t.spec});
            continue;
        }
        t.end = QDateTime::fromMSecsSinceEpoch(end, QTimeZone::UTC);
        t.remaining = int((end - now + 999) / 1000);
        // Armed again (the same id): what the platform kept stays, a lost one comes back.
        t.native = m_timerArm ? m_timerArm(timerNotification(it.key(), t)) : o.value(u"native"_s).toBool();
        m_timers.insert(it.key(), t);
        if (it.key() == u"silownia") {
            m_restTotal = t.spec.seconds;
            m_restRemaining = t.remaining;
            m_restLabel = t.spec.label.isEmpty() ? tr("Rest") : t.spec.label;
            Q_EMIT restChanged();
        }
    }
    storeTimers(); // without the ended ones
    if (!m_timers.isEmpty()) {
        m_restTimer.start();
        Q_EMIT timersChanged();
    }
}

// ---- an extension's own settings ----------------------------------------------------------------

QVariantList WorkspaceExtensions::settingsOf(const QString &extension) const
{
    if (!m_engine)
        return {};
    const QVariantMap values = m_engine->settingValues(extension);
    QVariantList out;
    for (const QVariant &f : m_engine->settingsOf(extension)) {
        const QVariantMap field = f.toMap();
        const QVariant current = values.value(field.value(u"key"_s).toString());
        // Every key on every row (the dialog's delegates take each as a typed property).
        out << QVariantMap{{u"key"_s, field.value(u"key"_s).toString()},
                           {u"label"_s, field.value(u"label"_s).toString()},
                           {u"kind"_s, field.value(u"kind"_s).toString()},
                           {u"description"_s, field.value(u"description"_s).toString()},
                           {u"step"_s, field.value(u"step"_s, 1).toDouble()},
                           {u"unit"_s, field.value(u"unit"_s).toString()},
                           {u"options"_s, field.value(u"options"_s).toStringList()},
                           {u"placeholder"_s, field.value(u"placeholder"_s).toString()},
                           {u"currentText"_s, current.toString()},
                           {u"currentNumber"_s, current.toDouble()},
                           {u"currentOn"_s, current.toBool()},
                           {u"display"_s, formatExtensionNumber(current.toDouble(), m_language == u"pl")}};
    }
    return out;
}

int WorkspaceExtensions::settingsCount(const QString &extension) const
{
    return m_engine ? int(m_engine->settingsOf(extension).size()) : 0;
}

QString WorkspaceExtensions::pageExtension(const QString &path) const
{
    const PageEntry *e = pageAt(path);
    return e ? e->runId : QString();
}

bool WorkspaceExtensions::setSetting(const QString &extension, const QString &key, const QVariant &value)
{
    if (!m_engine || !m_host)
        return false;
    for (const QVariant &f : m_engine->settingsOf(extension)) {
        const QVariantMap field = f.toMap();
        if (field.value(u"key"_s).toString() != key)
            continue;
        const QString kind = field.value(u"kind"_s).toString();
        QVariant stored;
        if (kind == u"number") {
            bool ok = value.metaType().id() != QMetaType::QString;
            double number = ok ? value.toDouble(&ok) : 0;
            if (value.metaType().id() == QMetaType::QString) { // typed: "82,5", "82.5 kg"
                const std::optional<double> typed =
                        parseExtensionNumber(value.toString(), field.value(u"unit"_s).toString());
                ok = typed.has_value();
                number = typed.value_or(0);
            }
            if (!ok || !std::isfinite(number))
                return false;
            stored = number == std::floor(number) && std::fabs(number) < 1e15 ? QVariant(qint64(number))
                                                                              : QVariant(number);
        } else if (kind == u"toggle") {
            stored = value.toBool();
        } else if (kind == u"choice") {
            if (!field.value(u"options"_s).toStringList().contains(value.toString()))
                return false;
            stored = value.toString();
        } else {
            if (value.toString().size() > extensions::QuestionLimits::settingText)
                return false;
            stored = value.toString();
        }
        if (!m_host->setSetting(extension, key, stored).isEmpty())
            return false;
        Q_EMIT changed(); // (views show what follows from it)
        bumpRevision();
        return true;
    }
    return false;
}

void WorkspaceExtensions::resetSettings(const QString &extension)
{
    if (!m_engine || !m_host)
        return;
    for (const QVariant &f : m_engine->settingsOf(extension))
        m_host->setSetting(extension, f.toMap().value(u"key"_s).toString(), QVariant());
    Q_EMIT changed();
    bumpRevision();
}

// ---- sharing between notebooks ------------------------------------------------------------------

void WorkspaceExtensions::setDeviceStates(std::function<QString(const QString &)> stateOf)
{
    m_stateOf = std::move(stateOf);
}

QString WorkspaceExtensions::copyToNotebook(const QString &path, const QString &root, bool replace)
{
    const PageEntry *e = pageAt(path);
    if (!e || !m_index || !m_vault)
        return tr("This page is no extension.");
    if (root.isEmpty() || QDir::cleanPath(root) == QDir::cleanPath(m_vault->rootPath()))
        return tr("Choose another notebook.");
    if (!m_stateOf)
        return tr("Extensions cannot be copied here.");
    const QString title = e->page.title;
    Vault target(root, m_stateOf(root));
    QString error;
    if (!target.open(&error))
        return error.isEmpty() ? tr("The notebook cannot be opened.") : error;
    const extensions::CopyReport report = extensions::copyExtension(*m_index, e->page.path, target, replace);
    if (!report.error.isEmpty())
        return report.error;
    if (!report.conflicts.isEmpty())
        return u"conflict"_s;
    // The user chose to run this code there too: approved on this device, as here.
    for (const extensions::PageExtension &page : report.pages)
        if (isApproved(page.hash))
            extensions::approveInStore(target.store(), page);
    target.store()->flush();
    Q_EMIT message(report.written.isEmpty() ? tr("%1 is in %2 already.").arg(title, QDir(root).dirName())
                                            : tr("%1 copied to %2 (%n page(s)).", "", int(report.written.size()))
                                                      .arg(title, QDir(root).dirName()),
                   u"success"_s);
    return {};
}

} // namespace brulion::shell
