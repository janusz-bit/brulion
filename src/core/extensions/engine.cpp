#include "extensions/engine.h"

#include "extensions/host.h"
#include "text/text.h"

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <QDate>
#include <QRandomGenerator>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

const char kTimeout[] = "timeout: the extension ran too long";
const char kNoMemory[] = "not enough memory";
constexpr int kMaxDepth = 64; // nesting of values crossing between the host and a script
constexpr int kRaise = -1; // the implementation pushed an error message
constexpr int kYield = -2; // the implementation pushed a question: suspend the call


// b.ui: the components a view is made of (docs/EXTENSIONS.md "Views"). Trusted code, compiled once
// per VM; the tables it returns are plain data the app's QML draws.
const char kUiPrelude[] = R"luau(
local ui = {}
function ui.column(children) return { type = "column", children = children } end
function ui.row(children) return { type = "row", children = children } end
function ui.heading(text) return { type = "heading", text = text } end
function ui.text(text, muted) return { type = "text", text = text, muted = muted == true } end
function ui.link(page, label) return { type = "link", page = page, label = label } end
function ui.button(label, action, args) return { type = "button", label = label, action = action, args = args } end
function ui.check(checked, action, args) return { type = "check", checked = checked == true, action = action, args = args } end
function ui.stepper(spec)
    return { type = "stepper", value = spec.value, step = spec.step, unit = spec.unit, action = spec.action, args = spec.args }
end
function ui.chart(spec)
    return { type = "chart", kind = spec.kind, title = spec.title, labels = spec.labels, series = spec.series }
end
function ui.table(columns, rows) return { type = "table", columns = columns, rows = rows } end
function ui.empty(title, text) return { type = "empty", title = title, text = text } end
function ui.open(label, view, params) return { type = "open", label = label, view = view, params = params } end
function ui.run(label, command) return { type = "run", label = label, command = command } end
return table.freeze(ui)
)luau";

void pushString(lua_State *L, QStringView s)
{
    const QByteArray u = s.toUtf8();
    lua_pushlstring(L, u.constData(), size_t(u.size()));
}

QString toQString(lua_State *L, int idx)
{
    size_t n = 0;
    const char *s = lua_tolstring(L, idx, &n);
    return QString::fromUtf8(s, qsizetype(n));
}

// An error message for the caller to raise (see Engine's api* wrappers).
int failWith(lua_State *L, const QString &message)
{
    pushString(L, message);
    return kRaise;
}

void pushVariant(lua_State *L, const QVariant &v, int depth = 0)
{
    lua_rawcheckstack(L, 4);
    if (!v.isValid() || v.isNull() || depth > kMaxDepth) {
        lua_pushnil(L);
        return;
    }
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        lua_pushboolean(L, v.toBool());
        return;
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Long: case QMetaType::ULong: case QMetaType::Short: case QMetaType::UShort:
    case QMetaType::Double: case QMetaType::Float:
        lua_pushnumber(L, v.toDouble()); // Luau has one number type
        return;
    case QMetaType::QStringList: case QMetaType::QVariantList: {
        const QVariantList list = v.toList();
        lua_createtable(L, int(list.size()), 0);
        for (qsizetype i = 0; i < list.size(); ++i) {
            pushVariant(L, list[i], depth + 1);
            lua_rawseti(L, -2, int(i + 1));
        }
        return;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = v.toMap();
        lua_createtable(L, 0, int(map.size()));
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            pushString(L, it.key());
            pushVariant(L, it.value(), depth + 1);
            lua_rawset(L, -3);
        }
        return;
    }
    default:
        pushString(L, v.toString());
    }
}

QVariant numberToVariant(double d)
{
    // Whole numbers go back to the host as integers.
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) <= 9007199254740992.0)
        return qint64(d);
    return d;
}

// A script value as a QVariant. Never runs script code (no metamethods: rawget / lua_next).
bool toVariant(lua_State *L, int idx, QVariant *out, QString *error, int depth = 0)
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        *out = QVariant();
        return true;
    case LUA_TBOOLEAN:
        *out = bool(lua_toboolean(L, idx));
        return true;
    case LUA_TNUMBER:
        *out = numberToVariant(lua_tonumber(L, idx));
        return true;
    case LUA_TSTRING:
        *out = toQString(L, idx);
        return true;
    case LUA_TTABLE: {
        if (depth > kMaxDepth) {
            *error = u"value nested too deeply (a cycle?)"_s;
            return false;
        }
        lua_rawcheckstack(L, 4);
        const int len = lua_objlen(L, idx);
        int count = 0;
        bool sequence = true;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            ++count;
            if (lua_type(L, -2) != LUA_TNUMBER) {
                sequence = false;
            } else {
                const double k = lua_tonumber(L, -2);
                if (k != std::floor(k) || k < 1 || k > len)
                    sequence = false;
            }
            lua_pop(L, 1);
        }
        if (count > 0 && sequence && count == len) {
            QVariantList list;
            list.reserve(len);
            for (int i = 1; i <= len; ++i) {
                lua_rawgeti(L, idx, i);
                QVariant item;
                const bool ok = toVariant(L, -1, &item, error, depth + 1);
                lua_pop(L, 1);
                if (!ok)
                    return false;
                list << item;
            }
            *out = list;
            return true;
        }
        QVariantMap map;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            QString key;
            if (lua_type(L, -2) == LUA_TSTRING) {
                key = toQString(L, -2);
            } else if (lua_type(L, -2) == LUA_TNUMBER) {
                key = numberToVariant(lua_tonumber(L, -2)).toString();
            } else {
                lua_pop(L, 2);
                *error = u"table keys must be strings or numbers"_s;
                return false;
            }
            QVariant item;
            if (!toVariant(L, -1, &item, error, depth + 1)) {
                lua_pop(L, 2);
                return false;
            }
            map.insert(key, item);
            lua_pop(L, 1);
        }
        *out = map;
        return true;
    }
    default:
        *error = u"cannot pass a value of type "_s + QString::fromUtf8(luaL_typename(L, idx));
        return false;
    }
}

// Arguments, checked without raising (errors are raised by the api* wrappers).
bool argString(lua_State *L, int i, const char *function, QString *out)
{
    if (lua_type(L, i) != LUA_TSTRING) {
        failWith(L, u"%1: argument %2 must be a string, not %3"_s.arg(QString::fromLatin1(function)).arg(i).arg(
                        QString::fromUtf8(luaL_typename(L, i))));
        return false;
    }
    *out = toQString(L, i);
    return true;
}

bool argOptString(lua_State *L, int i, const char *function, QString *out)
{
    if (lua_isnoneornil(L, i)) {
        out->clear();
        return true;
    }
    return argString(L, i, function, out);
}

bool argNumber(lua_State *L, int i, const char *function, double *out)
{
    if (lua_type(L, i) != LUA_TNUMBER) {
        failWith(L, u"%1: argument %2 must be a number, not %3"_s.arg(QString::fromLatin1(function)).arg(i).arg(
                        QString::fromUtf8(luaL_typename(L, i))));
        return false;
    }
    *out = lua_tonumber(L, i);
    return true;
}

bool argValue(lua_State *L, int i, const char *function, QVariant *out)
{
    QString error;
    if (!toVariant(L, i, out, &error)) {
        failWith(L, QString::fromLatin1(function) + u": "_s + error);
        return false;
    }
    return true;
}

// Raises the message an implementation pushed, or yields its question: here in the wrapper, after
// the implementation returned, so that no C++ object is alive when the stack unwinds (longjmp).
int finishApi(lua_State *L, int results)
{
    if (results == kRaise) {
        luaL_where(L, 1); // "extension:line: " of the script's call
        lua_insert(L, -2);
        lua_concat(L, 2);
        lua_error(L);
    }
    if (results == kYield)
        return lua_yield(L, 1);
    return results;
}

// Host-side allocations (arguments, results, environments, messages) must not fail half-way.
struct Unlimited
{
    explicit Unlimited(int *counter) : m_counter(counter) { ++*m_counter; }
    ~Unlimited() { --*m_counter; }
    Unlimited(const Unlimited &) = delete;
    Unlimited &operator=(const Unlimited &) = delete;
    int *m_counter;
};

QString extensionOf(lua_State *L) { return toQString(L, lua_upvalueindex(1)); }

QString formatNumber(double value, int decimals, bool comma)
{
    if (!std::isfinite(value))
        return std::isnan(value) ? u"—"_s : (value > 0 ? u"∞"_s : u"−∞"_s);
    decimals = std::clamp(decimals, 0, 10);
    QString s = QString::number(value, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(u'.'))
            s.chop(1);
    }
    if (s == u"-0")
        s = u"0"_s;
    if (comma)
        s.replace(u'.', u',');
    return s;
}

} // namespace

Engine::Engine() = default;

Engine::~Engine() { stop(); }

QString Engine::version() { return u"Luau 0.743"_s; }

Engine *Engine::self(lua_State *L) { return static_cast<Engine *>(lua_callbacks(L)->userdata); }

void *Engine::alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    auto *e = static_cast<Engine *>(ud);
    const size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        std::free(ptr);
        e->m_used -= old;
        return nullptr;
    }
    if (e->m_memoryLimit && !e->m_unlimited && nsize > old && e->m_used - old + nsize > e->m_memoryLimit) {
        e->m_outOfMemory = true; // sticky until the call ends: see interrupt()
        return nullptr;
    }
    void *p = std::realloc(ptr, nsize);
    if (p)
        e->m_used = e->m_used - old + nsize;
    return p;
}

// Called at every safepoint: loop back edges, calls and returns, every step of the pattern matcher
// and of the plain string search (gc = -1), and GC steps (gc >= 0, where raising is not allowed).
// Past the deadline (or after a refused allocation) the call is over: the thread the host resumed
// is yielded — a yield does not unwind, so no pcall on its stack ever sees it — and wherever a
// yield is impossible (a coroutine of the script, a library callback, the pattern matcher) an
// error is raised, again at every following safepoint, until it reaches that thread.
void Engine::interrupt(lua_State *L, int gc)
{
    if (gc >= 0)
        return;
    Engine *e = self(L);
    if (!e->m_timedOut && !e->m_outOfMemory) {
        // The clock every 16th safepoint: in the browser it is a call into JavaScript.
        if ((++e->m_ticks & 15) != 0 || e->m_clock.nsecsElapsed() <= e->m_deadlineNs)
            return;
        e->m_timedOut = true;
    }
    if (L == e->m_running && lua_isyieldable(L)) {
        lua_yield(L, 0);
        return;
    }
    lua_rawcheckstack(L, 1);
    ++e->m_unlimited;
    lua_pushstring(L, e->m_timedOut ? kTimeout : kNoMemory);
    --e->m_unlimited;
    lua_error(L);
}

bool Engine::start(Host *host, const Limits &limits, QString *error)
{
    stop();
    m_host = host;
    setLimits(limits);
    m_used = 0;
    m_clock.start();
    disarm();
    m_L = lua_newstate(&Engine::alloc, this);
    if (!m_L) {
        if (error)
            *error = u"could not create the Luau VM"_s;
        return false;
    }
    // tostring(table) and friends print encoded pointers: no heap addresses for scripts.
    QRandomGenerator *random = QRandomGenerator::system();
    lua_setpointerencodekey(m_L, random->generate64(), random->generate64(), random->generate64(), random->generate64());
    lua_Callbacks *cb = lua_callbacks(m_L);
    cb->userdata = this;
    cb->interrupt = &Engine::interrupt;
    Unlimited unlimited(&m_unlimited);
    luaL_openlibs(m_L);
    // The standard library is sandbox-safe (no io, os.execute, loadstring, require; debug has only
    // info and traceback); print writes to stdout, so each extension gets its own (b.log).
    lua_pushnil(m_L);
    lua_setglobal(m_L, "print");
    luaL_sandbox(m_L); // every library table, the string metatable and the globals: read-only

    lua_CompileOptions options = {};
    options.optimizationLevel = 1;
    size_t size = 0;
    char *bytecode = luau_compile(kUiPrelude, sizeof(kUiPrelude) - 1, &options, &size);
    const int rc = luau_load(m_L, "=b.ui", bytecode, size, 0);
    std::free(bytecode);
    if (rc != 0 || lua_pcall(m_L, 0, 1, 0) != 0) {
        if (error)
            *error = u"b.ui: "_s + toQString(m_L, -1);
        stop();
        return false;
    }
    m_uiRef = lua_ref(m_L, -1);
    lua_pop(m_L, 1);
    return true;
}

void Engine::setLimits(const Limits &limits)
{
    m_limits = limits;
    m_memoryLimit = limits.memoryBytes > 0 ? size_t(limits.memoryBytes) : 0;
}

void Engine::stop()
{
    if (m_L)
        lua_close(m_L);
    m_L = nullptr;
    m_loaded.clear();
    m_commands.clear();
    m_views.clear();
    m_running = nullptr;
    m_runningRef = 0;
    m_uiRef = 0;
    m_asking = false;
}

void Engine::arm()
{
    m_timedOut = m_outOfMemory = false;
    m_ticks = 0;
    m_deadlineNs = m_clock.nsecsElapsed() + qint64(m_limits.timeoutMs) * 1000000;
}

void Engine::disarm() { m_deadlineNs = LLONG_MAX; }

void Engine::pushEnvironment(const QString &extension)
{
    lua_State *L = m_L;
    const QByteArray id = extension.toUtf8();
    lua_createtable(L, 0, 4); // the environment: own globals, reads fall through to the library
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setreadonly(L, -1, true);
    lua_setmetatable(L, -2);

    lua_createtable(L, 0, 24); // b
    const auto set = [&](const char *name, lua_CFunction f) {
        lua_pushlstring(L, id.constData(), size_t(id.size()));
        lua_pushcclosure(L, f, name, 1);
        lua_setfield(L, -2, name);
    };
    set("command", &Engine::apiCommand);
    set("view", &Engine::apiView);
    set("pages", &Engine::apiPages);
    set("read", &Engine::apiRead);
    set("page", &Engine::apiPage);
    set("blocks", &Engine::apiBlocks);
    set("write", &Engine::apiWrite);
    set("appendToJournal", &Engine::apiAppendToJournal);
    set("replaceBlock", &Engine::apiReplaceBlock);
    set("today", &Engine::apiToday);
    set("daysBetween", &Engine::apiDaysBetween);
    set("sorted", &Engine::apiSorted);
    set("fold", &Engine::apiFold);
    set("formatNumber", &Engine::apiFormatNumber);
    set("lang", &Engine::apiLang);
    set("uiLang", &Engine::apiUiLang);
    set("log", &Engine::apiLog);
    set("timer", &Engine::apiTimer);
    set("data", &Engine::apiData);
    set("ask", &Engine::apiAsk);
    set("choose", &Engine::apiChoose);
    set("form", &Engine::apiForm);
    lua_getref(L, m_uiRef);
    lua_setfield(L, -2, "ui");
    lua_setreadonly(L, -1, true);
    lua_setfield(L, -2, "b");

    lua_pushlstring(L, id.constData(), size_t(id.size()));
    lua_pushcclosure(L, &Engine::apiPrint, "print", 1);
    lua_setfield(L, -2, "print");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
    // Nothing in the environment shadows a library: builtins may be resolved at load time.
    lua_setsafeenv(L, -1, true);
}

void Engine::dropExtension(const QString &extension)
{
    if (!m_L)
        return;
    for (auto *map : {&m_commands, &m_views}) {
        for (auto it = map->begin(); it != map->end();) {
            if (it->extension == extension) {
                lua_unref(m_L, it->ref);
                if (it->actionsRef)
                    lua_unref(m_L, it->actionsRef);
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
    if (const auto it = m_loaded.constFind(extension); it != m_loaded.constEnd()) {
        lua_unref(m_L, it->envRef);
        m_loaded.erase(it);
    }
}

Result Engine::load(const Extension &extension)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    releaseThread();
    dropExtension(extension.id);
    // Brulion compiles the source itself: scripts never see a compiler or a bytecode loader
    // (loading untrusted bytecode would not be safe).
    const QByteArray src = extension.source.toUtf8();
    lua_CompileOptions options = {};
    options.optimizationLevel = 1;
    options.debugLevel = 1; // line info for errors and tracebacks
    size_t size = 0;
    char *bytecode = luau_compile(src.constData(), size_t(src.size()), &options, &size);
    const QByteArray chunk = "=" + extension.id.toUtf8();
    int rc;
    {
        Unlimited unlimited(&m_unlimited);
        pushEnvironment(extension.id);
        Loaded loaded;
        loaded.envRef = lua_ref(m_L, -1);
        loaded.permissions = extension.permissions;
        loaded.data = extension.data;
        m_loaded.insert(extension.id, loaded);
        rc = luau_load(m_L, chunk.constData(), bytecode, size, -1);
    }
    std::free(bytecode);
    if (rc != 0) {
        const QString message = toQString(m_L, -1);
        lua_pop(m_L, 2);
        dropExtension(extension.id);
        return {Status::Error, {}, message};
    }
    lua_remove(m_L, -2); // the environment (the chunk keeps it)
    Result r = run(extension.id, {});
    if (r.status == Status::Suspended) {
        releaseThread();
        r = {Status::Error, {}, extension.id + u": an extension cannot ask a question while it loads"_s};
    }
    if (r.status != Status::Ok)
        dropExtension(extension.id);
    r.value = QVariant();
    return r;
}

void Engine::unload(const QString &id)
{
    releaseThread();
    dropExtension(id);
}

QStringList Engine::extensions() const
{
    QStringList ids = m_loaded.keys();
    ids.sort();
    return ids;
}

QList<CommandInfo> Engine::commands() const
{
    QList<CommandInfo> out;
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        const QString name = it.key().mid(it->extension.size() + 1);
        out.append({it->extension, name, it->label.isEmpty() ? name : it->label});
    }
    std::sort(out.begin(), out.end(), [](const CommandInfo &a, const CommandInfo &b) {
        return a.extension != b.extension ? a.extension < b.extension : a.name < b.name;
    });
    return out;
}

QList<ViewInfo> Engine::views() const
{
    QList<ViewInfo> out;
    for (auto it = m_views.cbegin(); it != m_views.cend(); ++it) {
        ViewInfo info{it->extension, it.key().mid(it->extension.size() + 1), {}};
        if (it->actionsRef) {
            lua_getref(m_L, it->actionsRef);
            lua_pushnil(m_L);
            while (lua_next(m_L, -2)) {
                if (lua_type(m_L, -2) == LUA_TSTRING)
                    info.actions << toQString(m_L, -2);
                lua_pop(m_L, 1);
            }
            lua_pop(m_L, 1);
            info.actions.sort();
        }
        out << info;
    }
    std::sort(out.begin(), out.end(), [](const ViewInfo &a, const ViewInfo &b) {
        return a.extension != b.extension ? a.extension < b.extension : a.name < b.name;
    });
    return out;
}

Result Engine::run(const QString &extension, const QVariantList &args)
{
    lua_State *co;
    {
        Unlimited unlimited(&m_unlimited);
        co = lua_newthread(m_L); // m_L: fn, co
        m_runningRef = lua_ref(m_L, -1);
        lua_pop(m_L, 1);
        lua_xmove(m_L, co, 1);
        // The thread's globals are the extension's environment too: getfenv(0) and the
        // environment of C functions give the extension nothing but its own table.
        lua_getref(co, m_loaded.value(extension).envRef);
        lua_replace(co, LUA_GLOBALSINDEX);
        for (const QVariant &a : args)
            pushVariant(co, a);
    }
    m_running = co;
    m_asking = false;
    arm();
    const int status = lua_resume(co, nullptr, int(args.size()));
    return finish(status);
}

Result Engine::call(const QString &extension, int functionRef, const QVariantList &args)
{
    releaseThread(); // a call still waiting for an answer is abandoned
    lua_getref(m_L, functionRef);
    return run(extension, args);
}

void Engine::releaseThread()
{
    if (!m_running)
        return;
    lua_unref(m_L, m_runningRef);
    m_running = nullptr;
    m_runningRef = 0;
    m_asking = false;
}

void Engine::cancel() { releaseThread(); }

Result Engine::finish(int status)
{
    lua_State *co = m_running;
    disarm();
    if (m_timedOut) {
        releaseThread();
        return {Status::Timeout, {}, QString::fromLatin1(kTimeout)};
    }
    if (status == LUA_ERRMEM || m_outOfMemory) {
        releaseThread();
        lua_gc(m_L, LUA_GCCOLLECT, 0);
        return {Status::OutOfMemory, {}, QString::fromLatin1(kNoMemory)};
    }
    if (status == LUA_YIELD) {
        if (m_asking) {
            QVariant question;
            QString error;
            {
                Unlimited unlimited(&m_unlimited);
                toVariant(co, -1, &question, &error);
            }
            lua_settop(co, 0);
            m_asking = false;
            return {Status::Suspended, question, {}};
        }
        releaseThread();
        return {Status::Error, {}, u"coroutine.yield outside a coroutine"_s};
    }
    Result r;
    Unlimited unlimited(&m_unlimited);
    if (status == LUA_OK) {
        if (lua_gettop(co) > 0) {
            QString error;
            if (!toVariant(co, 1, &r.value, &error))
                r = {Status::Error, {}, error};
        }
    } else {
        // No luaL_tolstring here: a __tostring metamethod would run script code with no deadline.
        const int t = lua_type(co, -1);
        const QString message = t == LUA_TSTRING || t == LUA_TNUMBER
                                        ? toQString(co, -1)
                                        : u"error object is a %1 value"_s.arg(QString::fromUtf8(luaL_typename(co, -1)));
        const bool overflow = message.contains(u"stack overflow"_s);
        QString traceback;
        if (!overflow) { // the thread's frames are still there after the error
            luaL_traceback(m_L, co, nullptr, 0);
            traceback = toQString(m_L, -1).trimmed();
            lua_pop(m_L, 1);
        }
        r = {overflow ? Status::StackOverflow : Status::Error, {},
             traceback.isEmpty() || traceback == message ? message : message + u"\nstack traceback:\n"_s + traceback};
    }
    releaseThread();
    return r;
}

Result Engine::runCommand(const QString &extension, const QString &name, const QVariantMap &ctx)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto it = m_commands.constFind(extension + u'/' + name);
    if (it == m_commands.constEnd())
        return {Status::NotFound, {}, u"no command %1/%2"_s.arg(extension, name)};
    return call(extension, it->ref, {ctx});
}

Result Engine::render(const QString &extension, const QString &view, const QVariantMap &params)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto it = m_views.constFind(extension + u'/' + view);
    if (it == m_views.constEnd())
        return {Status::NotFound, {}, u"no view %1/%2"_s.arg(extension, view)};
    return call(extension, it->ref, {params});
}

Result Engine::act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto it = m_views.constFind(extension + u'/' + view);
    if (it == m_views.constEnd() || !it->actionsRef)
        return {Status::NotFound, {}, u"no view %1/%2 with actions"_s.arg(extension, view)};
    releaseThread();
    lua_getref(m_L, it->actionsRef);
    const QByteArray key = action.toUtf8();
    lua_pushlstring(m_L, key.constData(), size_t(key.size()));
    lua_rawget(m_L, -2);
    lua_remove(m_L, -2);
    if (lua_type(m_L, -1) != LUA_TFUNCTION) {
        lua_pop(m_L, 1);
        return {Status::NotFound, {}, u"no action %1 in the view %2/%3"_s.arg(action, extension, view)};
    }
    return run(extension, {args});
}

Result Engine::resume(const QVariant &answer)
{
    if (!m_running || lua_status(m_running) != LUA_YIELD)
        return {Status::Error, {}, u"nothing is waiting for an answer"_s};
    {
        Unlimited unlimited(&m_unlimited);
        pushVariant(m_running, answer);
    }
    arm();
    const int status = lua_resume(m_running, nullptr, 1);
    return finish(status);
}

// ---- the `b` API -------------------------------------------------------------------------------

int Engine::apiCommand(lua_State *L) { return finishApi(L, self(L)->commandImpl(L)); }
int Engine::apiView(lua_State *L) { return finishApi(L, self(L)->viewImpl(L)); }
int Engine::apiPages(lua_State *L) { return finishApi(L, self(L)->pagesImpl(L)); }
int Engine::apiRead(lua_State *L) { return finishApi(L, self(L)->readImpl(L)); }
int Engine::apiPage(lua_State *L) { return finishApi(L, self(L)->pageImpl(L)); }
int Engine::apiBlocks(lua_State *L) { return finishApi(L, self(L)->blocksImpl(L)); }
int Engine::apiWrite(lua_State *L) { return finishApi(L, self(L)->writeImpl(L)); }
int Engine::apiAppendToJournal(lua_State *L) { return finishApi(L, self(L)->appendImpl(L)); }
int Engine::apiReplaceBlock(lua_State *L) { return finishApi(L, self(L)->replaceImpl(L)); }
int Engine::apiDaysBetween(lua_State *L) { return finishApi(L, self(L)->daysImpl(L)); }
int Engine::apiSorted(lua_State *L) { return finishApi(L, self(L)->sortedImpl(L)); }
int Engine::apiFold(lua_State *L) { return finishApi(L, self(L)->foldImpl(L)); }
int Engine::apiFormatNumber(lua_State *L) { return finishApi(L, self(L)->formatImpl(L)); }
int Engine::apiLog(lua_State *L) { return finishApi(L, self(L)->logImpl(L)); }
int Engine::apiPrint(lua_State *L) { return finishApi(L, self(L)->printImpl(L)); }
int Engine::apiTimer(lua_State *L) { return finishApi(L, self(L)->timerImpl(L)); }
int Engine::apiData(lua_State *L) { return finishApi(L, self(L)->dataImpl(L)); }
int Engine::apiAsk(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "ask")); }
int Engine::apiChoose(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "choose")); }
int Engine::apiForm(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "form")); }

int Engine::apiToday(lua_State *L)
{
    Engine *e = self(L);
    {
        Unlimited unlimited(&e->m_unlimited);
        pushString(L, e->m_host->today());
    }
    return 1;
}

int Engine::apiLang(lua_State *L)
{
    Engine *e = self(L);
    {
        Unlimited unlimited(&e->m_unlimited);
        pushString(L, e->m_host->language());
    }
    return 1;
}

int Engine::apiUiLang(lua_State *L)
{
    Engine *e = self(L);
    {
        Unlimited unlimited(&e->m_unlimited);
        pushString(L, e->m_host->uiLanguage());
    }
    return 1;
}

int Engine::commandImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString name;
    if (!argString(L, 1, "b.command", &name))
        return kRaise;
    if (lua_type(L, 2) != LUA_TFUNCTION)
        return failWith(L, u"b.command: argument 2 must be a function"_s);
    QString label;
    if (lua_type(L, 3) == LUA_TTABLE) {
        lua_rawgetfield(L, 3, "label");
        if (lua_type(L, -1) == LUA_TSTRING)
            label = toQString(L, -1);
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 2);
    Handler h{extension, lua_ref(L, -1), 0, label};
    lua_pop(L, 1);
    const QString key = extension + u'/' + name;
    if (const auto it = m_commands.constFind(key); it != m_commands.constEnd())
        lua_unref(L, it->ref);
    m_commands.insert(key, h);
    return 0;
}

int Engine::viewImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString name;
    if (!argString(L, 1, "b.view", &name))
        return kRaise;
    if (lua_type(L, 2) != LUA_TFUNCTION)
        return failWith(L, u"b.view: argument 2 must be a function"_s);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TTABLE)
        return failWith(L, u"b.view: argument 3 must be a table of actions"_s);
    Handler h{extension, 0, 0, {}};
    lua_pushvalue(L, 2);
    h.ref = lua_ref(L, -1);
    lua_pop(L, 1);
    if (lua_type(L, 3) == LUA_TTABLE) {
        // A copy: changing the table later does not add actions behind the host's back.
        lua_createtable(L, 0, 8);
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_rawset(L, -5);
            }
            lua_pop(L, 1);
        }
        h.actionsRef = lua_ref(L, -1);
        lua_pop(L, 1);
    }
    const QString key = extension + u'/' + name;
    if (const auto it = m_views.constFind(key); it != m_views.constEnd()) {
        lua_unref(L, it->ref);
        if (it->actionsRef)
            lua_unref(L, it->actionsRef);
    }
    m_views.insert(key, h);
    return 0;
}

int Engine::pagesImpl(lua_State *L)
{
    QString tag;
    if (lua_type(L, 1) == LUA_TTABLE) {
        lua_rawgetfield(L, 1, "tag");
        if (lua_type(L, -1) == LUA_TSTRING)
            tag = toQString(L, -1);
        lua_pop(L, 1);
    }
    const QStringList pages = m_host->pages(tag);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, pages);
    return 1;
}

int Engine::readImpl(lua_State *L)
{
    QString name;
    if (!argString(L, 1, "b.read", &name))
        return kRaise;
    const auto content = m_host->read(name);
    Unlimited unlimited(&m_unlimited);
    if (content)
        pushString(L, *content);
    else
        lua_pushnil(L);
    return 1;
}

int Engine::pageImpl(lua_State *L)
{
    QString name;
    if (!argString(L, 1, "b.page", &name))
        return kRaise;
    const QVariantMap page = m_host->page(name);
    Unlimited unlimited(&m_unlimited);
    if (page.isEmpty())
        lua_pushnil(L);
    else
        pushVariant(L, page);
    return 1;
}

int Engine::blocksImpl(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TTABLE)
        return failWith(L, u"b.blocks: argument 1 must be a query table"_s);
    QVariant value;
    if (!argValue(L, 1, "b.blocks", &value))
        return kRaise;
    const QVariantMap map = value.toMap();
    BlockQuery query;
    query.page = map.value(u"page"_s).toString();
    query.tag = map.value(u"tag"_s).toString().toLower();
    query.journalOnly = map.value(u"journal"_s).toBool();
    query.from = map.value(u"from"_s).toString();
    query.to = map.value(u"to"_s).toString();
    if (query.page.isEmpty() && query.tag.isEmpty())
        return failWith(L, u"b.blocks: the query needs a page or a tag"_s);
    const QVariantList blocks = m_host->blocks(query);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, blocks);
    return 1;
}

bool Engine::mayWrite(lua_State *L, const QString &extension, const char *function)
{
    if (m_loaded.value(extension).permissions.contains(u"write"_s))
        return true;
    failWith(L, QString::fromLatin1(function) + u": permission denied: write"_s);
    return false;
}

int Engine::writeImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString name, content;
    if (!argString(L, 1, "b.write", &name) || !argString(L, 2, "b.write", &content))
        return kRaise;
    if (!mayWrite(L, extension, "b.write"))
        return kRaise;
    const QString error = m_host->write(extension, name, content);
    if (!error.isEmpty())
        return failWith(L, u"b.write: "_s + error);
    lua_pushboolean(L, 1);
    return 1;
}

int Engine::appendImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString date, markdown;
    if (!argString(L, 1, "b.appendToJournal", &date) || !argString(L, 2, "b.appendToJournal", &markdown))
        return kRaise;
    if (!QDate::fromString(date, Qt::ISODate).isValid())
        return failWith(L, u"b.appendToJournal: invalid date: "_s + date);
    if (!mayWrite(L, extension, "b.appendToJournal"))
        return kRaise;
    const QString error = m_host->appendToJournal(extension, date, markdown);
    if (!error.isEmpty())
        return failWith(L, u"b.appendToJournal: "_s + error);
    return 0;
}

int Engine::replaceImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, text, task;
    if (!argString(L, 1, "b.replaceBlock", &ref) || !argString(L, 2, "b.replaceBlock", &text)
        || !argOptString(L, 3, "b.replaceBlock", &task))
        return kRaise;
    if (text.contains(u'\n') || text.contains(u'\r'))
        return failWith(L, u"b.replaceBlock: the text must be one line"_s);
    if (!lua_isnoneornil(L, 3) && task.size() != 1)
        return failWith(L, u"b.replaceBlock: the task status must be one character"_s);
    if (!mayWrite(L, extension, "b.replaceBlock"))
        return kRaise;
    QString newRef;
    const std::optional<QString> status = lua_isnoneornil(L, 3) ? std::nullopt : std::optional<QString>(task);
    const QString error = m_host->replaceBlock(extension, ref, text, status, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.replaceBlock: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

int Engine::daysImpl(lua_State *L)
{
    QString a, b;
    if (!argString(L, 1, "b.daysBetween", &a) || !argString(L, 2, "b.daysBetween", &b))
        return kRaise;
    const QDate from = QDate::fromString(a, Qt::ISODate), to = QDate::fromString(b, Qt::ISODate);
    if (!from.isValid() || !to.isValid())
        return failWith(L, u"b.daysBetween: invalid date: %1 / %2"_s.arg(a, b));
    lua_pushnumber(L, double(from.daysTo(to)));
    return 1;
}

int Engine::sortedImpl(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TTABLE)
        return failWith(L, u"b.sorted: argument 1 must be a list of strings"_s);
    QStringList list;
    const int n = lua_objlen(L, 1);
    if (n > 100000)
        return failWith(L, u"b.sorted: at most 100000 strings"_s);
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 1, i);
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return failWith(L, u"b.sorted: item %1 is not a string"_s.arg(i));
        }
        list << toQString(L, -1);
        lua_pop(L, 1);
    }
    std::stable_sort(list.begin(), list.end(), [](const QString &x, const QString &y) { return text::compareNames(x, y) < 0; });
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, list);
    return 1;
}

int Engine::foldImpl(lua_State *L)
{
    QString s;
    if (!argString(L, 1, "b.fold", &s))
        return kRaise;
    Unlimited unlimited(&m_unlimited);
    pushString(L, text::foldKey(s).simplified());
    return 1;
}

int Engine::formatImpl(lua_State *L)
{
    double value = 0;
    if (!argNumber(L, 1, "b.formatNumber", &value))
        return kRaise;
    double decimals = 2;
    if (!lua_isnoneornil(L, 2) && !argNumber(L, 2, "b.formatNumber", &decimals))
        return kRaise;
    const bool comma = m_host->language() == u"pl";
    Unlimited unlimited(&m_unlimited);
    pushString(L, formatNumber(value, int(decimals), comma));
    return 1;
}

int Engine::logImpl(lua_State *L)
{
    QString message;
    if (!argString(L, 1, "b.log", &message))
        return kRaise;
    m_host->log(extensionOf(L), message.left(4000));
    return 0;
}

int Engine::printImpl(lua_State *L)
{
    QStringList parts;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        // No luaL_tolstring: a __tostring metamethod would run script code here.
        switch (lua_type(L, i)) {
        case LUA_TSTRING: case LUA_TNUMBER: parts << toQString(L, i); break;
        case LUA_TBOOLEAN: parts << (lua_toboolean(L, i) ? u"true"_s : u"false"_s); break;
        case LUA_TNIL: parts << u"nil"_s; break;
        default: parts << QString::fromUtf8(luaL_typename(L, i));
        }
    }
    m_host->log(extensionOf(L), parts.join(u'\t').left(4000));
    return 0;
}

int Engine::timerImpl(lua_State *L)
{
    double seconds = 0;
    QString label;
    if (!argNumber(L, 1, "b.timer", &seconds) || !argOptString(L, 2, "b.timer", &label))
        return kRaise;
    if (!std::isfinite(seconds) || seconds < 1 || seconds > 24 * 3600)
        return failWith(L, u"b.timer: seconds must be between 1 and 86400"_s);
    m_host->timer(extensionOf(L), int(std::lround(seconds)), label);
    return 0;
}

int Engine::dataImpl(lua_State *L)
{
    QString name;
    if (!argString(L, 1, "b.data", &name))
        return kRaise;
    const QVariant value = m_loaded.value(extensionOf(L)).data.value(name);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, value);
    return 1;
}

int Engine::questionImpl(lua_State *L, const char *kind)
{
    const QString function = u"b."_s + QString::fromLatin1(kind);
    QVariantMap question{{u"kind"_s, QString::fromLatin1(kind)}};
    const QByteArray fn = function.toLatin1();
    if (qstrcmp(kind, "form") == 0) {
        QString title;
        QVariant fields;
        if (!argString(L, 1, fn.constData(), &title) || !argValue(L, 2, fn.constData(), &fields))
            return kRaise;
        if (fields.metaType().id() != QMetaType::QVariantList)
            return failWith(L, function + u": argument 2 must be a list of fields"_s);
        question.insert(u"title"_s, title);
        question.insert(u"fields"_s, fields);
    } else {
        QString prompt;
        if (!argString(L, 1, fn.constData(), &prompt))
            return kRaise;
        question.insert(u"prompt"_s, prompt);
        if (qstrcmp(kind, "choose") == 0) {
            QVariant options;
            if (!argValue(L, 2, fn.constData(), &options))
                return kRaise;
            if (options.metaType().id() != QMetaType::QVariantList && !(options.toMap().isEmpty() && lua_type(L, 2) == LUA_TTABLE))
                return failWith(L, function + u": argument 2 must be a list of strings"_s);
            question.insert(u"options"_s, options.toList());
        }
    }
    if (L != m_running || !lua_isyieldable(L))
        return failWith(L, function + u": cannot wait for an answer here (inside a coroutine or a library callback)"_s);
    {
        Unlimited unlimited(&m_unlimited);
        lua_settop(L, 0);
        pushVariant(L, question);
    }
    m_asking = true;
    return kYield;
}

} // namespace brulion::extensions
