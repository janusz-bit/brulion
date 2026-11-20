// What the sample extensions met as walls of the API, and the extension's own corner of the device
// (docs/EXTENSIONS.md "Shared code", "Settings", "Timers", "Sandbox and limits"), on a real notebook:
// pages of one name in different folders as two paths, code shared through b.require — each
// extension with its own copy running under its own permissions, a cycle, a page that is no
// extension or not approved, a module that fails or never ends —, settings declared, kept on the
// device and checked, timers with their own words, what a question may show, and the diff an
// approval shows.

#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "extensions/pageextension.h"
#include "index/vaultindex.h"
#include "markdown/parse.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QString kLibrary = u"---\ntags: [rozszerzenie]\n---\n# Wspólne funkcje\n\nPomocnicze funkcje dla innych rozszerzeń.\n\n"
                         "```luau\nlocal M = {}\nlocal calls = 0\nfunction M.wielkie(s)\n    calls += 1\n"
                         "    return string.rep(s, 2) .. \" (\" .. calls .. \")\"\nend\n"
                         "function M.zapisz(strona, tekst)\n    return b.write(strona, tekst)\nend\nreturn M\n```\n"_s;

} // namespace

class TestExtensionsShared : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    QStringList m_refused; // paths moduleAllowed() refuses
    QList<std::pair<QString, TimerSpec>> m_timers;

    void writeNote(const QString &path, const QString &content)
    {
        const QString file = m_dir + u'/' + path;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content.toUtf8());
        f.close();
        if (m_index)
            m_index->update(path, content);
    }

    void open(const QHash<QString, QString> &notes, bool dryRun = false, bool keepVault = false)
    {
        m_engine.reset();
        m_host.reset();
        if (!keepVault) {
            m_index.reset();
            m_vault.reset();
            m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
            for (auto it = notes.cbegin(); it != notes.cend(); ++it)
                writeNote(it.key(), it.value());
            m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
            QVERIFY(m_vault->open());
            m_index = std::make_unique<VaultIndex>(m_vault->config());
            for (auto it = notes.cbegin(); it != notes.cend(); ++it)
                m_index->update(it.key(), it.value());
        }
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 14), QTime(10, 30)); };
        options.updateIndex = true;
        options.dryRun = dryRun;
        options.moduleAllowed = [this](const QString &path, const QString &) {
            return m_refused.contains(path) ? u"waits for approval on this device"_s : QString();
        };
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_host->onExtensionTimer = [this](const QString &extension, const TimerSpec &spec) {
            m_timers.append({extension, spec});
        };
        m_engine = std::make_unique<Engine>();
        QVERIFY(m_engine->start(m_host.get(), Limits{test::kRoomyMs, 64ll << 20}));
    }

    Result load(const QString &id, const QString &source, const QStringList &permissions = {u"write"_s})
    {
        Extension e;
        e.id = id;
        e.source = source;
        e.permissions = permissions;
        return m_engine->load(e);
    }
    Result run(const QString &id, const QString &code) { return m_engine->evaluate(id, code, id); }

private Q_SLOTS:
    void pagesOfOneNameAreTwoPaths()
    {
        open({{u"Projekty/Dom.md"_s, u"tags:: projekt\n\n- [ ] Pomalować pokój\n"_s},
              {u"Archiwum/Dom.md"_s, u"tags:: projekt\n\n- [x] Stary dach\n"_s},
              {u"Ogród.md"_s, u"- róże\n"_s}});
        QVERIFY(load(u"t"_s, QString()).ok());
        // Names: one "Dom". Paths: both, and each of them is a page for every function.
        QCOMPARE(run(u"t"_s, u"b.pages({ tag = \"projekt\" })"_s).value.toStringList(), QStringList{u"Dom"_s});
        const Result paths = run(u"t"_s, u"b.pages({ tag = \"projekt\", paths = true })"_s);
        QCOMPARE(paths.value.toStringList(), (QStringList{u"Archiwum/Dom.md"_s, u"Projekty/Dom.md"_s}));
        QCOMPARE(run(u"t"_s, u"#b.pages({ paths = true })"_s).value.toInt(), 3);
        const Result read = run(u"t"_s, u"local out = {}\n"
                                        "for _, path in b.pages({ tag = \"projekt\", paths = true }) do\n"
                                        "    for _, block in b.blocks({ page = path }) do\n"
                                        "        if block.task then table.insert(out, b.page(path).path .. \"=\" .. block.plain) end\n"
                                        "    end\n"
                                        "end\nreturn table.concat(out, \"; \")"_s);
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.value.toString(), u"Archiwum/Dom.md=Stary dach; Projekty/Dom.md=Pomalować pokój"_s);
        QVERIFY(run(u"t"_s, u"b.pages({ paths = \"tak\" })"_s).error.contains(u"b.pages: paths must be true or false"_s));
    }

    void codeShared()
    {
        open({{u"Rozszerzenia/Wspólne funkcje.md"_s, kLibrary}, {u"Dom.md"_s, u"- a\n"_s}});
        const QString user = u"local w = b.require(\"Wspólne funkcje\")\n"
                             "b.command(\"krzycz\", function(ctx) return w.wielkie(\"żółw\") end)\n"_s;
        QVERIFY(load(u"pierwsze"_s, user).ok());
        QVERIFY(load(u"drugie"_s, user, {}).ok());
        // The library runs as an extension of its own too: what its top level returns is no result.
        const ParsedNote note = markdown::parseNote(u"Rozszerzenia/Wspólne funkcje.md"_s, kLibrary);
        const Result itself = m_engine->load(readPageExtension(u"Rozszerzenia/Wspólne funkcje.md"_s, note, kLibrary).extension());
        QVERIFY2(itself.ok(), qPrintable(itself.error));
        // Each extension runs its own copy: the counter of one is not the other's.
        QCOMPARE(m_engine->runCommand(u"pierwsze"_s, u"krzycz"_s, {}).value.toString(), u"żółwżółw (1)"_s);
        QCOMPARE(m_engine->runCommand(u"pierwsze"_s, u"krzycz"_s, {}).value.toString(), u"żółwżółw (2)"_s);
        QCOMPARE(m_engine->runCommand(u"drugie"_s, u"krzycz"_s, {}).value.toString(), u"żółwżółw (1)"_s);
        // Required again: the same table.
        QCOMPARE(run(u"pierwsze"_s, u"b.require(\"Wspólne funkcje\") == b.require(\"Rozszerzenia/Wspólne funkcje\")"_s).value,
                 QVariant(true));
        QCOMPARE(m_engine->modulesOf(u"pierwsze"_s), QStringList{u"Rozszerzenia/Wspólne funkcje.md"_s});
        // The module writes with the requester's permissions, never its own page's.
        QVERIFY(run(u"pierwsze"_s, u"b.require(\"Wspólne funkcje\").zapisz(\"Nowa\", \"- zapisane\\n\")"_s).ok());
        QVERIFY(m_vault->exists(u"Nowa.md"_s));
        const Result denied = run(u"drugie"_s, u"b.require(\"Wspólne funkcje\").zapisz(\"Inna\", \"x\")"_s);
        QVERIFY2(denied.error.contains(u"b.write: permission denied: write"_s), qPrintable(denied.error));
        // The module's globals are its own, the requester's its own.
        QVERIFY(load(u"trzecie"_s, u"M = 5\nlocal w = b.require(\"Wspólne funkcje\")\nassert(M == 5)\n"_s).ok());
    }

    void requireRefuses_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("error");
        QTest::newRow("no page") << u"b.require(\"Nie ma\")"_s << u"t:1: b.require: no page named “Nie ma”"_s;
        QTest::newRow("no extension") << u"b.require(\"Dom\")"_s << u"t:1: b.require: “Dom” is no extension page"_s;
        QTest::newRow("no code") << u"b.require(\"Pusta\")"_s << u"t:1: b.require: “Pusta” has no ```luau block"_s;
        QTest::newRow("not approved") << u"b.require(\"Niezatwierdzona\")"_s
                                      << u"t:1: b.require: “Niezatwierdzona” waits for approval on this device"_s;
        QTest::newRow("a number") << u"b.require(5)"_s << u"t:1: b.require: argument 1 must be a page's name, not number"_s;
        QTest::newRow("empty") << u"b.require(\"  \")"_s << u"t:1: b.require: the page's name is empty"_s;
        QTest::newRow("a cycle") << u"b.require(\"Pierwsza\")"_s << u"b.require: a cycle: Pierwsza → Druga → Pierwsza"_s;
        QTest::newRow("its error") << u"b.require(\"Błędna\")"_s << u"Błędna:6: attempt to index nil with 'pole'"_s;
        QTest::newRow("its syntax") << u"b.require(\"Składnia\")"_s << u"Składnia:7: Expected identifier"_s;
        QTest::newRow("caught") << u"local ok, e = pcall(b.require, \"Nie ma\")\nerror(\"złapany: \" .. e, 0)"_s
                                << u"złapany: b.require: no page named “Nie ma”"_s;
    }
    void requireRefuses()
    {
        QFETCH(QString, code);
        QFETCH(QString, error);
        const auto page = [](const QString &body) { return u"---\ntags: [extension]\n---\n```luau\n"_s + body + u"\n```\n"_s; };
        m_refused = {u"Niezatwierdzona.md"_s};
        open({{u"Dom.md"_s, u"- zwykła strona\n"_s},
              {u"Pusta.md"_s, u"tags:: rozszerzenie\n\nTylko tekst.\n"_s},
              {u"Niezatwierdzona.md"_s, page(u"return {}"_s)},
              {u"Pierwsza.md"_s, page(u"return b.require(\"Druga\")"_s)},
              {u"Druga.md"_s, page(u"return b.require(\"Pierwsza\")"_s)},
              {u"Błędna.md"_s, page(u"local t = nil\nreturn t.pole"_s)},
              {u"Składnia.md"_s, page(u"return {"_s)}});
        m_refused.clear();
        m_refused << u"Niezatwierdzona.md"_s;
        QVERIFY(load(u"t"_s, QString()).ok());
        const Result r = run(u"t"_s, code);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains(error), qPrintable(r.error));
        // After any of them the engine and the module are usable: no half-required state stays.
        QCOMPARE(run(u"t"_s, u"1 + 1"_s).value.toInt(), 2);
        QCOMPARE(run(u"t"_s, code).error.contains(u"a cycle"_s), error.contains(u"a cycle"_s));
    }

    void requireItsOwnPage()
    {
        open({{u"Rozszerzenia/Ja.md"_s, u"---\ntags: [extension]\n---\n```luau\nreturn {}\n```\n"_s}});
        const Result r = load(u"ja"_s, u"b.require(\"Ja\")"_s);
        QVERIFY2(r.error.contains(u"b.require: “Ja” is the extension's own page"_s), qPrintable(r.error));
    }

    void aModuleNeverEndingIsStopped()
    {
        // The time limit is no less for a module's code, and pcall around b.require catches none.
        open({{u"Pętla.md"_s, u"---\ntags: [extension]\n---\n```luau\nwhile true do end\n```\n"_s}});
        m_engine->setLimits(Limits{200, 64ll << 20}); // (the time limit is the subject here)
        const Result r = load(u"t"_s, u"pcall(b.require, \"Pętla\")\nb.command(\"x\", function() return 1 end)"_s);
        QCOMPARE(r.status, Status::Timeout);
        m_engine->setLimits(Limits{test::kRoomyMs, 64ll << 20});
        QVERIFY(load(u"t"_s, u"b.command(\"x\", function() return 1 end)"_s).ok());
        QCOMPARE(m_engine->runCommand(u"t"_s, u"x"_s, {}).value.toInt(), 1);
    }

    void requireNestedTooDeep()
    {
        QHash<QString, QString> notes;
        for (int i = 0; i <= QuestionLimits::requireDepth + 1; ++i)
            notes.insert(u"M%1.md"_s.arg(i), u"---\ntags: [extension]\n---\n```luau\nreturn b.require(\"M%1\")\n```\n"_s.arg(i + 1));
        notes.insert(u"M%1.md"_s.arg(QuestionLimits::requireDepth + 2), u"---\ntags: [extension]\n---\n```luau\nreturn 7\n```\n"_s);
        open(notes);
        QVERIFY(load(u"t"_s, QString()).ok());
        const Result r = run(u"t"_s, u"b.require(\"M0\")"_s);
        QVERIFY2(r.error.contains(u"modules nested more than 16 deep"_s), qPrintable(r.error));
        QCOMPARE(run(u"t"_s, u"b.require(\"M10\")"_s).value.toInt(), 7);
    }

    void settings()
    {
        open({{u"Dom.md"_s, u"- a\n"_s}});
        const QString declare = u"local s = b.settings({\n"
                                "    { key = \"przerwa\", label = \"Przerwa między seriami\", value = 90, step = 15, unit = \"s\" },\n"
                                "    { key = \"waluta\", label = \"Waluta\", value = \"zł\", placeholder = \"np. zł\" },\n"
                                "    { key = \"dzwiek\", label = \"Dźwięk na koniec\", value = true },\n"
                                "    { key = \"jednostka\", label = \"Jednostka\", options = { \"kg\", \"lb\" } },\n"
                                "})\n"
                                "b.command(\"pokaz\", function() return `{b.setting(\"przerwa\")} {b.setting(\"waluta\")} {tostring(b.setting(\"dzwiek\"))} {b.setting(\"jednostka\")}` end)\n"
                                "assert(type(s.przerwa) == \"number\" and type(s.jednostka) == \"string\")\n"_s;
        const Result loaded = load(u"silownia"_s, declare);
        QVERIFY2(loaded.ok(), qPrintable(u"%1 %2"_s.arg(int(loaded.status)).arg(loaded.error)));
        const QVariantList fields = m_engine->settingsOf(u"silownia"_s);
        QCOMPARE(fields.size(), 4);
        QCOMPARE(fields[0].toMap().value(u"kind"_s).toString(), u"number"_s);
        QCOMPARE(fields[1].toMap().value(u"kind"_s).toString(), u"text"_s);
        QCOMPARE(fields[2].toMap().value(u"kind"_s).toString(), u"toggle"_s);
        QCOMPARE(fields[3].toMap().value(u"kind"_s).toString(), u"choice"_s);
        QCOMPARE(fields[3].toMap().value(u"value"_s).toString(), u"kg"_s);
        QCOMPARE(m_engine->runCommand(u"silownia"_s, u"pokaz"_s, {}).value.toString(), u"90 zł true kg"_s);
        // Set by the extension itself, and by the app (the host, as Settings does).
        QVERIFY(run(u"silownia"_s, u"b.setSetting(\"przerwa\", 120)"_s).ok());
        QVERIFY(m_host->setSetting(u"silownia"_s, u"jednostka"_s, u"lb"_s).isEmpty());
        QCOMPARE(m_engine->runCommand(u"silownia"_s, u"pokaz"_s, {}).value.toString(), u"120 zł true lb"_s);
        QCOMPARE(m_engine->settingValues(u"silownia"_s).value(u"przerwa"_s).toInt(), 120);
        // Kept on the device: the notebook's device state, never a note.
        m_vault->store()->flush();
        QCOMPARE(m_vault->store()->get(settingsStoreKey(u"silownia"_s)).toObject().value(u"przerwa"_s).toInt(), 120);
        open({}, false, true);
        const Result again = load(u"silownia"_s, declare);
        QVERIFY2(again.ok(), qPrintable(again.error));
        QCOMPARE(m_engine->runCommand(u"silownia"_s, u"pokaz"_s, {}).value.toString(), u"120 zł true lb"_s);
        // A stored value that no longer fits its kind (a hand edit, another version): the default.
        QVERIFY(m_host->setSetting(u"silownia"_s, u"przerwa"_s, u"dużo"_s).isEmpty());
        QVERIFY(m_host->setSetting(u"silownia"_s, u"jednostka"_s, u"stone"_s).isEmpty());
        QCOMPARE(m_engine->runCommand(u"silownia"_s, u"pokaz"_s, {}).value.toString(), u"90 zł true kg"_s);
        // nil: back to the default; an undeclared key is kept as it is.
        QVERIFY(run(u"silownia"_s, u"b.setSetting(\"przerwa\", nil); b.setSetting(\"ostatnie\", \"Przysiad\")"_s).ok());
        QCOMPARE(run(u"silownia"_s, u"b.setting(\"przerwa\") .. b.setting(\"ostatnie\")"_s).value.toString(), u"90Przysiad"_s);
        QVERIFY(run(u"silownia"_s, u"b.setting(\"nie-ma\") == nil"_s).value.toBool());
    }

    void settingsRefused_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("error");
        QTest::newRow("not a list") << u"b.settings(\"przerwa\")"_s << u"b.settings: argument 1 must be a list of settings"_s;
        QTest::newRow("a record") << u"b.settings({ key = \"a\" })"_s << u"b.settings: argument 1 must be a list of settings"_s;
        QTest::newRow("no key") << u"b.settings({ { label = \"A\" } })"_s << u"setting 1 needs a key"_s;
        QTest::newRow("bad key") << u"b.settings({ { key = \"mój klucz\" } })"_s << u"setting 1 needs a key"_s;
        QTest::newRow("twice") << u"b.settings({ { key = \"a\" }, { key = \"a\" } })"_s << u"the key \"a\" is there twice"_s;
        QTest::newRow("kind") << u"b.settings({ { key = \"a\", kind = \"kolor\" } })"_s << u"kind is \"number\""_s;
        QTest::newRow("number") << u"b.settings({ { key = \"a\", kind = \"number\", value = \"5\" } })"_s
                                << u"its value must be a number"_s;
        QTest::newRow("choice") << u"b.settings({ { key = \"a\", options = { \"x\" }, value = \"y\" } })"_s
                                << u"must be one of its options"_s;
        QTest::newRow("no options") << u"b.settings({ { key = \"a\", kind = \"choice\" } })"_s << u"needs 1 to 50 options"_s;
        QTest::newRow("too many") << u"local f = {}\nfor i = 1, 33 do f[i] = { key = \"k\" .. i } end\nb.settings(f)"_s
                                  << u"at most 32 settings"_s;
        QTest::newRow("set: kind") << u"b.settings({ { key = \"a\", value = 1 } })\nb.setSetting(\"a\", \"x\")"_s
                                   << u"b.setSetting: \"a\" is a number"_s;
        QTest::newRow("set: table") << u"b.setSetting(\"a\", {})"_s << u"not table"_s;
        QTest::newRow("set: long") << u"b.setSetting(\"a\", string.rep(\"ż\", 4001))"_s << u"at most 4000 characters"_s;
        QTest::newRow("set: key") << u"b.setSetting(\"\", 1)"_s << u"a key is letters"_s;
        QTest::newRow("set: nan") << u"b.setSetting(\"a\", 0/0)"_s << u"the number is none"_s;
        QTest::newRow("full") << u"for i = 1, 40 do b.setSetting(\"k\" .. i, string.rep(\"x\", 3000)) end"_s
                              << u"the extension's settings are full (64 KB)"_s;
    }
    void settingsRefused()
    {
        QFETCH(QString, code);
        QFETCH(QString, error);
        open({});
        QVERIFY(load(u"t"_s, QString()).ok());
        const Result r = run(u"t"_s, code);
        QVERIFY2(!r.ok() && r.error.contains(error), qPrintable(r.error));
    }

    void settingsOfADryRun()
    {
        // `brulion-cli extension run` changes nothing on the device.
        open({}, true);
        QVERIFY(load(u"t"_s, QString()).ok());
        QVERIFY(run(u"t"_s, u"b.setSetting(\"a\", 5)"_s).ok());
        QCOMPARE(run(u"t"_s, u"b.setting(\"a\")"_s).value.toInt(), 5);
        QVERIFY(m_vault->store()->get(settingsStoreKey(u"t"_s)).isUndefined());
    }

    void timers()
    {
        open({});
        QVERIFY(load(u"herbata"_s, QString()).ok());
        QVERIFY(run(u"herbata"_s, u"b.timer(180, { label = \"Herbata\", message = \"Herbata gotowa — wyjmij torebkę.\" })"_s).ok());
        QVERIFY(run(u"herbata"_s, u"b.timer(60, \"Jajko\")"_s).ok());
        QVERIFY(run(u"herbata"_s, u"b.timer(5, { label = string.rep(\"ą\", 500), message = string.rep(\"\\n wiersz\", 200) })"_s).ok());
        QVERIFY(run(u"herbata"_s, u"b.stopTimer()"_s).ok());
        QCOMPARE(m_timers.size(), 4);
        QCOMPARE(m_timers[0].first, u"herbata"_s);
        QCOMPARE(m_timers[0].second.seconds, 180);
        QCOMPARE(m_timers[0].second.label, u"Herbata"_s);
        QCOMPARE(m_timers[0].second.message, u"Herbata gotowa — wyjmij torebkę."_s);
        QCOMPARE(m_timers[1].second.label, u"Jajko"_s);
        QVERIFY(m_timers[1].second.message.isEmpty());
        QCOMPARE(m_timers[2].second.label.size(), QuestionLimits::timerLabel);
        QVERIFY(m_timers[2].second.label.endsWith(u'…'));
        QVERIFY(m_timers[2].second.message.size() <= QuestionLimits::timerMessage);
        QVERIFY(!m_timers[2].second.message.contains(u'\n'));
        QCOMPARE(m_timers[3].second.seconds, 0); // stopped
        QVERIFY(run(u"herbata"_s, u"b.timer(10, { label = 5 })"_s).error.contains(u"b.timer: label must be a text"_s));
        QVERIFY(run(u"herbata"_s, u"b.timer(0)"_s).error.contains(u"between 1 and 86400"_s));
    }

    void questionsHaveLimits()
    {
        open({});
        QVERIFY(load(u"t"_s, u"b.command(\"duze\", function() return b.ask(\"Tytuł\\n\" .. string.rep(\"ż\", 1000000)) end)\n"
                              "b.command(\"wiersze\", function() return b.ask(\"Tytuł\\n\" .. string.rep(\"wiersz\\n\", 1000)) end)\n"
                              "b.command(\"tytul\", function() return b.ask(string.rep(\"T\", 5000)) end)\n"
                              "b.command(\"lista\", function(ctx) local o = {}\n"
                              "    for i = 1, ctx.args.n do o[i] = \"opcja \" .. i end\n"
                              "    return b.choose(\"Która?\", o) end)\n"
                              "b.command(\"dluga\", function() return b.choose(\"Która?\", { \"a\", string.rep(\"b\", 501) }) end)\n"
                              "b.command(\"liczby\", function() return b.choose(\"Ile?\", { 1, 2.5 }) end)\n"
                              "b.command(\"tabela\", function() return b.choose(\"Która?\", { {} }) end)\n"
                              "b.command(\"pola\", function(ctx) local f = {}\n"
                              "    for i = 1, ctx.args.n do f[i] = { key = \"k\" .. i, label = string.rep(\"ł\", 300), value = 1 } end\n"
                              "    return b.form(\"Formularz\", f) end)\n"_s)
                        .ok());
        Result r = m_engine->runCommand(u"t"_s, u"duze"_s, {});
        QCOMPARE(r.status, Status::Suspended);
        QString prompt = r.value.toMap().value(u"prompt"_s).toString();
        QVERIFY2(prompt.size() <= QuestionLimits::title + QuestionLimits::text + 3, qPrintable(QString::number(prompt.size())));
        QVERIFY(prompt.startsWith(u"Tytuł\nżżż"_s) && prompt.endsWith(u'…'));
        m_engine->cancel(r.call);
        r = m_engine->runCommand(u"t"_s, u"wiersze"_s, {});
        prompt = r.value.toMap().value(u"prompt"_s).toString();
        QCOMPARE(prompt.count(u'\n'), QuestionLimits::lines + 1);
        QVERIFY(prompt.endsWith(u"\n…"_s));
        m_engine->cancel(r.call);
        r = m_engine->runCommand(u"t"_s, u"tytul"_s, {});
        QCOMPARE(r.value.toMap().value(u"prompt"_s).toString().size(), QuestionLimits::title);
        m_engine->cancel(r.call);
        // Up to 500 options; more, or one too long, is the script's error.
        r = m_engine->runCommand(u"t"_s, u"lista"_s, {{u"args"_s, QVariantMap{{u"n"_s, 500}}}});
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList().size(), 500);
        QCOMPARE(m_engine->resume(r.call, u"opcja 500"_s).value.toString(), u"opcja 500"_s);
        r = m_engine->runCommand(u"t"_s, u"lista"_s, {{u"args"_s, QVariantMap{{u"n"_s, 5000}}}});
        QVERIFY2(r.error.contains(u"b.choose: at most 500 options"_s), qPrintable(r.error));
        r = m_engine->runCommand(u"t"_s, u"dluga"_s, {});
        QVERIFY2(r.error.contains(u"b.choose: option 2 is longer than 500 characters"_s), qPrintable(r.error));
        r = m_engine->runCommand(u"t"_s, u"liczby"_s, {});
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList(), (QStringList{u"1"_s, u"2.5"_s}));
        m_engine->cancel(r.call);
        QVERIFY(m_engine->runCommand(u"t"_s, u"tabela"_s, {}).error.contains(u"b.choose: option 1 is not a string"_s));
        r = m_engine->runCommand(u"t"_s, u"pola"_s, {{u"args"_s, QVariantMap{{u"n"_s, 50}}}});
        const QVariantList fields = r.value.toMap().value(u"fields"_s).toList();
        QCOMPARE(fields.size(), 50);
        QCOMPARE(fields[0].toMap().value(u"label"_s).toString().size(), QuestionLimits::label);
        m_engine->cancel(r.call);
        r = m_engine->runCommand(u"t"_s, u"pola"_s, {{u"args"_s, QVariantMap{{u"n"_s, 51}}}});
        QVERIFY(r.error.contains(u"b.form: at most 50 fields"_s));
    }

    void codeDiffOfAnApproval_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("shown");
        QTest::newRow("new") << QString() << u"a\nb"_s << u"+1 a|+2 b"_s;
        QTest::newRow("one line changed") << u"a\nb\nc"_s << u"a\nB\nc"_s << u" 1 a|-2 b|+2 B| 3 c"_s;
        QTest::newRow("context") << u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12"_s
                                 << u"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\nzmiana"_s
                                 << u"…8|9 9| 10 10| 11 11|-12 12|+12 zmiana"_s.replace(u"9 9"_s, u" 9 9"_s);
        QTest::newRow("same") << u"a\nb"_s << u"a\nb\n"_s << u"…2"_s;
        QTest::newRow("CRLF") << u"a\r\nb\r\n"_s << u"a\nb\nc"_s << u" 1 a| 2 b|+3 c"_s;
        QTest::newRow("removed") << u"x\nżółw\ny"_s << u"x\ny"_s << u" 1 x|-2 żółw| 2 y"_s;
    }
    void codeDiffOfAnApproval()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QFETCH(QString, shown);
        QStringList out;
        for (const CodeDiffLine &l : codeDiff(before, after, 3)) {
            switch (l.kind) {
            case CodeDiffLine::Same: out << u" %1 %2"_s.arg(l.line).arg(l.text); break;
            case CodeDiffLine::Added: out << u"+%1 %2"_s.arg(l.line).arg(l.text); break;
            case CodeDiffLine::Removed: out << u"-%1 %2"_s.arg(l.line).arg(l.text); break;
            case CodeDiffLine::Skipped: out << u"…%1"_s.arg(l.count); break;
            }
        }
        QCOMPARE(out.join(u'|'), shown);
    }

    void codeDiffOfALongCode()
    {
        // Two codes of 20 000 different lines: the budget ends the search, every line still shown.
        QString a, b;
        for (int i = 0; i < 20000; ++i) {
            a += u"local a%1 = %1\n"_s.arg(i);
            b += u"local b%1 = %1\n"_s.arg(i);
        }
        test::CpuTimer t;
        t.start();
        const QList<CodeDiffLine> lines = codeDiff(a, b);
        QCOMPARE(lines.size(), 40000);
        QVERIFY2(t.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsShared)
#include "tst_extensions_shared.moc"
