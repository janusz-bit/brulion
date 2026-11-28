#include "extensions/engine.h"

#include "dates/names.h"
#include "dates/recurrence.h"
#include "extensions/blockedit.h"
#include "extensions/host.h"
#include "markdown/macros.h"
#include "text/text.h"
#include "util/paths.h"
#include "util/strings.h"
#include "util/unicode.h"

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimeZone>

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
-- options: { key = "space" | "enter" | "1" … | "ctrl+z" (a keyboard shortcut of the view),
-- primary = true, tone = "danger" | "success" | "accent" | "warning", hint = "4 d" (a second line) }
function ui.button(label, action, args, options)
    local node = { type = "button", label = label, action = action, args = args }
    if type(options) == "table" then
        node.key = options.key
        node.primary = options.primary == true
        node.tone = options.tone
        node.hint = options.hint
        node.icon = options.icon
    end
    return node
end
-- Markdown as a note shows it (rendered by the app, links that open their pages); options:
-- { size = "large" | "small", align = "center", muted = true }
function ui.markdown(text, options)
    local node = { type = "markdown", text = text }
    if type(options) == "table" then
        node.size = options.size
        node.align = options.align
        node.muted = options.muted == true
    end
    return node
end
-- Numbers side by side: { { label = "Due", value = 12, tone = "accent" }, … }
function ui.stats(items) return { type = "stats", items = present(items) } end
function ui.divider() return { type = "divider" } end
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
-- options as b.ui.button's: { primary = true, icon = "play", key = "enter", tone = … } — a button
-- rather than a link
function ui.open(label, view, params, options)
    local node = { type = "open", label = label, view = view, params = params }
    if type(options) == "table" then
        node.key = options.key
        node.primary = options.primary == true
        node.tone = options.tone
        node.icon = options.icon
        node.button = true
    end
    return node
end
function ui.run(label, command) return { type = "run", label = label, command = command } end
-- The controls that take what is typed or chosen (docs/EXTENSIONS.md "Views"): each calls its action
-- with its args and the new `value`.
local function spec(value, name)
    if type(value) ~= "table" then
        error(name .. ": argument 1 must be a table", 3)
    end
    return value
end
-- A text field: Enter (Ctrl+Enter when multiline) or its button gives the text to the action.
function ui.input(s)
    s = spec(s, "b.ui.input")
    return { type = "input", value = s.value, placeholder = s.placeholder, label = s.label, action = s.action,
             args = s.args, multiline = s.multiline == true, submit = s.submit, keep = s.keep == true }
end
-- A list to choose from: options are texts or { label = …, value = … }.
function ui.select(s)
    s = spec(s, "b.ui.select")
    return { type = "select", options = present(s.options), value = s.value, label = s.label, action = s.action,
             args = s.args }
end
function ui.slider(s)
    s = spec(s, "b.ui.slider")
    return { type = "slider", value = s.value, min = s.min, max = s.max, step = s.step, unit = s.unit,
             label = s.label, action = s.action, args = s.args }
end
-- A day ("YYYY-MM-DD"; "" or nil: none), picked from a calendar.
function ui.date(s)
    s = spec(s, "b.ui.date")
    return { type = "date", value = s.value, label = s.label, action = s.action, args = s.args }
end
-- A switch; its action gets value = true or false.
function ui.toggle(on, action, args, options)
    local node = { type = "toggle", on = on == true, action = action, args = args }
    if type(options) == "table" then
        node.label = options.label
    end
    return node
end
-- A grid of tinted cells, a row per thing and a column per period (the gym's volume per muscle
-- and week): { columns = { "3.11", … }, rows = { { label, values = { n | nil, … }, texts?, note? } },
-- scale = "row" (every row against its own largest value, the default) | "all", label? }
function ui.heatmap(s)
    s = spec(s, "b.ui.heatmap")
    return { type = "heatmap", columns = s.columns, rows = present(s.rows), scale = s.scale, label = s.label }
end
-- Pictures side by side: each the path of a picture of the notebook ("assets/kot.jpg") or the
-- address of one on the internet, or { src = …, alt = …, caption = … }; options { label = … }. The
-- app shows those of the internet as notes show them: from the device's cache, fetched when the
-- user allows it (docs/EXTENSIONS.md "Views").
function ui.pictures(items, options)
    local node = { type = "pictures", items = present(items) }
    if type(options) == "table" then
        node.label = options.label
    end
    return node
end
-- A bar filled to value / max (max 1 by default), its label above it, a tone: written
-- b.ui.progress(3, 10, "3 of 10") or b.ui.progress({ value = 3, max = 10, label = …, tone = … }).
function ui.progress(value, max, label)
    if type(value) == "table" then
        local s = value
        return { type = "progress", value = s.value, max = s.max, label = s.label, tone = s.tone }
    end
    return { type = "progress", value = value, max = max, label = label }
end
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

// b.require: the raw function resolves the page and gives back either the module's value (it was
// required before), or its chunk — loaded in an environment of its own whose `b` is the
// requester's — which the wrapper runs here, in Luau: a long module is stopped by the time limit
// as any code is, and its errors reach the script. `done` keeps the value for the next require.
const char kRequirePrelude[] = R"luau(
return function(raw, done)
    return function(name)
        local state, value, key = raw(name)
        if state == nil then
            error(value, 2)
        end
        if state == true then
            return value
        end
        local ok, result = pcall(value)
        done(key, ok, result)
        if not ok then
            error(result, 0)
        end
        if result == nil then
            return true
        end
        return result
    end
end
)luau";

// `s` cut to at most `max` UTF-16 units — never inside a surrogate pair —, "…" where it was cut.
QString cutText(const QString &s, qsizetype max)
{
    if (s.size() <= max)
        return s;
    return s.left(strings::cutLength(s, max - 1)) + u'…';
}

// A question's prompt as a dialog may show it: its first line (the title) and at most so many
// characters and lines under it, "…" where it was cut.
QString questionText(const QString &prompt)
{
    const qsizetype newline = prompt.indexOf(u'\n');
    const QString title = cutText(newline < 0 ? prompt : prompt.left(newline), QuestionLimits::title);
    if (newline < 0)
        return title;
    QString body = prompt.mid(newline + 1);
    qsizetype at = -1;
    for (int lines = 0; lines < QuestionLimits::lines; ++lines) {
        at = body.indexOf(u'\n', at + 1);
        if (at < 0)
            break;
    }
    bool cut = false;
    if (at >= 0) {
        body.truncate(at);
        cut = true;
    }
    if (body.size() > QuestionLimits::text) {
        body = cutText(body, QuestionLimits::text);
        cut = false; // (cutText marked it)
    }
    return title + u'\n' + body + (cut ? u"\n…"_s : QString());
}

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

    bytecode = luau_compile(kRequirePrelude, sizeof(kRequirePrelude) - 1, &options, &size);
    const int requireRc = luau_load(m_L, "=b.require", bytecode, size, 0);
    std::free(bytecode);
    if (requireRc != 0 || lua_pcall(m_L, 0, 1, 0) != 0) {
        if (error)
            *error = u"b.require: "_s + toQString(m_L, -1);
        stop();
        return false;
    }
    m_requireFactoryRef = lua_ref(m_L, -1);
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
    m_macros.clear();
    m_variables.clear();
    m_reads = nullptr;
    m_noWrites = false;
    m_running = nullptr;
    m_runningRef = 0;
    m_runningExtension.clear();
    m_runningCall = 0;
    m_waiting.clear();
    m_uiRef = 0;
    m_aiFactoryRef = 0;
    m_requireFactoryRef = 0;
    m_moduleCode.clear();
    m_asking = false;
    if (!m_contributions.isEmpty() || !m_themes.isEmpty() || !m_inputRules.isEmpty() || !m_tagStyles.isEmpty()
        || !m_viewReplacements.isEmpty())
        ++m_appRevision;
    m_contributions.clear();
    m_themes.clear();
    m_inputRules.clear();
    m_tagStyles.clear();
    m_viewReplacements.clear();
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
    set("setProperties", &Engine::apiSetProperties);
    set("toUtc", &Engine::apiToUtc);
    set("fromUtc", &Engine::apiFromUtc);
    set("lines", &Engine::apiLines);
    set("replaceLine", &Engine::apiReplaceLine);
    set("insertLine", &Engine::apiInsertLine);
    set("removeLine", &Engine::apiRemoveLine);
    set("macro", &Engine::apiMacro);
    set("variable", &Engine::apiVariable);
    set("settings", &Engine::apiSettings);
    set("setting", &Engine::apiSetting);
    set("setSetting", &Engine::apiSetSetting);
    set("stopTimer", &Engine::apiStopTimer);
    set("notify", &Engine::apiNotify);
    set("aiModel", &Engine::apiAiModel);
    // b.require = factory(raw, done): the module's chunk runs in the Luau wrapper (prelude).
    lua_getref(L, m_requireFactoryRef);
    lua_pushlstring(L, id.constData(), size_t(id.size()));
    lua_pushcclosure(L, &Engine::apiRequire, "require", 1);
    lua_pushlstring(L, id.constData(), size_t(id.size()));
    lua_pushcclosure(L, &Engine::apiRequireDone, "require", 1);
    lua_call(L, 2, 1);
    lua_setfield(L, -2, "require");
    // b.ai = factory(raw): the raw C function yields; the Luau wrapper raises its errors.
    lua_getref(L, m_aiFactoryRef);
    lua_pushlstring(L, id.constData(), size_t(id.size()));
    lua_pushcclosure(L, &Engine::apiAi, "ai", 1);
    lua_call(L, 1, 1);
    lua_setfield(L, -2, "ai");
    lua_getref(L, m_uiRef);
    lua_setfield(L, -2, "ui");
    // b.app: the app as extensions see and change it (appapi.h); each function knows its
    // extension (upvalue 1) and the generic ones their operation (upvalue 2).
    lua_createtable(L, 0, 16);
    for (const QString &operation : appOperations()) {
        const QByteArray op = operation.toUtf8();
        lua_pushlstring(L, id.constData(), size_t(id.size()));
        lua_pushlstring(L, op.constData(), size_t(op.size()));
        lua_pushcclosure(L, &Engine::apiApp, op.constData(), 2);
        lua_setfield(L, -2, op.constData());
    }
    set("theme", &Engine::apiAppTheme);
    set("contribute", &Engine::apiAppContribute);
    set("remove", &Engine::apiAppRemove);
    set("contributions", &Engine::apiAppContributions);
    set("inputRule", &Engine::apiAppInputRule);
    set("inputRules", &Engine::apiAppInputRules);
    set("tagStyle", &Engine::apiAppTagStyle);
    set("tagStyles", &Engine::apiAppTagStyles);
    set("replaceView", &Engine::apiAppReplaceView);
    set("replacedViews", &Engine::apiAppReplacedViews);
    lua_setreadonly(L, -1, true);
    lua_setfield(L, -2, "app");
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
    dropContributions(extension);
    if (dropAppState(extension) && m_host)
        m_host->appChanged(extension);
    dropWaiting(extension);
    if (const auto it = m_loaded.constFind(extension); it != m_loaded.constEnd()) {
        lua_unref(m_L, it->envRef);
        for (const int ref : it->moduleRefs)
            lua_unref(m_L, ref);
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
    // (Its top level may return what b.require gives others — functions too —: nothing to convert.)
    m_loading = true;
    Result r = run(extension.id, {});
    m_loading = false;
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
        out.append({it->extension, name, it->label.isEmpty() ? name : it->label, it->needsBlock, it->palette, it->key,
                    it->chat});
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
        ViewInfo info{it->extension, it.key().mid(it->extension.size() + 1), {}, it->label, it->icon, it->panel};
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
    // A call that ends (not one that waits for an answer) leaves no module half-required: the
    // next b.require of a module it was running when it stopped is no cycle.
    if (!(status == LUA_YIELD && m_asking && !m_timedOut && !m_outOfMemory)) {
        if (const auto it = m_loaded.find(m_runningExtension); it != m_loaded.end())
            it->requiring.clear();
    }
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
        if (lua_gettop(co) > 0 && !m_loading) {
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
int Engine::apiApp(lua_State *L)
{
    return finishApi(L, self(L)->appImpl(L));
}
int Engine::apiAppTheme(lua_State *L)
{
    return finishApi(L, self(L)->appThemeImpl(L));
}
int Engine::apiAppContribute(lua_State *L)
{
    return finishApi(L, self(L)->appContributeImpl(L));
}
int Engine::apiAppRemove(lua_State *L)
{
    return finishApi(L, self(L)->appRemoveImpl(L));
}
int Engine::apiAppContributions(lua_State *L)
{
    return finishApi(L, self(L)->appContributionsImpl(L));
}
int Engine::apiAppInputRule(lua_State *L)
{
    return finishApi(L, self(L)->appInputRuleImpl(L));
}
int Engine::apiAppInputRules(lua_State *L)
{
    Engine *engine = self(L);
    QVariantList out;
    for (const InputRule &rule : engine->m_inputRules)
        out << inputRuleTable(rule);
    Unlimited unlimited(&engine->m_unlimited);
    pushVariant(L, out);
    return finishApi(L, 1);
}
int Engine::apiAppTagStyle(lua_State *L)
{
    return finishApi(L, self(L)->appTagStyleImpl(L));
}
int Engine::apiAppTagStyles(lua_State *L)
{
    Engine *engine = self(L);
    QVariantList out;
    for (const TagStyle &style : engine->m_tagStyles)
        out << tagStyleTable(style);
    Unlimited unlimited(&engine->m_unlimited);
    pushVariant(L, out);
    return finishApi(L, 1);
}
int Engine::apiAppReplaceView(lua_State *L)
{
    return finishApi(L, self(L)->appReplaceViewImpl(L));
}
int Engine::apiAppReplacedViews(lua_State *L)
{
    Engine *engine = self(L);
    QVariantList out;
    for (const ViewReplacement &replacement : engine->m_viewReplacements)
        out << viewReplacementTable(replacement);
    Unlimited unlimited(&engine->m_unlimited);
    pushVariant(L, out);
    return finishApi(L, 1);
}
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
int Engine::apiAiModel(lua_State *L)
{
    return finishApi(L, self(L)->aiModelImpl(L));
}
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
int Engine::apiSetProperties(lua_State *L) { return finishApi(L, self(L)->setPropertiesImpl(L)); }
int Engine::apiToUtc(lua_State *L) { return finishApi(L, self(L)->toUtcImpl(L)); }
int Engine::apiFromUtc(lua_State *L) { return finishApi(L, self(L)->fromUtcImpl(L)); }
int Engine::apiLines(lua_State *L) { return finishApi(L, self(L)->linesImpl(L)); }
int Engine::apiReplaceLine(lua_State *L) { return finishApi(L, self(L)->replaceLineImpl(L)); }
int Engine::apiInsertLine(lua_State *L) { return finishApi(L, self(L)->insertLineImpl(L)); }
int Engine::apiRemoveLine(lua_State *L) { return finishApi(L, self(L)->removeLineImpl(L)); }
int Engine::apiRequire(lua_State *L)
{
    return finishApi(L, self(L)->requireImpl(L));
}
int Engine::apiRequireDone(lua_State *L)
{
    return finishApi(L, self(L)->requireDoneImpl(L));
}
int Engine::apiSettings(lua_State *L)
{
    return finishApi(L, self(L)->settingsImpl(L));
}
int Engine::apiSetting(lua_State *L)
{
    return finishApi(L, self(L)->settingImpl(L));
}
int Engine::apiSetSetting(lua_State *L)
{
    return finishApi(L, self(L)->setSettingImpl(L));
}

int Engine::apiRevision(lua_State *L)
{
    self(L)->noteReadAll();
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
    QString label, shortcut;
    bool needsBlock = false;
    bool palette = true;
    bool chat = false;
    if (lua_type(L, 3) == LUA_TTABLE) {
        lua_rawgetfield(L, 3, "label");
        if (lua_type(L, -1) == LUA_TSTRING)
            label = toQString(L, -1);
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "block");
        needsBlock = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "chat");
        chat = lua_toboolean(L, -1);
        lua_pop(L, 1);
        // (A chat partner answers in the chat: the palette offers it only when told to.)
        lua_rawgetfield(L, 3, "palette");
        palette = lua_type(L, -1) == LUA_TBOOLEAN ? bool(lua_toboolean(L, -1)) : !chat;
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "key");
        if (lua_type(L, -1) == LUA_TSTRING)
            shortcut = toQString(L, -1).trimmed().left(64);
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 2);
    Handler h{extension, lua_ref(L, -1), 0, label, needsBlock, {}, palette, {}, shortcut, chat};
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
    Handler h{extension, 0, 0, {}, false, {}, true, {}, {}, false};
    if (lua_type(L, 4) == LUA_TTABLE) {
        lua_rawgetfield(L, 4, "label");
        if (lua_type(L, -1) == LUA_TSTRING)
            h.label = toQString(L, -1).left(200);
        lua_pop(L, 1);
        lua_rawgetfield(L, 4, "icon");
        if (lua_type(L, -1) == LUA_TSTRING)
            h.icon = toQString(L, -1).left(64);
        lua_pop(L, 1);
        // panel = true (beside pages and days), "page" or "journal": a section of the side panel.
        lua_rawgetfield(L, 4, "panel");
        if (lua_type(L, -1) == LUA_TBOOLEAN && lua_toboolean(L, -1)) {
            h.panel = u"all"_s;
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            const QString where = toQString(L, -1);
            if (where != u"page" && where != u"journal" && where != u"all" && where != u"sidebar") {
                lua_pop(L, 1);
                return failWith(L, u"b.view: panel is true, \"page\", \"journal\" or \"sidebar\""_s);
            }
            h.panel = where;
        }
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
        const QVariant paths = map.value(u"paths"_s);
        if (paths.isValid() && paths.metaType().id() != QMetaType::Bool)
            return failWith(L, u"b.pages: paths must be true or false"_s);
        query.paths = paths.toBool();
    } else if (!lua_isnoneornil(L, 1)) {
        return failWith(L, u"b.pages: argument 1 must be a table ({ tag = … }), not %1"_s.arg(
                                   QString::fromUtf8(luaL_typename(L, 1))));
    }
    noteReadAll();
    const QStringList pages =
            query.props.isEmpty() && !query.paths ? m_host->pages(query.tag) : m_host->findPages(query);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, pages);
    return 1;
}

int Engine::readImpl(lua_State *L)
{
    QString name;
    if (!argString(L, 1, "b.read", &name))
        return kRaise;
    noteRead(name);
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
    noteRead(name);
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
    if (!query.page.isEmpty())
        noteRead(query.page);
    else
        noteReadAll();
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
    noteReadAll();
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
    noteReadAll();
    const int first = countFrom(offset, 0, 1000000, 0);
    const QVariantMap result = first > 0 ? m_host->queryPage(source, countFrom(limit, 1, 1000, 50), first, &error)
                                         : m_host->query(source, countFrom(limit, 1, 1000, 50), &error);
    if (!error.isEmpty())
        return failWith(L, u"b.query: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, result);
    return 1;
}

bool Engine::mayChange(lua_State *L, const char *function)
{
    if (!m_noWrites)
        return true;
    failWith(L, QString::fromLatin1(function) + u": a macro or a template variable cannot change settings or timers"_s);
    return false;
}

bool Engine::mayWrite(lua_State *L, const QString &extension, const char *function)
{
    if (m_noWrites) {
        failWith(L, QString::fromLatin1(function) + u": a macro or a template variable cannot change notes"_s);
        return false;
    }
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
    TimerSpec spec;
    if (!argNumber(L, 1, "b.timer", &seconds))
        return kRaise;
    if (lua_type(L, 2) == LUA_TTABLE) {
        // b.timer(seconds, { label = …, message = … })
        QVariant options;
        if (!argValue(L, 2, "b.timer", &options))
            return kRaise;
        const QVariantMap map = options.toMap();
        for (const QString &key : {u"label"_s, u"message"_s}) {
            const QVariant v = map.value(key);
            if (v.isValid() && v.metaType().id() != QMetaType::QString)
                return failWith(L, u"b.timer: %1 must be a text"_s.arg(key));
        }
        spec.label = map.value(u"label"_s).toString();
        spec.message = map.value(u"message"_s).toString();
    } else if (!argOptString(L, 2, "b.timer", &spec.label)) {
        return kRaise;
    }
    if (!std::isfinite(seconds) || seconds < 1 || seconds > 24 * 3600)
        return failWith(L, u"b.timer: seconds must be between 1 and 86400"_s);
    if (!mayChange(L, "b.timer"))
        return kRaise;
    spec.seconds = int(std::lround(seconds));
    // What a notification and a banner show: one line each, of a length they have room for.
    spec.label = cutText(spec.label.simplified(), QuestionLimits::timerLabel);
    spec.message = cutText(spec.message.simplified(), QuestionLimits::timerMessage);
    m_host->startTimer(extensionOf(L), spec);
    return 0;
}

int Engine::apiStopTimer(lua_State *L)
{
    return finishApi(L, self(L)->stopTimerImpl(L));
}

int Engine::stopTimerImpl(lua_State *L)
{
    if (!mayChange(L, "b.stopTimer"))
        return kRaise;
    QString extension;
    {
        Unlimited unlimited(&m_unlimited);
        extension = extensionOf(L);
    }
    m_host->stopTimer(extension);
    return 0;
}

int Engine::apiNotify(lua_State *L)
{
    return finishApi(L, self(L)->notifyImpl(L));
}

int Engine::notifyImpl(lua_State *L)
{
    // b.notify(text) or b.notify(text, { title = …, page = … }): true, or false and why not. Only
    // arguments of the wrong kind raise; a notification that is not shown is an answer.
    NotifySpec spec;
    if (!argString(L, 1, "b.notify", &spec.text))
        return kRaise;
    if (!lua_isnoneornil(L, 2)) {
        if (lua_type(L, 2) != LUA_TTABLE)
            return failWith(
                    L, u"b.notify: argument 2 must be a table, not %1"_s.arg(QString::fromUtf8(luaL_typename(L, 2))));
        QVariant options;
        if (!argValue(L, 2, "b.notify", &options))
            return kRaise;
        const QVariantMap map = options.toMap();
        for (const QString &key : {u"title"_s, u"page"_s}) {
            const QVariant v = map.value(key);
            if (v.isValid() && v.metaType().id() != QMetaType::QString)
                return failWith(L, u"b.notify: %1 must be a text"_s.arg(key));
        }
        spec.title = map.value(u"title"_s).toString();
        spec.page = map.value(u"page"_s).toString();
    }
    QString why;
    if (m_noWrites) {
        why = u"not from a macro or a template variable"_s;
    } else if (m_loading) {
        why = u"not while the extension loads"_s;
    } else {
        spec.text = cutText(spec.text.trimmed(), QuestionLimits::notifyText);
        spec.title = cutText(spec.title.simplified(), QuestionLimits::notifyTitle);
        spec.page = spec.page.trimmed();
        if (spec.text.isEmpty())
            why = u"empty"_s;
        else
            why = m_host->notify(extensionOf(L), spec);
    }
    Unlimited unlimited(&m_unlimited);
    lua_pushboolean(L, why.isEmpty());
    if (why.isEmpty())
        return 1;
    pushString(L, why);
    return 2;
}

QVariant resolvedData(const QVariant &stored)
{
    if (stored.metaType() != QMetaType::fromType<DataFile>())
        return stored;
    const QString path = stored.value<DataFile>().path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (path.endsWith(u".json", Qt::CaseInsensitive))
        return QJsonDocument::fromJson(bytes).toVariant();
    return QString::fromUtf8(bytes);
}

int Engine::dataImpl(lua_State *L)
{
    QString name;
    QString key;
    if (!argString(L, 1, "b.data", &name) || (!lua_isnoneornil(L, 2) && !argString(L, 2, "b.data", &key)))
        return kRaise;
    QVariant value = m_loaded.value(extensionOf(L)).data.value(name);
    // A file: read and parsed when first asked for, kept until dropData().
    if (value.metaType() == QMetaType::fromType<DataFile>()) {
        const QString path = value.value<DataFile>().path;
        auto held = m_dataFiles.constFind(path);
        if (held == m_dataFiles.cend())
            held = m_dataFiles.insert(path, resolvedData(value));
        value = *held;
    }
    // b.data(name, key): one entry of a record (the gym's instructions of one exercise), without
    // crossing the whole record into the script.
    if (!lua_isnoneornil(L, 2))
        value = value.toMap().value(key);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, value);
    return 1;
}

QStringList Engine::hookEvents()
{
    return {u"save"_s, u"change"_s, u"task"_s, u"journal"_s, u"open"_s, u"day"_s, u"timer"_s, u"route"_s};
}

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
        if (fields.metaType().id() != QMetaType::QVariantList
            && !(fields.toMap().isEmpty() && lua_type(L, 2) == LUA_TTABLE))
            return failWith(L, function + u": argument 2 must be a list of fields"_s);
        const QVariantList list = fields.toList();
        if (list.size() > QuestionLimits::fields)
            return failWith(L, function + u": at most %1 fields"_s.arg(QuestionLimits::fields));
        // What the dialog shows of each field, cut to the room it has.
        QVariantList shown;
        for (const QVariant &f : list) {
            QVariantMap field = f.toMap();
            for (const auto &[key, max] : {std::pair{u"label"_s, QuestionLimits::label}, std::pair{u"unit"_s, 20},
                                           std::pair{u"placeholder"_s, QuestionLimits::label}}) {
                if (field.contains(key))
                    field.insert(key, cutText(field.value(key).toString().simplified(), max));
            }
            if (field.value(u"value"_s).metaType().id() == QMetaType::QString)
                field.insert(u"value"_s, cutText(field.value(u"value"_s).toString(), QuestionLimits::value));
            shown << field;
        }
        question.insert(u"title"_s, questionText(title));
        question.insert(u"fields"_s, shown);
    } else {
        QString prompt;
        if (!argString(L, 1, fn.constData(), &prompt))
            return kRaise;
        question.insert(u"prompt"_s, questionText(prompt));
        if (qstrcmp(kind, "choose") == 0) {
            QVariant options;
            if (!argValue(L, 2, fn.constData(), &options))
                return kRaise;
            if (options.metaType().id() != QMetaType::QVariantList && !(options.toMap().isEmpty() && lua_type(L, 2) == LUA_TTABLE))
                return failWith(L, function + u": argument 2 must be a list of strings"_s);
            const QVariantList list = options.toList();
            // A list a dialog can show, of options a tap can give back as they are (never cut).
            if (list.size() > QuestionLimits::options)
                return failWith(L, function + u": at most %1 options"_s.arg(QuestionLimits::options));
            QStringList texts;
            for (qsizetype i = 0; i < list.size(); ++i) {
                const int type = list[i].metaType().id();
                if (type != QMetaType::QString && type != QMetaType::LongLong && type != QMetaType::Double)
                    return failWith(L, function + u": option %1 is not a string"_s.arg(i + 1));
                const QString text = list[i].toString();
                if (text.size() > QuestionLimits::option)
                    return failWith(L, function
                                               + u": option %1 is longer than %2 characters"_s.arg(i + 1).arg(
                                                       QuestionLimits::option));
                texts << text;
            }
            question.insert(u"options"_s, texts);
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

int Engine::aiModelImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!m_loaded.value(extension).permissions.contains(u"ai"_s))
        return failWith(L, u"b.aiModel: permission denied: ai (the page needs `uprawnienia:: ai`)"_s);
    const QVariantMap model = m_host->aiModel();
    Unlimited unlimited(&m_unlimited);
    if (model.isEmpty())
        lua_pushnil(L);
    else
        pushVariant(L, model);
    return 1;
}

// ---- helpers and writes added with the habits (docs/EXTENSIONS.md "Helpers", "Writing") --------

int Engine::attachmentsImpl(lua_State *L)
{
    noteReadAll();
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

int Engine::linesImpl(lua_State *L)
{
    QString name;
    if (!argString(L, 1, "b.lines", &name))
        return kRaise;
    noteRead(name);
    const std::optional<QVariantList> lines = m_host->lines(name);
    Unlimited unlimited(&m_unlimited);
    if (lines)
        pushVariant(L, *lines);
    else
        lua_pushnil(L);
    return 1;
}

int Engine::replaceLineImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, text;
    if (!argString(L, 1, "b.replaceLine", &ref) || !argString(L, 2, "b.replaceLine", &text))
        return kRaise;
    if (text.contains(u'\n') || text.contains(u'\r'))
        return failWith(L, u"b.replaceLine: the text must be one line"_s);
    if (!mayWrite(L, extension, "b.replaceLine"))
        return kRaise;
    QString newRef;
    const QString error = m_host->replaceLine(extension, ref, text, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.replaceLine: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

int Engine::insertLineImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref, text, where;
    if (!argString(L, 1, "b.insertLine", &ref) || !argString(L, 2, "b.insertLine", &text)
        || !argOptString(L, 3, "b.insertLine", &where))
        return kRaise;
    if (!where.isEmpty() && where != u"after" && where != u"before")
        return failWith(L, u"b.insertLine: argument 3 is \"after\" or \"before\""_s);
    if (!mayWrite(L, extension, "b.insertLine"))
        return kRaise;
    QString newRef;
    const QString error = m_host->insertLines(extension, ref, text, where == u"before", &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.insertLine: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, newRef);
    return 1;
}

int Engine::removeLineImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref;
    if (!argString(L, 1, "b.removeLine", &ref))
        return kRaise;
    if (!mayWrite(L, extension, "b.removeLine"))
        return kRaise;
    const QString error = m_host->removeLine(extension, ref);
    if (!error.isEmpty())
        return failWith(L, u"b.removeLine: "_s + error);
    return 0;
}

int Engine::setPropertiesImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString ref;
    QVariant value;
    if (!argString(L, 1, "b.setProperties", &ref))
        return kRaise;
    if (lua_type(L, 2) != LUA_TTABLE)
        return failWith(L, u"b.setProperties: argument 2 must be a list of { key, value } pairs"_s);
    if (!argValue(L, 2, "b.setProperties", &value))
        return kRaise;
    QList<std::pair<QString, QString>> props;
    for (const QVariant &v : value.toList()) {
        const QVariantList pair = v.toList();
        if (pair.isEmpty() || pair.size() > 2 || pair.at(0).metaType().id() != QMetaType::QString)
            return failWith(L, u"b.setProperties: argument 2 must be a list of { key, value } pairs"_s);
        QString text;
        if (pair.size() == 2) {
            const QVariant &x = pair.at(1);
            const int type = x.metaType().id();
            if (type == QMetaType::Double || type == QMetaType::LongLong || type == QMetaType::Int)
                text = numberToVariant(x.toDouble()).toString();
            else if (type == QMetaType::QString)
                text = x.toString();
            else if (x.isValid())
                return failWith(L, u"b.setProperties: a value must be a text or a number"_s);
        }
        if (text.contains(u'\n') || text.contains(u'\r'))
            return failWith(L, u"b.setProperties: the value must be one line"_s);
        props.append({pair.at(0).toString().trimmed(), text});
    }
    if (props.size() > 64)
        return failWith(L, u"b.setProperties: at most 64 properties at once"_s);
    if (!mayWrite(L, extension, "b.setProperties"))
        return kRaise;
    QString newRef;
    const QString error = props.isEmpty() ? QString() : m_host->setProperties(extension, ref, props, &newRef);
    if (!error.isEmpty())
        return failWith(L, u"b.setProperties: "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushString(L, props.isEmpty() ? ref : newRef);
    return 1;
}

int Engine::toUtcImpl(lua_State *L)
{
    QString text;
    if (!argString(L, 1, "b.toUtc", &text))
        return kRaise;
    const QString t = text.trimmed();
    const QDate date = QDate::fromString(t.left(10), Qt::ISODate);
    QTime time(0, 0);
    if (t.size() > 10) {
        time = QTime::fromString(t.mid(11).trimmed(), u"H:mm"_s);
        if (!time.isValid())
            time = QTime::fromString(t.mid(11).trimmed(), u"H:mm:ss"_s);
    }
    if (!date.isValid() || !time.isValid() || (t.size() > 10 && t.at(10) != u' ' && t.at(10) != u'T'))
        return failWith(L, u"b.toUtc: not a date and time: "_s + text);
    const QDateTime local(date, time, QTimeZone::LocalTime);
    Unlimited unlimited(&m_unlimited);
    pushString(L, local.toUTC().toString(u"yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"_s));
    return 1;
}

int Engine::fromUtcImpl(lua_State *L)
{
    QString text;
    if (!argString(L, 1, "b.fromUtc", &text))
        return kRaise;
    QDateTime at = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
    if (!at.isValid())
        at = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    Unlimited unlimited(&m_unlimited);
    // Only a time with its zone ("Z", "+01:00") is a moment: anything else is nil.
    if (!at.isValid() || at.timeSpec() == Qt::LocalTime)
        lua_pushnil(L);
    else
        pushString(L, at.toLocalTime().toString(u"yyyy-MM-dd HH:mm"_s));
    return 1;
}

// ---- Macros and template variables (docs/EXTENSIONS.md "Macros", docs/TEMPLATES.md) -------------

namespace {
constexpr int kMaxContributions = 256; // macros (and, apart, variables) of one extension
}

int Engine::apiMacro(lua_State *L)
{
    return finishApi(L, self(L)->contributionImpl(L, false));
}
int Engine::apiVariable(lua_State *L)
{
    return finishApi(L, self(L)->contributionImpl(L, true));
}

int Engine::contributionImpl(lua_State *L, bool variable)
{
    const QString extension = extensionOf(L);
    const char *function = variable ? "b.variable" : "b.macro";
    const QString fn = QString::fromLatin1(function);
    QString name;
    if (!argString(L, 1, function, &name))
        return kRaise;
    if (!markdown::isMacroName(name))
        return failWith(
                L,
                fn + u": the name \""_s + name.left(80)
                        + u"\" is not one (letters, digits, -, _ and ., at most 64, the first a letter or a digit)"_s);
    if (lua_type(L, 2) != LUA_TFUNCTION)
        return failWith(L, fn + u": argument 2 must be a function"_s);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TTABLE)
        return failWith(L, fn + u": argument 3 must be a table ({ description = …, args = … })"_s);
    MacroEntry c;
    c.extension = extension;
    if (lua_type(L, 3) == LUA_TTABLE) {
        Unlimited unlimited(&m_unlimited);
        lua_rawgetfield(L, 3, "description");
        if (lua_type(L, -1) == LUA_TSTRING)
            c.description = toQString(L, -1).simplified().left(300);
        lua_pop(L, 1);
        lua_rawgetfield(L, 3, "example");
        if (lua_type(L, -1) == LUA_TSTRING)
            c.example = toQString(L, -1).simplified().left(200);
        lua_pop(L, 1);
        // args = "exercise" or { "habit", "days" }: the arguments' names, for the suggestions.
        lua_rawgetfield(L, 3, "args");
        if (lua_type(L, -1) == LUA_TSTRING) {
            c.arguments << toQString(L, -1).simplified().left(40);
        } else if (lua_type(L, -1) == LUA_TTABLE) {
            const int count = std::min(lua_objlen(L, -1), 8);
            for (int i = 1; i <= count; ++i) {
                lua_rawgeti(L, -1, i);
                if (lua_type(L, -1) == LUA_TSTRING)
                    c.arguments << toQString(L, -1).simplified().left(40);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    QHash<QString, MacroEntry> &map = variable ? m_variables : m_macros;
    const QString key = extension + u'/' + name;
    const auto it = map.constFind(key);
    if (it == map.constEnd()) {
        qsizetype own = 0;
        for (const MacroEntry &other : std::as_const(map))
            own += other.extension == extension ? 1 : 0;
        if (own >= kMaxContributions)
            return failWith(L, fn + u": at most %1 per extension"_s.arg(kMaxContributions));
    }
    {
        Unlimited unlimited(&m_unlimited);
        lua_pushvalue(L, 2);
        c.ref = lua_ref(L, -1);
        lua_pop(L, 1);
    }
    if (it != map.constEnd())
        lua_unref(L, it->ref);
    map.insert(key, c);
    return 0;
}

void Engine::dropContributions(const QString &extension)
{
    for (auto *map : {&m_macros, &m_variables}) {
        for (auto it = map->begin(); it != map->end();) {
            if (it->extension == extension) {
                if (m_L)
                    lua_unref(m_L, it->ref);
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}

QList<MacroInfo> Engine::macros() const
{
    QList<MacroInfo> out;
    for (auto it = m_macros.cbegin(); it != m_macros.cend(); ++it)
        out.append(
                {it->extension, it.key().mid(it->extension.size() + 1), it->description, it->arguments, it->example});
    std::sort(out.begin(), out.end(), [](const MacroInfo &a, const MacroInfo &b) {
        return a.extension != b.extension ? a.extension < b.extension : a.name < b.name;
    });
    return out;
}

QList<VariableInfo> Engine::variables() const
{
    QList<VariableInfo> out;
    for (auto it = m_variables.cbegin(); it != m_variables.cend(); ++it)
        out.append({it->extension, it.key().mid(it->extension.size() + 1), it->description, it->arguments});
    std::sort(out.begin(), out.end(), [](const VariableInfo &a, const VariableInfo &b) {
        return a.extension != b.extension ? a.extension < b.extension : a.name < b.name;
    });
    return out;
}

Result Engine::runMacro(const QString &extension, const QString &name, const QVariantMap &ctx, int timeoutMs)
{
    return runContribution(false, extension, name, ctx, timeoutMs);
}

Result Engine::runVariable(const QString &extension, const QString &name, const QVariantMap &ctx, int timeoutMs)
{
    return runContribution(true, extension, name, ctx, timeoutMs);
}

Result Engine::runContribution(bool variable, const QString &extension, const QString &name, const QVariantMap &ctx,
                               int timeoutMs)
{
    if (!m_L)
        return {Status::Error, {}, u"the extension engine is not started"_s};
    const QHash<QString, MacroEntry> &map = variable ? m_variables : m_macros;
    const auto it = map.constFind(extension + u'/' + name);
    if (it == map.constEnd())
        return {Status::NotFound,
                {},
                (variable ? u"no template variable %1/%2"_s : u"no macro %1/%2"_s).arg(extension, name)};
    if (m_running)
        return {Status::Error, {}, QString::fromLatin1(kBusy)};
    // A macro is drawn as part of a page: a shorter time limit than a command's, no waiting, no
    // writes (what it shows must not change the notes it is drawn from).
    const int ownLimit = m_limits.timeoutMs;
    m_limits.timeoutMs = std::clamp(timeoutMs, 1, std::max(1, ownLimit));
    m_noWrites = true;
    Result r = call(extension, it->ref, {ctx});
    m_noWrites = false;
    m_limits.timeoutMs = ownLimit;
    if (r.status == Status::Suspended) {
        cancel(r.call);
        r = {Status::Error,
             {},
             u"%1: a macro or a template variable cannot wait for a question or for AI"_s.arg(
                     m_loaded.value(extension).name),
             r.call};
    }
    return r;
}

void Engine::startRecording()
{
    m_recorded = Reads();
    m_reads = &m_recorded;
}

Reads Engine::stopRecording()
{
    m_reads = nullptr;
    return std::exchange(m_recorded, Reads());
}

void Engine::noteRead(const QString &page)
{
    if (!m_reads)
        return;
    // By its file: a change names the file (a link's name, an alias or a day all read the same).
    const QString path = m_host->page(page).value(u"path"_s).toString();
    if (path.isEmpty())
        m_reads->names.insert(text::foldKey(page).simplified());
    else
        m_reads->paths.insert(path);
}

// ---- shared code and settings (docs/EXTENSIONS.md "Shared code", "Settings") --------------------

namespace {

// What b.require's raw function gives back when it cannot: nil and the message (the wrapper raises
// it at the script's line).
int requireFailed(lua_State *L, const QString &message)
{
    lua_pushnil(L);
    pushString(L, QString(u"b.require: "_s + message));
    return 2;
}

bool isSettingKey(const QString &key)
{
    static const QRegularExpression valid(u"^[A-Za-z0-9_-]{1,64}$"_s);
    return valid.match(key).hasMatch();
}

bool isNumber(const QVariant &v)
{
    const int type = v.metaType().id();
    return type == QMetaType::LongLong || type == QMetaType::Double || type == QMetaType::Int;
}

// Whether `value` is one a declared setting can hold.
bool fitsSetting(const QVariantMap &field, const QVariant &value)
{
    const QString kind = field.value(u"kind"_s).toString();
    if (kind == u"number")
        return isNumber(value) && std::isfinite(value.toDouble());
    if (kind == u"toggle")
        return value.metaType().id() == QMetaType::Bool;
    if (kind == u"choice")
        return value.metaType().id() == QMetaType::QString
               && field.value(u"options"_s).toStringList().contains(value.toString());
    return value.metaType().id() == QMetaType::QString && value.toString().size() <= QuestionLimits::settingText;
}

// A setting as b.settings declares it, checked and normalized; "" or the error.
QString settingField(const QVariant &value, int index, QVariantMap *out)
{
    if (value.metaType().id() != QMetaType::QVariantMap)
        return u"setting %1 must be a table { key = …, label = …, value = … }"_s.arg(index);
    const QVariantMap map = value.toMap();
    const QString key = map.value(u"key"_s).toString();
    if (map.value(u"key"_s).metaType().id() != QMetaType::QString || !isSettingKey(key))
        return u"setting %1 needs a key of letters, digits, - and _ (at most 64)"_s.arg(index);
    const QVariant initial = map.value(u"value"_s);
    QString kind = map.value(u"kind"_s).toString();
    if (kind.isEmpty())
        kind = map.contains(u"options"_s)                   ? u"choice"_s
               : initial.metaType().id() == QMetaType::Bool ? u"toggle"_s
               : isNumber(initial)                          ? u"number"_s
                                                            : u"text"_s;
    if (kind != u"number" && kind != u"text" && kind != u"toggle" && kind != u"choice")
        return u"the setting \"%1\": kind is \"number\", \"text\", \"toggle\" or \"choice\""_s.arg(key);
    QVariantMap field{{u"key"_s, key}, {u"kind"_s, kind}};
    const QString label = map.value(u"label"_s).toString().simplified();
    field.insert(u"label"_s, cutText(label.isEmpty() ? key : label, QuestionLimits::label));
    field.insert(u"description"_s, cutText(map.value(u"description"_s).toString().simplified(), 300));
    if (kind == u"number") {
        if (initial.isValid() && !isNumber(initial))
            return u"the setting \"%1\": its value must be a number"_s.arg(key);
        double step = map.value(u"step"_s, 1).toDouble();
        if (!std::isfinite(step) || step <= 0)
            step = 1;
        field.insert(u"value"_s, initial.isValid() ? initial : QVariant(qint64(0)));
        field.insert(u"step"_s, step);
        field.insert(u"unit"_s, cutText(map.value(u"unit"_s).toString().simplified(), 20));
    } else if (kind == u"toggle") {
        if (initial.isValid() && initial.metaType().id() != QMetaType::Bool)
            return u"the setting \"%1\": its value must be true or false"_s.arg(key);
        field.insert(u"value"_s, initial.toBool());
    } else if (kind == u"choice") {
        QStringList options;
        for (const QVariant &o : map.value(u"options"_s).toList()) {
            if (o.metaType().id() != QMetaType::QString || o.toString().size() > QuestionLimits::label)
                return u"the setting \"%1\": its options are texts of at most %2 characters"_s.arg(key).arg(
                        QuestionLimits::label);
            if (!options.contains(o.toString()))
                options << o.toString();
        }
        if (options.isEmpty() || options.size() > 50)
            return u"the setting \"%1\" needs 1 to 50 options"_s.arg(key);
        if (initial.isValid() && !options.contains(initial.toString()))
            return u"the setting \"%1\": its value must be one of its options"_s.arg(key);
        field.insert(u"options"_s, options);
        field.insert(u"value"_s, initial.isValid() ? initial.toString() : options.first());
    } else {
        if (initial.isValid() && initial.metaType().id() != QMetaType::QString)
            return u"the setting \"%1\": its value must be a text"_s.arg(key);
        field.insert(u"value"_s, cutText(initial.toString(), QuestionLimits::settingText));
        field.insert(u"placeholder"_s,
                     cutText(map.value(u"placeholder"_s).toString().simplified(), QuestionLimits::label));
    }
    *out = field;
    return {};
}

} // namespace

int Engine::requireImpl(lua_State *L)
{
    Unlimited unlimited(&m_unlimited); // (host-side: nothing of it may fail half-way)
    lua_rawcheckstack(L, 8);
    const QString extension = extensionOf(L);
    if (lua_type(L, 1) != LUA_TSTRING)
        return requireFailed(L,
                             u"argument 1 must be a page's name, not %1"_s.arg(QString::fromUtf8(luaL_typename(L, 1))));
    const QString name = toQString(L, 1).trimmed();
    const auto loaded = m_loaded.find(extension);
    if (loaded == m_loaded.end() || name.isEmpty())
        return requireFailed(L, u"the page's name is empty"_s);
    const ModuleSource module = m_host->module(extension, name);
    // A macro that requires a page read it (its extension loads again when it changes, too).
    if (m_reads && !module.path.isEmpty())
        m_reads->paths.insert(module.path);
    if (!module.error.isEmpty())
        return requireFailed(L, module.error);
    if (const auto known = loaded->moduleRefs.constFind(module.path); known != loaded->moduleRefs.constEnd()) {
        lua_pushboolean(L, 1);
        lua_getref(L, *known);
        return 2;
    }
    if (loaded->requiring.contains(module.path)) {
        QStringList chain;
        for (const QString &path : std::as_const(loaded->requiring))
            chain << paths::pageTitle(path);
        chain << module.title;
        return requireFailed(L, u"a cycle: "_s + chain.join(u" → "_s));
    }
    if (loaded->requiring.size() >= QuestionLimits::requireDepth)
        return requireFailed(L, u"modules nested more than %1 deep"_s.arg(QuestionLimits::requireDepth));
    // Brulion compiles the page's code itself (never bytecode from elsewhere), once per version.
    QByteArray code = m_moduleCode.value(module.hash);
    if (code.isEmpty() || module.hash.isEmpty()) {
        lua_CompileOptions options = {};
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        const QByteArray src = module.source.toUtf8();
        size_t size = 0;
        char *bytecode = luau_compile(src.constData(), size_t(src.size()), &options, &size);
        code = QByteArray(bytecode, qsizetype(size));
        std::free(bytecode);
        if (m_moduleCode.size() >= 64)
            m_moduleCode.clear();
        if (!module.hash.isEmpty())
            m_moduleCode.insert(module.hash, code);
    }
    // Its own globals, the requester's `b` and print: its code does what the requester may.
    lua_createtable(L, 0, 4);
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setreadonly(L, -1, true);
    lua_setmetatable(L, -2);
    lua_getref(L, loaded->envRef);
    lua_rawgetfield(L, -1, "b");
    lua_setfield(L, -3, "b");
    lua_rawgetfield(L, -1, "print");
    lua_setfield(L, -3, "print");
    lua_pop(L, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
    lua_setsafeenv(L, -1, true);
    const QByteArray chunk = "=" + module.title.toUtf8();
    if (luau_load(L, chunk.constData(), code.constData(), size_t(code.size()), -1) != 0) {
        const QString message = toQString(L, -1);
        lua_pop(L, 2);
        lua_pushnil(L);
        pushString(L, message); // (the module's own line: "Wspólne:3: …")
        return 2;
    }
    lua_remove(L, -2); // the environment (the chunk keeps it)
    loaded->requiring << module.path;
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    pushString(L, module.path);
    return 3;
}

int Engine::requireDoneImpl(lua_State *L)
{
    Unlimited unlimited(&m_unlimited);
    lua_rawcheckstack(L, 2);
    const auto loaded = m_loaded.find(extensionOf(L));
    if (loaded == m_loaded.end() || lua_type(L, 1) != LUA_TSTRING)
        return 0;
    const QString path = toQString(L, 1);
    loaded->requiring.removeAll(path);
    if (!loaded->modules.contains(path))
        loaded->modules << path; // (a module that failed too: fixing its page loads this again)
    if (!lua_toboolean(L, 2))
        return 0;
    if (lua_isnil(L, 3))
        lua_pushboolean(L, 1);
    else
        lua_pushvalue(L, 3);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    if (const auto old = loaded->moduleRefs.constFind(path); old != loaded->moduleRefs.constEnd())
        lua_unref(L, *old);
    loaded->moduleRefs.insert(path, ref);
    return 0;
}

QStringList Engine::modulesOf(const QString &extension) const
{
    return m_loaded.value(extension).modules;
}

QVariantList Engine::settingsOf(const QString &extension) const
{
    return m_loaded.value(extension).settings;
}

QVariantMap Engine::settingValues(const QString &extension) const
{
    QVariantMap values = m_host ? m_host->settings(extension) : QVariantMap();
    for (const QVariant &f : m_loaded.value(extension).settings) {
        const QVariantMap field = f.toMap();
        const QString key = field.value(u"key"_s).toString();
        if (!fitsSetting(field, values.value(key)))
            values.insert(key, field.value(u"value"_s));
    }
    return values;
}

int Engine::settingsImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    const auto loaded = m_loaded.find(extension);
    if (loaded == m_loaded.end())
        return failWith(L, u"b.settings: no such extension"_s);
    if (!lua_isnoneornil(L, 1)) {
        if (!mayChange(L, "b.settings"))
            return kRaise;
        if (lua_type(L, 1) != LUA_TTABLE)
            return failWith(L, u"b.settings: argument 1 must be a list of settings"_s);
        QVariant value;
        if (!argValue(L, 1, "b.settings", &value))
            return kRaise;
        if (value.metaType().id() != QMetaType::QVariantList && !value.toMap().isEmpty())
            return failWith(L, u"b.settings: argument 1 must be a list of settings"_s);
        const QVariantList list = value.toList();
        if (list.size() > QuestionLimits::settings)
            return failWith(L, u"b.settings: at most %1 settings"_s.arg(QuestionLimits::settings));
        QVariantList fields;
        QStringList keys;
        for (qsizetype i = 0; i < list.size(); ++i) {
            QVariantMap field;
            if (const QString error = settingField(list[i], int(i + 1), &field); !error.isEmpty())
                return failWith(L, u"b.settings: "_s + error);
            const QString key = field.value(u"key"_s).toString();
            if (keys.contains(key))
                return failWith(L, u"b.settings: the key \"%1\" is there twice"_s.arg(key));
            keys << key;
            fields << field;
        }
        loaded->settings = fields;
    }
    const QVariantMap values = settingValues(extension);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, values);
    return 1;
}

int Engine::settingImpl(lua_State *L)
{
    QString key;
    if (!argString(L, 1, "b.setting", &key))
        return kRaise;
    const QVariant value = settingValues(extensionOf(L)).value(key);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, value);
    return 1;
}

int Engine::setSettingImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString key;
    if (!argString(L, 1, "b.setSetting", &key))
        return kRaise;
    if (!isSettingKey(key))
        return failWith(L, u"b.setSetting: a key is letters, digits, - and _ (at most 64)"_s);
    if (!mayChange(L, "b.setSetting"))
        return kRaise;
    const int type = lua_type(L, 2);
    if (type != LUA_TNIL && type != LUA_TNONE && type != LUA_TBOOLEAN && type != LUA_TNUMBER && type != LUA_TSTRING)
        return failWith(L, u"b.setSetting: a setting is a text, a number or true / false, not %1"_s.arg(
                                   QString::fromUtf8(luaL_typename(L, 2))));
    QVariant value;
    if (type != LUA_TNONE && !argValue(L, 2, "b.setSetting", &value))
        return kRaise;
    if (value.metaType().id() == QMetaType::QString && value.toString().size() > QuestionLimits::settingText)
        return failWith(L, u"b.setSetting: a text of at most %1 characters"_s.arg(QuestionLimits::settingText));
    if (value.isValid() && value.metaType().id() == QMetaType::Double && !std::isfinite(value.toDouble()))
        return failWith(L, u"b.setSetting: the number is none"_s);
    for (const QVariant &f : m_loaded.value(extension).settings) {
        const QVariantMap field = f.toMap();
        if (field.value(u"key"_s).toString() != key || !value.isValid() || fitsSetting(field, value))
            continue;
        const QString kind = field.value(u"kind"_s).toString();
        const QString what = kind == u"number"   ? u"a number"_s
                             : kind == u"toggle" ? u"true or false"_s
                             : kind == u"choice" ? u"one of its options"_s
                                                 : u"a text"_s;
        return failWith(L, u"b.setSetting: \"%1\" is %2"_s.arg(key, what));
    }
    const QString error = m_host->setSetting(extension, key, value);
    if (!error.isEmpty())
        return failWith(L, u"b.setSetting: "_s + error);
    return 0;
}

// ---- b.app (appapi.h; docs/EXTENSIONS.md "The app (`b.app`)") --------------------------------------

bool Engine::mayChangeApp(lua_State *L, const QString &extension, const QString &function, bool notWhileLoading)
{
    if (m_noWrites) {
        failWith(L, function + u": a macro or a template variable cannot change the app"_s);
        return false;
    }
    if (!m_loaded.value(extension).permissions.contains(u"app"_s)) {
        failWith(L, function + u": permission denied: app (the page needs `permissions: [app]`)"_s);
        return false;
    }
    if (notWhileLoading && m_loading) {
        failWith(L, function + u" cannot run while the extension loads: call it from a command, an action or a hook"_s);
        return false;
    }
    return true;
}

int Engine::appImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    const QString operation = toQString(L, lua_upvalueindex(2));
    const QString function = u"b.app."_s + operation;
    const QByteArray fn = function.toUtf8();
    QVariantMap args;
    if (operation == u"get" || operation == u"set") {
        QString key;
        if (!argString(L, 1, fn.constData(), &key))
            return kRaise;
        args.insert(u"key"_s, key);
        if (operation == u"set") {
            if (lua_isnone(L, 2))
                return failWith(L, function + u": argument 2 is the setting's new value"_s);
            QVariant value;
            if (!argValue(L, 2, fn.constData(), &value))
                return kRaise;
            args.insert(u"value"_s, value);
        }
    } else if (operation == u"navigate") {
        const int type = lua_type(L, 1);
        if (type != LUA_TSTRING && type != LUA_TTABLE)
            return failWith(L, function
                                       + u": argument 1 is a view's name (\"calendar\") or a route table "
                                         "({ view = \"page\", page = … })"_s);
        QVariant route;
        if (!argValue(L, 1, fn.constData(), &route))
            return kRaise;
        args.insert(u"route"_s, route);
    } else if (operation == u"open") {
        QString page;
        if (!argString(L, 1, fn.constData(), &page))
            return kRaise;
        if (page.trimmed().isEmpty())
            return failWith(L, function + u": the page's name is empty"_s);
        args.insert(u"page"_s, page.trimmed());
        if (!lua_isnoneornil(L, 2)) {
            if (lua_type(L, 2) != LUA_TTABLE)
                return failWith(L, function + u": argument 2 is a table ({ line = 12, side = true })"_s);
            QVariant options;
            if (!argValue(L, 2, fn.constData(), &options))
                return kRaise;
            const QVariantMap o = options.toMap();
            for (auto it = o.cbegin(); it != o.cend(); ++it) {
                if (it.key() != u"line" && it.key() != u"side")
                    return failWith(L, function + u": no option “%1” (line, side)"_s.arg(it.key()));
            }
            if (o.contains(u"line"_s)) {
                bool ok = false;
                const qint64 line = o.value(u"line"_s).metaType().id() == QMetaType::QString
                                            ? 0
                                            : o.value(u"line"_s).toLongLong(&ok);
                if (!ok || line < 1)
                    return failWith(L, function + u": “line” is a line of the page, counted from 1"_s);
                args.insert(u"line"_s, line);
            }
            if (o.contains(u"side"_s))
                args.insert(u"side"_s, o.value(u"side"_s).toBool());
        }
    } else if (operation == u"run") {
        QString id;
        if (!argString(L, 1, fn.constData(), &id))
            return kRaise;
        args.insert(u"id"_s, id.trimmed());
    } else if (operation == u"toast") {
        QString text;
        if (!argString(L, 1, fn.constData(), &text))
            return kRaise;
        if (text.trimmed().isEmpty())
            return failWith(L, function + u": the text is empty"_s);
        args.insert(u"text"_s, cutText(text.simplified(), QuestionLimits::timerMessage));
        if (!lua_isnoneornil(L, 2)) {
            if (lua_type(L, 2) != LUA_TTABLE)
                return failWith(L, function + u": argument 2 is a table ({ tone = \"success\", button = … })"_s);
            QVariant options;
            if (!argValue(L, 2, fn.constData(), &options))
                return kRaise;
            const QVariantMap o = options.toMap();
            for (auto it = o.cbegin(); it != o.cend(); ++it) {
                if (it.key() != u"tone" && it.key() != u"button")
                    return failWith(L, function + u": no option “%1” (tone, button)"_s.arg(it.key()));
            }
            const QString tone = o.value(u"tone"_s).toString();
            if (!tone.isEmpty() && tone != u"success" && tone != u"danger" && tone != u"warning" && tone != u"info"
                && tone != u"accent")
                return failWith(L, function + u": “tone” is success, danger, warning, info or accent"_s);
            if (!tone.isEmpty())
                args.insert(u"tone"_s, tone);
            if (o.contains(u"button"_s)) {
                const QVariantMap button = o.value(u"button"_s).toMap();
                const QString label = button.value(u"label"_s).toString().simplified();
                const QString command = button.value(u"command"_s).toString().trimmed();
                if (label.isEmpty() || command.isEmpty() || button.size() != 2)
                    return failWith(L, function
                                               + u": a button is { label = …, command = … } (one of the "
                                                 "extension's commands)"_s);
                args.insert(u"button"_s,
                            QVariantMap{{u"label"_s, cutText(label, QuestionLimits::label)}, {u"command"_s, command}});
            }
        }
    }
    if (isAppChange(operation) && !mayChangeApp(L, extension, function, true))
        return kRaise;
    QString error;
    const QVariant value = m_host->app(extension, operation, args, &error);
    if (!error.isEmpty())
        return failWith(L, function + u": "_s + error);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, value);
    return 1;
}

int Engine::appThemeImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!mayChangeApp(L, extension, u"b.app.theme"_s, false))
        return kRaise;
    ThemeLayer layer;
    if (!lua_isnoneornil(L, 1)) {
        if (lua_type(L, 1) != LUA_TTABLE)
            return failWith(L, u"b.app.theme: argument 1 is a table of tokens ({ accent = \"#3a7\", dark = { … } }) "
                               "or nil"_s);
        QVariant table;
        if (!argValue(L, 1, "b.app.theme", &table))
            return kRaise;
        QString error;
        if (!themeLayerFrom(table.toMap(), &layer, &error))
            return failWith(L, u"b.app.theme: "_s + error);
    }
    qsizetype at = -1;
    for (qsizetype i = 0; i < m_themes.size(); ++i)
        if (m_themes[i].extension == extension)
            at = i;
    if (at >= 0) {
        if (m_themes[at].layer == layer)
            return 0;
        m_themes.removeAt(at);
    } else if (layer.isEmpty()) {
        return 0;
    }
    if (!layer.isEmpty())
        m_themes.append({extension, layer}); // set last: it wins
    ++m_appRevision;
    m_host->appChanged(extension);
    return 0;
}

int Engine::appContributeImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString slot;
    if (!argString(L, 1, "b.app.contribute", &slot))
        return kRaise;
    if (!mayChangeApp(L, extension, u"b.app.contribute"_s, false))
        return kRaise;
    if (lua_type(L, 2) != LUA_TTABLE)
        return failWith(L, u"b.app.contribute: argument 2 is the item ({ id = …, label = …, command = … })"_s);
    QVariant table;
    if (!argValue(L, 2, "b.app.contribute", &table))
        return kRaise;
    Contribution item;
    QString error;
    if (!contributionFrom(slot, table.toMap(), &item, &error))
        return failWith(L, u"b.app.contribute: "_s + error);
    item.extension = extension;
    if (!item.command.isEmpty() && !m_commands.contains(extension + u'/' + item.command) && !m_loading)
        return failWith(L, u"b.app.contribute: the extension has no command “%1”"_s.arg(item.command));
    if (!item.view.isEmpty() && !m_views.contains(extension + u'/' + item.view) && !m_loading)
        return failWith(L, u"b.app.contribute: the extension has no view “%1”"_s.arg(item.view));
    int own = 0, all = 0;
    for (qsizetype i = 0; i < m_contributions.size(); ++i) {
        Contribution &c = m_contributions[i];
        if (c.slot != slot)
            continue;
        if (c.extension == extension && c.id == item.id) {
            if (c == item)
                return 0;
            c = item; // the same id: replaced in its place
            ++m_appRevision;
            m_host->appChanged(extension);
            return 0;
        }
        ++all;
        if (c.extension == extension)
            ++own;
    }
    const ContributionLimits limits = contributionLimits(slot);
    if (own >= limits.perExtension)
        return failWith(
                L, u"b.app.contribute: at most %1 items of one extension in “%2”"_s.arg(limits.perExtension).arg(slot));
    if (all >= limits.all)
        return failWith(L, u"b.app.contribute: “%1” is full (%2 items of all extensions)"_s.arg(slot).arg(limits.all));
    m_contributions.append(item);
    ++m_appRevision;
    m_host->appChanged(extension);
    return 0;
}

int Engine::appRemoveImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    QString slot, id;
    if (!argString(L, 1, "b.app.remove", &slot) || !argOptString(L, 2, "b.app.remove", &id))
        return kRaise;
    if (slot == u"inputRule" || slot == u"tagStyle" || slot == u"replaceView") {
        if (!mayChangeApp(L, extension, u"b.app.remove"_s, false))
            return kRaise;
        return appRemoveDeclarationImpl(L, extension, slot, id);
    }
    if (!isContributionSlot(slot))
        return failWith(L, u"b.app.remove: unknown slot “%1” (%2, or inputRule, tagStyle, replaceView)"_s.arg(
                                   slot, contributionSlots().join(u", "_s)));
    if (!mayChangeApp(L, extension, u"b.app.remove"_s, false))
        return kRaise;
    const qsizetype removed = m_contributions.removeIf([&](const Contribution &c) {
        return c.extension == extension && c.slot == slot && (id.isEmpty() || c.id == id);
    });
    if (removed > 0) {
        ++m_appRevision;
        m_host->appChanged(extension);
    }
    lua_pushboolean(L, removed > 0);
    return 1;
}

int Engine::appContributionsImpl(lua_State *L)
{
    QString slot;
    if (!argOptString(L, 1, "b.app.contributions", &slot))
        return kRaise;
    if (!slot.isEmpty() && !isContributionSlot(slot))
        return failWith(L,
                        u"b.app.contributions: unknown slot “%1” (%2)"_s.arg(slot, contributionSlots().join(u", "_s)));
    QVariantList out;
    for (const Contribution &c : contributions(slot))
        out << contributionTable(c);
    Unlimited unlimited(&m_unlimited);
    pushVariant(L, out);
    return 1;
}

int Engine::appInputRuleImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!mayChangeApp(L, extension, u"b.app.inputRule"_s, false))
        return kRaise;
    if (lua_type(L, 1) != LUA_TTABLE)
        return failWith(L, u"b.app.inputRule: argument 1 is the rule ({ match = \"->\", replace = \"→\" })"_s);
    QVariant table;
    if (!argValue(L, 1, "b.app.inputRule", &table))
        return kRaise;
    InputRule rule;
    QString error;
    if (!inputRuleFrom(table.toMap(), &rule, &error))
        return failWith(L, u"b.app.inputRule: "_s + error);
    rule.extension = extension;
    int own = 0;
    for (InputRule &r : m_inputRules) {
        if (r.extension != extension)
            continue;
        if (r.match == rule.match) {
            if (r == rule)
                return 0;
            r = rule; // the same text typed: replaced in its place
            ++m_appRevision;
            m_host->appChanged(extension);
            return 0;
        }
        ++own;
    }
    if (own >= MaxInputRulesPerExtension)
        return failWith(L, u"b.app.inputRule: at most %1 rules of one extension"_s.arg(MaxInputRulesPerExtension));
    if (m_inputRules.size() >= MaxInputRules)
        return failWith(L, u"b.app.inputRule: at most %1 rules of all extensions"_s.arg(MaxInputRules));
    m_inputRules.append(rule);
    ++m_appRevision;
    m_host->appChanged(extension);
    return 0;
}

int Engine::appTagStyleImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!mayChangeApp(L, extension, u"b.app.tagStyle"_s, false))
        return kRaise;
    if (lua_type(L, 1) != LUA_TTABLE)
        return failWith(L, u"b.app.tagStyle: argument 1 is the style ({ tag = \"pilne\", tint = \"danger\" })"_s);
    QVariant table;
    if (!argValue(L, 1, "b.app.tagStyle", &table))
        return kRaise;
    TagStyle style;
    QString error;
    if (!tagStyleFrom(table.toMap(), &style, &error))
        return failWith(L, u"b.app.tagStyle: "_s + error);
    style.extension = extension;
    const QString key = tagStyleKey(style.tag);
    qsizetype at = -1;
    int own = 0;
    for (qsizetype i = 0; i < m_tagStyles.size(); ++i) {
        if (m_tagStyles[i].extension != extension)
            continue;
        if (tagStyleKey(m_tagStyles[i].tag) == key)
            at = i;
        else
            ++own;
    }
    if (at >= 0) {
        if (m_tagStyles[at] == style)
            return 0;
        m_tagStyles.removeAt(at); // declared again: last, so it wins the tag
    } else if (own >= MaxTagStylesPerExtension) {
        return failWith(L, u"b.app.tagStyle: at most %1 styles of one extension"_s.arg(MaxTagStylesPerExtension));
    } else if (m_tagStyles.size() >= MaxTagStyles) {
        return failWith(L, u"b.app.tagStyle: at most %1 styles of all extensions"_s.arg(MaxTagStyles));
    }
    m_tagStyles.append(style);
    ++m_appRevision;
    m_host->appChanged(extension);
    return 0;
}

int Engine::appReplaceViewImpl(lua_State *L)
{
    const QString extension = extensionOf(L);
    if (!mayChangeApp(L, extension, u"b.app.replaceView"_s, false))
        return kRaise;
    QString builtin, view;
    if (!argString(L, 1, "b.app.replaceView", &builtin) || !argString(L, 2, "b.app.replaceView", &view))
        return kRaise;
    QVariant params;
    if (!lua_isnoneornil(L, 3)) {
        if (lua_type(L, 3) != LUA_TTABLE)
            return failWith(L, u"b.app.replaceView: argument 3 is the view's params (a table)"_s);
        if (!argValue(L, 3, "b.app.replaceView", &params))
            return kRaise;
    }
    ViewReplacement replacement;
    QString error;
    if (!viewReplacementFrom(builtin, view, params.toMap(), &replacement, &error))
        return failWith(L, u"b.app.replaceView: "_s + error);
    replacement.extension = extension;
    // At the top level the view may come later in the file; once loaded it must be there.
    if (!m_views.contains(extension + u'/' + view) && !m_loading)
        return failWith(L, u"b.app.replaceView: the extension has no view “%1”"_s.arg(view));
    qsizetype at = 0;
    while (at < m_viewReplacements.size()
           && (m_viewReplacements[at].extension != extension || m_viewReplacements[at].builtin != builtin))
        ++at;
    if (at < m_viewReplacements.size()) {
        if (m_viewReplacements[at] == replacement)
            return 0;
        m_viewReplacements.removeAt(at); // declared again: last, so it wins the view
    } else if (m_viewReplacements.size() >= MaxViewReplacements) {
        return failWith(L,
                        u"b.app.replaceView: at most %1 views replaced by all extensions"_s.arg(MaxViewReplacements));
    }
    m_viewReplacements.append(replacement);
    ++m_appRevision;
    m_host->appChanged(extension);
    return 0;
}

std::optional<ViewReplacement> Engine::viewReplacement(const QString &builtin) const
{
    for (auto it = m_viewReplacements.crbegin(); it != m_viewReplacements.crend(); ++it)
        if (it->builtin == builtin && m_views.contains(it->extension + u'/' + it->view))
            return *it;
    return std::nullopt;
}

int Engine::appRemoveDeclarationImpl(lua_State *L, const QString &extension, const QString &kind, const QString &key)
{
    qsizetype removed = 0;
    if (kind == u"inputRule") {
        removed = m_inputRules.removeIf([&](const InputRule &r) {
            return r.extension == extension
                   && (key.isEmpty() || r.match == key.normalized(QString::NormalizationForm_C));
        });
    } else if (kind == u"tagStyle") {
        const QString folded = tagStyleKey(key);
        removed = m_tagStyles.removeIf([&](const TagStyle &t) {
            return t.extension == extension && (key.isEmpty() || tagStyleKey(t.tag) == folded);
        });
    } else {
        removed = m_viewReplacements.removeIf([&](const ViewReplacement &r) {
            return r.extension == extension && (key.isEmpty() || r.builtin == key);
        });
    }
    if (removed > 0) {
        ++m_appRevision;
        m_host->appChanged(extension);
    }
    lua_pushboolean(L, removed > 0);
    return 1;
}

bool Engine::dropAppState(const QString &extension)
{
    const qsizetype items = m_contributions.removeIf([&](const Contribution &c) { return c.extension == extension; });
    const qsizetype themes = m_themes.removeIf([&](const ThemeInfo &t) { return t.extension == extension; });
    const qsizetype rules = m_inputRules.removeIf([&](const InputRule &r) { return r.extension == extension; });
    const qsizetype styles = m_tagStyles.removeIf([&](const TagStyle &t) { return t.extension == extension; });
    const qsizetype views =
            m_viewReplacements.removeIf([&](const ViewReplacement &r) { return r.extension == extension; });
    if (items + themes + rules + styles + views == 0)
        return false;
    ++m_appRevision;
    return true;
}

QList<Contribution> Engine::contributions(const QString &slot) const
{
    QList<Contribution> out;
    for (const Contribution &c : m_contributions)
        if (slot.isEmpty() || c.slot == slot)
            out << c;
    std::stable_sort(out.begin(), out.end(),
                     [](const Contribution &a, const Contribution &b) { return a.order < b.order; });
    return out;
}

QList<ThemeInfo> Engine::themes() const
{
    return m_themes;
}

} // namespace brulion::extensions
