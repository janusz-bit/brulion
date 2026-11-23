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
//   engine.startRecording();                                         // what a macro reads …
//   engine.runMacro(u"silownia"_s, u"rekord"_s, {{u"args"_s, …}}, 250); // {{ext:silownia/rekord …}}
//   Reads reads = engine.stopRecording();                            // … to refresh it after
//   engine.runVariable(u"silownia"_s, u"plan"_s, ctx, 250)           // {{silownia/plan}} in a template
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

#include "extensions/appapi.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
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
    bool palette = true; ///< meta.palette ~= false: the palette offers it (else only the app or a view runs it)
    /// meta.key: the keyboard shortcut it suggests ("Ctrl+Alt+W"; the app takes it only while no
    /// command of its own and no other binding has it), "" = none.
    QString key;
    /// meta.chat: a chat partner (docs/EXTENSIONS.md "Chat partners") — the AI chat may let it
    /// answer a conversation (ctx.messages); the palette does not offer it unless meta.palette says so.
    bool chat = false;
};

struct ViewInfo
{
    QString extension;
    QString name;
    QStringList actions;
    QString label; ///< b.view's meta.label: a view the palette offers; "" = none
    QString icon; ///< meta.icon, an icon name of the app ("" = the extensions' own)
    /// meta.panel: a section of the side panel beside "page"s, "journal" days or "all" of them, or
    /// of the pages panel ("sidebar"); "" = none (docs/EXTENSIONS.md "View blocks and panels").
    QString panel;
};

/// A macro an extension offers (b.macro, docs/EXTENSIONS.md "Macros"): `{{ext:<extension>/<name> …}}`
/// in a note shows what it returns.
struct MacroInfo {
    QString extension;
    QString name;
    QString description;   ///< meta.description: what it shows (the editor's suggestions)
    QStringList arguments; ///< meta.args: its arguments' names ("exercise"), for the suggestions
    QString example;       ///< meta.example: arguments to suggest ("Przysiad")
};

/// A variable an extension offers to templates (b.variable, docs/TEMPLATES.md "Variables of
/// extensions"): `{{<extension>/<name> …}}` in a template is filled with what it returns.
struct VariableInfo {
    QString extension;
    QString name;
    QString description;
    QStringList arguments;
};

/// One extension's overrides of the theme (b.app.theme), in the order the extensions set them.
struct ThemeInfo {
    QString extension;
    ThemeLayer layer;
};

/// What calls read of the notebook while a recording is on (Engine::startRecording): a macro is
/// computed again when one of these changes.
struct Reads {
    QSet<QString> paths;     ///< the notes it read by name (b.read, b.page, b.lines, b.blocks({page}))
    QSet<QString> names;     ///< names it read that are no note (b.read gave nil), folded (text::foldKey)
    bool everything = false; ///< a query, a search, a list of pages or blocks, the revision: any note
};

/// A function an extension registered with b.on(event, fn) (docs/EXTENSIONS.md "Hooks").
struct HookInfo
{
    int id = 0; ///< in the order of registration
    QString extension;
    QString event; ///< one of Engine::hookEvents(): "save", "change", "task", "journal", "open", "day", "timer"
};

/// What the questions and the settings may show (docs/EXTENSIONS.md "Sandbox and limits"): a
/// question's text is cut to fit — a megabyte of prompt never reaches a dialog —, a list or a form
/// that is too long is an error.
struct QuestionLimits {
    static constexpr int title = 300;        ///< characters of a prompt's first line (the dialog's title)
    static constexpr int text = 10000;       ///< characters of the lines under it …
    static constexpr int lines = 200;        ///< … and lines
    static constexpr int options = 500;      ///< b.choose: options
    static constexpr int option = 500;       ///< characters of one option
    static constexpr int fields = 50;        ///< b.form: fields
    static constexpr int label = 200;        ///< characters of a field's label, a setting's label
    static constexpr int value = 10000;      ///< characters of a text field's value
    static constexpr int timerLabel = 100;   ///< b.timer's label
    static constexpr int timerMessage = 300; ///< b.timer's message
    static constexpr int settings = 32;      ///< b.settings: fields
    static constexpr int settingText = 4000; ///< characters of a setting's text
    static constexpr int requireDepth = 16;  ///< b.require inside b.require
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
    /// The extension whose call executes now ("" when none): a host function knows whose call
    /// asks (the app: whose write a note's change is).
    QString runningExtension() const { return m_running ? m_runningExtension : QString(); }
    /// The settings `extension` declared with b.settings, normalized: [{key, label, kind ("number" |
    /// "text" | "toggle" | "choice"), value (the default), step, unit, options, placeholder,
    /// description}]; [] when none.
    QVariantList settingsOf(const QString &extension) const;
    /// Its settings as b.setting reads them: each declared one's stored value when it fits its
    /// kind, else its default; values stored without a declaration as they are.
    QVariantMap settingValues(const QString &extension) const;
    /// The extension pages b.require loaded for `extension` (their paths), in the order first
    /// required: when one of them changes, the extension has to load again.
    QStringList modulesOf(const QString &extension) const;
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

    // ---- The app (b.app; docs/EXTENSIONS.md "The app (`b.app`)") -------------------------------
    /// The UI items extensions contributed (b.app.contribute) to `slot` ("" = every slot), by
    /// their `order`, then in the order contributed. Dropped with their extension.
    QList<Contribution> contributions(const QString &slot = QString()) const;
    /// The extensions' overrides of the theme, the first set first (a later one wins per token).
    QList<ThemeInfo> themes() const;
    /// The input rules extensions declared (b.app.inputRule), the first declared first.
    QList<InputRule> inputRules() const { return m_inputRules; }
    /// The styles of blocks by tag extensions declared (b.app.tagStyle), the first declared first
    /// (a later one wins a tag).
    QList<TagStyle> tagStyles() const { return m_tagStyles; }
    /// Counts the changes of contributions(), themes(), inputRules() and tagStyles().
    int appRevision() const { return m_appRevision; }

    qint64 memoryUsed() const { return qint64(m_used); }
    static QString version(); ///< "Luau 0.743"

    // ---- Macros and template variables (engine_macros.cpp; docs/EXTENSIONS.md "Macros") ---------
    QList<MacroInfo> macros() const; ///< sorted by extension, then name
    QList<VariableInfo> variables() const;
    /// Runs the macro `extension`/`name` with `ctx` ({args, text, page, path, date?}) within
    /// `timeoutMs` (≤ the engine's own limit): its value as returned (extensions/macros.h shapes
    /// it). It cannot wait (a question or b.ai is an error) nor write (b.write … are refused).
    Result runMacro(const QString &extension, const QString &name, const QVariantMap &ctx, int timeoutMs);
    /// The same for a template variable ({args, text, page, path, date, template}).
    Result runVariable(const QString &extension, const QString &name, const QVariantMap &ctx, int timeoutMs);
    /// Notes what the calls from now on read (Reads), until stopRecording() gives it.
    void startRecording();
    Reads stopRecording();

private:
    struct Handler
    {
        QString extension;
        int ref = 0; // registry ref of the function (a view: its render function)
        int actionsRef = 0; // a view's actions table, 0 = none
        QString label;
        bool needsBlock = false;
        QString icon;
        bool palette = true;
        QString panel; // a view's meta.panel
        QString key;   // a command's meta.key
        bool chat = false;
    };
    /// A macro or a template variable (b.macro, b.variable).
    struct MacroEntry {
        QString extension;
        int ref = 0;
        QString description;
        QStringList arguments;
        QString example;
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
        QString name; // as errors name it: Extension::name, else the id
        QVariantList settings;          // declared with b.settings (settingsOf())
        QStringList modules;            // the pages b.require loaded, by path, in order
        QHash<QString, int> moduleRefs; // path → registry ref of the module's value
        QStringList requiring;          // the modules whose code runs now (a cycle is an error)
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
    static int apiCheckExtension(lua_State *L);
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
    static int apiStatus(lua_State *L);
    static int apiPrint(lua_State *L);
    static int apiTimer(lua_State *L);
    static int apiData(lua_State *L);
    static int apiAsk(lua_State *L);
    static int apiChoose(lua_State *L);
    static int apiForm(lua_State *L);
    static int apiOn(lua_State *L);
    static int apiAi(lua_State *L);
    static int apiAttachments(lua_State *L);
    static int apiRevision(lua_State *L);
    static int apiNow(lua_State *L);
    static int apiAddDays(lua_State *L);
    static int apiAddMonths(lua_State *L);
    static int apiWeekday(lua_State *L);
    static int apiWeekStart(lua_State *L);
    static int apiWeekdayName(lua_State *L);
    static int apiMonthName(lua_State *L);
    static int apiLower(lua_State *L);
    static int apiPlain(lua_State *L);
    static int apiParseRecurrence(lua_State *L);
    static int apiParseWeekdays(lua_State *L);
    static int apiFormatRecurrence(lua_State *L);
    static int apiAppendToPage(lua_State *L);
    static int apiInsertBlock(lua_State *L);
    static int apiRemoveBlock(lua_State *L);
    static int apiSetTask(lua_State *L);
    static int apiSetProperty(lua_State *L);
    static int apiSetProperties(lua_State *L);
    static int apiToUtc(lua_State *L);
    static int apiFromUtc(lua_State *L);
    static int apiLines(lua_State *L);
    static int apiReplaceLine(lua_State *L);
    static int apiInsertLine(lua_State *L);
    static int apiRemoveLine(lua_State *L);
    static int apiMacro(lua_State *L);
    static int apiVariable(lua_State *L);
    static int apiRequire(lua_State *L);
    static int apiRequireDone(lua_State *L);
    static int apiSettings(lua_State *L);
    static int apiSetting(lua_State *L);
    static int apiSetSetting(lua_State *L);
    static int apiStopTimer(lua_State *L);
    static int apiApp(lua_State *L); // b.app.state … b.app.toast: upvalues (extension, operation)
    static int apiAppTheme(lua_State *L);
    static int apiAppContribute(lua_State *L);
    static int apiAppRemove(lua_State *L);
    static int apiAppContributions(lua_State *L);
    static int apiAppInputRule(lua_State *L);
    static int apiAppInputRules(lua_State *L);
    static int apiAppTagStyle(lua_State *L);
    static int apiAppTagStyles(lua_State *L);

    int commandImpl(lua_State *L);
    int viewImpl(lua_State *L);
    int pagesImpl(lua_State *L);
    int readImpl(lua_State *L);
    int pageImpl(lua_State *L);
    int blocksImpl(lua_State *L);
    int searchImpl(lua_State *L);
    int queryImpl(lua_State *L);
    int checkImpl(lua_State *L);
    int writeImpl(lua_State *L);
    int appendImpl(lua_State *L);
    int replaceImpl(lua_State *L);
    int daysImpl(lua_State *L);
    int sortedImpl(lua_State *L);
    int foldImpl(lua_State *L);
    int formatImpl(lua_State *L);
    int logImpl(lua_State *L);
    int statusImpl(lua_State *L);
    int printImpl(lua_State *L);
    int timerImpl(lua_State *L);
    int dataImpl(lua_State *L);
    int questionImpl(lua_State *L, const char *kind);
    int onImpl(lua_State *L);
    int aiImpl(lua_State *L);
    int attachmentsImpl(lua_State *L);
    int addDaysImpl(lua_State *L, bool months);
    int weekdayImpl(lua_State *L);
    int nameImpl(lua_State *L, bool month);
    int lowerImpl(lua_State *L);
    int plainImpl(lua_State *L);
    int parseRecurrenceImpl(lua_State *L);
    int parseWeekdaysImpl(lua_State *L);
    int formatRecurrenceImpl(lua_State *L);
    int appendToPageImpl(lua_State *L);
    int insertBlockImpl(lua_State *L);
    int removeBlockImpl(lua_State *L);
    int setTaskImpl(lua_State *L);
    int setPropertyImpl(lua_State *L);
    int setPropertiesImpl(lua_State *L);
    int toUtcImpl(lua_State *L);
    int fromUtcImpl(lua_State *L);
    int linesImpl(lua_State *L);
    int replaceLineImpl(lua_State *L);
    int insertLineImpl(lua_State *L);
    int removeLineImpl(lua_State *L);
    /// b.macro / b.variable (`variable`): registers fn with its meta.
    int contributionImpl(lua_State *L, bool variable);
    /// A macro's or a variable's call: no waiting, no writes, `timeoutMs` at most.
    Result runContribution(bool variable, const QString &extension, const QString &name, const QVariantMap &ctx,
                           int timeoutMs);
    void dropContributions(const QString &extension);
    /// While recording: a note read by its name (b.read, b.page, b.lines, b.blocks({page})).
    void noteRead(const QString &page);
    /// While recording: a read that any note may change (a query, a search, the list of pages).
    void noteReadAll()
    {
        if (m_reads)
            m_reads->everything = true;
    }
    int requireImpl(lua_State *L);
    int requireDoneImpl(lua_State *L);
    int settingsImpl(lua_State *L);
    int settingImpl(lua_State *L);
    int setSettingImpl(lua_State *L);
    int stopTimerImpl(lua_State *L);
    int appImpl(lua_State *L);
    int appThemeImpl(lua_State *L);
    int appContributeImpl(lua_State *L);
    int appRemoveImpl(lua_State *L);
    int appContributionsImpl(lua_State *L);
    int appInputRuleImpl(lua_State *L);
    int appTagStyleImpl(lua_State *L);
    /// b.app.remove("inputRule" | "tagStyle", key?): the extension's rules or styles taken away.
    int appRemoveDeclarationImpl(lua_State *L, const QString &extension, const QString &kind, const QString &key);
    /// False, its error raised, when `extension` may not change the app now: no permission "app",
    /// a macro's or a template variable's call, or — `notWhileLoading` — its top level.
    bool mayChangeApp(lua_State *L, const QString &extension, const QString &function, bool notWhileLoading);
    /// Drops the extension's contributions, theme, input rules and tag styles; true when it had any.
    bool dropAppState(const QString &extension);

    bool mayWrite(lua_State *L, const QString &extension, const char *function);
    /// False, its error raised, inside a macro's or a template variable's call (they only read: no
    /// setting set or declared, no timer started or stopped).
    bool mayChange(lua_State *L, const char *function);
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
    QHash<QString, MacroEntry> m_macros; // "extension/name"
    QHash<QString, MacroEntry> m_variables;
    Reads *m_reads = nullptr; // a recording is on (startRecording)
    Reads m_recorded;
    bool m_noWrites = false;                 // a macro's or a variable's call runs: writes are refused
    int m_requireFactoryRef = 0;             // the b.require wrapper's factory (a prelude)
    bool m_loading = false;                  // load(): what the top level returns is no result (a library's table)
    QHash<QString, QByteArray> m_moduleCode; // a module's bytecode by its hash (compiled once)
    QList<Contribution> m_contributions;     // in the order contributed
    QList<ThemeInfo> m_themes;               // in the order set
    QList<InputRule> m_inputRules;           // in the order declared
    QList<TagStyle> m_tagStyles;             // in the order declared
    int m_appRevision = 0;
};

} // namespace brulion::extensions
