// brulion-cli in process: the argument parser (command words, options anywhere, `--`, global
// options, errors), argument checking (the MCP path), the registry (unique ids and tool names,
// destructive commands write), the JSON Schema (the main text required for MCP), help and
// completion (help translatable, the schema English), unified diffs, and the command table of
// docs/CLI.md against the registry.
#include "cli_p.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTranslator>

using namespace Qt::StringLiterals;
using namespace brulion::cli;

class TestCliArgs : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_data()
    {
        QTest::addColumn<QStringList>("argv");
        QTest::addColumn<QString>("command"); // "" when it fails
        QTest::addColumn<QString>("args");    // compact JSON, or the error code
        QTest::newRow("page read") << QStringList{u"page"_s, u"read"_s, u"Dom"_s} << u"page.read"_s
                                   << uR"({"ref":"Dom"})"_s;
        QTest::newRow("globals first and after")
                << QStringList{u"--vault"_s, u"/v"_s, u"page"_s, u"read"_s, u"Dom"_s, u"--blocks"_s, u"--compact"_s}
                << u"page.read"_s << uR"({"blocks":true,"ref":"Dom"})"_s;
        QTest::newRow("option with =") << QStringList{u"page"_s, u"read"_s, u"--blocks=false"_s, u"Dom"_s}
                                       << u"page.read"_s << uR"({"blocks":false,"ref":"Dom"})"_s;
        QTest::newRow("Polish task")
                << QStringList{u"task"_s,     u"add"_s,        u"Kupić"_s,   u"żółte"_s,     u"jabłka"_s,
                               u"--remind"_s, u"jutro 9:00"_s, u"--every"_s, u"co tydzień"_s}
                << u"task.add"_s << uR"({"every":"co tydzień","remind":"jutro 9:00","text":"Kupić żółte jabłka"})"_s;
        QTest::newRow("journal alone") << QStringList{u"journal"_s} << u"journal.read"_s << u"{}"_s;
        QTest::newRow("journal date") << QStringList{u"journal"_s, u"w"_s, u"piątek"_s} << u"journal.read"_s
                                      << uR"({"date":"w piątek"})"_s;
        QTest::newRow("journal read") << QStringList{u"journal"_s, u"read"_s, u"wczoraj"_s} << u"journal.read"_s
                                      << uR"({"date":"wczoraj"})"_s;
        QTest::newRow("text after --") << QStringList{u"journal"_s, u"append"_s, u"--"_s, u"-5"_s, u"stopni"_s}
                                       << u"journal.append"_s << uR"({"text":"-5 stopni"})"_s;
        QTest::newRow("calc") << QStringList{u"calc"_s, u"2+2"_s} << u"calc"_s << uR"({"expression":"2+2"})"_s;
        QTest::newRow("calc page") << QStringList{u"calc"_s, u"page"_s, u"Budżet"_s} << u"calc.page"_s
                                   << uR"({"ref":"Budżet"})"_s;
        QTest::newRow("task done") << QStringList{u"task"_s, u"done"_s, u"Dom.md#L3@00aa00aa"_s} << u"task.done"_s
                                   << uR"({"ref":"Dom.md#L3@00aa00aa"})"_s;
        QTest::newRow("help topic") << QStringList{u"help"_s, u"task"_s, u"add"_s} << u"help"_s
                                    << uR"({"topic":"task add"})"_s;
        QTest::newRow("--version") << QStringList{u"--version"_s} << u"version"_s << u"{}"_s;
        // The running app's remote control.
        QTest::newRow("app click a point") << QStringList{u"app"_s, u"click"_s, u"120,340"_s, u"--button"_s, u"right"_s}
                                           << u"app.click"_s << uR"({"button":"right","target":"120,340"})"_s;
        QTest::newRow("app click Polish text")
                << QStringList{u"app"_s, u"click"_s, u"Zapisz"_s, u"zmiany"_s, u"--modifiers"_s, u"ctrl+shift"_s}
                << u"app.click"_s << uR"({"modifiers":"ctrl+shift","target":"Zapisz zmiany"})"_s;
        QTest::newRow("app long-press") << QStringList{u"app"_s, u"long-press"_s, u"h12"_s, u"--ms"_s, u"900"_s}
                                        << u"app.long-press"_s << uR"({"ms":"900","target":"h12"})"_s;
        QTest::newRow("app double-click") << QStringList{u"app"_s, u"double-click"_s, u"--object-name"_s, u"x"_s}
                                          << u"app.double-click"_s << uR"({"objectName":"x"})"_s;
        QTest::newRow("app drag") << QStringList{u"app"_s, u"drag"_s, u"h1"_s, u"10,20"_s, u"--hold-ms"_s, u"300"_s}
                                  << u"app.drag"_s << uR"({"from":"h1","holdMs":"300","to":"10,20"})"_s;
        QTest::newRow("app scroll up") << QStringList{u"app"_s, u"scroll"_s, u"--dy"_s, u"-200"_s} << u"app.scroll"_s
                                       << uR"({"dy":"-200"})"_s;
        QTest::newRow("app type") << QStringList{u"app"_s, u"type"_s, u"zażółć"_s, u"gęślą"_s, u"jaźń"_s}
                                  << u"app.type"_s << uR"({"text":"zażółć gęślą jaźń"})"_s;
        QTest::newRow("app key") << QStringList{u"app"_s, u"key"_s, u"Ctrl+K"_s} << u"app.key"_s
                                 << uR"({"sequence":"Ctrl+K"})"_s;
        QTest::newRow("app keys") << QStringList{u"app"_s, u"key"_s, u"Down,"_s, u"Return"_s, u"--count"_s, u"2"_s}
                                  << u"app.key"_s << uR"({"count":"2","sequence":"Down, Return"})"_s;
        QTest::newRow("app wait") << QStringList{u"app"_s, u"wait"_s, u"--route"_s, u"tasks"_s, u"--idle"_s}
                                  << u"app.wait"_s << uR"({"idle":true,"route":"tasks"})"_s;
        QTest::newRow("app find") << QStringList{u"app"_s, u"find"_s, u"Żółw"_s, u"--include-hidden"_s} << u"app.find"_s
                                  << uR"({"includeHidden":true,"text":"Żółw"})"_s;
        QTest::newRow("app open a line") << QStringList{u"app"_s, u"open"_s, u"Dom"_s, u"--line"_s, u"4"_s}
                                         << u"app.open"_s << uR"({"line":"4","target":"Dom"})"_s;
        QTest::newRow("app alone opens") << QStringList{u"app"_s} << u"app.open"_s << u"{}"_s;
        QTest::newRow("app capture text") << QStringList{u"app"_s, u"capture"_s, u"Kupić"_s, u"mleko"_s}
                                          << u"app.capture"_s << uR"({"text":"Kupić mleko"})"_s;
        QTest::newRow("app drag needs two")
                << QStringList{u"app"_s, u"drag"_s, u"h1"_s, u"h2"_s, u"h3"_s} << QString() << u"extra_arguments"_s;
        QTest::newRow("unknown command") << QStringList{u"pagez"_s} << QString() << u"unknown_command"_s;
        QTest::newRow("half a command") << QStringList{u"page"_s} << QString() << u"unknown_command"_s;
        QTest::newRow("no command") << QStringList{} << QString() << u"no_command"_s;
        QTest::newRow("unknown option") << QStringList{u"page"_s, u"read"_s, u"Dom"_s, u"--bogus"_s} << QString()
                                        << u"unknown_option"_s;
        QTest::newRow("too many") << QStringList{u"page"_s, u"read"_s, u"a"_s, u"b"_s} << QString()
                                  << u"extra_arguments"_s;
        QTest::newRow("option without value")
                << QStringList{u"task"_s, u"add"_s, u"x"_s, u"--due"_s} << QString() << u"missing_value"_s;
        QTest::newRow("bad format") << QStringList{u"--format"_s, u"xml"_s, u"version"_s} << QString()
                                    << u"bad_format"_s;
        QTest::newRow("bad now") << QStringList{u"--now"_s, u"2026"_s, u"version"_s} << QString() << u"bad_now"_s;
        QTest::newRow("bad lang") << QStringList{u"--lang"_s, u"de"_s, u"version"_s} << QString() << u"bad_lang"_s;
        QTest::newRow("vault without value") << QStringList{u"--vault"_s} << QString() << u"missing_value"_s;
    }
    void parse()
    {
        QFETCH(QStringList, argv);
        QFETCH(QString, command);
        QFETCH(QString, args);
        const Invocation inv = parseArguments(argv);
        if (command.isEmpty()) {
            QVERIFY(inv.error);
            QCOMPARE(inv.error->code, args);
            QCOMPARE(inv.error->exit, int(ExitUsage));
            return;
        }
        QVERIFY2(!inv.error, qPrintable(inv.error ? inv.error->message : QString()));
        QVERIFY(inv.command);
        QCOMPARE(inv.command->id, command);
        QCOMPARE(QString::fromUtf8(QJsonDocument(inv.args).toJson(QJsonDocument::Compact)), args);
    }

    void globals()
    {
        const Invocation inv = parseArguments({u"--vault=~/Notatki"_s, u"--dry-run"_s, u"capture"_s, u"x"_s,
                                               u"--allow-destructive"_s, u"--now"_s, u"2026-10-14 10:30"_s, u"--lang"_s,
                                               u"en"_s, u"--stdin"_s, u"-v"_s, u"--read-only"_s});
        QVERIFY(!inv.error);
        QCOMPARE(inv.globals.vault, u"~/Notatki"_s);
        QVERIFY(inv.globals.dryRun);
        QVERIFY(inv.globals.allowDestructive);
        QVERIFY(inv.globals.stdinText);
        QVERIFY(inv.globals.verbose);
        QVERIFY(inv.globals.readOnly);
        QCOMPARE(inv.globals.now, u"2026-10-14 10:30"_s);
        QCOMPARE(inv.globals.lang, u"en"_s);
        // -h with a command: its help.
        const Invocation help = parseArguments({u"task"_s, u"add"_s, u"-h"_s});
        QVERIFY(help.globals.help);
        QCOMPARE(help.command->id, u"task.add"_s);
    }

    void checkArguments_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("json");
        QTest::addColumn<QString>("error"); // "" = fine
        QTest::newRow("fine") << u"search"_s << uR"({"text":"żółw","limit":5})"_s << QString();
        QTest::newRow("int as text") << u"search"_s << uR"({"text":"x","limit":"7"})"_s << QString();
        QTest::newRow("not an int") << u"search"_s << uR"({"text":"x","limit":"siedem"})"_s << u"bad_argument"_s;
        QTest::newRow("fraction") << u"search"_s << uR"({"text":"x","limit":2.5})"_s << u"bad_argument"_s;
        QTest::newRow("unknown key") << u"search"_s << uR"({"text":"x","bogus":1})"_s << u"unknown_argument"_s;
        QTest::newRow("missing") << u"search"_s << uR"({})"_s << u"missing_argument"_s;
        QTest::newRow("bool") << u"page.read"_s << uR"({"ref":"x","blocks":"yes"})"_s << u"bad_argument"_s;
        QTest::newRow("enum") << u"page.list"_s << uR"({"sort":"size"})"_s << u"bad_argument"_s;
        QTest::newRow("enum case") << u"page.list"_s << uR"({"sort":"Title"})"_s << QString();
        QTest::newRow("string expected") << u"page.read"_s << uR"({"ref":12})"_s << u"bad_argument"_s;
        QTest::newRow("stdin text not required") << u"capture"_s << uR"({})"_s << QString();
    }
    void checkArguments()
    {
        QFETCH(QString, command);
        QFETCH(QString, json);
        QFETCH(QString, error);
        const Command *c = findCommand(command);
        QVERIFY(c);
        std::optional<Error> e;
        const QJsonObject checked =
                brulion::cli::checkArguments(*c, QJsonDocument::fromJson(json.toUtf8()).object(), &e);
        QCOMPARE(e ? e->code : QString(), error);
        if (!e && command == u"search" && json.contains(u"\"7\""_s))
            QCOMPARE(checked.value(u"limit"_s).toInt(), 7);
    }

    void registry()
    {
        QSet<QString> ids;
        QSet<QString> tools;
        static const QRegularExpression toolPattern(u"^[a-zA-Z0-9_-]{1,64}$"_s);
        static const QRegularExpression argPattern(u"^[a-z][a-zA-Z0-9]*$"_s);
        for (const Command &c : commands()) {
            QVERIFY2(!ids.contains(c.id), qPrintable(c.id));
            ids.insert(c.id);
            QVERIFY2(!c.summary.isEmpty(), qPrintable(c.id));
            QVERIFY2(bool(c.run), qPrintable(c.id));
            QVERIFY2(!c.destructive || c.writes, qPrintable(c.id));
            if (c.mcp) {
                QVERIFY2(toolPattern.match(toolName(c)).hasMatch(), qPrintable(toolName(c)));
                QVERIFY(!tools.contains(toolName(c)));
                tools.insert(toolName(c));
            }
            bool restSeen = false;
            QSet<QString> names;
            for (const Arg &a : c.args) {
                QVERIFY2(argPattern.match(a.name).hasMatch(), qPrintable(c.id + u'/' + a.name));
                QVERIFY2(!names.contains(a.name), qPrintable(c.id + u'/' + a.name));
                names.insert(a.name);
                if (a.positional) {
                    QVERIFY2(!restSeen, qPrintable(c.id)); // a `rest` argument comes last
                    restSeen = a.rest;
                }
                QVERIFY(a.type != ArgType::Enum || !a.values.isEmpty());
            }
        }
        // The commands the design and the docs promise.
        for (const char *id : {"info",
                               "page.list",
                               "page.read",
                               "page.create",
                               "page.append",
                               "page.write",
                               "page.rename",
                               "page.move",
                               "page.delete",
                               "folder.list",
                               "folder.create",
                               "folder.move",
                               "folder.rename",
                               "folder.delete",
                               "journal.read",
                               "journal.path",
                               "journal.append",
                               "capture",
                               "task.list",
                               "task.add",
                               "task.set",
                               "task.done",
                               "task.doing",
                               "task.todo",
                               "task.cancel",
                               "task.defer",
                               "remind.add",
                               "remind.list",
                               "remind.snooze",
                               "query",
                               "search",
                               "tags",
                               "tag",
                               "backlinks",
                               "links",
                               "unlinked",
                               "calendar",
                               "date.parse",
                               "calc",
                               "calc.page",
                               "block.link",
                               "history.list",
                               "history.show",
                               "history.restore",
                               "schema",
                               "help",
                               "version",
                               "completion",
                               "mcp",
                               "app.open",
                               "app.today",
                               "app.capture",
                               "app.state",
                               "app.navigate",
                               "app.commands",
                               "app.command",
                               "app.screenshot",
                               "app.find",
                               "app.click",
                               "app.long-press",
                               "app.double-click",
                               "app.drag",
                               "app.scroll",
                               "app.type",
                               "app.key",
                               "app.wait",
                               "app.quit",
                               "init"})
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        // The app control commands: "app …", no notebook needed, the input ones refused in
        // read-only mode (they can change notes).
        for (const Command &c : commands()) {
            if (!c.appControl)
                continue;
            QVERIFY2(c.id.startsWith(u"app."_s), qPrintable(c.id));
            QVERIFY2(!c.needsVault && !c.destructive, qPrintable(c.id));
        }
        for (const char *id : {"app.click", "app.long-press", "app.double-click", "app.drag", "app.type", "app.key",
                               "app.command", "app.quit", "app.capture"})
            QVERIFY2(findCommand(QString::fromLatin1(id))->writes, id);
        for (const char *id : {"app.state", "app.find", "app.screenshot", "app.wait", "app.navigate", "app.scroll"})
            QVERIFY2(!findCommand(QString::fromLatin1(id))->writes, id);
        // Offered over MCP by default (no remote control needed for them).
        for (const char *id : {"app.open", "app.today", "app.capture"})
            QVERIFY2(!findCommand(QString::fromLatin1(id))->appControl, id);
        // Destructive as decided: replacing, renaming, moving, deleting and restoring.
        QStringList destructive;
        for (const Command &c : commands()) {
            if (c.destructive)
                destructive.append(c.id);
        }
        destructive.sort();
        QCOMPARE(destructive,
                 (QStringList{u"folder.delete"_s, u"folder.move"_s, u"folder.rename"_s, u"history.restore"_s,
                              u"page.delete"_s, u"page.move"_s, u"page.rename"_s, u"page.write"_s}));
    }

    void schema()
    {
        const QJsonObject doc = schemaDocument();
        QCOMPARE(doc.value(u"$schema"_s).toString(), u"https://json-schema.org/draft/2020-12/schema"_s);
        const QJsonObject all = doc.value(u"commands"_s).toObject();
        QCOMPARE(all.size(), commands().size());
        for (const Command &c : commands()) {
            const QJsonObject s = all.value(c.id).toObject();
            QCOMPARE(s.value(u"type"_s).toString(), u"object"_s);
            QCOMPARE(s.value(u"additionalProperties"_s).toBool(true), false);
            const QJsonObject props = s.value(u"properties"_s).toObject();
            QCOMPARE(props.size(), c.args.size());
            for (const Arg &a : c.args) {
                const QString type = props.value(a.name).toObject().value(u"type"_s).toString();
                QVERIFY2(QStringList({u"string"_s, u"integer"_s, u"boolean"_s}).contains(type), qPrintable(a.name));
            }
            for (const QJsonValue &r : s.value(u"required"_s).toArray())
                QVERIFY(props.contains(r.toString()));
            // MCP adds dryRun to the tools that write.
            QCOMPARE(inputSchema(c, true).value(u"properties"_s).toObject().contains(u"dryRun"_s), c.writes);
        }
        // The main text is needed (MCP has no --stdin); a new page's text is not.
        const auto required = [](const QString &id) {
            return inputSchema(*findCommand(id), true).value(u"required"_s).toArray();
        };
        for (const QString &id : {u"capture"_s, u"journal.append"_s, u"page.append"_s, u"task.add"_s, u"remind.add"_s})
            QVERIFY2(required(id).contains(u"text"_s), qPrintable(id));
        QVERIFY(required(u"calc"_s).contains(u"expression"_s));
        QVERIFY(required(u"page.write"_s).contains(u"content"_s));
        QVERIFY(required(u"page.write"_s).contains(u"expectHash"_s));
        QVERIFY(!required(u"page.create"_s).contains(u"content"_s));
    }

    // The help printed for people is translatable (context "brulion::cli"); the schema, help
    // --json and the MCP tools keep the English source.
    void helpIsTranslatedSchemaStaysEnglish()
    {
        class Polish : public QTranslator {
        public:
            QString translate(const char *context, const char *source, const char *, int) const override
            {
                if (qstrcmp(context, "brulion::cli") != 0)
                    return {};
                const QByteArray s(source);
                if (s == "Quick capture into today's journal, as the app's: a date in it makes a task with ⏰ or 📅")
                    return u"Szybka notatka do dzisiejszego dziennika"_s;
                if (s == "What to capture (Polish or English)")
                    return u"Co zanotować"_s;
                if (s == "Arguments:")
                    return u"Argumenty:"_s;
                return {};
            }
            bool isEmpty() const override { return false; }
        } polish;
        QCoreApplication::installTranslator(&polish);
        const Command *capture = findCommand(u"capture"_s);
        const QString help = helpText(u"brulion-cli"_s, capture);
        const QString all = helpText(u"brulion-cli"_s, nullptr);
        const QJsonObject schema = schemaDocument().value(u"commands"_s).toObject().value(u"capture"_s).toObject();
        QCoreApplication::removeTranslator(&polish);
        QVERIFY2(help.contains(u"Szybka notatka do dzisiejszego dziennika"_s), qPrintable(help));
        QVERIFY(help.contains(u"Co zanotować"_s));
        QVERIFY(help.contains(u"Argumenty:"_s));
        QVERIFY(all.contains(QRegularExpression(u"\n  capture +Szybka notatka do dzisiejszego dziennika\n"_s)));
        QVERIFY(schema.value(u"description"_s).toString().startsWith(u"Quick capture into today's journal"_s));
        QCOMPARE(
                schema.value(u"properties"_s).toObject().value(u"text"_s).toObject().value(u"description"_s).toString(),
                u"What to capture (Polish or English)"_s);
        QCOMPARE(capture->summary,
                 u"Quick capture into today's journal, as the app's: a date in it makes a task with ⏰ or 📅"_s);
        // Without a catalog: English.
        QVERIFY(helpText(u"brulion-cli"_s, capture).contains(u"What to capture (Polish or English)"_s));
    }

    void helpAndCompletion()
    {
        const QString help = helpText(u"brulion-cli"_s, nullptr);
        for (const Command &c : commands())
            QVERIFY2(help.contains(c.words().join(u' ')), qPrintable(c.id));
        QVERIFY(help.contains(u"--allow-destructive"_s));
        QVERIFY(helpText(u"brulion-cli"_s, findCommand(u"task.add"_s)).contains(u"--remind <date>"_s));
        for (const QString &shell : {u"bash"_s, u"fish"_s, u"zsh"_s}) {
            const QString script = completionScript(shell, u"brulion-cli"_s);
            QVERIFY(script.contains(u"brulion-cli"_s));
            for (const QString &word : {u"page"_s, u"journal"_s, u"capture"_s, u"vault"_s, u"allow-destructive"_s})
                QVERIFY2(script.contains(word), qPrintable(shell + u": "_s + word));
        }
        QVERIFY(completionScript(u"tcsh"_s, u"brulion-cli"_s).isEmpty());
        QCOMPARE(optionName(u"expectHash"_s), u"--expect-hash"_s);
        QCOMPARE(toolName(*findCommand(u"task.add"_s)), u"brulion_task_add"_s);
        QCOMPARE(toolName(*findCommand(u"app.long-press"_s)), u"brulion_app_long_press"_s);
    }

    void diff()
    {
        QCOMPARE(unifiedDiff(u"- a\n- b\n"_s, u"- a\n- b\n"_s, u"x.md"_s), QString());
        QCOMPARE(unifiedDiff(u"- a\n- b\n"_s, u"- a\n- b\n- ż\n"_s, u"Dziennik.md"_s),
                 u"--- a/Dziennik.md\n+++ b/Dziennik.md\n@@ -1,2 +1,3 @@\n - a\n - b\n+- ż\n"_s);
        QCOMPARE(unifiedDiff(QString(), u"- nowy\n"_s, u"n.md"_s),
                 u"--- a/n.md\n+++ b/n.md\n@@ -0,0 +1,1 @@\n+- nowy\n"_s);
        // A note that moves, one that goes.
        QCOMPARE(unifiedDiff(u"- [[Dom]]\n"_s, u"- [[Projekty/Dom]]\n"_s, u"Dom.md"_s, u"Archiwum/Dom.md"_s),
                 u"--- a/Dom.md\n+++ b/Archiwum/Dom.md\n@@ -1,1 +1,1 @@\n-- [[Dom]]\n+- [[Projekty/Dom]]\n"_s);
        QCOMPARE(unifiedDiff(u"- żółw\n"_s, QString(), u"Żółw.md"_s, u"/dev/null"_s),
                 u"--- a/Żółw.md\n+++ /dev/null\n@@ -1,1 +0,0 @@\n-- żółw\n"_s);
        // CRLF: lines without their terminators.
        QCOMPARE(unifiedDiff(u"a\r\nb\r\n"_s, u"a\r\nc\r\n"_s, u"c.md"_s),
                 u"--- a/c.md\n+++ b/c.md\n@@ -1,2 +1,2 @@\n a\n-b\n+c\n"_s);
        // Far apart: two hunks.
        QString before;
        for (int i = 0; i < 20; ++i)
            before += u"l%1\n"_s.arg(i);
        QString after = before;
        after.replace(u"l1\n"_s, u"L1\n"_s).replace(u"l18\n"_s, u"L18\n"_s);
        QCOMPARE(unifiedDiff(before, after, u"d.md"_s).count(u"@@ -"_s), 2);
    }

    // docs/CLI.md's command table lists exactly the commands of the registry.
    void docsTableMatchesTheRegistry()
    {
        QFile file(QStringLiteral(BRULION_SOURCE_DIR "/docs/CLI.md"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString docs = QString::fromUtf8(file.readAll());
        const qsizetype start = docs.indexOf(u"<!-- commands -->"_s);
        const qsizetype end = docs.indexOf(u"<!-- /commands -->"_s);
        QVERIFY(start >= 0 && end > start);
        QSet<QString> documented;
        static const QRegularExpression row(u"^\\| `([a-z]+(?: [a-z-]+)?)"_s, QRegularExpression::MultilineOption);
        for (const auto &m : row.globalMatch(docs.mid(start, end - start)))
            documented.insert(m.captured(1));
        QSet<QString> registered;
        for (const Command &c : commands())
            registered.insert(c.words().join(u' '));
        QCOMPARE(documented, registered);
    }
};

QTEST_GUILESS_MAIN(TestCliArgs)
#include "tst_cli_args.moc"
