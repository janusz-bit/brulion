// Macros and template variables of extensions (docs/EXTENSIONS.md "Macros", docs/TEMPLATES.md
// "Variables of extensions") on a real notebook: b.macro / b.variable and their meta, what a call
// reads (Engine's recording), the bounds of a call (time, no writes, no questions), what a value
// shows (macroViewFrom: escaped pieces, bounded), the cache that computes a macro again only when
// what it read changed, hostile extensions; the new nodes of b.ui and views as side panels.

#include "extensions/engine.h"
#include "extensions/macros.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;
using markdown::MacroPiece;
using markdown::MacroView;

namespace {

// The extension the tests load (defined at the end: moc reads the class first).
extern const char kSource[];

} // namespace

class TestExtensionsMacros : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;

    QString dir() const { return m_tmp.filePath(u"v"_s); }
    void writeNote(const QString &path, const QString &text)
    {
        const QString file = dir() + u'/' + path;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text.toUtf8());
        f.close();
        m_index->update(path, text);
    }
    QVariantMap ctx(const QString &arguments, const QString &path = u"journals/2026-11-20.md"_s)
    {
        QVariantList args;
        for (const QString &a : markdown::macroArguments(arguments))
            args << a;
        return {{u"args"_s, args},
                {u"text"_s, arguments},
                {u"page"_s, QFileInfo(path).completeBaseName()},
                {u"path"_s, path},
                {u"date"_s, u"2026-11-20"_s}};
    }
    MacroView macro(const QString &name, const QString &arguments = QString(), Reads *reads = nullptr)
    {
        m_engine->startRecording();
        const Result r = m_engine->runMacro(u"g"_s, name, ctx(arguments), MacroTimeoutMs * BRULION_TEST_SLOWDOWN);
        const Reads recorded = m_engine->stopRecording();
        if (reads)
            *reads = recorded;
        if (!r.ok()) {
            MacroView v;
            v.state = MacroView::State::Error;
            v.message = r.error;
            return v;
        }
        return macroViewFrom(r.value, true);
    }

private Q_SLOTS:
    void init()
    {
        QDir(dir()).removeRecursively();
        m_vault = std::make_unique<Vault>(dir(), m_tmp.filePath(u"s"_s));
        QVERIFY(m_vault->open());
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        writeNote(u"journals/2026-11-18.md"_s, u"- Przysiad: 100 kg × 5\n- Martwy ciąg: 140 kg × 3\n"_s);
        writeNote(u"journals/2026-11-20.md"_s, u"- Przysiad: 110 kg × 3\n- Rekord: {{ext:g/rekord Przysiad}}\n"_s);
        writeNote(u"Dom.md"_s, u"Pierwsza linia domu\nDruga\n"_s);
        writeNote(u"Ukradzione.md"_s, u"oryginał\n"_s);
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); };
        options.updateIndex = true;
        options.uiLanguage = u"pl"_s;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        QVERIFY(m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}));
        Extension e;
        e.id = u"g"_s;
        e.name = u"Siłka"_s;
        e.source = QString::fromUtf8(kSource);
        e.permissions = {u"write"_s};
        const Result loaded = m_engine->load(e);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    }

    void registered()
    {
        const QList<MacroInfo> macros = m_engine->macros();
        QStringList names;
        for (const MacroInfo &m : macros)
            names << m.name;
        QVERIFY(names.contains(u"rekord"_s));
        const auto rekord = std::find_if(macros.cbegin(), macros.cend(), [](const MacroInfo &m) { return m.name == u"rekord"; });
        QCOMPARE(rekord->extension, u"g"_s);
        QCOMPARE(rekord->description, u"Najlepsza seria ćwiczenia"_s);
        QCOMPARE(rekord->arguments, QStringList{u"ćwiczenie"_s});
        QCOMPARE(rekord->example, u"Przysiad"_s);
        const auto first = std::find_if(macros.cbegin(), macros.cend(), [](const MacroInfo &m) { return m.name == u"pierwsza"; });
        QCOMPARE(first->arguments, QStringList{u"page"_s}); // a single text is one argument
        QCOMPARE(m_engine->variables().size(), 2);
        QCOMPARE(m_engine->variables().constFirst().name, u"plan"_s);
        QCOMPARE(m_engine->variables().constFirst().description, u"Today's plan"_s);
        // Views offered as panels.
        for (const ViewInfo &v : m_engine->views()) {
            if (v.name == u"dzis")
                QCOMPARE(v.panel, u"journal"_s);
            else if (v.name == u"boczny")
                QCOMPARE(v.panel, u"all"_s);
        }
        // Loading the extension again drops what it registered; unloading it too.
        m_engine->unload(u"g"_s);
        QVERIFY(m_engine->macros().isEmpty());
        QVERIFY(m_engine->variables().isEmpty());
    }

    void registrationErrors_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("error");
        QTest::newRow("no name") << u"b.macro(nil, function() end)"_s << u"b.macro: argument 1 must be a string"_s;
        QTest::newRow("bad name") << u"b.macro(\"a b\", function() end)"_s << u"is not one"_s;
        QTest::newRow("slash in the name") << u"b.variable(\"a/b\", function() end)"_s << u"b.variable: the name"_s;
        QTest::newRow("no function") << u"b.macro(\"x\", 3)"_s << u"argument 2 must be a function"_s;
        QTest::newRow("bad meta") << u"b.macro(\"x\", function() end, \"opis\")"_s << u"argument 3 must be a table"_s;
        QTest::newRow("bad panel") << u"b.view(\"v\", function() end, nil, { panel = \"left\" })"_s
                                   << u"panel is true, \"page\" or \"journal\""_s;
        QTest::newRow("spec of input") << u"return b.ui.input(\"x\")"_s << u"b.ui.input: argument 1 must be a table"_s;
        QTest::newRow("too many") << u"for i = 1, 300 do b.macro(\"m\" .. i, function() end) end"_s << u"at most 256"_s;
    }

    void registrationErrors()
    {
        QFETCH(QString, code);
        QFETCH(QString, error);
        const Result r = m_engine->evaluate(u"g"_s, code, u"test"_s);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(error), qPrintable(r.error));
    }

    void values()
    {
        // The record: the heaviest squat of the journal, a heading (bold), a link.
        MacroView v = macro(u"rekord"_s, u"Przysiad"_s);
        QCOMPARE(v.state, MacroView::State::Ok);
        QCOMPARE(markdown::macroViewText(v), u"110 kg × 3 [[Przysiad]]"_s);
        QVERIFY(v.pieces[0].bold);
        QCOMPARE(v.pieces[2].kind, MacroPiece::Kind::Link);
        QCOMPARE(markdown::macroViewText(macro(u"rekord"_s, u"Wiosłowanie"_s)), u"brak"_s);
        QCOMPARE(macro(u"rekord"_s, u"Wiosłowanie"_s).pieces[0].tone, u"muted"_s);
        // The context: the note it is in, its day, the arguments split at commas.
        QCOMPARE(markdown::macroViewText(macro(u"kontekst"_s, u"a, \"b, c\""_s)),
                 u"2026-11-20|journals/2026-11-20.md|2026-11-20|2|a, \"b, c\""_s);
        // A number as the notebook writes it; nothing; line breaks and control characters out.
        QCOMPARE(markdown::macroViewText(macro(u"liczba"_s)), u"82,5"_s);
        QVERIFY(macro(u"nic"_s).pieces.isEmpty());
        QCOMPARE(markdown::macroViewText(macro(u"html"_s)), u"<b>gruby</b> nowa linia"_s);
    }

    void wrongValues_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("error");
        QTest::newRow("a record") << u"tabela"_s << u"a table that is no b.ui node"_s;
        QTest::newRow("a button") << u"przycisk"_s << u"b.ui.button"_s;
        QTest::newRow("a write") << u"pisze"_s << u"b.write: a macro or a template variable cannot change notes"_s;
        QTest::newRow("a question") << u"pyta"_s << u"cannot wait for a question or for AI"_s;
        QTest::newRow("an error") << u"blad"_s << u"nie tym razem"_s;
        QTest::newRow("unknown") << u"nie-ma"_s << u"no macro g/nie-ma"_s;
    }

    void wrongValues()
    {
        QFETCH(QString, name);
        QFETCH(QString, error);
        const MacroView v = macro(name);
        QCOMPARE(v.state, MacroView::State::Error);
        QVERIFY2(v.message.contains(error), qPrintable(v.message));
        // A refused write wrote nothing; the engine runs on.
        QFile f(dir() + u"/Ukradzione.md"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("oryginał\n"));
        QCOMPARE(markdown::macroViewText(macro(u"liczba"_s)), u"82,5"_s);
        // A command of the same extension may still write afterwards (the flag ends with the call).
        QVERIFY(m_engine->evaluate(u"g"_s, u"b.write(\"Inna\", \"x\")"_s, u"test"_s).ok());
    }

    void bounds()
    {
        // Time: a loop ends at the macro's own limit, far below a command's.
        QElapsedTimer clock;
        clock.start();
        m_engine->startRecording();
        const Result r = m_engine->runMacro(u"g"_s, u"petla"_s, ctx({}), 100);
        m_engine->stopRecording();
        QCOMPARE(r.status, Status::Timeout);
        QVERIFY2(clock.elapsed() < 1900 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(clock.elapsed())));
        // The engine's own limit stays the commands'.
        QVERIFY(m_engine->evaluate(u"g"_s, u"local n = 0 for i = 1, 1e6 do n += i end return n"_s, u"t"_s).ok());
        // Output: at most 400 characters, 32 pieces.
        const MacroView longText = macro(u"duzo"_s);
        QCOMPARE(longText.pieces.size(), 1);
        QCOMPARE(longText.pieces[0].text.size(), MaxMacroCharacters);
        QVERIFY(longText.pieces[0].text.endsWith(u'…'));
        const MacroView many = macro(u"kawalki"_s);
        QCOMPARE(many.pieces.size(), MaxMacroPieces);
        QCOMPARE(many.pieces.last().text, u"…"_s);
        QCOMPARE(many.pieces[0].tone, u"success"_s);
    }

    void reads()
    {
        // A page read by name: its file; a page that is not there: its folded name; a query: anything.
        Reads reads;
        macro(u"pierwsza"_s, u"Dom"_s, &reads);
        QCOMPARE(reads.paths, QSet<QString>{u"Dom.md"_s});
        QVERIFY(!reads.everything);
        macro(u"pierwsza"_s, u"Nowa Strona"_s, &reads);
        QVERIFY(reads.paths.isEmpty());
        QCOMPARE(reads.names, QSet<QString>{u"nowa strona"_s});
        macro(u"rekord"_s, u"Przysiad"_s, &reads);
        QVERIFY(reads.everything);
        // Nothing is recorded outside a recording.
        QVERIFY(m_engine->stopRecording().paths.isEmpty());
    }

    void cache()
    {
        MacroCache cache;
        const MacroRequest home{u"g"_s, u"pierwsza"_s, u"Dom"_s, u"journals/2026-11-20.md"_s};
        const MacroRequest fresh{u"g"_s, u"pierwsza"_s, u"Nowa"_s, u"journals/2026-11-20.md"_s};
        const MacroRequest record{u"g"_s, u"rekord"_s, u"Przysiad"_s, u"Inna.md"_s};
        const QString homeKey = MacroCache::keyOf(home), freshKey = MacroCache::keyOf(fresh),
                      recordKey = MacroCache::keyOf(record);
        QVERIFY(cache.needsRun(homeKey));
        QVERIFY(!cache.view(homeKey));
        QVERIFY(cache.ask(homeKey, home, 10));
        QVERIFY(cache.ask(freshKey, fresh, 11));
        QVERIFY(cache.ask(recordKey, record, 12));
        QCOMPARE(cache.pending(0), (QStringList{recordKey, freshKey, homeKey})); // the latest asked first
        const auto runAndStore = [&](const MacroRequest &req, const QString &key) {
            Reads reads;
            const MacroView v = macro(req.name, req.arguments, &reads);
            return cache.store(key, v, reads, false, 20);
        };
        QVERIFY(runAndStore(home, homeKey)); // a first value: changed
        QVERIFY(runAndStore(fresh, freshKey));
        QVERIFY(runAndStore(record, recordKey));
        QVERIFY(!cache.needsRun(homeKey));
        QCOMPARE(markdown::macroViewText(*cache.view(homeKey)), u"Pierwsza linia domu"_s);
        QVERIFY(cache.pending(0).isEmpty());
        // Another note changed: only the macro that reads everything goes stale.
        QCOMPARE(cache.notesChanged({u"Ogród.md"_s}), QStringList{recordKey});
        // Dom.md changed: the macro reading it.
        QCOMPARE(cache.notesChanged({u"Dom.md"_s}), QStringList{homeKey});
        QVERIFY(cache.needsRun(homeKey));
        // While stale it still shows its old value.
        QCOMPARE(markdown::macroViewText(*cache.view(homeKey)), u"Pierwsza linia domu"_s);
        writeNote(u"Dom.md"_s, u"Nowa pierwsza\n"_s);
        QVERIFY(runAndStore(home, homeKey));
        QCOMPARE(markdown::macroViewText(*cache.view(homeKey)), u"Nowa pierwsza"_s);
        QVERIFY(!runAndStore(home, homeKey)); // the same again: no change to show
        // A page of the name the macro looked for is made: it goes stale.
        QCOMPARE(cache.notesChanged({u"Projekty/Nowa.md"_s}), QStringList{freshKey});
        // The extension loads again: all of its macros.
        QCOMPARE(cache.extensionChanged(u"g"_s).size(), 3);
        QVERIFY(cache.extensionChanged(u"inne"_s).isEmpty());
        // Timeouts: given up on after two in a row, until the extension changes.
        cache.countTimeout(homeKey);
        cache.countTimeout(homeKey);
        QVERIFY(!cache.needsRun(homeKey));
        QVERIFY(!cache.pending(0).contains(homeKey));
        cache.extensionChanged(u"g"_s);
        QVERIFY(cache.needsRun(homeKey));
        // The keys of a note; old entries go.
        QCOMPARE(cache.keysOf(u"journals/2026-11-20.md"_s).size(), 2);
        cache.ask(homeKey, home, 1000);
        cache.prune(500);
        QCOMPARE(cache.size(), 1);
        QVERIFY(cache.contains(homeKey));
    }

    void perPageBound()
    {
        MacroCache cache;
        for (int i = 0; i < MaxMacrosPerPage; ++i) {
            const MacroRequest r{u"g"_s, u"liczba"_s, QString::number(i), u"Długa.md"_s};
            QVERIFY(cache.ask(MacroCache::keyOf(r), r, i));
        }
        const MacroRequest over{u"g"_s, u"liczba"_s, u"jeszcze"_s, u"Długa.md"_s};
        QVERIFY(!cache.ask(MacroCache::keyOf(over), over, 1));
        const MacroRequest elsewhere{u"g"_s, u"liczba"_s, u"jeszcze"_s, u"Inna.md"_s};
        QVERIFY(cache.ask(MacroCache::keyOf(elsewhere), elsewhere, 1));
        cache.prune(0, 50);
        QCOMPARE(cache.size(), 50);
    }

    void variables()
    {
        const Result r = m_engine->runVariable(u"g"_s, u"plan"_s, ctx(u"Wiosłowanie"_s), MacroTimeoutMs * BRULION_TEST_SLOWDOWN);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(variableTextFrom(r.value, true), std::optional<QString>(u"## Plan na 2026-11-20\n- Przysiad 3 × 5\n- Wiosłowanie"_s));
        const Result table = m_engine->runVariable(u"g"_s, u"tabela"_s, ctx({}), 250);
        QVERIFY(table.ok());
        QVERIFY(!variableTextFrom(table.value, true));
        QCOMPARE(variableTextFrom(QVariant(), false), std::optional<QString>(QString()));
        QCOMPARE(variableTextFrom(2.25, false), std::optional<QString>(u"2.25"_s));
        QCOMPARE(m_engine->runVariable(u"g"_s, u"rekord"_s, ctx({}), 250).status, Status::NotFound);
    }

    void newNodes()
    {
        const Result r = m_engine->render(u"g"_s, u"dzis"_s, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QVariantList children = r.value.toMap().value(u"children"_s).toList();
        QCOMPARE(children.size(), 6);
        const QVariantMap input = children[0].toMap();
        QCOMPARE(input.value(u"type"_s).toString(), u"input"_s);
        QCOMPARE(input.value(u"placeholder"_s).toString(), u"Ćwiczenie"_s);
        QCOMPARE(input.value(u"multiline"_s).toBool(), false);
        const QVariantMap select = children[1].toMap();
        QCOMPARE(select.value(u"options"_s).toList().size(), 2);
        QCOMPARE(select.value(u"value"_s).toString(), u"mc"_s);
        QCOMPARE(children[2].toMap().value(u"max"_s).toInt(), 5);
        QCOMPARE(children[3].toMap().value(u"value"_s).toString(), u"2026-11-20"_s);
        const QVariantMap toggle = children[4].toMap();
        QCOMPARE(toggle.value(u"on"_s).toBool(), true);
        QCOMPARE(toggle.value(u"label"_s).toString(), u"Rozgrzewka"_s);
        QCOMPARE(children[5].toMap().value(u"tone"_s).toString(), u"success"_s);
    }

    void hostileExtensions()
    {
        // A macro that eats memory ends with the engine's memory limit; the next call works.
        Extension e;
        e.id = u"zly"_s;
        e.source = u"b.macro(\"mem\", function() local t = {} for i = 1, 1e9 do t[i] = string.rep(\"x\", 1000) end end)\n"
                   u"b.macro(\"rek\", function(ctx) local function f() return f() + 1 end return f() end)\n"
                   u"b.macro(\"meta\", function() return setmetatable({}, { __index = function() error(\"x\") end }) end)"_s;
        QVERIFY(m_engine->load(e).ok());
        QCOMPARE(m_engine->runMacro(u"zly"_s, u"mem"_s, ctx({}), 2000 * BRULION_TEST_SLOWDOWN).status, Status::OutOfMemory);
        const Result deep = m_engine->runMacro(u"zly"_s, u"rek"_s, ctx({}), 2000 * BRULION_TEST_SLOWDOWN);
        QVERIFY(!deep.ok());
        // A table with a metatable crosses as plain data (no metamethod runs): no node, an error view.
        const Result meta = m_engine->runMacro(u"zly"_s, u"meta"_s, ctx({}), 250);
        QVERIFY(meta.ok());
        QCOMPARE(macroViewFrom(meta.value, false).state, MacroView::State::Error);
        QCOMPARE(markdown::macroViewText(macro(u"liczba"_s)), u"82,5"_s);
        // Another extension's macro is not this one's: ids keep them apart.
        QCOMPARE(m_engine->runMacro(u"zly"_s, u"rekord"_s, ctx({}), 250).status, Status::NotFound);
    }
};

namespace {

const char kSource[] = R"luau(
-- The gym's record of an exercise: the heaviest set of the journal's lines "Przysiad: 100 kg × 5".
b.macro("rekord", function(ctx)
    local exercise = ctx.args[1] or ""
    local best, reps = 0, 0
    for _, day in b.query("type:pages date:-30d..today", 50).items do
        for line in (b.read(day.page) or ""):gmatch("[^\n]+") do
            local name, kg, n = line:match("^%- (.-): (%d+) kg × (%d+)")
            if name == exercise and tonumber(kg) > best then
                best, reps = tonumber(kg), tonumber(n)
            end
        end
    end
    if best == 0 then
        return b.ui.text("brak", { muted = true })
    end
    return { b.ui.heading(`{best} kg × {reps}`), " ", b.ui.link(exercise) }
end, { description = "Najlepsza seria ćwiczenia", args = { "ćwiczenie" }, example = "Przysiad" })

-- What a page says, read by its name only: refreshed when that page changes.
b.macro("pierwsza", function(ctx)
    local text = b.read(ctx.args[1] or "") or ""
    return text:match("^[^\n]*") or ""
end, { description = "The first line of a page", args = "page" })

b.macro("kontekst", function(ctx)
    return `{ctx.page}|{ctx.path}|{ctx.date or "-"}|{#ctx.args}|{ctx.text}`
end)

b.macro("liczba", function(ctx) return 82.5 end)
b.macro("nic", function(ctx) return nil end)
b.macro("tabela", function(ctx) return { a = 1 } end)
b.macro("przycisk", function(ctx) return b.ui.button("x", "y") end)
b.macro("pisze", function(ctx) b.write("Ukradzione", "x") return "zapisane" end)
b.macro("pyta", function(ctx) return b.ask("Co?") end)
b.macro("petla", function(ctx) while true do end end)
b.macro("duzo", function(ctx) return string.rep("ż", 5000) end)
b.macro("kawalki", function(ctx)
    local out = {}
    for i = 1, 100 do table.insert(out, b.ui.text(tostring(i), { tone = "success" })) end
    return out
end)
b.macro("html", function(ctx) return "<b>gruby</b>\nnowa linia\u{0007}" end)
b.macro("blad", function(ctx) error("nie tym razem") end)

b.variable("plan", function(ctx)
    return `## Plan na {ctx.date}\n- Przysiad 3 × 5\n- {ctx.args[1] or "?"}`
end, { description = "Today's plan", args = { "extra" } })
b.variable("tabela", function(ctx) return { 1, 2 } end)

b.view("dzis", function(params)
    return b.ui.column({
        b.ui.input({ placeholder = "Ćwiczenie", action = "add", label = "Nowe" }),
        b.ui.select({ options = { "Przysiad", { label = "Martwy ciąg", value = "mc" } }, value = "mc", action = "pick" }),
        b.ui.slider({ value = 3, min = 1, max = 5, step = 1, action = "mood" }),
        b.ui.date({ value = "2026-11-20", action = "move" }),
        b.ui.toggle(true, "flip", { id = 1 }, { label = "Rozgrzewka" }),
        b.ui.progress({ value = 3, max = 4, label = "3 z 4", tone = "success" }),
    })
end, {}, { label = "Dziś", panel = "journal" })
b.view("boczny", function(params) return b.ui.text(params.page or "-") end, nil, { panel = true })
)luau";

} // namespace

QTEST_GUILESS_MAIN(TestExtensionsMacros)
#include "tst_extensions_macros.moc"
