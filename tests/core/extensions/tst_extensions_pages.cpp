// Extensions written in the notebook (src/core/extensions/pageextension.h): which pages are
// extensions, their ids, their code from ```luau blocks with the page's line numbers kept, the
// permissions in either language, the hash an approval covers, the new-extension page and a
// built-in extension's source as a page (its override) that runs like the built-in.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/host.h"
#include "extensions/pageextension.h"
#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

class NullHost : public Host
{
public:
    QStringList logs;
    QMap<QString, QString> texts; // pages b.read finds
    QStringList appended; // b.appendToJournal's markdown
    QString lang = u"pl"_s;
    QStringList pages(const QString &) override { return {}; }
    std::optional<QString> read(const QString &page) override
    {
        if (!texts.contains(page))
            return std::nullopt;
        return texts.value(page);
    }
    QVariantMap page(const QString &) override { return {}; }
    QVariantList blocks(const BlockQuery &) override { return {}; }
    QVariantList search(const QString &, int) override { return {}; }
    QVariantMap query(const QString &, int, QString *) override { return {}; }
    QString write(const QString &, const QString &, const QString &) override { return {}; }
    QString appendToJournal(const QString &, const QString &, const QString &markdown) override
    {
        appended << markdown;
        return {};
    }
    QString replaceBlock(const QString &, const QString &, const QString &, const std::optional<QString> &,
                         QString *) override
    {
        return {};
    }
    QString today() override { return u"2026-11-11"_s; }
    QString language() override { return lang; }
    QString uiLanguage() override { return lang; }
    void timer(const QString &, int, const QString &) override { }
    void log(const QString &extension, const QString &message) override { logs << extension + u": "_s + message; }
};

PageExtension pageOf(const QString &path, const QString &content)
{
    const ParsedNote note = markdown::parseNote(path, content);
    return readPageExtension(path, note, content);
}

} // namespace

class TestExtensionsPages : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ids_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("id");
        QTest::newRow("Polish") << u"Licznik słów"_s << u"licznik-slow"_s;
        QTest::newRow("a built-in's") << u"Siłownia"_s << u"silownia"_s;
        QTest::newRow("punctuation") << u"  Ćwiczenia!!  2026 (wersja 2) "_s << u"cwiczenia-2026-wersja-2"_s;
        QTest::newRow("ł and ż") << u"Żółw/Jeż"_s << u"zolw-jez"_s;
        QTest::newRow("nothing left") << u"###"_s << u"extension"_s;
        QTest::newRow("empty") << QString() << u"extension"_s;
        QTest::newRow("long") << QString(100, u'a') << QString(64, u'a');
    }
    void ids()
    {
        QFETCH(QString, title);
        QFETCH(QString, id);
        QCOMPARE(extensionId(title), id);
    }

    void whichPagesAreExtensions_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("extension");
        QTest::newRow("frontmatter list") << u"---\ntags: [rozszerzenie]\n---\ntekst"_s << true;
        QTest::newRow("frontmatter English") << u"---\ntags:\n  - notatki\n  - extension\n---\n"_s << true;
        QTest::newRow("Logseq property") << u"tags:: projekt, rozszerzenie\n\n- treść"_s << true;
        QTest::newRow("hash in the property") << u"tags:: #Rozszerzenie\n"_s << true;
        QTest::newRow("a block's tag only") << u"- pomysł na #rozszerzenie\n"_s << false;
        QTest::newRow("another tag") << u"---\ntags: [rozszerzenia]\n---\n"_s << false;
        QTest::newRow("no tags") << u"```luau\nprint(1)\n```\n"_s << false;
    }
    void whichPagesAreExtensions()
    {
        QFETCH(QString, content);
        QFETCH(bool, extension);
        QCOMPARE(isExtensionPage(markdown::parseNote(u"Strona.md"_s, content)), extension);
    }

    void codeKeepsThePageLines()
    {
        const QString content = u"---\n"
                                "tags: [rozszerzenie]\n"
                                "---\n"
                                "Opis po polsku.\n"
                                "\n"
                                "```luau\n"
                                "local a = 1\n"
                                "```\n"
                                "Przykład, który nie jest kodem:\n"
                                "```python\n"
                                "print('x')\n"
                                "```\n"
                                "````markdown\n"
                                "```luau\n"
                                "nie = 'kod'\n"
                                "```\n"
                                "````\n"
                                "- punkt z kodem (Logseq)\n"
                                "  - ```lua\n"
                                "    local b = 2\r\n"
                                "    ```\n"
                                "~~~ luau {.numberLines}\n"
                                "local c = 3\n"
                                "~~~\n"_s;
        const ExtensionCode code = extensionCode(content);
        QCOMPARE(code.blocks, 3);
        QVERIFY(!code.unclosed);
        QCOMPARE(code.firstLine, 6);
        QCOMPARE(code.code, u"local a = 1\n\n    local b = 2\n\nlocal c = 3"_s);
        const QStringList lines = code.source.split(u'\n');
        QCOMPARE(lines.size(), content.split(u'\n').size());
        QCOMPARE(lines[6], u"local a = 1"_s);
        QCOMPARE(lines[19], u"    local b = 2"_s);
        QCOMPARE(lines[22], u"local c = 3"_s);
        for (int i : {0, 1, 3, 5, 7, 10, 14, 18, 20, 21})
            QVERIFY2(lines[i].isEmpty(), qPrintable(u"line %1: %2"_s.arg(i).arg(lines[i])));
        // An unclosed block runs to the end; a fence in a list item with a number works too.
        const ExtensionCode open = extensionCode(u"1. ```luau\nx = 1\ny = 2"_s);
        QVERIFY(open.unclosed);
        QCOMPARE(open.code, u"x = 1\ny = 2"_s);
        // Inline code is no fence; neither is a "```" line inside another fence.
        QCOMPARE(extensionCode(u"```luau``` nie\nx"_s).blocks, 0);
        QCOMPARE(extensionCode(QString()).blocks, 0);
    }

    void codeBlockAtALine()
    {
        const QString content = u"tekst\n```luau\nx = 1\ny = 2\n```\nprzerwa\n```luau\nz = 3\n```"_s;
        QCOMPARE(codeBlockAt(content, 2), u"\n\nx = 1\ny = 2\n\n\n\n\n"_s);
        QCOMPARE(codeBlockAt(content, 1), codeBlockAt(content, 4)); // the fence lines count
        QCOMPARE(codeBlockAt(content, 7), u"\n\n\n\n\n\n\nz = 3\n"_s);
        QVERIFY(codeBlockAt(content, 0).isEmpty());
        QVERIFY(codeBlockAt(content, 5).isEmpty());
        QVERIFY(codeBlockAt(content, 99).isEmpty());
    }

    void permissions()
    {
        QStringList unknown;
        QCOMPARE(permissionsFrom({u"Zapis"_s, u"AI"_s, u"odczyt"_s, u"sieć"_s}, &unknown), (QStringList{u"ai"_s, u"write"_s}));
        QCOMPARE(unknown, QStringList{u"sieć"_s});
        QCOMPARE(permissionsFrom({u"write"_s, u"write"_s}), QStringList{u"write"_s});

        const PageExtension pl = pageOf(u"Rozszerzenia/Licznik słów.md"_s,
                                        u"---\ntags: [rozszerzenie]\nuprawnienia: [zapis, sieć]\nopis: Liczy słowa.\n---\n"
                                        "```luau\nb.command(\"x\", function() end)\n```\n"_s);
        QCOMPARE(pl.id, u"licznik-slow"_s);
        QCOMPARE(pl.title, u"Licznik słów"_s);
        QCOMPARE(pl.permissions, QStringList{u"write"_s});
        QCOMPARE(pl.description, u"Liczy słowa."_s);
        QCOMPARE(pl.problems, QStringList{u"unknown permission: sieć"_s});
        QCOMPARE(pl.firstLine, 6);
        const PageExtension en = pageOf(u"Word count.md"_s, u"tags:: extension\npermissions:: write, ai\n\n```lua\nx = 1\n```"_s);
        QCOMPARE(en.permissions, (QStringList{u"ai"_s, u"write"_s}));
        QVERIFY(en.problems.isEmpty());
        const PageExtension none = pageOf(u"Pusta.md"_s, u"tags:: rozszerzenie\n\nbez kodu"_s);
        QCOMPARE(none.problems, QStringList{u"no ```luau block"_s});
        QCOMPARE(pageOf(u"P.md"_s, u"tags:: rozszerzenie\n```luau\nx = 1"_s).problems, QStringList{u"unclosed ```luau block"_s});
    }

    void hashCoversCodeAndPermissions()
    {
        const QString base = u"---\ntags: [rozszerzenie]\n---\nOpis.\n\n```luau\nx = 1\n```\n"_s;
        const QString hash = pageOf(u"A.md"_s, base).hash;
        QCOMPARE(hash.size(), 64);
        // Prose, blank lines between the blocks, the title: no new approval.
        QCOMPARE(pageOf(u"A.md"_s, u"---\ntags: [rozszerzenie]\n---\nInny opis,\n\ndłuższy.\n\n```luau\nx = 1\n```\nKoniec.\n"_s).hash,
                 hash);
        QCOMPARE(pageOf(u"Inna nazwa.md"_s, base).hash, hash);
        // The code or the permissions: a new approval.
        QVERIFY(pageOf(u"A.md"_s, QString(base).replace(u"x = 1"_s, u"x = 2"_s)).hash != hash);
        QVERIFY(pageOf(u"A.md"_s, QString(base).replace(u"---\nOpis"_s, u"uprawnienia: zapis\n---\nOpis"_s)).hash != hash);
        QCOMPARE(extensionHash({u"write"_s, u"ai"_s}, u"x"_s), extensionHash({u"ai"_s, u"write"_s}, u"x"_s));
    }

    void errorsNameThePageLines()
    {
        NullHost host;
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        const PageExtension e = pageOf(u"Licznik słów.md"_s, u"---\ntags: [rozszerzenie]\n---\nOpis.\n\n```luau\n"
                                                             "b.command(\"policz\", function()\n"
                                                             "    local t = nil\n"
                                                             "    return t.pole\n"
                                                             "end)\n```\n"_s);
        const Result loaded = engine.load(e.extension());
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        const Result r = engine.runCommand(e.id, u"policz"_s, {});
        QVERIFY2(r.error.startsWith(u"Licznik słów:9: attempt to index nil"_s), qPrintable(r.error));
        const PageExtension broken = pageOf(u"Zepsute.md"_s, u"tags:: rozszerzenie\n\n```luau\nx = = 1\n```"_s);
        const Result bad = engine.load(broken.extension());
        QCOMPARE(bad.status, Status::Error);
        QVERIFY2(bad.error.startsWith(u"Zepsute:4:"_s), qPrintable(bad.error));
    }

    void newExtensionPageRuns_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("view");
        QTest::newRow("pl") << u"pl"_s << u"powitanie"_s << u"projekty"_s;
        QTest::newRow("en") << u"en"_s << u"hello"_s << u"projects"_s;
    }
    void newExtensionPageRuns()
    {
        QFETCH(QString, language);
        QFETCH(QString, command);
        QFETCH(QString, view);
        const QString content = newExtensionPage(language);
        const ParsedNote note = markdown::parseNote(u"Moje.md"_s, content);
        QVERIFY(isExtensionPage(note));
        const PageExtension e = readPageExtension(u"Moje.md"_s, note, content);
        QVERIFY2(e.problems.isEmpty(), qPrintable(e.problems.join(u", "_s)));
        QVERIFY(!e.description.isEmpty());
        QVERIFY(e.permissions.isEmpty()); // the example only reads
        NullHost host;
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        const Result loaded = engine.load(e.extension());
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(engine.commands().size(), 1);
        QCOMPARE(engine.commands().first().name, command);
        const Result hello = engine.runCommand(e.id, command, {});
        QVERIFY2(hello.value.toString().endsWith(u"2026-11-11"_s), qPrintable(hello.value.toString()));
        QCOMPARE(engine.views().size(), 1);
        QCOMPARE(engine.views().first().name, view);
        QVERIFY(!engine.views().first().label.isEmpty());
        const Result rendered = engine.render(e.id, view, {});
        QVERIFY2(rendered.ok(), qPrintable(rendered.error));
        QCOMPARE(rendered.value.toMap().value(u"type"_s).toString(), u"empty"_s); // no pages tagged
        const QList<HookInfo> hooks = engine.hooks(u"save"_s);
        QCOMPARE(hooks.size(), 1);
        QVERIFY(engine.runHook(hooks.first().id, {{u"page"_s, u"Żółw"_s}}).ok());
        QVERIFY2(host.logs.last().endsWith(u": Żółw"_s), qPrintable(host.logs.last()));
    }

    void assistantAsksAndWrites()
    {
        // The built-in AI assistant: a question goes to b.ai with the page on screen's text and the
        // agent's tools (tst_extensions_assistant: the tools on a notebook); the answer is written
        // into today's note as an outline under the question.
        NullHost host;
        host.texts.insert(u"Trening A"_s, u"tags:: plan-treningowy\n\n- [[Przysiad]] 3×5\n"_s);
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        const Extension assistant = builtinExtension(u"asystent"_s);
        QVERIFY(assistant.permissions.contains(u"ai"_s));
        const Result loaded = engine.load(assistant);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(engine.commands().size(), 2);
        Result r = engine.runCommand(u"asystent"_s, u"ask"_s, {{u"page"_s, u"Trening A"_s}});
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ask"_s);
        r = engine.resume(r.call, u"  O czym to jest?  "_s);
        QCOMPARE(r.status, Status::Suspended);
        const QVariantMap request = r.value.toMap().value(u"request"_s).toMap();
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        const QString first = request.value(u"messages"_s).toList().value(0).toMap().value(u"content"_s).toString();
        QVERIFY2(first.contains(u"[[Przysiad]] 3×5"_s), qPrintable(first));
        QVERIFY(first.endsWith(u"O czym to jest?"_s));
        QVERIFY(!request.value(u"system"_s).toString().isEmpty());
        QVERIFY(!request.value(u"tools"_s).toList().isEmpty());
        r = engine.resume(
                r.call,
                QVariantMap{{u"text"_s, u"Plan siłowy.\n\n## Ćwiczenia\n- Przysiad\n1. Martwy ciąg\n```\nkod\n```"_s},
                            {u"raw"_s, u"[]"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Odpowiedź AI jest w dzisiejszej notatce."_s);
        QCOMPARE(host.appended.size(), 1);
        QCOMPARE(host.appended.first(), u"- 🤖 O czym to jest? ([[Trening A]])\n  - Plan siłowy.\n  - **Ćwiczenia**\n"
                                        "  - Przysiad\n  - Martwy ciąg\n  - kod"_s);
        // A summary needs a page; an error of the model is the command's error.
        QCOMPARE(engine.runCommand(u"asystent"_s, u"summarize"_s, {{u"page"_s, QString()}}).value.toString(),
                 u"Otwórz stronę, którą streścić."_s);
        r = engine.runCommand(u"asystent"_s, u"summarize"_s, {{u"page"_s, u"Trening A"_s}});
        QCOMPARE(r.status, Status::Suspended);
        r = engine.resume(r.call, QVariantMap{{u"error"_s, u"the API key was refused (HTTP 401)"_s}});
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.contains(u"b.ai: the API key was refused"_s), qPrintable(r.error));
        QCOMPARE(host.appended.size(), 1);
        // Cancelled question: nothing asked, nothing written.
        r = engine.runCommand(u"asystent"_s, u"ask"_s, {{u"page"_s, u"Trening A"_s}});
        r = engine.resume(r.call, QString());
        QVERIFY(r.ok());
        QCOMPARE(host.appended.size(), 1);
    }

    void pageFromCode_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("description");
        QTest::addColumn<QStringList>("permissions");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("expected");
        const QString hello = u"b.command(\"hej\", function() return \"Cześć\" end)"_s;
        QTest::newRow("pl") << u"pl"_s << u"Liczy słowa"_s << QStringList{u"zapis"_s, u"ai"_s} << hello
                            << u"---\ntags:\n  - rozszerzenie\nopis: Liczy słowa\nuprawnienia:\n  - ai\n  - zapis\n---\n"
                               "```luau\n"_s + hello + u"\n```\n"_s;
        QTest::newRow("en, only reads") << u"en"_s << QString() << QStringList() << hello + u"\r\n\r\n"_s
                                        << u"---\ntags:\n  - extension\n---\n```luau\n"_s + hello + u"\n```\n"_s;
        // A colon, a hash and quotes in the description survive as YAML; a fence in the code
        // (a Markdown string) gets a longer fence around it.
        QTest::newRow("tricky") << u"en"_s << u"Sums: #tags \"quoted\""_s << QStringList{u"write"_s}
                                << u"local md = [[\n```\ncode\n```\n]]\nb.command(\"x\", function() return md end)"_s
                                << QString();
    }
    void pageFromCode()
    {
        QFETCH(QString, language);
        QFETCH(QString, description);
        QFETCH(QStringList, permissions);
        QFETCH(QString, code);
        QFETCH(QString, expected);
        const QString content = extensionPageText(language, description, permissions, code);
        if (!expected.isEmpty())
            QCOMPARE(content, expected);
        const ParsedNote note = markdown::parseNote(u"Licznik słów.md"_s, content);
        QVERIFY(isExtensionPage(note));
        const PageExtension e = readPageExtension(u"Licznik słów.md"_s, note, content);
        QVERIFY2(e.problems.isEmpty(), qPrintable(e.problems.join(u", "_s)));
        QCOMPARE(e.description, description);
        QCOMPARE(e.permissions, permissionsFrom(permissions));
        QString trimmed = code;
        trimmed.replace(u"\r\n"_s, u"\n"_s);
        QCOMPARE(e.code, trimmed.trimmed());
        QCOMPARE(e.blocks, 1);
        NullHost host;
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        const Result loaded = engine.load(e.extension());
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(engine.commands().size(), 1);
    }

    void errorLineOfAMessage_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("error");
        QTest::addColumn<int>("line");
        QTest::newRow("page line") << u"Licznik słów"_s << u"Licznik słów:14: attempt to index nil\nstack"_s << 13;
        QTest::newRow("another page") << u"Licznik"_s << u"Licznik słów:14: x"_s << -1;
        QTest::newRow("no line") << u"Żółw"_s << u"Żółw: x"_s << -1;
        QTest::newRow("no colon after") << u"Żółw"_s << u"Żółw:12 x"_s << -1;
        QTest::newRow("line 0") << u"Żółw"_s << u"Żółw:0: x"_s << -1;
        QTest::newRow("title with a colon") << u"A: b"_s << u"A: b:3: x"_s << 2;
        QTest::newRow("empty") << u"Żółw"_s << QString() << -1;
    }
    void errorLineOfAMessage()
    {
        QFETCH(QString, title);
        QFETCH(QString, error);
        QFETCH(int, line);
        QCOMPARE(errorLine(title, error), line);
    }

    void builtinSourceAsAPage_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("pl") << u"pl"_s;
        QTest::newRow("en") << u"en"_s;
    }
    void builtinSourceAsAPage()
    {
        // The gym's source as a page of the notebook runs like the built-in: the same commands and
        // views, under the same id (the page overrides it), with the built-in's data.
        QFETCH(QString, language);
        const Extension gym = builtinExtension(u"silownia"_s);
        QVERIFY(!gym.source.isEmpty());
        const QString content = builtinSourcePage(gym, language);
        QVERIFY(content.contains(u"\n## Parsing\n"_s));
        QVERIFY(content.contains(u"\n## Views\n"_s));
        const QString title = language == u"en" ? u"Gym"_s : u"Siłownia"_s;
        const ParsedNote note = markdown::parseNote(title + u".md"_s, content);
        QVERIFY(isExtensionPage(note));
        const PageExtension page = readPageExtension(title + u".md"_s, note, content);
        QVERIFY2(page.problems.isEmpty(), qPrintable(page.problems.join(u", "_s)));
        QCOMPARE(page.permissions, gym.permissions);
        QCOMPARE(page.overrides, gym.id);
        QVERIFY(page.blocks > 5);

        NullHost host;
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        QVERIFY(engine.load(gym).ok());
        const QList<CommandInfo> commands = engine.commands();
        const QList<ViewInfo> views = engine.views();
        engine.unload(gym.id);
        Extension fromPage = page.extension();
        fromPage.id = gym.id; // what the app does for a page named like a built-in
        fromPage.data = gym.data;
        const Result loaded = engine.load(fromPage);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(engine.commands().size(), commands.size());
        for (int i = 0; i < commands.size(); ++i)
            QCOMPARE(engine.commands()[i].name, commands[i].name);
        QCOMPARE(engine.views().size(), views.size());
        QCOMPARE(builtinFor(page.id), gym.id); // "Siłownia" and "Gym" both name it too
        QVERIFY(builtinFor(u"licznik-slow"_s).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestExtensionsPages)
#include "tst_extensions_pages.moc"
