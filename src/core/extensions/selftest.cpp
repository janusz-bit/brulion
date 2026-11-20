#include "extensions/selftest.h"

#include "extensions/engine.h"
#include "extensions/host.h"

#include <QElapsedTimer>
#include <QStringList>
#include <QVariantList>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

/// A host without notes: what a hostile script may reach of the app, and nothing to harm.
class EmptyHost : public Host
{
public:
    QStringList pages(const QString &) override { return {}; }
    std::optional<QString> read(const QString &) override { return std::nullopt; }
    QVariantMap page(const QString &) override { return {}; }
    QVariantList blocks(const BlockQuery &) override { return {}; }
    QVariantList search(const QString &, int) override { return {}; }
    QVariantMap query(const QString &, int, QString *) override
    {
        return {{u"total"_s, 0}, {u"items"_s, QVariantList()}};
    }
    QString write(const QString &, const QString &, const QString &) override { return u"nothing is written here"_s; }
    QString appendToJournal(const QString &, const QString &, const QString &) override
    {
        return u"nothing is written here"_s;
    }
    QString replaceBlock(const QString &, const QString &, const QString &, const std::optional<QString> &,
                         QString *) override
    {
        return u"nothing is written here"_s;
    }
    QString today() override { return u"2026-11-20"_s; }
    QString language() override { return u"pl"_s; }
    QString uiLanguage() override { return u"pl"_s; }
    void timer(const QString &, int, const QString &label) override { lastTimerLabel = label; }
    void log(const QString &, const QString &) override {}
    /// Two pages that require each other, and one that never ends.
    ModuleSource module(const QString &, const QString &name) override
    {
        ModuleSource m;
        m.id = m.title = name;
        m.path = name + u".md"_s;
        if (name == u"Pętla")
            m.source = u"while true do end\nreturn {}"_s;
        else if (name == u"Ping")
            m.source = u"return b.require(\"Pong\")"_s;
        else if (name == u"Pong")
            m.source = u"return b.require(\"Ping\")"_s;
        else
            m.error = u"no page named “%1”"_s.arg(name);
        return m;
    }
    QString lastTimerLabel;
};

struct Check
{
    const char *name;
    const char *code; // a command's body
    Status expected;
    const char *contains = nullptr; // in the error or the value (as text)
};

QString statusName(Status s)
{
    switch (s) {
    case Status::Ok: return u"ok"_s;
    case Status::Error: return u"error"_s;
    case Status::Suspended: return u"waiting"_s;
    case Status::Timeout: return u"timeout"_s;
    case Status::OutOfMemory: return u"out_of_memory"_s;
    case Status::StackOverflow: return u"stack_overflow"_s;
    case Status::NotFound: return u"not_found"_s;
    }
    return u"error"_s;
}

} // namespace

QVariantMap hostileSelfTest(int limitMs)
{
    static const Check checks[] = {
            {"a loop for ever", "while true do end", Status::Timeout},
            {"a loop in pcall, retried", "while true do pcall(function() while true do end end) end", Status::Timeout},
            {"a loop in an error handler", "xpcall(function() error('x') end, function() while true do end end)",
             Status::Timeout},
            {"a loop in a coroutine", "local co = coroutine.wrap(function() while true do end end) co()", Status::Timeout},
            {"a loop in a sort's comparator", "local t = {3, 2, 1} table.sort(t, function(a, b) while true do end end)",
             Status::Timeout},
            {"pattern backtracking", "return string.find(string.rep('a', 60), 'a-a-a-a-a-a-a-a-a-a-a-b')", Status::Timeout},
            {"a plain search for ever",
             "local s = string.rep('a', 2000000) local p = string.rep('a', 20000) .. 'b' while true do string.find(s, p, 1, true) end",
             Status::Timeout},
            {"a string that doubles", "local s = 'x' while true do s = s .. s end", Status::OutOfMemory},
            {"a table that grows", "local t = {} local i = 0 while true do i += 1 t[i] = {i, i, i} end", Status::OutOfMemory},
            {"memory in pcall", "while true do pcall(function() local s = 'x' while true do s = s .. s end end) end",
             Status::OutOfMemory},
            {"endless recursion", "local function f(n) return 1 + f(n + 1) end return f(1)", Status::StackOverflow},
            {"a module that never ends", "return b.require('Pętla')", Status::Timeout},
            {"modules in a cycle", "return b.require('Ping')", Status::Error, "a cycle"},
            {"a megabyte of question", "return b.ask('Tytuł\\n' .. string.rep('ż', 1000000))", Status::Suspended},
            {"thousands of options",
             "local o = {} for i = 1, 5000 do o[i] = tostring(i) end return b.choose('Które?', o)", Status::Error,
             "at most 500 options"},
            {"a form of thousands of fields",
             "local f = {} for i = 1, 5000 do f[i] = { key = 'k' .. i, value = i } end return b.form('F', f)",
             Status::Error, "at most 50 fields"},
            {"a timer's long label", "b.timer(60, string.rep('ą', 100000)) return 'ok'", Status::Ok},
            {"no way out",
             "return tostring(loadstring == nil and load == nil and require == nil and io == nil and os.execute == nil "
             "and string.dump == nil and os.getenv == nil and os.remove == nil)",
             Status::Ok, "true"},
            {"the library is read-only", "string.rep = nil", Status::Error, "readonly"},
            {"b is read-only", "b.write = function() end", Status::Error, "readonly"},
    };

    QVariantList results;
    int failed = 0;
    EmptyHost host;
    Engine engine;
    QString error;
    if (!engine.start(&host, Limits{limitMs, 16ll << 20}, &error))
        return {{u"ok"_s, false}, {u"error"_s, error}};
    QElapsedTimer clock;
    for (const Check &c : checks) {
        const QString name = QString::fromUtf8(c.name);
        Extension x;
        x.id = u"hostile"_s;
        x.name = u"Wrogie"_s;
        x.source = u"b.command(\"run\", function(ctx)\n"_s + QString::fromUtf8(c.code) + u"\nend)\n"_s;
        bool ok = true;
        QString detail;
        Result r = engine.load(x);
        if (!r.ok()) {
            ok = false;
            detail = u"did not load: "_s + r.error;
        }
        clock.start();
        if (ok)
            r = engine.runCommand(x.id, u"run"_s, {});
        const qint64 ms = clock.elapsed();
        const QString text = r.status == Status::Ok ? r.value.toString() : r.error;
        if (ok && r.status != c.expected) {
            ok = false;
            detail = u"%1 instead of %2: %3"_s.arg(statusName(r.status), statusName(c.expected), text.section(u'\n', 0, 0));
        }
        if (ok && c.contains && !text.contains(QString::fromUtf8(c.contains))) {
            ok = false;
            detail = u"no “%1” in: %2"_s.arg(QString::fromUtf8(c.contains), text.section(u'\n', 0, 0));
        }
        if (ok && r.status == Status::Suspended) {
            // What a dialog gets of it: cut to what it shows.
            const qsizetype shown = r.value.toMap().value(u"prompt"_s).toString().size();
            if (shown > QuestionLimits::title + QuestionLimits::text + 3) {
                ok = false;
                detail = u"the question shows %1 characters"_s.arg(shown);
            }
            engine.cancel(r.call);
        }
        if (ok && name == u"a timer's long label" && host.lastTimerLabel.size() > QuestionLimits::timerLabel) {
            ok = false;
            detail = u"the label is %1 characters"_s.arg(host.lastTimerLabel.size());
        }
        // In time: a limit that holds holds within a few of its periods, whatever the platform.
        if (ok && ms > qint64(limitMs) * 20 + 2000) {
            ok = false;
            detail = u"it took %1 ms"_s.arg(ms);
        }
        // And the engine works afterwards.
        if (ok) {
            const Result after = engine.evaluate(x.id, u"1 + 1"_s, u"after"_s);
            if (!after.ok() || after.value.toInt() != 2) {
                ok = false;
                detail = u"the engine is broken afterwards: "_s + after.error;
            }
        }
        failed += ok ? 0 : 1;
        results << QVariantMap{{u"name"_s, name}, {u"ok"_s, ok}, {u"got"_s, statusName(r.status)}, {u"ms"_s, ms},
                               {u"detail"_s, detail}};
    }
    return {{u"ok"_s, failed == 0},
            {u"engine"_s, Engine::version()},
            {u"limitMs"_s, limitMs},
            {u"failed"_s, failed},
            {u"memory"_s, engine.memoryUsed()},
            {u"checks"_s, results}};
}

} // namespace brulion::extensions
