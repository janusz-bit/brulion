// The Luau engine of extensions (src/core/extensions/engine.h): loading, commands, views and their
// actions, questions that suspend a call, errors with lines, permissions, the sandbox, and the time,
// memory and stack limits a script cannot catch.

#include "extensions/engine.h"
#include "extensions/host.h"

#include <QElapsedTimer>
#include <QTest>

#include <clocale>

using namespace Qt::StringLiterals;
using namespace brulion::extensions;

namespace {

class FakeHost : public Host
{
public:
    QMap<QString, QString> pagesByName;
    QStringList logs;
    QList<QPair<int, QString>> timers;
    QString lang = u"pl"_s;
    QString uiLang = u"en"_s;

    QStringList pages(const QString &tag) override
    {
        QStringList out;
        for (auto it = pagesByName.cbegin(); it != pagesByName.cend(); ++it)
            if (tag.isEmpty() || it.value().contains(u"#"_s + tag))
                out << it.key();
        return out;
    }
    std::optional<QString> read(const QString &page) override
    {
        if (!pagesByName.contains(page))
            return std::nullopt;
        return pagesByName.value(page);
    }
    QVariantMap page(const QString &page) override
    {
        if (!pagesByName.contains(page))
            return {};
        return {{u"name"_s, page}, {u"tags"_s, QVariantList{}}, {u"props"_s, QVariantMap{}}};
    }
    QVariantList blocks(const BlockQuery &query) override
    {
        QVariantList out;
        const QString content = pagesByName.value(query.page);
        int line = 0;
        for (const QString &text : content.split(u'\n')) {
            ++line;
            if (text.startsWith(u"- "_s))
                out << QVariantMap{{u"ref"_s, u"%1.md#L%2@0"_s.arg(query.page).arg(line)},
                                   {u"page"_s, query.page},
                                   {u"text"_s, text.mid(2)},
                                   {u"tags"_s, QVariantList{}},
                                   {u"props"_s, QVariantMap{}},
                                   {u"children"_s, QVariantList{}}};
        }
        return out;
    }
    /// search(): the lines of every page holding the query's text (as written), the query asked.
    QVariantList search(const QString &query, int limit) override
    {
        searched << query;
        QVariantList out;
        for (auto it = pagesByName.cbegin(); it != pagesByName.cend() && out.size() < limit; ++it) {
            int line = 0;
            for (const QString &text : it.value().split(u'\n')) {
                ++line;
                if (text.contains(query))
                    out << QVariantMap{{u"page"_s, it.key()},
                                       {u"line"_s, line},
                                       {u"excerpt"_s, text},
                                       {u"ref"_s, u"%1.md#L%2@0"_s.arg(it.key()).arg(line)}};
            }
        }
        return out;
    }
    /// query(): `queryResult` for any query but "zła", which is none.
    QVariantMap query(const QString &source, int limit, QString *error) override
    {
        queried << source;
        if (source == u"zła")
            *error = u"unknown filter"_s;
        QVariantMap result = queryResult;
        result.insert(u"limit"_s, limit);
        return error->isEmpty() ? result : QVariantMap();
    }
    QStringList searched, queried;
    QVariantMap queryResult{{u"total"_s, 1},
                            {u"items"_s, QVariantList{QVariantMap{{u"page"_s, u"Dom"_s},
                                                                  {u"text"_s, u"Zapłacić czynsz"_s},
                                                                  {u"task"_s, u" "_s},
                                                                  {u"due"_s, u"2026-11-13"_s}}}}};
    QString write(const QString &, const QString &page, const QString &content) override
    {
        pagesByName.insert(page, content);
        return {};
    }
    QString appendToJournal(const QString &, const QString &date, const QString &markdown) override
    {
        QString &page = pagesByName[date];
        page += (page.isEmpty() ? QString() : u"\n"_s) + markdown;
        return {};
    }
    QString replaceBlock(const QString &, const QString &ref, const QString &text, const std::optional<QString> &task,
                         QString *newRef) override
    {
        if (ref.endsWith(u"@stale"_s))
            return u"stale: "_s + ref;
        lastReplace = text + u'|' + task.value_or(u"none"_s);
        *newRef = ref + u"'"_s;
        return {};
    }
    QString today() override { return u"2026-11-11"_s; }
    QString language() override { return lang; }
    QString uiLanguage() override { return uiLang; }
    void timer(const QString &, int seconds, const QString &label) override { timers.append({seconds, label}); }
    void log(const QString &extension, const QString &message) override { logs << extension + u": "_s + message; }

    QString lastReplace;
};

Extension ext(const QString &id, const QString &source, bool write = true)
{
    return {id, source, write ? QStringList{u"write"_s} : QStringList{}, {}, {}};
}

} // namespace

#define LOAD_OK(extension)                                                                                             \
    do {                                                                                                               \
        const Result loaded = engine->load(extension);                                                                 \
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));                                                               \
    } while (false)

class TestExtensionsEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        host = FakeHost();
        engine = std::make_unique<Engine>();
        QString error;
        QVERIFY2(engine->start(&host, Limits{300, 32ll << 20}, &error), qPrintable(error));
    }
    void cleanup() { engine.reset(); }

    void commandsAndLabels()
    {
        const Result r = engine->load(ext(u"zadania"_s, uR"lua(
            b.command("z-zaznaczenia", function(ctx)
                local out = {}
                for _, line in ctx.selection:split("\n") do
                    if line ~= "" then table.insert(out, `- [ ] {line}`) end
                end
                return table.concat(out, "\n")
            end, { label = "Zadania: z zaznaczenia" })
            b.command("bez-etykiety", function() return 42 end)
        )lua"_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QList<CommandInfo> commands = engine->commands();
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands[0].name, u"bez-etykiety"_s);
        QCOMPARE(commands[0].label, u"bez-etykiety"_s);
        QCOMPARE(commands[1].label, u"Zadania: z zaznaczenia"_s);
        Result c = engine->runCommand(u"zadania"_s, u"z-zaznaczenia"_s, {{u"selection"_s, u"mleko\nchleb żytni"_s}});
        QVERIFY(c.ok());
        QCOMPARE(c.value.toString(), u"- [ ] mleko\n- [ ] chleb żytni"_s);
        c = engine->runCommand(u"zadania"_s, u"bez-etykiety"_s, {});
        QCOMPARE(c.value.toLongLong(), 42);
        QCOMPARE(engine->runCommand(u"zadania"_s, u"nie-ma"_s, {}).status, Status::NotFound);
        QCOMPARE(engine->runCommand(u"inne"_s, u"z-zaznaczenia"_s, {}).status, Status::NotFound);
    }

    void reloadReplacesRegistrations()
    {
        LOAD_OK(ext(u"r"_s, uR"lua(b.command("wersja", function() return "v1" end)
                                              b.command("stara", function() return 1 end))lua"_s));
        LOAD_OK(ext(u"r"_s, uR"lua(b.command("wersja", function() return "v2" end))lua"_s));
        QCOMPARE(engine->runCommand(u"r"_s, u"wersja"_s, {}).value.toString(), u"v2"_s);
        QCOMPARE(engine->runCommand(u"r"_s, u"stara"_s, {}).status, Status::NotFound);
        // a failed load leaves nothing registered
        const Result bad = engine->load(ext(u"r"_s, u"b.command(\"x\", function() end)\nerror(\"nie\")"_s));
        QCOMPARE(bad.status, Status::Error);
        QVERIFY(engine->commands().isEmpty());
        QVERIFY(!engine->extensions().contains(u"r"_s));
    }

    void errorsNameTheLine()
    {
        Result r = engine->load(ext(u"składnia"_s, u"local a = 1\nlocal b2 = 2\nlocal = 3\n"_s));
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.startsWith(u"składnia:3:"_s), qPrintable(r.error));
        LOAD_OK(ext(u"błąd"_s, u"b.command(\"x\", function()\n  local a = 1\n  local c = 2\n  nieistniejaca()\nend)"_s));
        r = engine->runCommand(u"błąd"_s, u"x"_s, {});
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.startsWith(u"błąd:4:"_s), qPrintable(r.error));
        // host errors carry the caller's line
        LOAD_OK(ext(u"daty"_s, u"b.command(\"x\", function()\n  return b.daysBetween(b.today(), \"jutro\")\nend)"_s));
        r = engine->runCommand(u"daty"_s, u"x"_s, {});
        QVERIFY2(r.error.startsWith(u"daty:2: b.daysBetween: invalid date: 2026-11-11 / jutro"_s), qPrintable(r.error));
        // wrong arguments are script errors, not crashes
        LOAD_OK(ext(u"arg"_s, u"b.command(\"x\", function() return b.read(42) end)"_s));
        r = engine->runCommand(u"arg"_s, u"x"_s, {});
        QVERIFY2(r.error.contains(u"b.read: argument 1 must be a string, not number"_s), qPrintable(r.error));
    }

    void questionsSuspendTheCall()
    {
        LOAD_OK(ext(u"q"_s, uR"lua(
            b.command("nowa", function()
                local name = b.ask("Nazwa strony?")
                if name == "" then return "anulowano" end
                local kind = b.choose("Rodzaj?", { "zwykła", "plan" })
                if kind == nil then return "bez rodzaju" end
                local form = b.form("Szczegóły", { { key = "n", label = "Ile", kind = "number", value = 3, step = 1 } })
                b.write(name, `# {name} ({kind}, {form and form.n})`)
                return "utworzono: " .. name
            end)
        )lua"_s));
        Result r = engine->runCommand(u"q"_s, u"nowa"_s, {});
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ask"_s);
        QCOMPARE(r.value.toMap().value(u"prompt"_s).toString(), u"Nazwa strony?"_s);
        QVERIFY(engine->isWaiting());
        r = engine->resume(u"Łąki"_s);
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList(), (QStringList{u"zwykła"_s, u"plan"_s}));
        r = engine->resume(u"plan"_s);
        QCOMPARE(r.status, Status::Suspended);
        const QVariantMap form = r.value.toMap();
        QCOMPARE(form.value(u"kind"_s).toString(), u"form"_s);
        QCOMPARE(form.value(u"fields"_s).toList().value(0).toMap().value(u"value"_s).toLongLong(), 3);
        r = engine->resume(QVariantMap{{u"n"_s, 5}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"utworzono: Łąki"_s);
        QCOMPARE(host.pagesByName.value(u"Łąki"_s), u"# Łąki (plan, 5)"_s);
        QVERIFY(!engine->isWaiting());

        // choose cancelled: nil
        engine->runCommand(u"q"_s, u"nowa"_s, {});
        engine->resume(u"Inna"_s);
        r = engine->resume(QVariant());
        QCOMPARE(r.value.toString(), u"bez rodzaju"_s);

        // a new call abandons the waiting one; resume without a question is an error
        QCOMPARE(engine->runCommand(u"q"_s, u"nowa"_s, {}).status, Status::Suspended);
        engine->cancel();
        QVERIFY(!engine->isWaiting());
        QCOMPARE(engine->resume(u"x"_s).status, Status::Error);
    }

    void questionsRefusedWhereTheyCannotWait()
    {
        // at load time and inside a coroutine of the script
        Result r = engine->load(ext(u"q1"_s, u"b.ask(\"teraz?\")"_s));
        QCOMPARE(r.status, Status::Error);
        LOAD_OK(ext(u"q2"_s, uR"lua(
            b.command("x", function()
                local co = coroutine.create(function() return b.ask("w korutynie?") end)
                local ok, err = coroutine.resume(co)
                return tostring(err)
            end)
        )lua"_s));
        r = engine->runCommand(u"q2"_s, u"x"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(r.value.toString().contains(u"cannot wait for an answer here"_s), qPrintable(r.value.toString()));
    }

    void permissions()
    {
        LOAD_OK(ext(u"tylko-odczyt"_s, uR"lua(
            b.command("próba", function()
                local ok, err = pcall(b.write, "Hack", "x")
                local ok2, err2 = pcall(b.replaceBlock, "A.md#L1@0", "x")
                local ok3, err3 = pcall(b.appendToJournal, "2026-11-11", "x")
                return { tostring(err), tostring(err2), tostring(err3), ok or ok2 or ok3 }
            end)
        )lua"_s, false));
        const QVariantList r = engine->runCommand(u"tylko-odczyt"_s, u"próba"_s, {}).value.toList();
        QVERIFY2(r[0].toString().endsWith(u"b.write: permission denied: write"_s), qPrintable(r[0].toString()));
        QVERIFY(r[1].toString().endsWith(u"b.replaceBlock: permission denied: write"_s));
        QVERIFY(r[2].toString().endsWith(u"b.appendToJournal: permission denied: write"_s));
        QCOMPARE(r[3].toBool(), false);
        QVERIFY(!host.pagesByName.contains(u"Hack"_s));
    }

    void viewsAndActions()
    {
        host.pagesByName.insert(u"Lista"_s, u"- jeden\n- dwa"_s);
        LOAD_OK(ext(u"v"_s, uR"lua(
            b.view("lista", function(params)
                local rows = {}
                for _, block in b.blocks({ page = params.page }) do
                    table.insert(rows, b.ui.row({ b.ui.check(false, "odhacz", { ref = block.ref, text = block.text }),
                                                  b.ui.text(block.text) }))
                end
                table.insert(rows, b.ui.stepper({ value = 2.5, step = 2.5, unit = "kg", action = "waga" }))
                table.insert(rows, b.ui.open("Wszystko", "lista", {}))
                table.insert(rows, b.ui.run("Zacznij", "start"))
                return b.ui.column(rows)
            end, {
                odhacz = function(args) b.replaceBlock(args.ref, args.text, "x") end,
                waga = function(args) b.log(`waga {args.value}`) end,
            })
        )lua"_s));
        QCOMPARE(engine->views().size(), 1);
        QCOMPARE(engine->views()[0].actions, (QStringList{u"odhacz"_s, u"waga"_s}));
        const Result r = engine->render(u"v"_s, u"lista"_s, {{u"page"_s, u"Lista"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QVariantMap root = r.value.toMap();
        QCOMPARE(root.value(u"type"_s).toString(), u"column"_s);
        const QVariantList children = root.value(u"children"_s).toList();
        QCOMPARE(children.size(), 5);
        const QVariantMap check = children[0].toMap().value(u"children"_s).toList()[0].toMap();
        QCOMPARE(check.value(u"type"_s).toString(), u"check"_s);
        QCOMPARE(check.value(u"action"_s).toString(), u"odhacz"_s);
        QCOMPARE(children[2].toMap().value(u"value"_s).toDouble(), 2.5);
        QCOMPARE(children[3].toMap().value(u"type"_s).toString(), u"open"_s);
        QCOMPARE(children[4].toMap().value(u"command"_s).toString(), u"start"_s);

        Result a = engine->act(u"v"_s, u"lista"_s, u"odhacz"_s, check.value(u"args"_s).toMap());
        QVERIFY2(a.ok(), qPrintable(a.error));
        QCOMPARE(host.lastReplace, u"jeden|x"_s);
        QVERIFY(engine->act(u"v"_s, u"lista"_s, u"waga"_s, {{u"value"_s, 5}}).ok());
        QCOMPARE(host.logs.last(), u"v: waga 5"_s);
        QCOMPARE(engine->act(u"v"_s, u"lista"_s, u"nie-ma"_s, {}).status, Status::NotFound);
        // a stale block is an error the action sees, nothing is written
        a = engine->act(u"v"_s, u"lista"_s, u"odhacz"_s, {{u"ref"_s, u"Lista.md#L1@stale"_s}, {u"text"_s, u"x"_s}});
        QCOMPARE(a.status, Status::Error);
        QVERIFY2(a.error.contains(u"b.replaceBlock: stale: Lista.md#L1@stale"_s), qPrintable(a.error));
    }

    void viewsDrawWhatIsThere()
    {
        // A list written with conditions in it — { a, show and b or nil, c } — is no sequence: as
        // it is, it would cross to the app as a record with the keys "1" and "3" and nothing of it
        // would be drawn. b.ui.column and b.ui.row take what is there; a table's nil cell shows
        // empty and its row keeps its columns.
        Extension views = ext(u"n"_s, uR"lua(
            b.view("v", function(params)
                local show = params.show == true
                return b.ui.column({
                    b.ui.heading("a"),
                    show and b.ui.text("sometimes") or nil,
                    b.ui.row({ nil, b.ui.text("b"), nil, b.ui.link("C"), nil }),
                    show and b.ui.text("sometimes too") or nil,
                    b.ui.table({ "x", "y", "z" }, { { "1", nil, "3" }, nil, { nil, nil, "c" }, { "a", "b", "c", "d" } }),
                    nil,
                })
            end)
            b.view("list", function() return { b.ui.heading("a"), b.ui.text("b") } end)
            b.view("nothing", function() end)
            b.view("words", function() return "just words" end)
            b.view("record", function() return { title = "no type" } end)
            b.view("gaps", function() return { b.ui.heading("a"), nil, b.ui.text("b") } end)
        )lua"_s);
        views.name = u"Widoki"_s;
        LOAD_OK(views);
        const auto types = [](const QVariant &list) {
            QStringList out;
            for (const QVariant &node : list.toList())
                out << node.toMap().value(u"type"_s).toString();
            return out;
        };
        Result r = engine->render(u"n"_s, u"v"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVariantList children = r.value.toMap().value(u"children"_s).toList();
        QCOMPARE(types(children), (QStringList{u"heading"_s, u"row"_s, u"table"_s}));
        QCOMPARE(types(children[1].toMap().value(u"children"_s)), (QStringList{u"text"_s, u"link"_s}));
        const QVariantList rows = children[2].toMap().value(u"rows"_s).toList();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toStringList(), (QStringList{u"1"_s, QString(), u"3"_s}));
        QCOMPARE(rows[1].toStringList(), (QStringList{QString(), QString(), u"c"_s}));
        QCOMPARE(rows[2].toStringList(), (QStringList{u"a"_s, u"b"_s, u"c"_s, u"d"_s}));
        r = engine->render(u"n"_s, u"v"_s, {{u"show"_s, true}});
        children = r.value.toMap().value(u"children"_s).toList();
        QCOMPARE(types(children), (QStringList{u"heading"_s, u"text"_s, u"row"_s, u"text"_s, u"table"_s}));

        // What a view returns is a node; a list of nodes is a column of them. Anything else would
        // be an empty view without a word: an error that says what came instead.
        r = engine->render(u"n"_s, u"list"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"type"_s).toString(), u"column"_s);
        QCOMPARE(types(r.value.toMap().value(u"children"_s)), (QStringList{u"heading"_s, u"text"_s}));
        const QList<std::pair<QString, QString>> wrong{{u"nothing"_s, u"returned nothing"_s},
                                                       {u"words"_s, u"returned a value that is no b.ui node"_s},
                                                       {u"record"_s, u"returned a table that is no b.ui node"_s},
                                                       {u"gaps"_s, u"returned a table that is no b.ui node"_s}};
        for (const auto &[view, why] : wrong) {
            r = engine->render(u"n"_s, view, {});
            QCOMPARE(r.status, Status::Error);
            QVERIFY2(r.error.startsWith(u"Widoki: the view \"%1\" %2"_s.arg(view, why)), qPrintable(r.error));
        }
    }

    void filtersThatWouldMeanEverything()
    {
        // b.pages("projekt") and a day that is none used to be read as "no filter": the script got
        // every page, the blocks of every day, and never knew. They are errors; a tag may be
        // written with its '#'.
        host.pagesByName = {{u"Dom"_s, u"- remont #projekt"_s}, {u"Inne"_s, u"- nic"_s}};
        LOAD_OK(ext(u"f"_s, QString()));
        const auto value = [&](const QString &code) {
            const Result r = engine->evaluate(u"f"_s, code, u"t"_s);
            return r.ok() ? r.value.toString() : r.error.section(u'\n', 0, 0);
        };
        QCOMPARE(value(u"#b.pages()"_s), u"2"_s);
        QCOMPARE(value(u"#b.pages({})"_s), u"2"_s);
        QCOMPARE(value(u"#b.pages({ tag = \"projekt\" })"_s), u"1"_s);
        QCOMPARE(value(u"#b.pages({ tag = \"#projekt\" })"_s), u"1"_s);
        QCOMPARE(value(u"b.pages(\"projekt\")"_s),
                 u"t:1: b.pages: argument 1 must be a table ({ tag = … }), not string"_s);
        QCOMPARE(value(u"b.pages({ tag = 5 })"_s), u"t:1: b.pages: tag must be a text"_s);
        QCOMPARE(value(u"#b.blocks({ page = \"Dom\", from = \"2026-11-01\", to = \"2026-11-30\" })"_s), u"1"_s);
        QCOMPARE(value(u"b.blocks({ tag = \"projekt\", from = \"2026-11-31\" })"_s),
                 u"t:1: b.blocks: invalid date: 2026-11-31"_s);
        QCOMPARE(value(u"b.blocks({ tag = \"projekt\", to = \"wczoraj\" })"_s),
                 u"t:1: b.blocks: invalid date: wczoraj"_s);
        QCOMPARE(value(u"b.blocks({ tag = \"projekt\", from = 20261101 })"_s),
                 u"t:1: b.blocks: invalid date: 20261101"_s);
        // A count that is no number (0/0) is the default, not whatever a cast makes of it.
        host.lang = u"en"_s;
        QCOMPARE(value(u"b.formatNumber(1/3, 0/0)"_s), u"0.33"_s);
        QCOMPARE(value(u"b.query(\"q\", 0/0).limit"_s), u"50"_s);
        QCOMPARE(value(u"b.query(\"q\", 1e99).limit"_s), u"1000"_s);
        QCOMPARE(value(u"#b.search(\"remont\", 0/0)"_s), u"1"_s);
    }

    void textHelpers_data()
    {
        QTest::addColumn<QString>("lang");
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("expected");
        QTest::newRow("comma") << u"pl"_s << u"b.formatNumber(82.5)"_s << u"82,5"_s;
        QTest::newRow("point") << u"en"_s << u"b.formatNumber(82.5)"_s << u"82.5"_s;
        QTest::newRow("whole") << u"pl"_s << u"b.formatNumber(100)"_s << u"100"_s;
        QTest::newRow("decimals") << u"pl"_s << u"b.formatNumber(116.6666, 1)"_s << u"116,7"_s;
        QTest::newRow("no -0") << u"pl"_s << u"b.formatNumber(-0.001)"_s << u"0"_s;
        QTest::newRow("nan") << u"pl"_s << u"b.formatNumber(0/0)"_s << u"—"_s;
        QTest::newRow("fold") << u"pl"_s << u"b.fold(\"  Wyciskanie  LEŻĄC \")"_s << u"wyciskanie lezac"_s;
        QTest::newRow("sorted") << u"pl"_s << u"table.concat(b.sorted({ \"żaba\", \"Łódź\", \"ala\", \"Rozdział 10\", \"Rozdział 2\" }), \",\")"_s
                                << u"ala,Łódź,Rozdział 2,Rozdział 10,żaba"_s;
        QTest::newRow("days") << u"pl"_s << u"b.daysBetween(\"2026-11-04\", b.today())"_s << u"7"_s;
        QTest::newRow("lang") << u"en"_s << u"b.lang() .. \"/\" .. b.uiLang()"_s << u"en/en"_s;
    }
    void textHelpers()
    {
        QFETCH(QString, lang);
        QFETCH(QString, expression);
        QFETCH(QString, expected);
        host.lang = lang;
        LOAD_OK(ext(u"t"_s, u"b.command(\"x\", function() return "_s + expression + u" end)"_s));
        const Result r = engine->runCommand(u"t"_s, u"x"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), expected);
    }

    void timerAndData()
    {
        Extension e = ext(u"d"_s, uR"lua(
            b.command("x", function()
                b.timer(90, "Przerwa")
                local ok = pcall(b.timer, 0, "zero")
                local catalog = b.data("katalog")
                return `{#catalog} {catalog[2].nazwa} {tostring(ok)} {tostring(b.data("brak"))}`
            end)
        )lua"_s);
        e.data.insert(u"katalog"_s, QVariantList{QVariantMap{{u"nazwa"_s, u"Przysiad"_s}}, QVariantMap{{u"nazwa"_s, u"Wiosłowanie"_s}}});
        LOAD_OK(e);
        const Result r = engine->runCommand(u"d"_s, u"x"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"2 Wiosłowanie false nil"_s);
        QCOMPARE(host.timers, (QList<QPair<int, QString>>{{90, u"Przerwa"_s}}));
    }

    void valuesCrossBothWays()
    {
        LOAD_OK(ext(u"w"_s, uR"lua(
            b.command("echo", function(ctx) return ctx end)
            b.command("cykl", function() local t = {} t.self = t return t end)
            b.command("funkcja", function() return function() end end)
        )lua"_s));
        const QVariantMap ctx{{u"page"_s, u"Strona"_s}, {u"n"_s, 3}, {u"x"_s, 2.5}, {u"list"_s, QVariantList{1, u"dwa"_s, true}},
                              {u"nested"_s, QVariantMap{{u"a"_s, QVariantList{}}}}};
        Result r = engine->runCommand(u"w"_s, u"echo"_s, ctx);
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QVariantMap back = r.value.toMap();
        QCOMPARE(back.value(u"page"_s).toString(), u"Strona"_s);
        QCOMPARE(back.value(u"n"_s).toLongLong(), 3);
        QCOMPARE(back.value(u"x"_s).toDouble(), 2.5);
        QCOMPARE(back.value(u"list"_s).toList().size(), 3);
        r = engine->runCommand(u"w"_s, u"cykl"_s, {});
        QCOMPARE(r.status, Status::Error);
        QVERIFY(r.error.contains(u"nested too deeply"_s));
        QCOMPARE(engine->runCommand(u"w"_s, u"funkcja"_s, {}).status, Status::Error);
    }

    void limits_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("status");
        QTest::newRow("loop") << u"while true do end"_s << int(Status::Timeout);
        QTest::newRow("loop in pcall, retried") << u"while true do pcall(function() while true do end end) end"_s << int(Status::Timeout);
        QTest::newRow("loop in xpcall handler") << u"xpcall(function() error('x') end, function() while true do end end)"_s
                                               << int(Status::Timeout);
        QTest::newRow("pattern backtracking")
            << u"return string.find(string.rep('a', 60), 'a-a-a-a-a-a-a-a-a-a-a-b')"_s << int(Status::Timeout);
        QTest::newRow("plain find (patched)")
            << u"local s = string.rep('a', 4000000) local p = string.rep('a', 20000) .. 'b' while true do string.find(s, p, 1, true) end"_s
            << int(Status::Timeout);
        QTest::newRow("sort comparator") << u"local t = {3, 2, 1} table.sort(t, function(a, b) while true do end end)"_s
                                        << int(Status::Timeout);
        QTest::newRow("coroutine loop") << u"local co = coroutine.wrap(function() while true do end end) co()"_s
                                       << int(Status::Timeout);
        QTest::newRow("memory") << u"local t, i = {}, 0 while true do i += 1 t[i] = string.rep('x', 1024) .. i end"_s
                               << int(Status::OutOfMemory);
        QTest::newRow("memory in pcall, retried")
            << u"local keep = {} while true do pcall(function() local t = {} for i = 1, math.huge do t[i] = string.rep('y', 1024) .. i end end) table.insert(keep, string.rep('z', 1000) .. #keep) end"_s
            << int(Status::OutOfMemory);
        QTest::newRow("huge result") << u"local s = 'x' while #s < 256 * 1024 * 1024 do s ..= s end return s"_s
                                    << int(Status::OutOfMemory);
        QTest::newRow("recursion") << u"local function f(n) return 1 + f(n + 1) end return f(1)"_s << int(Status::StackOverflow);
    }
    void limits()
    {
        QFETCH(QString, body);
        QFETCH(int, status);
        // A memory limit is reached by allocating, which takes longer than the time limit under the
        // sanitizers (BRULION_TEST_SLOWDOWN): those rows get more time.
        if (status == int(Status::OutOfMemory))
            engine->setLimits(Limits{300 * BRULION_TEST_SLOWDOWN, 32ll << 20});
        LOAD_OK(ext(u"zły"_s, u"b.command(\"x\", function()\n"_s + body + u"\nend)\nb.command(\"ok\", function() return 7 end)"_s));
        QElapsedTimer clock;
        clock.start();
        const Result r = engine->runCommand(u"zły"_s, u"x"_s, {});
        QCOMPARE(int(r.status), status);
        QVERIFY2(clock.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(clock.elapsed())));
        // the VM stays usable
        QCOMPARE(engine->runCommand(u"zły"_s, u"ok"_s, {}).value.toLongLong(), 7);
        QVERIFY(engine->memoryUsed() < (32ll << 20));
    }

    void escapes()
    {
        LOAD_OK(ext(u"ucieczki"_s, uR"lua(
            b.command("x", function()
              local tried, escaped = 0, {}
              local function try(name, f)
                tried += 1
                local ok, result = pcall(f)
                if ok and result then table.insert(escaped, name) end
              end
              local env = getfenv(1)
              try("io.open", function() return io.open("/etc/passwd") end)
              try("os.execute", function() return os.execute("true") end)
              try("os.getenv", function() return os.getenv("HOME") end)
              try("require", function() return require("os") end)
              try("load", function() return load("return 1") end)
              try("loadstring", function() return loadstring("return 1") end)
              try("dofile", function() return dofile("/etc/passwd") end)
              try("string.dump", function() return string.dump(function() end) end)
              try("debug.getregistry", function() return debug.getregistry() end)
              try("debug.getupvalue", function() return debug.getupvalue(b.pages, 1) end)
              try("getfenv(0)", function() return getfenv(0) ~= env end)
              try("getfenv of a C function", function() return getfenv(b.pages) ~= env end)
              try("getmetatable(_G).__index", function() local mt = getmetatable(_G) return type(mt) == "table" and mt.__index ~= nil end)
              try("_G", function() return _G ~= env end)
              try("patch string.upper", function() string.upper = function() return "x" end return true end)
              try("rawset string", function() return rawset(string, "zz", 1) end)
              try("string metatable", function() local mt = getmetatable("") if type(mt) ~= "table" then return false end mt.__index = {} return true end)
              try("modify b", function() b.write = function() return true end return true end)
              try("modify b.ui", function() b.ui.text = function() end return true end)
              try("collectgarbage stop", function() return collectgarbage("stop") or true end)
              try("buffer out of bounds", function() return buffer.readu32(buffer.create(1), 1000) end)
              try("print to stdout", function() print("x") return false end)
              return { tried = tried, escaped = escaped }
            end)
        )lua"_s));
        const Result r = engine->runCommand(u"ucieczki"_s, u"x"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"tried"_s).toInt(), 22);
        QCOMPARE(r.value.toMap().value(u"escaped"_s).toList(), QVariantList{});
        QVERIFY(host.logs.contains(u"ucieczki: x"_s)); // print goes to the log
    }

    void extensionsAreIsolated()
    {
        LOAD_OK(ext(u"a"_s, uR"lua(
            sekret = "a"
            pcall(function() string.upper = function() return "zmienione" end end)
        )lua"_s));
        LOAD_OK(ext(u"b"_s, uR"lua(
            b.command("x", function() return `{tostring(sekret)} {string.upper("ą-a")}` end)
        )lua"_s));
        QCOMPARE(engine->runCommand(u"b"_s, u"x"_s, {}).value.toString(), u"nil ą-A"_s);
    }

    void numbersWhateverTheLocale()
    {
        // Qt sets the C locale from the environment; with a decimal comma Luau's strtod read
        // "82.5" as 82 (patched: 3rdparty/luau, Luau/StrtodC.h).
        const QByteArray saved = setlocale(LC_NUMERIC, nullptr);
        bool comma = false;
        for (const char *name : {"pl_PL.UTF-8", "pl_PL.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"}) {
            if (setlocale(LC_NUMERIC, name) && localeconv()->decimal_point[0] == ',') {
                comma = true;
                break;
            }
        }
        LOAD_OK(ext(u"liczby"_s, uR"lua(
            b.command("x", function()
                return `{82.5 * 2} {tonumber("42.25")} {tonumber("1e3")} {string.format("%.1f", 2.25)} {tostring(0.1 + 0.2 > 0.3)}`
            end)
        )lua"_s));
        const Result r = engine->runCommand(u"liczby"_s, u"x"_s, {});
        setlocale(LC_NUMERIC, saved.constData());
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"165 42.25 1000 2.2 true"_s);
        if (!comma)
            QSKIP("no locale with a decimal comma installed: checked with the current one only");
    }

    void hooks()
    {
        LOAD_OK(ext(u"dziennik"_s, uR"lua(
            local zapisane = {}
            b.on("save", function(e) table.insert(zapisane, e.page) return #zapisane end)
            b.on("change", function(e) return table.concat(e.pages, ", ") end)
            b.command("ile", function() return #zapisane end)
        )lua"_s));
        LOAD_OK(ext(u"drugie"_s, u"b.on(\"save\", function(e) error(\"zepsute: \" .. e.page) end)"_s));
        const QList<HookInfo> save = engine->hooks(u"save"_s);
        QCOMPARE(save.size(), 2);
        QCOMPARE(save[0].extension, u"dziennik"_s);
        QCOMPARE(save[1].extension, u"drugie"_s);
        QVERIFY(save[0].id < save[1].id);
        QCOMPARE(engine->hooks().size(), 3);
        Result r = engine->runHook(save[0].id, {{u"page"_s, u"Żółw"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toInt(), 1);
        r = engine->runHook(engine->hooks(u"change"_s).first().id, {{u"pages"_s, QStringList{u"A"_s, u"Bęben"_s}}});
        QCOMPARE(r.value.toString(), u"A, Bęben"_s);
        r = engine->runHook(save[1].id, {{u"page"_s, u"X"_s}});
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.startsWith(u"drugie:1: zepsute: X"_s), qPrintable(r.error));
        // Removed, and gone with its extension.
        engine->removeHook(save[1].id);
        QCOMPARE(engine->runHook(save[1].id, {}).status, Status::NotFound);
        LOAD_OK(ext(u"dziennik"_s, u"-- nowa wersja bez hooków"_s));
        QVERIFY(engine->hooks().isEmpty());
        // Unknown events and non-functions are refused at once, with the line.
        Result bad = engine->load(ext(u"zle"_s, u"\nb.on(\"zamknięcie\", function() end)"_s));
        QCOMPARE(bad.status, Status::Error);
        QVERIFY2(bad.error.startsWith(u"zle:2: b.on: unknown event \"zamknięcie\" (save, change)"_s), qPrintable(bad.error));
        bad = engine->load(ext(u"zle"_s, u"b.on(\"save\", 42)"_s));
        QVERIFY2(bad.error.contains(u"b.on: argument 2 must be a function"_s), qPrintable(bad.error));
    }

    void evaluate()
    {
        LOAD_OK(ext(u"*console*"_s, QString()));
        Result r = engine->evaluate(u"*console*"_s, u"1 + 1"_s, u"console"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toInt(), 2);
        // Statements; globals stay between evaluations (a REPL), print goes to the log.
        r = engine->evaluate(u"*console*"_s, u"licznik = 41\nlicznik += 1"_s, u"console"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(!r.value.isValid());
        QCOMPARE(engine->evaluate(u"*console*"_s, u"licznik"_s, u"console"_s).value.toInt(), 42);
        QVERIFY(engine->evaluate(u"*console*"_s, u"print(\"zażółć\", licznik)"_s, u"console"_s).ok());
        QCOMPARE(host.logs.last(), u"*console*: zażółć\t42"_s);
        QCOMPARE(engine->evaluate(u"*console*"_s, u"b.today()"_s, u"console"_s).value.toString(), u"2026-11-11"_s);
        // Errors and syntax errors name the code's lines.
        r = engine->evaluate(u"*console*"_s, u"\n\nerror(\"nie\")"_s, u"Strona"_s);
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.startsWith(u"Strona:3: nie"_s), qPrintable(r.error));
        r = engine->evaluate(u"*console*"_s, u"local = = 2"_s, u"console"_s);
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.startsWith(u"console:1:"_s), qPrintable(r.error));
        // The limits hold here too; another extension's environment is not reachable.
        QCOMPARE(engine->evaluate(u"*console*"_s, u"while true do end"_s, u"console"_s).status, Status::Timeout);
        QCOMPARE(engine->evaluate(u"nie-ma"_s, u"1"_s, u"x"_s).status, Status::NotFound);
        LOAD_OK(ext(u"inne"_s, u"sekret = 7"_s));
        QVERIFY(!engine->evaluate(u"*console*"_s, u"sekret"_s, u"console"_s).value.isValid());
        QCOMPARE(engine->evaluate(u"inne"_s, u"sekret"_s, u"x"_s).value.toInt(), 7);
        // A question waits like a command's.
        r = engine->evaluate(u"*console*"_s, u"b.ask(\"Imię?\")"_s, u"console"_s);
        QCOMPARE(r.status, Status::Suspended);
        r = engine->resume(u"Ala"_s);
        QCOMPARE(r.value.toString(), u"Ala"_s);
    }

    void aiWaitsForTheHost()
    {
        // b.ai needs the permission "ai"; it suspends the call with the request, the host resumes
        // it with {text} (b.ai returns the text) or {error} (an error at the script's line).
        Extension e = ext(u"asystent"_s, uR"lua(
            b.command("zapytaj", function(ctx)
                local text, reply = b.ai({ prompt = "Streść: " .. ctx.page, system = "Krótko.", maxTokens = 300 })
                return text .. " (" .. reply.model .. ")"
            end)
            b.command("krotko", function() return b.ai("Ile to 2 + 2?") end)
            b.command("blad", function()
                return b.ai("x")
            end)
        )lua"_s);
        e.permissions = {u"ai"_s};
        LOAD_OK(e);
        Result r = engine->runCommand(u"asystent"_s, u"zapytaj"_s, {{u"page"_s, u"Żółw"_s}});
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY(r.call > 0);
        const QVariantMap question = r.value.toMap();
        QCOMPARE(question.value(u"kind"_s).toString(), u"ai"_s);
        const QVariantMap request = question.value(u"request"_s).toMap();
        QCOMPARE(request.value(u"prompt"_s).toString(), u"Streść: Żółw"_s);
        QCOMPARE(request.value(u"system"_s).toString(), u"Krótko."_s);
        QCOMPARE(request.value(u"maxTokens"_s).toInt(), 300);
        // Another call waits too, and a third one runs meanwhile: calls no longer abandon each other.
        const Result second = engine->runCommand(u"asystent"_s, u"krotko"_s, {});
        QCOMPARE(second.status, Status::Suspended);
        QVERIFY(second.call != r.call);
        QCOMPARE(second.value.toMap().value(u"request"_s).toMap().value(u"prompt"_s).toString(), u"Ile to 2 + 2?"_s);
        QCOMPARE(engine->waitingCalls(), (QList<int>{r.call, second.call}));
        LOAD_OK(ext(u"inne"_s, u"b.command(\"x\", function() return 7 end)"_s));
        QCOMPARE(engine->runCommand(u"inne"_s, u"x"_s, {}).value.toInt(), 7);
        // Answered in any order.
        Result done = engine->resume(second.call, QVariantMap{{u"text"_s, u"4"_s}, {u"model"_s, u"m"_s}});
        QVERIFY2(done.ok(), qPrintable(done.error));
        QCOMPARE(done.value.toString(), u"4"_s);
        QCOMPARE(done.call, second.call);
        done = engine->resume(r.call, QVariantMap{{u"text"_s, u"Gad."_s}, {u"model"_s, u"claude-opus-5-5"_s}});
        QCOMPARE(done.value.toString(), u"Gad. (claude-opus-5-5)"_s);
        QVERIFY(engine->waitingCalls().isEmpty());
        QCOMPARE(engine->resume(r.call, QVariant()).status, Status::Error); // nothing waits under that id
        // An error from the host: raised at the script's line.
        r = engine->runCommand(u"asystent"_s, u"blad"_s, {});
        QCOMPARE(r.status, Status::Suspended);
        done = engine->resume(r.call, QVariantMap{{u"error"_s, u"invalid API key"_s}});
        QCOMPARE(done.status, Status::Error);
        QVERIFY2(done.error.startsWith(u"asystent:8: b.ai: invalid API key"_s), qPrintable(done.error));
        // Loading the extension again drops its waiting calls.
        r = engine->runCommand(u"asystent"_s, u"krotko"_s, {});
        QCOMPARE(r.status, Status::Suspended);
        LOAD_OK(e);
        QVERIFY(engine->waitingCalls().isEmpty());
        // Without the permission: refused at once.
        LOAD_OK(ext(u"bez"_s, u"b.command(\"x\", function() return b.ai(\"hej\") end)"_s));
        const Result denied = engine->runCommand(u"bez"_s, u"x"_s, {});
        QCOMPARE(denied.status, Status::Error);
        QVERIFY2(denied.error.contains(u"b.ai: permission denied: ai"_s), qPrintable(denied.error));
        // Not from a coroutine of the script; not with a function as the request.
        LOAD_OK(e);
        QVERIFY(engine->evaluate(u"asystent"_s, u"coroutine.wrap(function() return b.ai(\"x\") end)()"_s, u"t"_s)
                        .error.contains(u"cannot wait for an answer here"_s));
        QVERIFY(engine->evaluate(u"asystent"_s, u"b.ai(print)"_s, u"t"_s).error.contains(u"argument 1 must be"_s));
    }

    void searchAndQuery()
    {
        // b.search and b.query: the host's full-text search and its query language, no permission
        // needed (reading, as b.read).
        host.pagesByName.insert(u"Dom"_s, u"- [ ] Zapłacić czynsz 📅 2026-11-13\n- Żółw w ogrodzie"_s);
        LOAD_OK(ext(u"szukaj"_s, uR"lua(
            b.command("s", function()
                local hits = b.search("Żółw", 5)
                return #hits .. " " .. hits[1].page .. ":" .. hits[1].line .. " " .. hits[1].ref
            end)
            b.command("q", function()
                local r = b.query("type:tasks status:todo")
                return r.total .. " " .. r.items[1].text .. " " .. r.items[1].due .. " " .. r.limit
            end)
            b.command("zla", function() return b.query("zła") end)
        )lua"_s,
                    false));
        QCOMPARE(engine->runCommand(u"szukaj"_s, u"s"_s, {}).value.toString(), u"1 Dom:2 Dom.md#L2@0"_s);
        QCOMPARE(host.searched, QStringList{u"Żółw"_s});
        QCOMPARE(engine->runCommand(u"szukaj"_s, u"q"_s, {}).value.toString(), u"1 Zapłacić czynsz 2026-11-13 50"_s);
        QCOMPARE(host.queried, QStringList{u"type:tasks status:todo"_s});
        const Result bad = engine->runCommand(u"szukaj"_s, u"zla"_s, {});
        QCOMPARE(bad.status, Status::Error);
        QVERIFY2(bad.error.contains(u"b.query: unknown filter"_s), qPrintable(bad.error));
        QVERIFY(engine->evaluate(u"szukaj"_s, u"b.search(5)"_s, u"t"_s)
                        .error.contains(u"argument 1 must be a string"_s));
        QVERIFY(engine->evaluate(u"szukaj"_s, u"b.query(\"x\", \"dużo\")"_s, u"t"_s)
                        .error.contains(u"argument 2 must be a number"_s));
    }

    void aiWithTools()
    {
        // An agent's loop in a script: b.ai offers tools; a reply's toolCalls are run and their
        // results sent with the turn back as it came (raw), until an answer without calls.
        Extension e = ext(u"agent"_s, uR"lua(
            local tools = {{ name = "search_notes", description = "Szuka.",
                             input = { type = "object", properties = { query = { type = "string" } }, required = { "query" } } }}
            b.command("ask", function()
                local messages = {{ role = "user", content = "Co o żółwiach?" }}
                for round = 1, 4 do
                    local text, reply = b.ai({ messages = messages, tools = tools })
                    table.insert(messages, { role = "assistant", raw = reply.raw })
                    if not reply.toolCalls then return text end
                    local results = {}
                    for _, call in reply.toolCalls do
                        local hits = b.search(call.input.query)
                        table.insert(results, { id = call.id, content = #hits .. " trafień" })
                    end
                    table.insert(messages, { role = "user", results = results })
                end
                return "za długo"
            end)
        )lua"_s,
                          false);
        e.permissions = {u"ai"_s};
        LOAD_OK(e);
        host.pagesByName.insert(u"Ogród"_s, u"- Żółw je sałatę"_s);
        Result r = engine->runCommand(u"agent"_s, u"ask"_s, {});
        QCOMPARE(r.status, Status::Suspended);
        QVariantMap request = r.value.toMap().value(u"request"_s).toMap();
        const QVariantList tools = request.value(u"tools"_s).toList();
        QCOMPARE(tools.size(), 1);
        QCOMPARE(tools[0].toMap().value(u"input"_s).toMap().value(u"required"_s).toList(), QVariantList{u"query"_s});
        // The model asks for a search.
        r = engine->resume(
                r.call, QVariantMap{{u"text"_s, QString()},
                                    {u"raw"_s, u"[1]"_s},
                                    {u"toolCalls"_s,
                                     QVariantList{QVariantMap{{u"id"_s, u"t1"_s},
                                                              {u"name"_s, u"search_notes"_s},
                                                              {u"input"_s, QVariantMap{{u"query"_s, u"Żółw"_s}}}}}}});
        QCOMPARE(r.status, Status::Suspended);
        request = r.value.toMap().value(u"request"_s).toMap();
        const QVariantList messages = request.value(u"messages"_s).toList();
        QCOMPARE(messages.size(), 3);
        QCOMPARE(messages[1].toMap().value(u"raw"_s).toString(), u"[1]"_s);
        const QVariantList results = messages[2].toMap().value(u"results"_s).toList();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].toMap().value(u"id"_s).toString(), u"t1"_s);
        QCOMPARE(results[0].toMap().value(u"content"_s).toString(), u"1 trafień"_s);
        // The answer ends the loop.
        r = engine->resume(r.call, QVariantMap{{u"text"_s, u"Żółw je sałatę."_s}, {u"raw"_s, u"[2]"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Żółw je sałatę."_s);
    }

    void viewMetaAndNames()
    {
        Extension e = ext(u"licznik-slow"_s, uR"lua(
            b.view("projekty", function() return b.ui.text("x") end, nil, { label = "Projekty", icon = "folder" })
            b.view("bez", function() return b.ui.text("y") end)
            b.command("blad", function() local t = nil; return t.pole end)
        )lua"_s);
        e.name = u"Licznik słów"_s;
        LOAD_OK(e);
        const QList<ViewInfo> views = engine->views();
        QCOMPARE(views.size(), 2);
        QCOMPARE(views[0].name, u"bez"_s);
        QVERIFY(views[0].label.isEmpty());
        QCOMPARE(views[1].label, u"Projekty"_s);
        QCOMPARE(views[1].icon, u"folder"_s);
        // Errors carry the extension's name (a page's title), not its id.
        const Result r = engine->runCommand(u"licznik-slow"_s, u"blad"_s, {});
        QVERIFY2(r.error.startsWith(u"Licznik słów:4: attempt to index nil"_s), qPrintable(r.error));
        QVERIFY(engine->isLoaded(u"licznik-slow"_s));
        const Result bad = engine->load(ext(u"z"_s, u"b.view(\"v\", function() end, nil, 5)"_s));
        QVERIFY2(bad.error.contains(u"b.view: argument 4 must be a table"_s), qPrintable(bad.error));
    }

    void restartAndStop()
    {
        LOAD_OK(ext(u"x"_s, u"b.command(\"y\", function() return 1 end)"_s));
        engine->stop();
        QVERIFY(!engine->isStarted());
        QCOMPARE(engine->runCommand(u"x"_s, u"y"_s, {}).status, Status::Error);
        QVERIFY(engine->start(&host, Limits{}));
        QVERIFY(engine->commands().isEmpty());
    }

private:
    FakeHost host;
    std::unique_ptr<Engine> engine;
};

QTEST_GUILESS_MAIN(TestExtensionsEngine)
#include "tst_extensions_engine.moc"
