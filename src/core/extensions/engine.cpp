#include "extensions/engine.h"

#include "extensions/blockedit.h"
#include "extensions/host.h"
#include "dates/names.h"
#include "dates/recurrence.h"
#include "text/text.h"
#include "util/unicode.h"

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <QDate>
#include <QRandomGenerator>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <utility>

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
-- A list written with conditions in it — { a, show and b or nil, c } — as what is there, in order.
-- (A table with a nil in the middle is no sequence: it would reach the app as a record with the
-- keys "1" and "3", and nothing of it would be drawn.)
local function present(list)
    if type(list) ~= "table" then
        return list
    end
    local last = table.maxn(list)
    local out = nil
    for i = 1, last do
        local item = list[i]
        if item == nil then
            out = out or table.move(list, 1, i - 1, 1, {})
        elseif out then
            table.insert(out, item)
        end
    end
    return out or list
end
function ui.column(children) return { type = "column", children = present(children) } end
function ui.row(children) return { type = "row", children = present(children) } end
function ui.heading(text) return { type = "heading", text = text } end
-- b.ui.text(text, true) or b.ui.text(text, { muted = true, icon = "flame", tone = "success" })
function ui.text(text, options)
    if type(options) == "table" then
        return { type = "text", text = text, muted = options.muted == true, icon = options.icon, tone = options.tone }
    end
    return { type = "text", text = text, muted = options == true }
end
function ui.link(page, label, line) return { type = "link", page = page, label = label, line = line } end
function ui.card(children) return { type = "card", children = present(children) } end
function ui.menu(items, label) return { type = "menu", items = present(items), label = label } end
function ui.month(spec)
    return { type = "month", month = spec.month, days = spec.days, label = spec.label }
end
function ui.button(label, action, args) return { type = "button", label = label, action = action, args = args } end
-- checked: true, false or "skipped" (a cancelled task's look); options: { menu = items, label = … }
function ui.check(checked, action, args, options)
    local state = if checked == "skipped" then "skipped" elseif checked == true then "done" else "open"
    local node = { type = "check", checked = checked == true, state = state, action = action, args = args }
    if type(options) == "table" then
        node.menu = present(options.menu)
        node.label = options.label
    end
    return node
end
function ui.stepper(spec)
    return { type = "stepper", value = spec.value, step = spec.step, unit = spec.unit, action = spec.action, args = spec.args }
end
function ui.chart(spec)
    return { type = "chart", kind = spec.kind, title = spec.title, labels = spec.labels, series = spec.series }
end
function ui.table(columns, rows)
    -- A cell that is nil (a value that is not there) shows empty, and its row keeps its columns.
    if type(rows) == "table" then
        local filled = {}
        for _, row in present(rows) do
            if type(row) == "table" then
                local cells = {}
                for i = 1, table.maxn(row) do
                    local cell = row[i]
                    cells[i] = if cell == nil then "" else cell
                end
                row = cells
            end
            table.insert(filled, row)
        end
        rows = filled
    end
    return { type = "table", columns = columns, rows = rows }
end
function ui.empty(title, text, icon) return { type = "empty", title = title, text = text, icon = icon } end
function ui.open(label, view, params) return { type = "open", label = label, view = view, params = params } end
function ui.run(label, command) return { type = "run", label = label, command = command } end
return table.freeze(ui)
)luau";

// b.ai: the host's answer comes back as a table; the wrapper turns {error} into an error at the
// script's line and returns the text (and the whole reply).
const char kAiPrelude[] = R"luau(
return function(raw)
    return function(request)
        local reply = raw(request)
        if type(reply) ~= "table" then
            error("b.ai: no answer", 2)
        end
        if reply.error ~= nil then
            error("b.ai: " .. tostring(reply.error), 2)
        end
        return reply.text, reply
    end
end
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

// A count a script gave as a number, within [low, high]; what is no number (0/0): `fallback`.
int countFrom(double value, int low, int high, int fallback)
{
    return std::isnan(value) ? fallback : int(std::clamp(value, double(low), double(high)));
}

// A tag as a script names it: "project", "#project" and " Project " are one tag.
QString tagName(const QString &tag)
{
    QString name = tag.trimmed();
    while (name.startsWith(u'#'))
        name.remove(0, 1);
    return name;
}

// A property filter as a script writes it: "habit" or { "habit", "nawyk" } (keys in any case).
bool propsFrom(const QVariant &value, QStringList *out)
{
    out->clear();
    if (!value.isValid() || value.isNull())
        return true;
    QVariantList list;
    if (value.metaType().id() == QMetaType::QString)
        list << value;
    else if (value.metaType().id() == QMetaType::QVariantList)
        list = value.toList();
    else
        return false;
    for (const QVariant &item : std::as_const(list)) {
        if (item.metaType().id() != QMetaType::QString)
            return false;
        const QString key = item.toString().trimmed().toLower();
        if (!key.isEmpty() && !out->contains(key))
            *out << key;
    }
    return true;
}

dates::UiLocale localeOf(const QString &language)
{
    return language == u"pl" ? dates::UiLocale::Polish : dates::UiLocale::English;
}

QString frequencyName(dates::RecurrenceRule::Freq freq)
{
    switch (freq) {
    case dates::RecurrenceRule::Freq::Day: return u"day"_s;
    case dates::RecurrenceRule::Freq::Week: return u"week"_s;
    case dates::RecurrenceRule::Freq::Month: return u"month"_s;
    case dates::RecurrenceRule::Freq::Year: return u"year"_s;
    }
    return {};
}

QVariantMap ruleMap(const dates::RecurrenceRule &rule)
{
    QVariantMap map{{u"every"_s, frequencyName(rule.freq)}, {u"interval"_s, rule.interval}};
    if (!rule.weekdays.isEmpty()) {
        QVariantList days;
        for (const int d : rule.weekdays)
            days << d;
        map.insert(u"weekdays"_s, days);
    }
    if (rule.monthDay)
        map.insert(u"monthDay"_s, *rule.monthDay);
    if (rule.lastDayOfMonth)
        map.insert(u"lastDay"_s, true);
    if (rule.weekdaysOnly)
        map.insert(u"workdays"_s, true);
    if (rule.whenDone)
        map.insert(u"whenDone"_s, true);
    return map;
}

// A rule as ruleMap() gives it (or a script builds it); an error message when it is none.
QString ruleFrom(const QVariantMap &map, dates::RecurrenceRule *rule)
{
    const QString every = map.value(u"every"_s).toString();
    if (every == u"day")
        rule->freq = dates::RecurrenceRule::Freq::Day;
    else if (every == u"week")
        rule->freq = dates::RecurrenceRule::Freq::Week;
    else if (every == u"month")
        rule->freq = dates::RecurrenceRule::Freq::Month;
    else if (every == u"year")
        rule->freq = dates::RecurrenceRule::Freq::Year;
    else
        return u"every must be \"day\", \"week\", \"month\" or \"year\""_s;
    const QVariant interval = map.value(u"interval"_s, 1);
    bool ok = false;
    rule->interval = interval.toInt(&ok);
    if (!ok || rule->interval < 1 || rule->interval > 999)
        return u"interval must be a whole number from 1 to 999"_s;
    for (const QVariant &d : map.value(u"weekdays"_s).toList()) {
        const int day = d.toInt(&ok);
        if (!ok || day < 1 || day > 7)
            return u"weekdays are numbers from 1 (Monday) to 7 (Sunday)"_s;
        if (!rule->weekdays.contains(day))
            rule->weekdays << day;
    }
    std::sort(rule->weekdays.begin(), rule->weekdays.end());
    if (map.contains(u"monthDay"_s)) {
        const int day = map.value(u"monthDay"_s).toInt(&ok);
        if (!ok || day < 1 || day > 31)
            return u"monthDay must be a day from 1 to 31"_s;
        rule->monthDay = day;
    }
    rule->lastDayOfMonth = map.value(u"lastDay"_s).toBool();
    rule->weekdaysOnly = map.value(u"workdays"_s).toBool();
    rule->whenDone = map.value(u"whenDone"_s).toBool();
    return {};
}

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

    bytecode = luau_compile(kAiPrelude, sizeof(kAiPrelude) - 1, &options, &size);
    const int aiRc = luau_load(m_L, "=b.ai", bytecode, size, 0);
    std::free(bytecode);
    if (aiRc != 0 || lua_pcall(m_L, 0, 1, 0) != 0) {
        if (error)
            *error = u"b.ai: "_s + toQString(m_L, -1);
        stop();
        return false;
    }
    m_aiFactoryRef = lua_ref(m_L, -1);
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
    m_hooks.clear();
    m_running = nullptr;
    m_runningRef = 0;
    m_runningExtension.clear();
    m_runningCall = 0;
    m_waiting.clear();
    m_uiRef = 0;
    m_aiFactoryRef = 0;
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

    lua_createtable(L, 0, 48); // b
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
    set("search", &Engine::apiSearch);
    set("query", &Engine::apiQuery);
    set("checkExtension", &Engine::apiCheckExtension);
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
    set("status", &Engine::apiStatus);
    set("timer", &Engine::apiTimer);
    set("data", &Engine::apiData);
    set("ask", &Engine::apiAsk);
    set("choose", &Engine::apiChoose);
    set("form", &Engine::apiForm);
    set("on", &Engine::apiOn);
    set("attachments", &Engine::apiAttachments);
    set("revision", &Engine::apiRevision);
    set("now", &Engine::apiNow);
    set("addDays", &Engine::apiAddDays);
    set("addMonths", &Engine::apiAddMonths);
    set("weekday", &Engine::apiWeekday);
    set("weekStart", &Engine::apiWeekStart);
    set("weekdayName", &Engine::apiWeekdayName);
    set("monthName", &Engine::apiMonthName);
    set("lower", &Engine::apiLower);
    set("plain", &Engine::apiPlain);
    set("parseRecurrence", &Engine::apiParseRecurrence);
    set("parseWeekdays", &Engine::apiParseWeekdays);
    set("formatRecurrence", &Engine::apiFormatRecurrence);
    set("appendToPage", &Engine::apiAppendToPage);
    set("insertBlock", &Engine::apiInsertBlock);
    set("removeBlock", &Engine::apiRemoveBlock);
    set("setTask", &Engine::apiSetTask);
    set("setProperty", &Engine::apiSetProperty);
    // b.ai = factory(raw): the raw C function yields; the Luau wrapper raises its errors.
    lua_getref(L, m_aiFactoryRef);
    lua_pushlstring(L, id.constData(), size_t(id.size()));
    lua_pushcclosure(L, &Engine::apiAi, "ai", 1);
    lua_call(L, 1, 1);
    lua_setfield(L, -2, "ai");
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
    for (auto it = m_hooks.begin(); it != m_hooks.end();) {
        if (it->extension == extension) {
            lua_unref(m_L, it->ref);
            it = m_hooks.erase(it);
        } else {
            ++it;
        }
    }
    dropWaiting(extension);
    if (const auto it = m_loaded.constFind(extension); it != m_loaded.constEnd()) {
        lua_unref(m_L, it->envRef);
        m_loaded.erase(it);
    }
}

namespace {
const char kBusy[] = "the extension engine is busy: a call runs (a host function may not start another)";
}

Result Engine::load(const Extension &extension)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    if (m_running)
        return {Status::Error, {}, QString::fromLatin1(kBusy)};
    dropExtension(extension.id);
    // Brulion compiles the source itself: scripts never see a compiler or a bytecode loader
    // (loading untrusted bytecode would not be safe).
    const QByteArray src = extension.source.toUtf8();
    lua_CompileOptions options = {};
    options.optimizationLevel = 1;
    options.debugLevel = 1; // line info for errors and tracebacks
    size_t size = 0;
    char *bytecode = luau_compile(src.constData(), size_t(src.size()), &options, &size);
    const QByteArray chunk = "=" + (extension.name.isEmpty() ? extension.id : extension.name).toUtf8();
    int rc;
    {
        Unlimited unlimited(&m_unlimited);
        pushEnvironment(extension.id);
        Loaded loaded;
        loaded.envRef = lua_ref(m_L, -1);
        loaded.permissions = extension.permissions;
        loaded.data = extension.data;
        loaded.name = extension.name.isEmpty() ? extension.id : extension.name;
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
        cancel(r.call);
        r = {Status::Error, {}, extension.id + u": an extension cannot ask a question while it loads"_s};
    }
    if (r.status != Status::Ok)
        dropExtension(extension.id);
    r.value = QVariant();
    return r;
}

void Engine::unload(const QString &id)
{
    if (m_running && m_runningExtension == id)
        return; // never under a running call of its own (it is unloaded by whoever asks later)
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
        out.append({it->extension, name, it->label.isEmpty() ? name : it->label, it->needsBlock, it->palette});
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
        ViewInfo info{it->extension, it.key().mid(it->extension.size() + 1), {}, it->label, it->icon};
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
    m_runningExtension = extension;
    m_runningCall = 0;
    m_asking = false;
    arm();
    const int status = lua_resume(co, nullptr, int(args.size()));
    return finish(status);
}

Result Engine::call(const QString &extension, int functionRef, const QVariantList &args)
{
    if (m_running)
        return {Status::Error, {}, QString::fromLatin1(kBusy)};
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
    m_runningExtension.clear();
    m_runningCall = 0;
    m_asking = false;
}

void Engine::dropWaiting(const QString &extension)
{
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (it->extension == extension) {
            if (m_L)
                lua_unref(m_L, it->ref);
            it = m_waiting.erase(it);
        } else {
            ++it;
        }
    }
}

QList<int> Engine::waitingCalls() const { return m_waiting.keys(); }

void Engine::cancel(int call)
{
    const auto it = m_waiting.constFind(call);
    if (it == m_waiting.constEnd())
        return;
    if (m_L)
        lua_unref(m_L, it->ref);
    m_waiting.erase(it);
}

void Engine::cancel()
{
    for (auto it = m_waiting.cend(); it != m_waiting.cbegin();) {
        --it;
        if (it->kind != u"ai") {
            cancel(it.key());
            return;
        }
    }
}

Result Engine::finish(int status)
{
    lua_State *co = m_running;
    disarm();
    // The call is over: the limits' sticky flags end with it (host-side Luau — the environment's
    // b.ai wrapper — must run before the next call is armed).
    const bool timedOut = std::exchange(m_timedOut, false);
    const bool outOfMemory = std::exchange(m_outOfMemory, false);
    if (timedOut) {
        Result r{Status::Timeout, {}, QString::fromLatin1(kTimeout), m_runningCall};
        releaseThread();
        return r;
    }
    if (status == LUA_ERRMEM || outOfMemory) {
        Result r{Status::OutOfMemory, {}, QString::fromLatin1(kNoMemory), m_runningCall};
        releaseThread();
        lua_gc(m_L, LUA_GCCOLLECT, 0);
        return r;
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
            // The call waits (its thread kept by its registry reference) under a stable id.
            const int id = m_runningCall ? m_runningCall : m_nextCall++;
            m_waiting.insert(id, Waiting{co, m_runningRef, m_runningExtension, m_askingKind});
            m_running = nullptr;
            m_runningRef = 0;
            m_runningExtension.clear();
            m_runningCall = 0;
            m_asking = false;
            return {Status::Suspended, question, {}, id};
        }
        Result r{Status::Error, {}, u"coroutine.yield outside a coroutine"_s, m_runningCall};
        releaseThread();
        return r;
    }
    Result r;
    r.call = m_runningCall;
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
             traceback.isEmpty() || traceback == message ? message : message + u"\nstack traceback:\n"_s + traceback,
             r.call};
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
    Result r = call(extension, it->ref, {params});
    if (!r.ok())
        return r;
    // What a view returns is a node of b.ui; a list of nodes is a column of them. Anything else —
    // nothing, a text, a table that is no node — would be drawn as an empty view without a word.
    if (r.value.metaType().id() == QMetaType::QVariantList) {
        r.value = QVariantMap{{u"type"_s, u"column"_s}, {u"children"_s, r.value}};
    } else if (r.value.toMap().value(u"type"_s).metaType().id() != QMetaType::QString) {
        QString got = u"a value that is no b.ui node"_s;
        if (!r.value.isValid() || r.value.isNull())
            got = u"nothing"_s;
        else if (r.value.metaType().id() == QMetaType::QVariantMap)
            got = u"a table that is no b.ui node"_s;
        const QString message =
                u"%1: the view \"%2\" returned %3 (return b.ui.column({ … }) or another node of b.ui)"_s;
        r = {Status::Error, {}, message.arg(m_loaded.value(extension).name, view, got), r.call};
    }
    return r;
}

Result Engine::act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto it = m_views.constFind(extension + u'/' + view);
    if (it == m_views.constEnd() || !it->actionsRef)
        return {Status::NotFound, {}, u"no view %1/%2 with actions"_s.arg(extension, view)};
    if (m_running)
        return {Status::Error, {}, QString::fromLatin1(kBusy)};
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

Result Engine::resume(int call, const QVariant &answer)
{
    const auto it = m_waiting.constFind(call);
    if (!m_L || it == m_waiting.constEnd() || m_running || lua_status(it->thread) != LUA_YIELD)
        return {Status::Error, {}, u"nothing is waiting for an answer"_s, call};
    m_running = it->thread;
    m_runningRef = it->ref;
    m_runningExtension = it->extension;
    m_runningCall = call;
    m_waiting.erase(it);
    m_asking = false;
    {
        Unlimited unlimited(&m_unlimited);
        pushVariant(m_running, answer);
    }
    arm();
    const int status = lua_resume(m_running, nullptr, 1);
    return finish(status);
}

Result Engine::resume(const QVariant &answer)
{
    for (auto it = m_waiting.cend(); it != m_waiting.cbegin();) {
        --it;
        if (it->kind != u"ai")
            return resume(it.key(), answer);
    }
    return {Status::Error, {}, u"nothing is waiting for an answer"_s};
}

// ---- the `b` API -------------------------------------------------------------------------------

int Engine::apiCommand(lua_State *L) { return finishApi(L, self(L)->commandImpl(L)); }
int Engine::apiView(lua_State *L) { return finishApi(L, self(L)->viewImpl(L)); }
int Engine::apiPages(lua_State *L) { return finishApi(L, self(L)->pagesImpl(L)); }
int Engine::apiRead(lua_State *L) { return finishApi(L, self(L)->readImpl(L)); }
int Engine::apiPage(lua_State *L) { return finishApi(L, self(L)->pageImpl(L)); }
int Engine::apiBlocks(lua_State *L) { return finishApi(L, self(L)->blocksImpl(L)); }
int Engine::apiSearch(lua_State *L)
{
    return finishApi(L, self(L)->searchImpl(L));
}
int Engine::apiQuery(lua_State *L)
{
    return finishApi(L, self(L)->queryImpl(L));
}
int Engine::apiCheckExtension(lua_State *L) { return finishApi(L, self(L)->checkImpl(L)); }
int Engine::apiWrite(lua_State *L) { return finishApi(L, self(L)->writeImpl(L)); }
int Engine::apiAppendToJournal(lua_State *L) { return finishApi(L, self(L)->appendImpl(L)); }
int Engine::apiReplaceBlock(lua_State *L) { return finishApi(L, self(L)->replaceImpl(L)); }
int Engine::apiDaysBetween(lua_State *L) { return finishApi(L, self(L)->daysImpl(L)); }
int Engine::apiSorted(lua_State *L) { return finishApi(L, self(L)->sortedImpl(L)); }
int Engine::apiFold(lua_State *L) { return finishApi(L, self(L)->foldImpl(L)); }
int Engine::apiFormatNumber(lua_State *L) { return finishApi(L, self(L)->formatImpl(L)); }
int Engine::apiLog(lua_State *L) { return finishApi(L, self(L)->logImpl(L)); }
int Engine::apiStatus(lua_State *L) { return finishApi(L, self(L)->statusImpl(L)); }
int Engine::apiPrint(lua_State *L) { return finishApi(L, self(L)->printImpl(L)); }
int Engine::apiTimer(lua_State *L) { return finishApi(L, self(L)->timerImpl(L)); }
int Engine::apiData(lua_State *L) { return finishApi(L, self(L)->dataImpl(L)); }
int Engine::apiAsk(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "ask")); }
int Engine::apiChoose(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "choose")); }
int Engine::apiForm(lua_State *L) { return finishApi(L, self(L)->questionImpl(L, "form")); }
int Engine::apiOn(lua_State *L) { return finishApi(L, self(L)->onImpl(L)); }
int Engine::apiAi(lua_State *L) { return finishApi(L, self(L)->aiImpl(L)); }
int Engine::apiAttachments(lua_State *L) { return finishApi(L, self(L)->attachmentsImpl(L)); }
int Engine::apiAddDays(lua_State *L) { return finishApi(L, self(L)->addDaysImpl(L, false)); }
int Engine::apiAddMonths(lua_State *L) { return finishApi(L, self(L)->addDaysImpl(L, true)); }
int Engine::apiWeekday(lua_State *L) { return finishApi(L, self(L)->weekdayImpl(L)); }
int Engine::apiWeekdayName(lua_State *L) { return finishApi(L, self(L)->nameImpl(L, false)); }
int Engine::apiMonthName(lua_State *L) { return finishApi(L, self(L)->nameImpl(L, true)); }
int Engine::apiLower(lua_State *L) { return finishApi(L, self(L)->lowerImpl(L)); }
int Engine::apiPlain(lua_State *L) { return finishApi(L, self(L)->plainImpl(L)); }
int Engine::apiParseRecurrence(lua_State *L) { return finishApi(L, self(L)->parseRecurrenceImpl(L)); }
int Engine::apiParseWeekdays(lua_State *L) { return finishApi(L, self(L)->parseWeekdaysImpl(L)); }
int Engine::apiFormatRecurrence(lua_State *L) { return finishApi(L, self(L)->formatRecurrenceImpl(L)); }
int Engine::apiAppendToPage(lua_State *L) { return finishApi(L, self(L)->appendToPageImpl(L)); }
int Engine::apiInsertBlock(lua_State *L) { return finishApi(L, self(L)->insertBlockImpl(L)); }
int Engine::apiRemoveBlock(lua_State *L) { return finishApi(L, self(L)->removeBlockImpl(L)); }
int Engine::apiSetTask(lua_State *L) { return finishApi(L, self(L)->setTaskImpl(L)); }
int Engine::apiSetProperty(lua_State *L) { return finishApi(L, self(L)->setPropertyImpl(L)); }

int Engine::apiRevision(lua_State *L)
{
    lua_pushnumber(L, double(self(L)->m_host->revision()));
    return 1;
}

int Engine::apiNow(lua_State *L)
{
    Engine *e = self(L);
    {
        Unlimited unlimited(&e->m_unlimited);
        pushString(L, e->m_host->now());
    }
    return 1;
}

int Engine::apiWeekStart(lua_State *L)
{
    const int day = self(L)->m_host->weekStart();
    lua_pushnumber(L, day >= 1 && day <= 7 ? day : 1);
    return 1;
}

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
    bool needsBlock = false;
    bool palette = true;
    if (lua_type(L, 3) == LUA_TTABLE) {
        lua_rawgetfield(L, 3, "label");
        if (lua_type(L, -1) == LUA_TSTRING)
            label = toQString(L, -1);
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "block");
        needsBlock = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "palette");
        palette = lua_type(L, -1) != LUA_TBOOLEAN || lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 2);
    Handler h{extension, lua_ref(L, -1), 0, label, needsBlock, {}, palette};
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
    if (!lua_isnoneornil(L, 4) && lua_type(L, 4) != LUA_TTABLE)
        return failWith(L, u"b.view: argument 4 must be a table ({label = …, icon = …})"_s);
    Handler h{extension, 0, 0, {}, false, {}, true};
    if (lua_type(L, 4) == LUA_TTABLE) {
        lua_rawgetfield(L, 4, "label");
        if (lua_type(L, -1) == LUA_TSTRING)
            h.label = toQString(L, -1).left(200);
        lua_pop(L, 1);
        lua_rawgetfield(L, 4, "icon");
        if (lua_type(L, -1) == LUA_TSTRING)
            h.icon = toQString(L, -1).left(64);
        lua_pop(L, 1);
    }
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
    // b.pages("project") would silently list every page: the filter is a table, its tag a text.
    PageQuery query;
    if (lua_type(L, 1) == LUA_TTABLE) {
        QVariant value;
        if (!argValue(L, 1, "b.pages", &value))
            return kRaise;
        const QVariantMap map = value.toMap();
        const QVariant tag = map.value(u"tag"_s);
        if (tag.isValid() && tag.metaType().id() != QMetaType::QString)
            return failWith(L, u"b.pages: tag must be a text"_s);
        query.tag = tagName(tag.toString());
        if (!propsFrom(map.value(u"prop"_s), &query.props))
            return failWith(L, u"b.pages: prop must be a text or a list of texts"_s);
    } else if (!lua_isnoneornil(L, 1)) {
        return failWith(L, u"b.pages: argument 1 must be a table ({ tag = … }), not %1"_s.arg(
                                   QString::fromUtf8(luaL_typename(L, 1))));
    }
    const QStringList pages = query.props.isEmpty() ? m_host->pages(query.tag) : m_host->findPages(query);
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
    query.tag = tagName(map.value(u"tag"_s).toString()).toLower();
    query.journalOnly = map.value(u"journal"_s).toBool();
    query.from = map.value(u"from"_s).toString().trimmed();
    query.to = map.value(u"to"_s).toString().trimmed();
    if (!propsFrom(map.value(u"prop"_s), &query.props))
        return failWith(L, u"b.blocks: prop must be a text or a list of texts"_s);
    if (query.page.isEmpty() && query.tag.isEmpty() && query.props.isEmpty())
        return failWith(L, u"b.blocks: the query needs a page, a tag or a prop"_s);
    // A day that is none ("2026-11-31", "yesterday") must not read as "no limit": the script would
    // get the blocks of every day and never know.
    for (const QString &day : {query.from, query.to})
        if (!day.isEmpty() && !QDate::fromString(day, Qt::ISODate).isValid())
            return failWith(L, u"b.blocks: invalid date: "_s + day);
    const QVariantList blocks = m_host->blocks(query);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, blocks);
    return 1;
}

int Engine::searchImpl(lua_State *L)
{
    QString text;
    double limit = 20;
    if (!argString(L, 1, "b.search", &text) || (!lua_isnoneornil(L, 2) && !argNumber(L, 2, "b.search", &limit)))
        return kRaise;
    const QVariantList hits = m_host->search(text, countFrom(limit, 1, 200, 20));
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, hits);
    return 1;
}

int Engine::queryImpl(lua_State *L)
{
    QString source;
    double limit = 50;
    double offset = 0;
    if (!argString(L, 1, "b.query", &source) || (!lua_isnoneornil(L, 2) && !argNumber(L, 2, "b.query", &limit))
        || (!lua_isnoneornil(L, 3) && !argNumber(L, 3, "b.query", &offset)))
        return kRaise;
    QString error;
    const int first = countFrom(offset, 0, 1000000, 0);
    const QVariantMap result = first > 0 ? m_host->queryPage(source, countFrom(limit, 1, 1000, 50), first, &error)
                                         : m_host->query(source, countFrom(limit, 1, 1000, 50), &error);
    if (!error.isEmpty())
        return failWith(L, u"b.query: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, result);
    return 1;
}

bool Engine::mayWrite(lua_State *L, const QString &extension, const char *function)
{
    if (m_loaded.value(extension).permissions.contains(u"write"_s))
        return true;
    failWith(L, QString::fromLatin1(function) + u": permission denied: write"_s);
    return false;
}

int Engine::checkImpl(lua_State *L)
{
    QString name, content;
    if (!argString(L, 1, "b.checkExtension", &name) || !argString(L, 2, "b.checkExtension", &content))
        return kRaise;
    if (name.trimmed().isEmpty())
        return failWith(L, u"b.checkExtension: the page's name is empty"_s);
    const QVariantMap report = m_host->checkExtension(name.trimmed(), content);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, report);
    return 1;
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
    pushString(L, formatNumber(value, countFrom(decimals, 0, 10, 2), comma));
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

int Engine::statusImpl(lua_State *L)
{
    QString text;
    if (!argOptString(L, 1, "b.status", &text))
        return kRaise;
    m_host->status(extensionOf(L), text.simplified().left(300));
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

QStringList Engine::hookEvents() { return {u"save"_s, u"change"_s}; }

int Engine::onImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString event;
    if (!argString(L, 1, "b.on", &event))
        return kRaise;
    if (!hookEvents().contains(event))
        return failWith(L, u"b.on: unknown event \"%1\" (%2)"_s.arg(event, hookEvents().join(u", "_s)));
    if (lua_type(L, 2) != LUA_TFUNCTION)
        return failWith(L, u"b.on: argument 2 must be a function"_s);
    if (m_hooks.size() >= 256)
        return failWith(L, u"b.on: too many hooks"_s);
    lua_pushvalue(L, 2);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    m_hooks.insert(m_nextHook++, Hook{extension, event, ref});
    return 0;
}

QList<HookInfo> Engine::hooks(const QString &event) const
{
    QList<HookInfo> out;
    for (auto it = m_hooks.cbegin(); it != m_hooks.cend(); ++it)
        if (event.isEmpty() || it->event == event)
            out.append({it.key(), it->extension, it->event});
    return out;
}

Result Engine::runHook(int id, const QVariantMap &event)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto it = m_hooks.constFind(id);
    if (it == m_hooks.constEnd())
        return {Status::NotFound, {}, u"no hook %1"_s.arg(id)};
    return call(it->extension, it->ref, {event});
}

void Engine::removeHook(int id)
{
    const auto it = m_hooks.constFind(id);
    if (it == m_hooks.constEnd())
        return;
    if (m_L)
        lua_unref(m_L, it->ref);
    m_hooks.erase(it);
}

Result Engine::evaluate(const QString &extension, const QString &code, const QString &name)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const auto loaded = m_loaded.constFind(extension);
    if (loaded == m_loaded.constEnd())
        return {Status::NotFound, {}, u"no extension "_s + extension};
    if (m_running)
        return {Status::Error, {}, QString::fromLatin1(kBusy)};
    lua_CompileOptions options = {};
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    // An expression first ("1 + 1", "b.today()"): its value is the result. "return " shares the
    // first line, so the line numbers stay those of the code.
    size_t size = 0;
    const QByteArray expression = "return " + code.toUtf8();
    char *bytecode = luau_compile(expression.constData(), size_t(expression.size()), &options, &size);
    if (size == 0 || bytecode[0] == 0) { // a syntax error: statements then
        std::free(bytecode);
        const QByteArray statements = code.toUtf8();
        bytecode = luau_compile(statements.constData(), size_t(statements.size()), &options, &size);
    }
    const QByteArray chunk = "=" + (name.isEmpty() ? extension : name).toUtf8();
    int rc;
    {
        Unlimited unlimited(&m_unlimited);
        lua_getref(m_L, loaded->envRef);
        rc = luau_load(m_L, chunk.constData(), bytecode, size, -1);
    }
    std::free(bytecode);
    if (rc != 0) {
        const QString message = toQString(m_L, -1);
        lua_pop(m_L, 2);
        return {Status::Error, {}, message};
    }
    lua_remove(m_L, -2); // the environment (the chunk keeps it)
    return run(extension, {});
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
    m_askingKind = QString::fromLatin1(kind);
    return kYield;
}

int Engine::aiImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!m_loaded.value(extension).permissions.contains(u"ai"_s))
        return failWith(L, u"b.ai: permission denied: ai (the page needs `uprawnienia:: ai`)"_s);
    QVariant request;
    if (lua_type(L, 1) == LUA_TSTRING) {
        request = QVariantMap{{u"prompt"_s, toQString(L, 1)}};
    } else if (lua_type(L, 1) == LUA_TTABLE) {
        if (!argValue(L, 1, "b.ai", &request))
            return kRaise;
        if (request.metaType().id() != QMetaType::QVariantMap)
            return failWith(L, u"b.ai: the request must be a text or a table {prompt = …}"_s);
    } else {
        return failWith(L, u"b.ai: argument 1 must be a text or a table {prompt = …}"_s);
    }
    if (L != m_running || !lua_isyieldable(L))
        return failWith(L, u"b.ai: cannot wait for an answer here (inside a coroutine or a library callback)"_s);
    {
        Unlimited unlimited(&m_unlimited);
        lua_settop(L, 0);
        pushVariant(L, QVariantMap{{u"kind"_s, u"ai"_s}, {u"request"_s, request}});
    }
    m_asking = true;
    m_askingKind = u"ai"_s;
    return kYield;
}

// ---- helpers and writes added with the habits (docs/EXTENSIONS.md "Helpers", "Writing") --------

int Engine::attachmentsImpl(lua_State *L)
{
    const QStringList files = m_host->attachments();
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, files);
    return 1;
}

int Engine::addDaysImpl(lua_State *L, bool months)
{
    const char *function = months ? "b.addMonths" : "b.addDays";
    QString date;
    double n = 0;
    if (!argString(L, 1, function, &date) || !argNumber(L, 2, function, &n))
        return kRaise;
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (!day.isValid())
        return failWith(L, QString::fromLatin1(function) + u": invalid date: "_s + date);
    if (!std::isfinite(n) || n != std::floor(n) || std::fabs(n) > (months ? 120000.0 : 3660000.0))
        return failWith(L, QString::fromLatin1(function) + u": the count must be a whole number"_s);
    const QDate result = months ? day.addMonths(int(n)) : day.addDays(qint64(n));
    if (!result.isValid() || result.year() < 1 || result.year() > 9999)
        return failWith(L, QString::fromLatin1(function) + u": the date is out of range"_s);
    Unlimited unlimited(&m_unlimited);
    pushString(L, result.toString(Qt::ISODate));
    return 1;
}

int Engine::weekdayImpl(lua_State *L)
{
    QString date;
    if (!argString(L, 1, "b.weekday", &date))
        return kRaise;
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (!day.isValid())
        return failWith(L, u"b.weekday: invalid date: "_s + date);
    lua_pushnumber(L, day.dayOfWeek());
    return 1;
}

int Engine::nameImpl(lua_State *L, bool month)
{
    const char *function = month ? "b.monthName" : "b.weekdayName";
    double n = 0;
    QString form;
    if (!argNumber(L, 1, function, &n) || !argOptString(L, 2, function, &form))
        return kRaise;
    const int high = month ? 12 : 7;
    if (n != std::floor(n) || n < 1 || n > high)
        return failWith(L, QString::fromLatin1(function) + u": argument 1 must be a whole number from 1 to %1"_s.arg(high));
    dates::NameForm nameForm = dates::NameForm::Nominative;
    if (form == u"short")
        nameForm = dates::NameForm::Short;
    else if (form == u"of")
        nameForm = dates::NameForm::Genitive;
    else if (!form.isEmpty() && form != u"full")
        return failWith(L, QString::fromLatin1(function) + u": the form is \"full\", \"short\" or \"of\""_s);
    const dates::UiLocale locale = localeOf(m_host->uiLanguage());
    const QString name = month ? dates::monthName(int(n), locale, nameForm) : dates::weekdayName(int(n), locale, nameForm);
    Unlimited unlimited(&m_unlimited);
    pushString(L, name);
    return 1;
}

int Engine::lowerImpl(lua_State *L)
{
    QString s;
    if (!argString(L, 1, "b.lower", &s))
        return kRaise;
    Unlimited unlimited(&m_unlimited);
    pushString(L, unicode::nfc(s).toCaseFolded());
    return 1;
}

int Engine::plainImpl(lua_State *L)
{
    QString s;
    if (!argString(L, 1, "b.plain", &s))
        return kRaise;
    Unlimited unlimited(&m_unlimited);
    pushString(L, plainWords(s));
    return 1;
}

int Engine::parseRecurrenceImpl(lua_State *L)
{
    QString s;
    if (!argString(L, 1, "b.parseRecurrence", &s))
        return kRaise;
    const std::optional<dates::RecurrenceRule> rule = dates::parseRecurrence(s);
    Unlimited unlimited(&m_unlimited);
    if (rule)
        pushVariant(L, ruleMap(*rule));
    else
        lua_pushnil(L);
    return 1;
}

int Engine::parseWeekdaysImpl(lua_State *L)
{
    QString s;
    if (!argString(L, 1, "b.parseWeekdays", &s))
        return kRaise;
    const QList<int> days = dates::parseWeekdayList(s);
    Unlimited unlimited(&m_unlimited);
    if (days.isEmpty()) {
        lua_pushnil(L);
        return 1;
    }
    QVariantList list;
    for (const int d : days)
        list << d;
    pushVariant(L, list);
    return 1;
}

int Engine::formatRecurrenceImpl(lua_State *L)
{
    QVariant value;
    QString language;
    if (lua_type(L, 1) != LUA_TTABLE)
        return failWith(L, u"b.formatRecurrence: argument 1 must be a rule ({ every = \"week\", … })"_s);
    if (!argValue(L, 1, "b.formatRecurrence", &value) || !argOptString(L, 2, "b.formatRecurrence", &language))
        return kRaise;
    dates::RecurrenceRule rule;
    if (const QString error = ruleFrom(value.toMap(), &rule); !error.isEmpty())
        return failWith(L, u"b.formatRecurrence: "_s + error);
    if (language.isEmpty())
        language = m_host->uiLanguage();
    else if (language != u"pl" && language != u"en")
        return failWith(L, u"b.formatRecurrence: the language is \"pl\" or \"en\""_s);
    Unlimited unlimited(&m_unlimited);
    pushString(L, dates::formatRecurrence(rule, localeOf(language)));
    return 1;
}

int Engine::appendToPageImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString page, markdown;
    if (!argString(L, 1, "b.appendToPage", &page) || !argString(L, 2, "b.appendToPage", &markdown))
        return kRaise;
    if (page.trimmed().isEmpty())
        return failWith(L, u"b.appendToPage: the page's name is empty"_s);
    if (!mayWrite(L, extension, "b.appendToPage"))
        return kRaise;
    const QString error = m_host->appendToPage(extension, page.trimmed(), markdown);
    if (!error.isEmpty())
        return failWith(L, u"b.appendToPage: "_s + error);
    return 0;
}

int Engine::insertBlockImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, markdown, where;
    if (!argString(L, 1, "b.insertBlock", &ref) || !argString(L, 2, "b.insertBlock", &markdown)
        || !argOptString(L, 3, "b.insertBlock", &where))
        return kRaise;
    InsertAt at = InsertAt::After;
    if (where == u"before")
        at = InsertAt::Before;
    else if (where == u"child")
        at = InsertAt::Child;
    else if (!where.isEmpty() && where != u"after")
        return failWith(L, u"b.insertBlock: argument 3 is \"after\", \"before\" or \"child\""_s);
    if (markdown.trimmed().isEmpty())
        return failWith(L, u"b.insertBlock: nothing to insert"_s);
    if (!mayWrite(L, extension, "b.insertBlock"))
        return kRaise;
    QString newRef;
    const QString error = m_host->insertBlock(extension, ref, markdown, at, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.insertBlock: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

int Engine::removeBlockImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref;
    if (!argString(L, 1, "b.removeBlock", &ref))
        return kRaise;
    if (!mayWrite(L, extension, "b.removeBlock"))
        return kRaise;
    const QString error = m_host->removeBlock(extension, ref);
    if (!error.isEmpty())
        return failWith(L, u"b.removeBlock: "_s + error);
    return 0;
}

int Engine::setTaskImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, status;
    if (!argString(L, 1, "b.setTask", &ref) || !argString(L, 2, "b.setTask", &status))
        return kRaise;
    if (status.size() != 1)
        return failWith(L, u"b.setTask: the task status must be one character"_s);
    if (!mayWrite(L, extension, "b.setTask"))
        return kRaise;
    QString newRef;
    const QString error = m_host->setTask(extension, ref, status, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.setTask: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

int Engine::setPropertyImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, key, value;
    if (!argString(L, 1, "b.setProperty", &ref) || !argString(L, 2, "b.setProperty", &key))
        return kRaise;
    if (lua_type(L, 3) == LUA_TNUMBER) {
        value = numberToVariant(lua_tonumber(L, 3)).toString();
    } else if (!argOptString(L, 3, "b.setProperty", &value)) {
        return kRaise;
    }
    if (value.contains(u'\n') || value.contains(u'\r'))
        return failWith(L, u"b.setProperty: the value must be one line"_s);
    if (!mayWrite(L, extension, "b.setProperty"))
        return kRaise;
    QString newRef;
    const QString error = m_host->setProperty(extension, ref, key.trimmed(), value, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.setProperty: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

} // namespace brulion::extensions
