// The documentation of extensions keeps its word: every ```luau example of docs/EXTENSIONS.md,
// docs/SYNTAX.md and the documentation notebook's pages (src/app/shell/guide, both languages) runs
// on a real notebook — a vault, its index and the NotebookHost, the user and the model played by
// the test: it loads, its commands run, its views draw and their actions work, its hooks fire —
// and the references name every function of `b` and every hook event, in both languages.
//
// An example is code as an extension page has it (statements that register commands, views and
// hooks) or as the Luau console takes it (an expression, statements with a question): both run.
// Errors name the documentation's own lines ("EXTENSIONS.md:412: …").

#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QString kExtension = u"docs"_s;

/// A notebook the examples find something in: projects (Polish and English tags), tasks with
/// dates, a reminder, tagged blocks with children and properties, counters, the pages the
/// examples name ("Tasks", "Zadania": pages of the documentation notebook), a week of daily notes.
const QHash<QString, QString> &sampleNotes()
{
    static const QHash<QString, QString> notes{
            {u"Dom.md"_s,
             u"---\ntags: [projekt, project]\nstatus: aktywny\n---\n# Dom\n\n- [ ] Zapłacić czynsz 📅 2026-11-13 #dom\n"
             "- [/] Pomalować pokój ⏳ 2026-11-15\n- Żółw je sałatę w ogrodzie #pomysł #idea\n  - szczegół pierwszy\n"_s},
            {u"Praca.md"_s, u"tags:: projekt, project\n\n- [ ] Raport kwartalny 📅 2026-11-20 ⏫\n"
                            "- [ ] Zadzwonić do klienta ⏰ 2026-11-12 09:00\n- [x] Wysłać ofertę ✅ 2026-11-10\n"_s},
            {u"Zakupy.md"_s, u"- [ ] mleko 📅 2026-11-12\n- [ ] chleb żytni\n- [x] masło ✅ 2026-11-10\n"_s},
            {u"Książki.md"_s, u"- [[Lalka]] #książka #book\n  autor:: Bolesław Prus\n- [[Solaris]] #książka #book\n"_s},
            {u"Liczniki.md"_s, u"- Szklanki wody 3 #licznik\n- Glasses of water 3 #counter\n"_s},
            {u"Tasks.md"_s,
             u"# Tasks\n\nA task is a list item with a checkbox.\n\n- [ ] An example task\n  - a detail under it\n"_s},
            {u"Zadania.md"_s,
             u"# Zadania\n\nZadanie to punkt listy z polem wyboru.\n\n- [ ] Przykładowe zadanie\n  - szczegół pod nim\n"_s},
            {u"journals/2026-11-05.md"_s, u"- Deszcz od rana, czytanie w domu\n"_s},
            {u"journals/2026-11-08.md"_s,
             u"- Wycieczka do lasu, grzyby i żółte liście\n- [x] Naprawić rower ✅ 2026-11-08\n"_s},
            {u"journals/2026-11-10.md"_s,
             u"- Spotkanie z Anią ⏰ 2026-11-12 18:00\n- Pomysł na prezent #pomysł #idea\n"_s},
            {u"journals/2026-11-11.md"_s, u"- Poranny spacer nad rzeką\n- [ ] Kupić bilety 📅 2026-11-14\n"_s},
    };
    return notes;
}

struct Example {
    QString file; ///< relative to the source directory
    int line = 0; ///< the first code line, 1-based
    QString code; ///< padded with empty lines: its line numbers are the file's
};

QString readSource(const QString &relativePath)
{
    QFile file(QStringLiteral(BRULION_SOURCE_DIR) + u'/' + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

/// Every ```luau (or ```lua) block of the file, also one inside a ````markdown example.
QList<Example> examplesIn(const QString &relativePath)
{
    static const QRegularExpression opening(uR"(^\s*(`{3,}|~{3,})\s*(?:luau|lua)\s*$)"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    QList<Example> out;
    const QStringList lines = readSource(relativePath).split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = opening.match(lines[i]);
        if (!m.hasMatch())
            continue;
        const QString fence = m.captured(1);
        QStringList body;
        qsizetype j = i + 1;
        for (; j < lines.size(); ++j) {
            const QString t = lines[j].trimmed();
            if (t.size() >= fence.size() && t.count(fence[0]) == t.size())
                break;
            body << lines[j];
        }
        out.append({relativePath, int(i + 2), QString(i + 1, u'\n') + body.join(u'\n')});
        i = j;
    }
    return out;
}

/// The documentation's files with Luau in them: the developer documents and the documentation
/// notebook's pages of `language` ("" = both).
QStringList documents(const QString &language = QString())
{
    QStringList out;
    if (language.isEmpty())
        out << u"docs/EXTENSIONS.md"_s << u"docs/SYNTAX.md"_s;
    for (const QString &lang : {u"en"_s, u"pl"_s}) {
        if (!language.isEmpty() && lang != language)
            continue;
        const QString folder = u"src/app/shell/guide/"_s + lang;
        QStringList pages;
        QDirIterator it(QStringLiteral(BRULION_SOURCE_DIR) + u'/' + folder, {u"*.md"_s}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            pages << folder + u'/' + it.fileName();
        }
        pages.sort();
        out << pages;
    }
    return out;
}

/// What a model would answer: a turn that calls every tool the request offers (its arguments
/// made from the tool's schema) until the conversation has their results, then a text.
QVariantMap modelReply(const QVariantMap &request)
{
    const QVariantList tools = request.value(u"tools"_s).toList();
    bool answered = false;
    for (const QVariant &m : request.value(u"messages"_s).toList())
        answered = answered || m.toMap().contains(u"results"_s);
    QVariantMap reply{
            {u"model"_s, u"test-model"_s}, {u"inputTokens"_s, 12}, {u"outputTokens"_s, 7}, {u"raw"_s, u"[]"_s}};
    if (tools.isEmpty() || answered) {
        reply.insert(u"text"_s, u"Odpowiedź modelu.\n- punkt pierwszy\n- punkt drugi"_s);
        reply.insert(u"stopReason"_s, u"end_turn"_s);
        return reply;
    }
    QVariantList calls;
    for (const QVariant &t : tools) {
        const QVariantMap tool = t.toMap();
        QVariantMap input;
        const QVariantMap properties = tool.value(u"input"_s).toMap().value(u"properties"_s).toMap();
        for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
            const QVariantMap property = it.value().toMap();
            const QString type = property.value(u"type"_s).toString();
            const QVariantList choices = property.value(u"enum"_s).toList();
            if (!choices.isEmpty())
                input.insert(it.key(), choices.first());
            else if (type == u"integer" || type == u"number")
                input.insert(it.key(), 3);
            else if (type == u"boolean")
                input.insert(it.key(), true);
            else
                input.insert(it.key(), u"mleko"_s);
        }
        calls << QVariantMap{
                {u"id"_s, u"call-%1"_s.arg(calls.size() + 1)}, {u"name"_s, tool.value(u"name"_s)}, {u"input"_s, input}};
    }
    reply.insert(u"text"_s, QString());
    reply.insert(u"stopReason"_s, u"tool_use"_s);
    reply.insert(u"toolCalls"_s, calls);
    return reply;
}

/// The nodes of a view's tree that call an action (button, check, stepper), one per action name.
void collectActions(const QVariant &node, QMap<QString, QVariantMap> *actions)
{
    const QVariantMap map = node.toMap();
    const QString action = map.value(u"action"_s).toString();
    if (!action.isEmpty() && !actions->contains(action))
        actions->insert(action, map);
    for (const QVariant &child : map.value(u"children"_s).toList())
        collectActions(child, actions);
}

} // namespace

class TestExtensionsDocs : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    /// What the examples made this test do, all of them together.
    struct Totals {
        int pages = 0, console = 0, commands = 0, views = 0, actions = 0, hooks = 0, questions = 0, modelRequests = 0;
    } m_totals;

    /// A fresh notebook and engine, `source` loaded as the extension "docs" (every permission).
    Result open(const QString &source, const QString &name, const QString &language)
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        const QHash<QString, QString> &files = sampleNotes();
        const QString dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly))
                return {Status::Error, {}, u"cannot write "_s + file};
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return {Status::Error, {}, u"cannot open the notebook"_s};
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 11), QTime(18, 30)); };
        options.language = language;
        options.uiLanguage = language;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return {Status::Error, {}, u"cannot start the engine"_s};
        Extension extension;
        extension.id = kExtension;
        extension.source = source;
        extension.permissions = {u"ai"_s, u"write"_s};
        extension.name = name;
        return m_engine->load(extension);
    }

    /// Answers what a call waits for until it ends: a question as a user would (after a dozen
    /// answers: cancelled, so a command that asks in a loop ends), an AI request as a model.
    /// `mayAsk`: a view and a hook never ask a question (a hook may ask the model).
    Result drive(Result r, bool mayAsk, bool mayAskAi = true)
    {
        for (int answers = 0; r.status == Status::Suspended; ++answers) {
            const QVariantMap question = r.value.toMap();
            const QString kind = question.value(u"kind"_s).toString();
            if (answers > 60 || (kind == u"ai" ? !mayAskAi : !mayAsk)) {
                m_engine->cancel(r.call);
                return {Status::Error, {}, u"it waits for b.%1 where nothing can answer"_s.arg(kind)};
            }
            const bool cancel = answers >= 12;
            ++(kind == u"ai" ? m_totals.modelRequests : m_totals.questions);
            QVariant answer;
            if (kind == u"ai") {
                answer = modelReply(question.value(u"request"_s).toMap());
            } else if (kind == u"ask") {
                answer = cancel ? QString() : u"Mleko i chleb"_s;
            } else if (kind == u"choose") {
                const QStringList options = question.value(u"options"_s).toStringList();
                if (!cancel && !options.isEmpty())
                    answer = options.first();
            } else if (kind == u"form" && !cancel) {
                QVariantMap values;
                for (const QVariant &f : question.value(u"fields"_s).toList())
                    values.insert(f.toMap().value(u"key"_s).toString(), f.toMap().value(u"value"_s));
                answer = values;
            }
            r = m_engine->resume(r.call, answer);
        }
        return r;
    }

    /// Runs everything the loaded example registered; "" when all of it worked.
    QString exercise()
    {
        const QString page = u"Zakupy"_s, path = u"Zakupy.md"_s;
        for (const CommandInfo &c : m_engine->commands()) {
            QVariantMap ctx{{u"page"_s, page}, {u"selection"_s, QString()}};
            if (c.needsBlock)
                ctx.insert(u"block"_s, m_host->blockAt(path, 0));
            const Result r = drive(m_engine->runCommand(kExtension, c.name, ctx), true);
            if (!r.ok())
                return u"command %1: %2"_s.arg(c.name, r.error);
            ++m_totals.commands;
        }
        for (const ViewInfo &v : m_engine->views()) {
            Result r = drive(m_engine->render(kExtension, v.name, {}), false, false);
            if (!r.ok())
                return u"view %1: %2"_s.arg(v.name, r.error);
            if (r.value.toMap().value(u"type"_s).toString().isEmpty())
                return u"view %1 returns no b.ui node"_s.arg(v.name);
            QMap<QString, QVariantMap> actions;
            collectActions(r.value, &actions);
            for (const QString &action : v.actions) {
                if (!actions.contains(action))
                    continue; // nothing on screen calls it with this notebook
                const QVariantMap node = actions.value(action);
                QVariantMap args = node.value(u"args"_s).toMap();
                if (node.value(u"type"_s) == u"stepper"_s)
                    args.insert(u"value"_s, node.value(u"value"_s).toDouble() + node.value(u"step"_s, 1).toDouble());
                r = drive(m_engine->act(kExtension, v.name, action, args), true);
                if (!r.ok())
                    return u"view %1, action %2: %3"_s.arg(v.name, action, r.error);
                ++m_totals.actions;
            }
            for (auto it = actions.cbegin(); it != actions.cend(); ++it)
                if (!v.actions.contains(it.key()))
                    return u"view %1: a node calls the action %2, which the view does not have"_s.arg(v.name, it.key());
            r = drive(m_engine->render(kExtension, v.name, {}), false, false);
            if (!r.ok())
                return u"view %1 after its actions: %2"_s.arg(v.name, r.error);
            ++m_totals.views;
        }
        for (const HookInfo &h : m_engine->hooks()) {
            const QVariantMap event = h.event == u"save" ? QVariantMap{{u"page"_s, page}, {u"path"_s, path}}
                                                         : QVariantMap{{u"pages"_s, QStringList{page, u"Dom"_s}}};
            const Result r = drive(m_engine->runHook(h.id, event), false);
            if (!r.ok())
                return u"hook %1: %2"_s.arg(h.event, r.error);
            ++m_totals.hooks;
        }
        return {};
    }

private Q_SLOTS:
    void findsTheExamples()
    {
        // The documents are where the test looks for them, and each kind has examples.
        QVERIFY2(!readSource(u"docs/EXTENSIONS.md"_s).isEmpty(), BRULION_SOURCE_DIR);
        QVERIFY(examplesIn(u"docs/EXTENSIONS.md"_s).size() >= 10);
        QVERIFY(!examplesIn(u"docs/SYNTAX.md"_s).isEmpty());
        // A fence inside a ````markdown example counts; its line numbers are the file's.
        const QList<Example> syntax = examplesIn(u"docs/SYNTAX.md"_s);
        const QStringList lines = readSource(u"docs/SYNTAX.md"_s).split(u'\n');
        for (const Example &e : syntax) {
            const QStringList code = e.code.split(u'\n');
            QCOMPARE(code.value(e.line - 1), lines.value(e.line - 1));
            QVERIFY(lines.value(e.line - 2).trimmed().endsWith(u"lua"_s)
                    || lines.value(e.line - 2).trimmed().endsWith(u"luau"_s));
        }
    }

    void bothLanguagesHaveTheSameExamples()
    {
        // The documentation notebook is written twice (docs: "change both languages together").
        int english = 0, polish = 0;
        for (const QString &file : documents(u"en"_s))
            english += int(examplesIn(file).size());
        for (const QString &file : documents(u"pl"_s))
            polish += int(examplesIn(file).size());
        QVERIFY(english >= 10);
        QCOMPARE(polish, english);
    }

    void everyExampleRuns_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("code");
        for (const QString &file : documents()) {
            for (const Example &e : examplesIn(file))
                QTest::newRow(qPrintable(u"%1:%2"_s.arg(e.file).arg(e.line))) << e.file << e.line << e.code;
        }
    }
    void everyExampleRuns()
    {
        QFETCH(QString, file);
        QFETCH(QString, code);
        const QString name = file.section(u'/', -1);
        // The Polish pages' examples write Polish notes, as in a notebook of that language.
        const QString language = file.contains(u"/guide/pl/"_s) ? u"pl"_s : u"en"_s;
        // As an extension page's code: it loads, and what it registered works.
        Result loaded = open(code, name, language);
        if (loaded.ok()) {
            const QString failure = exercise();
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
            ++m_totals.pages;
            return;
        }
        // Else as the console takes it: an expression, or statements that ask at once.
        const Result empty = open(QString(), name, language);
        QVERIFY2(empty.ok(), qPrintable(empty.error));
        const Result r = drive(m_engine->evaluate(kExtension, code, name), true);
        QVERIFY2(r.ok(), qPrintable(u"as a page: %1\nin the console: %2"_s.arg(loaded.error, r.error)));
        const QString failure = exercise();
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        ++m_totals.console;
    }

    void theExamplesShowEverything()
    {
        // Together the examples show every kind of thing at work, and the test above really did
        // it: a change that makes it find no code, or run none of it, fails here.
        const Totals &t = m_totals;
        if (t.pages + t.console == 0)
            QSKIP("everyExampleRuns did not run in this process");
        QStringList parts;
        const QList<std::pair<int, QString>> counts{
                {t.pages, u"examples as pages"_s}, {t.console, u"in the console"_s},
                {t.commands, u"commands"_s},       {t.views, u"views"_s},
                {t.actions, u"actions"_s},         {t.hooks, u"hooks"_s},
                {t.questions, u"questions"_s},     {t.modelRequests, u"requests to a model"_s}};
        for (const auto &[count, what] : counts)
            parts << u"%1 %2"_s.arg(count).arg(what);
        const QString summary = parts.join(u", "_s);
        qInfo().noquote() << summary;
        QVERIFY2(t.commands >= 15 && t.views >= 5 && t.actions >= 3 && t.hooks >= 2 && t.questions >= 6
                         && t.modelRequests >= 4,
                 qPrintable(summary));
    }

    void referencesNameEverything_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("developer") << u"docs/EXTENSIONS.md"_s;
        QTest::newRow("en") << u"src/app/shell/guide/en/Extension API.md"_s;
        QTest::newRow("pl") << u"src/app/shell/guide/pl/API rozszerzeń.md"_s;
    }
    void referencesNameEverything()
    {
        // A function added to `b` (or to b.ui), or a new hook event, fails here until each
        // reference names it: `b.search`, `b.ui.chart`, "change".
        QFETCH(QString, file);
        const QString content = readSource(file);
        QVERIFY2(!content.isEmpty(), qPrintable(file));
        const Result empty = open(QString(), u"docs"_s, u"en"_s);
        QVERIFY2(empty.ok(), qPrintable(empty.error));
        const Result names = m_engine->evaluate(kExtension, uR"lua(
            local names = { "print" }
            for name, value in b do
                if type(value) == "table" then
                    for inner in value do
                        table.insert(names, "b." .. name .. "." .. inner)
                    end
                else
                    table.insert(names, "b." .. name)
                end
            end
            table.sort(names)
            return names
        )lua"_s,
                                                u"names"_s);
        QVERIFY2(names.ok(), qPrintable(names.error));
        const QStringList all = names.value.toStringList();
        QVERIFY2(all.contains(u"b.replaceBlock"_s) && all.contains(u"b.ui.stepper"_s) && all.size() >= 40,
                 qPrintable(all.join(u' ')));
        QStringList missing;
        for (const QString &name : all) {
            const QRegularExpression mention(u"(?<![A-Za-z0-9_.])"_s + QRegularExpression::escape(name)
                                             + u"(?![A-Za-z0-9_])"_s);
            if (!mention.match(content).hasMatch())
                missing << name;
        }
        for (const QString &event : Engine::hookEvents()) {
            if (!content.contains(u'"' + event + u'"'))
                missing << u"the event \"%1\""_s.arg(event);
        }
        QVERIFY2(missing.isEmpty(), qPrintable(file + u" does not name: "_s + missing.join(u", "_s)));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsDocs)
#include "tst_extensions_docs.moc"
