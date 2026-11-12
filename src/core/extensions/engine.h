#pragma once
// The Luau engine of Brulion's extensions (docs/EXTENSIONS.md): one VM, every extension in an
// environment of its own with its own read-only `b` table, compiled from source by the host
// (scripts never see a compiler or a bytecode loader), every call in a thread of its own.
//
//   Engine engine;
//   engine.start(&host, Limits{});
//   engine.load({u"silownia"_s, source, {u"read"_s, u"write"_s}, data});
//   Result r = engine.runCommand(u"silownia"_s, u"start"_s, ctx);
//   while (r.status == Status::Suspended)        // b.ask / b.choose / b.form: r.value = question
//       r = engine.resume(answerOf(r.value));     // QVariant() = cancelled (choose/form give nil)
//   engine.render(u"silownia"_s, u"today"_s, params)          // a view: r.value = its node tree
//   engine.act(u"silownia"_s, u"today"_s, u"addSet"_s, args)  // one of the view's actions
//   for (const HookInfo &h : engine.hooks(u"save"_s)) engine.runHook(h.id, {{u"page"_s, title}});
//   engine.evaluate(u"*console*"_s, u"1 + 1"_s, u"console"_s) // the console: r.value = 2
//
// Limits (Limits): a time limit per call (also per resume and load) enforced on the engine's own
// thread by the VM's interrupt callback — safepoints at loops, calls, returns, the pattern matcher
// and the plain string search (patched, 3rdparty/luau) — and a memory limit through the allocator.
// Neither can be caught by the script: past the deadline the call's thread is yielded (a yield
// never unwinds through pcall) and an error is raised wherever a yield is impossible. Works in the
// single-threaded browser build (no watchdog thread). Errors name the extension and the line.
//
// Waiting calls (Status::Suspended, value = QVariantMap, Result::call = its id): a call that asks
// the host for something suspends until resume(call, answer); other calls run meanwhile.
//   {kind: "ask", prompt}                       → resume(call, QString)
//   {kind: "choose", prompt, options: [string]} → resume(call, QString) or QVariant() = nil
//   {kind: "form", title, fields: [{key, label, kind, value, step, unit, options}]}
//                                               → resume(call, QVariantMap) or QVariant() = nil
//   {kind: "ai", request: {prompt | messages, system, model, maxTokens, effort}}
//                                               → resume(call, {text, model, …} or {error})
// One call executes at a time (one thread); any number may wait. Loading an extension again
// drops its waiting calls.
//
// Not thread-safe: one Engine is used from one thread.

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

struct lua_State;

namespace brulion::extensions {

class Host;

struct Limits
{
    int timeoutMs = 2000; ///< per call, resume and load
    qint64 memoryBytes = 64ll << 20; ///< the VM's heap; <= 0: no limit
};

struct Extension
{
    QString id; ///< "silownia"
    QString source; ///< Luau source
    QStringList permissions; ///< "write" lets it change notes (reading is always allowed)
    QVariantMap data; ///< b.data(name): read-only data bundled with it (parsed JSON)
    QString name; ///< the name in error messages ("Licznik słów:14: …"); "" = the id
};

enum class Status {
    Ok,
    Error, ///< a script error: `error` has the message (extension:line: …, a traceback)
    Suspended, ///< waiting for an answer: `value` is the question, continue with resume()
    Timeout,
    OutOfMemory,
    StackOverflow,
    NotFound, ///< no such extension, command, view or action
};

struct Result
{
    Status status = Status::Ok;
    QVariant value; ///< QString, qint64, double, bool, QVariantList, QVariantMap or null
    QString error;
    /// The call's id once it waited (Suspended: resume it with this; a later result of it: its
    /// end), 0 for a call that never waited.
    int call = 0;
    bool ok() const { return status == Status::Ok; }
};

struct CommandInfo
{
    QString extension;
    QString name; ///< the stable id within the extension ("start")
    QString label; ///< the palette's label (b.command's meta.label), else the name
    bool needsBlock = false; ///< meta.block: it works on the block under the caret (ctx.block)
};

struct ViewInfo
{
    QString extension;
    QString name;
    QStringList actions;
    QString label; ///< b.view's meta.label: a view the palette offers; "" = none
    QString icon; ///< meta.icon, an icon name of the app ("" = the extensions' own)
};

/// A function an extension registered with b.on(event, fn) (docs/EXTENSIONS.md "Hooks").
struct HookInfo
{
    int id = 0; ///< in the order of registration
    QString extension;
    QString event; ///< "save" | "change"
};

class Engine
{
public:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    /// A fresh VM (a running one is stopped first). `host` must outlive the engine's use.
    bool start(Host *host, const Limits &limits, QString *error = nullptr);
    void stop();
    bool isStarted() const { return m_L != nullptr; }
    void setLimits(const Limits &limits);

    /// Runs the extension's source once (it registers its commands and views). Loading the same
    /// id again first drops every registration of the previous version; a failed load leaves
    /// nothing registered.
    Result load(const Extension &extension);
    void unload(const QString &id);
    QStringList extensions() const;

    QList<CommandInfo> commands() const; ///< sorted by extension, then name
    QList<ViewInfo> views() const;
    bool isLoaded(const QString &id) const { return m_loaded.contains(id); }
    /// The events b.on accepts.
    static QStringList hookEvents();
    /// The hooks of `event` ("" = all), in the order of registration.
    QList<HookInfo> hooks(const QString &event = QString()) const;
    /// Calls a hook with `event` (its one argument); NotFound when it is gone.
    Result runHook(int id, const QVariantMap &event);
    void removeHook(int id);

    Result runCommand(const QString &extension, const QString &name, const QVariantMap &ctx);
    Result render(const QString &extension, const QString &view, const QVariantMap &params);
    Result act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args);
    Result resume(int call, const QVariant &answer);
    /// resume() of the newest call waiting on a question (ask / choose / form).
    Result resume(const QVariant &answer);
    /// Runs `code` in the environment of the loaded extension `extension` (its globals, its `b`,
    /// its permissions): an expression gives its value ("1 + 1" → 2), statements run as written.
    /// `name` names the code in errors ("console:1: …"). The console and "Run Luau block".
    Result evaluate(const QString &extension, const QString &code, const QString &name);
    bool isWaiting() const { return !m_waiting.isEmpty(); }
    /// A call executes now (a host function is running inside it): no other call may start.
    bool isRunning() const { return m_running != nullptr; }
    /// The calls waiting for an answer, oldest first.
    QList<int> waitingCalls() const;
    void cancel(int call); ///< drops a waiting call
    void cancel(); ///< drops the newest call waiting on a question, if any

    qint64 memoryUsed() const { return qint64(m_used); }
    static QString version(); ///< "Luau 0.743"

private:
    struct Handler
    {
        QString extension;
        int ref = 0; // registry ref of the function (a view: its render function)
        int actionsRef = 0; // a view's actions table, 0 = none
        QString label;
        bool needsBlock = false;
        QString icon;
    };
    struct Hook
    {
        QString extension;
        QString event;
        int ref = 0;
    };
    struct Loaded
    {
        int envRef = 0;
        QStringList permissions;
        QVariantMap data;
    };

    static void *alloc(void *ud, void *ptr, size_t osize, size_t nsize);
    static void interrupt(lua_State *L, int gc);
    static Engine *self(lua_State *L);

    // The `b` API: each is a thin lua_CFunction around an implementation returning the number of
    // results or -1 after pushing an error message (errors are longjmp: no C++ object may be
    // alive when the error is raised, so the wrappers raise it after the implementation returned).
    static int apiCommand(lua_State *L);
    static int apiView(lua_State *L);
    static int apiPages(lua_State *L);
    static int apiRead(lua_State *L);
    static int apiPage(lua_State *L);
    static int apiBlocks(lua_State *L);
    static int apiSearch(lua_State *L);
    static int apiQuery(lua_State *L);
    static int apiWrite(lua_State *L);
    static int apiAppendToJournal(lua_State *L);
    static int apiReplaceBlock(lua_State *L);
    static int apiToday(lua_State *L);
    static int apiDaysBetween(lua_State *L);
    static int apiSorted(lua_State *L);
    static int apiFold(lua_State *L);
    static int apiFormatNumber(lua_State *L);
    static int apiLang(lua_State *L);
    static int apiUiLang(lua_State *L);
    static int apiLog(lua_State *L);
    static int apiPrint(lua_State *L);
    static int apiTimer(lua_State *L);
    static int apiData(lua_State *L);
    static int apiAsk(lua_State *L);
    static int apiChoose(lua_State *L);
    static int apiForm(lua_State *L);
    static int apiOn(lua_State *L);
    static int apiAi(lua_State *L);

    int commandImpl(lua_State *L);
    int viewImpl(lua_State *L);
    int pagesImpl(lua_State *L);
    int readImpl(lua_State *L);
    int pageImpl(lua_State *L);
    int blocksImpl(lua_State *L);
    int searchImpl(lua_State *L);
    int queryImpl(lua_State *L);
    int writeImpl(lua_State *L);
    int appendImpl(lua_State *L);
    int replaceImpl(lua_State *L);
    int daysImpl(lua_State *L);
    int sortedImpl(lua_State *L);
    int foldImpl(lua_State *L);
    int formatImpl(lua_State *L);
    int logImpl(lua_State *L);
    int printImpl(lua_State *L);
    int timerImpl(lua_State *L);
    int dataImpl(lua_State *L);
    int questionImpl(lua_State *L, const char *kind);
    int onImpl(lua_State *L);
    int aiImpl(lua_State *L);

    bool mayWrite(lua_State *L, const QString &extension, const char *function);
    void pushEnvironment(const QString &extension);
    void dropExtension(const QString &extension);
    void arm();
    void disarm();
    Result call(const QString &extension, int functionRef, const QVariantList &args);
    Result run(const QString &extension, const QVariantList &args);
    Result finish(int status);
    void releaseThread();
    void dropWaiting(const QString &extension);

    lua_State *m_L = nullptr;
    Host *m_host = nullptr;
    Limits m_limits;
    size_t m_used = 0;
    size_t m_memoryLimit = 0;
    int m_unlimited = 0; // > 0: host-side allocations that must not fail
    bool m_timedOut = false;
    bool m_outOfMemory = false;
    unsigned m_ticks = 0;
    QElapsedTimer m_clock;
    qint64 m_deadlineNs = 0;
    int m_uiRef = 0;
    QHash<QString, Loaded> m_loaded;
    QHash<QString, Handler> m_commands; // "extension/name"
    QHash<QString, Handler> m_views;
    QMap<int, Hook> m_hooks;
    int m_nextHook = 1;
    struct Waiting
    {
        lua_State *thread = nullptr;
        int ref = 0;
        QString extension;
        QString kind;
    };
    lua_State *m_running = nullptr; // the call executing now
    int m_runningRef = 0;
    QString m_runningExtension;
    int m_runningCall = 0; // its id when it waited before, else 0
    bool m_asking = false;
    QString m_askingKind;
    QMap<int, Waiting> m_waiting; // by call id
    int m_nextCall = 1;
    int m_aiFactoryRef = 0; // the b.ai wrapper's factory (a prelude)
};

} // namespace brulion::extensions
