// The built-in AI assistant (src/core/extensions/builtin/asystent.luau) as an agent on a real
// notebook: a vault, its index and the NotebookHost, the model played by the test. The host's
// full-text search and queries (b.search, b.query), the tools' requests (valid for ai::requestFrom
// and both providers), a question answered from a query's results, a change made with the user's
// yes, one declined, "yes to every change" asked once, an extension written (the reference read,
// the page checked, refused while it does not load, written with the user's yes), an unknown
// tool, too many turns, and the summary without tools.

#include "ai/ai.h"
#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QHash<QString, QString> &sampleNotes()
{
    static const QHash<QString, QString> notes{
            {u"Dom.md"_s,
             u"- [ ] Zapłacić czynsz 📅 2026-11-13\n- [x] Wymienić żarówkę\n- Żółw je sałatę w ogrodzie\n"_s},
            {u"Zakupy.md"_s, u"- chleb\n"_s},
            {u"journals/2026-11-10.md"_s, u"- Spotkanie z Anią ⏰ 2026-11-12 18:00\n"_s},
    };
    return notes;
}

/// A model's turn calling tools: {text, raw, toolCalls}.
QVariantMap calling(const QVariantList &calls)
{
    return {{u"text"_s, QString()}, {u"raw"_s, u"[]"_s}, {u"toolCalls"_s, calls}};
}

QVariantMap call(const QString &id, const QString &name, const QVariantMap &input)
{
    return {{u"id"_s, id}, {u"name"_s, name}, {u"input"_s, input}};
}

/// A model's last turn: its answer.
QVariantMap answering(const QString &text)
{
    return {{u"text"_s, text}, {u"raw"_s, u"[]"_s}};
}

/// The tool results of a request's last message: {id, content, isError}.
QVariantList lastResults(const Result &r)
{
    const QVariantList messages = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList();
    return messages.isEmpty() ? QVariantList() : messages.last().toMap().value(u"results"_s).toList();
}

} // namespace

class TestExtensionsAssistant : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;

    bool open(const QHash<QString, QString> &files)
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        const QString dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return false;
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 11), QTime(18, 30)); };
        options.language = u"pl"_s;
        options.uiLanguage = u"pl"_s;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return false;
        const Result loaded = m_engine->load(builtinExtension(u"asystent"_s));
        if (!loaded.ok())
            qWarning("%s", qPrintable(loaded.error));
        return loaded.ok();
    }

    QString file(const QString &path) const { return m_vault->readText(path); }

    /// "ask" with `question` (and the page on screen): the call waiting for the model's first turn.
    Result ask(const QString &question, const QString &page = QString())
    {
        Result r = m_engine->runCommand(u"asystent"_s, u"ask"_s, {{u"page"_s, page}});
        if (r.status != Status::Suspended || r.value.toMap().value(u"kind"_s) != u"ask"_s)
            return r;
        return m_engine->resume(r.call, question);
    }

private Q_SLOTS:
    void searchAndQuery()
    {
        QVERIFY(open(sampleNotes()));
        // Full-text search, diacritics optional: the block holding the line, its ref.
        const QVariantList hits = m_host->search(u"zolw salata"_s, 10);
        QCOMPARE(hits.size(), 1);
        const QVariantMap hit = hits[0].toMap();
        QCOMPARE(hit.value(u"page"_s).toString(), u"Dom"_s);
        QCOMPARE(hit.value(u"line"_s).toInt(), 3);
        QCOMPARE(hit.value(u"text"_s).toString(), u"Żółw je sałatę w ogrodzie"_s);
        QVERIFY2(hit.value(u"ref"_s).toString().startsWith(u"Dom.md#L"_s), qPrintable(hit.value(u"ref"_s).toString()));
        QVERIFY(m_host->search(u"nic takiego"_s, 10).isEmpty());
        // Queries: an open task with its date, a reminder of a daily note, pages.
        QString error;
        QVariantMap result = m_host->query(u"type:tasks status:open due:<=+7d"_s, 50, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.value(u"total"_s).toInt(), 1);
        QVariantMap item = result.value(u"items"_s).toList().value(0).toMap();
        QCOMPARE(item.value(u"page"_s).toString(), u"Dom"_s);
        QCOMPARE(item.value(u"task"_s).toString(), u" "_s);
        QCOMPARE(item.value(u"due"_s).toString(), u"2026-11-13"_s);
        QVERIFY(item.value(u"text"_s).toString().startsWith(u"Zapłacić czynsz"_s));
        QVERIFY(!item.contains(u"children"_s));
        result = m_host->query(u"type:reminders"_s, 50, &error);
        item = result.value(u"items"_s).toList().value(0).toMap();
        QCOMPARE(item.value(u"reminder"_s).toString(), u"2026-11-12 18:00"_s);
        QCOMPARE(item.value(u"date"_s).toString(), u"2026-11-10"_s);
        result = m_host->query(u"type:pages"_s, 1, &error);
        QCOMPARE(result.value(u"total"_s).toInt(), 3);
        QCOMPARE(result.value(u"items"_s).toList().size(), 1); // the limit
        QVERIFY(m_host->query(u"status:kiedyś"_s, 50, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void answersFromTheNotes()
    {
        QVERIFY(open(sampleNotes()));
        const QString question = u"Co mam do zrobienia w tym tygodniu?"_s;
        Result r = ask(question);
        QCOMPARE(r.status, Status::Suspended);
        QVariantMap request = r.value.toMap().value(u"request"_s).toMap();
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QCOMPARE(request.value(u"tools"_s).toList().size(), 11);
        QVERIFY(request.value(u"system"_s).toString().contains(u"2026-11-11"_s));
        QCOMPARE(request.value(u"messages"_s).toList().value(0).toMap().value(u"content"_s).toString(), question);
        // The script's tools are a request both providers take.
        QString error;
        std::optional<ai::Request> made = ai::requestFrom(request, &error);
        QVERIFY2(made, qPrintable(error));
        ai::Settings claude;
        claude.apiKey = u"k"_s;
        QVERIFY2(ai::httpRequest(claude, *made, &error), qPrintable(error));
        ai::Settings own;
        own.provider = u"openai"_s;
        own.model = u"qwen3"_s;
        QVERIFY2(ai::httpRequest(own, *made, &error), qPrintable(error));
        for (const ai::Tool &tool : made->tools)
            QVERIFY2(tool.schema.value(u"properties"_s).isObject(), qPrintable(tool.name));

        // The model queries the tasks; the result names the task, its date and its ref.
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"query_notes"_s,
                                                   {{u"query"_s, u"type:tasks status:open due:<=+7d"_s}})}));
        QCOMPARE(r.status, Status::Suspended);
        const QVariantList messages = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList();
        QCOMPARE(messages.size(), 3);
        QCOMPARE(messages[1].toMap().value(u"raw"_s).toString(), u"[]"_s);
        QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].toMap().value(u"id"_s).toString(), u"c1"_s);
        const QString found = results[0].toMap().value(u"content"_s).toString();
        QVERIFY2(found.contains(u"[[Dom]]"_s) && found.contains(u"Zapłacić czynsz"_s)
                         && found.contains(u"due 2026-11-13"_s) && found.contains(u"ref Dom.md#L"_s),
                 qPrintable(found));
        QVERIFY(!results[0].toMap().value(u"isError"_s).toBool());
        // It searches and reads in one turn: both results in one message.
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"search_notes"_s, {{u"query"_s, u"żółw"_s}}),
                                              call(u"c3"_s, u"read_page"_s, {{u"name"_s, u"Zakupy"_s}})}));
        results = lastResults(r);
        QCOMPARE(results.size(), 2);
        QVERIFY(results[0].toMap().value(u"content"_s).toString().contains(u"Żółw je sałatę"_s));
        QCOMPARE(results[1].toMap().value(u"content"_s).toString(), u"- chleb\n"_s);
        // The answer goes into today's note, under the question.
        r = m_engine->resume(r.call, answering(u"W tym tygodniu:\n- [[Dom]]: zapłacić czynsz do piątku."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Odpowiedź AI jest w dzisiejszej notatce."_s);
        const QString journal = file(u"journals/2026-11-11.md"_s);
        QVERIFY2(
                journal.contains(
                        u"- 🤖 Co mam do zrobienia w tym tygodniu?\n  - W tym tygodniu:\n  - [[Dom]]: zapłacić czynsz do piątku."_s),
                qPrintable(journal));
        QVERIFY(!journal.contains(u"✏️"_s));
        QCOMPARE(file(u"Dom.md"_s), sampleNotes().value(u"Dom.md"_s));
    }

    void changesWithTheUsersYes()
    {
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Dopisz mleko do zakupów"_s, u"Zakupy"_s);
        // The page on screen goes along.
        QVERIFY(r.value.toMap()
                        .value(u"request"_s)
                        .toMap()
                        .value(u"messages"_s)
                        .toList()
                        .value(0)
                        .toMap()
                        .value(u"content"_s)
                        .toString()
                        .contains(u"[[Zakupy]]"_s));
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"append_to_page"_s,
                                                   {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}})}));
        // Asked first, with what would be written.
        QCOMPARE(r.status, Status::Suspended);
        QVariantMap question = r.value.toMap();
        QCOMPARE(question.value(u"kind"_s).toString(), u"choose"_s);
        QVERIFY2(question.value(u"prompt"_s).toString().contains(u"„Zakupy”"_s)
                         && question.value(u"prompt"_s).toString().contains(u"- mleko"_s),
                 qPrintable(question.value(u"prompt"_s).toString()));
        const QStringList options = question.value(u"options"_s).toStringList();
        QCOMPARE(options.size(), 3);
        QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n"_s);
        r = m_engine->resume(r.call, options[0]);
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(lastResults(r).value(0).toMap().value(u"content"_s).toString(), u"Added to [[Zakupy]]."_s);
        QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        r = m_engine->resume(r.call, answering(u"Dopisałem mleko do [[Zakupy]]."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QString journal = file(u"journals/2026-11-11.md"_s);
        QVERIFY2(
                journal.contains(
                        u"- 🤖 Dopisz mleko do zakupów ([[Zakupy]])\n  - Dopisałem mleko do [[Zakupy]].\n  - ✏️ Zmienione: [[Zakupy]]"_s),
                qPrintable(journal));
    }

    void aDeclinedChangeIsNotMade()
    {
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Wyczyść stronę Dom"_s);
        r = m_engine->resume(
                r.call, calling({call(u"c1"_s, u"write_page"_s, {{u"name"_s, u"Dom"_s}, {u"content"_s, u"nic"_s}})}));
        const QVariantMap question = r.value.toMap();
        QCOMPARE(question.value(u"kind"_s).toString(), u"choose"_s);
        QVERIFY2(question.value(u"prompt"_s).toString().contains(u"zastąpić"_s),
                 qPrintable(question.value(u"prompt"_s).toString()));
        r = m_engine->resume(r.call, question.value(u"options"_s).toStringList().value(2));
        const QVariantMap result = lastResults(r).value(0).toMap();
        QCOMPARE(result.value(u"content"_s).toString(), u"The user declined this change."_s);
        QVERIFY(result.value(u"isError"_s).toBool());
        // Cancelled (the dialog closed) is a no too.
        r = m_engine->resume(
                r.call, calling({call(u"c2"_s, u"write_page"_s, {{u"name"_s, u"Dom"_s}, {u"content"_s, u"nic"_s}})}));
        r = m_engine->resume(r.call, QVariant());
        QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
        r = m_engine->resume(r.call, answering(u"Nie zmieniłem strony."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(file(u"Dom.md"_s), sampleNotes().value(u"Dom.md"_s));
        QVERIFY(!file(u"journals/2026-11-11.md"_s).contains(u"✏️"_s));
    }

    void yesToEveryChangeIsAskedOnce()
    {
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref = m_host->query(u"type:tasks status:open"_s, 5, &error)
                                    .value(u"items"_s)
                                    .toList()
                                    .value(0)
                                    .toMap()
                                    .value(u"ref"_s)
                                    .toString();
        QVERIFY(!ref.isEmpty());
        Result r = ask(u"Odhacz czynsz i zapisz w dzienniku, że zapłacony"_s);
        r = m_engine->resume(
                r.call,
                calling({call(u"c1"_s, u"update_block"_s,
                              {{u"ref"_s, ref}, {u"text"_s, u"Zapłacić czynsz"_s}, {u"status"_s, u"done"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        QVERIFY2(r.value.toMap().value(u"prompt"_s).toString().contains(u"„Dom”"_s),
                 qPrintable(r.value.toMap().value(u"prompt"_s).toString()));
        r = m_engine->resume(r.call,
                             r.value.toMap().value(u"options"_s).toStringList().value(1)); // yes to every change
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVERIFY2(
                lastResults(r).value(0).toMap().value(u"content"_s).toString().startsWith(u"Changed; its ref is now"_s),
                qPrintable(lastResults(r).value(0).toMap().value(u"content"_s).toString()));
        QCOMPARE(file(u"Dom.md"_s).split(u'\n').value(0), u"- [x] Zapłacić czynsz 📅 2026-11-13 ✅ 2026-11-11"_s);
        // The next change is not asked again.
        r = m_engine->resume(
                r.call, calling({call(u"c2"_s, u"append_to_journal"_s, {{u"markdown"_s, u"- Czynsz zapłacony"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVERIFY(file(u"journals/2026-11-11.md"_s).contains(u"- Czynsz zapłacony"_s));
        // A stale ref is refused (its line changed): an error for the model, nothing written.
        r = m_engine->resume(r.call,
                             calling({call(u"c3"_s, u"update_block"_s, {{u"ref"_s, ref}, {u"text"_s, u"Inaczej"_s}})}));
        QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
        QVERIFY(!file(u"Dom.md"_s).contains(u"Inaczej"_s));
        r = m_engine->resume(r.call, answering(u"Gotowe."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(file(u"journals/2026-11-11.md"_s).contains(u"✏️ Zmienione: [[Dom]], [[2026-11-11]]"_s),
                 qPrintable(file(u"journals/2026-11-11.md"_s)));
    }

    void writesAnExtension()
    {
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Napisz polecenie, które policzy otwarte zadania"_s);
        // The reference: its sections, one of them, a heading no section has.
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"extension_reference"_s, {}),
                                              call(u"c2"_s, u"extension_reference"_s, {{u"section"_s, u"hooks"_s}}),
                                              call(u"c3"_s, u"extension_reference"_s, {{u"section"_s, u"żółw"_s}})}));
        QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 3);
        const QString sections = results[0].toMap().value(u"content"_s).toString();
        QVERIFY2(sections.contains(u"\n- Quick start\n"_s) && sections.contains(u"\n  - Reading\n"_s), qPrintable(sections));
        // Also those after a line that starts with code in prose (```` ```luau ````): no fence.
        QVERIFY2(sections.contains(u"\n- Troubleshooting\n"_s) && sections.contains(u"\n- Tests"_s), qPrintable(sections));
        const QString hooks = results[1].toMap().value(u"content"_s).toString();
        QVERIFY2(hooks.startsWith(u"## Hooks\n"_s) && hooks.contains(u"b.on(\"change\""_s), qPrintable(hooks.left(200)));
        QVERIFY(!hooks.contains(u"## Questions"_s));
        QVERIFY(!results[1].toMap().value(u"isError"_s).toBool());
        QVERIFY(results[2].toMap().value(u"isError"_s).toBool());
        // A check: the error names the page's line; nothing is asked, nothing written.
        const QString broken = u"---\ntags: [rozszerzenie]\n---\nLiczy zadania.\n\n```luau\nlocal n = = 1\n```\n"_s;
        r = m_engine->resume(r.call, calling({call(u"c4"_s, u"check_extension"_s,
                                                   {{u"name"_s, u"Licznik zadań"_s}, {u"content"_s, broken}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVariantMap result = lastResults(r).value(0).toMap();
        QVERIFY(result.value(u"isError"_s).toBool());
        QVERIFY2(result.value(u"content"_s).toString().contains(u"line 7 of the page"_s),
                 qPrintable(result.value(u"content"_s).toString()));
        // Code that does not load is never written; a page that is no extension is never replaced.
        r = m_engine->resume(r.call, calling({call(u"c5"_s, u"write_extension"_s,
                                                   {{u"name"_s, u"Licznik zadań"_s}, {u"content"_s, broken}}),
                                              call(u"c6"_s, u"write_extension"_s,
                                                   {{u"name"_s, u"Dom"_s}, {u"content"_s, broken}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        results = lastResults(r);
        QVERIFY(results[0].toMap().value(u"content"_s).toString().startsWith(u"Not written."_s));
        QVERIFY(results[1].toMap().value(u"content"_s).toString().contains(u"no extension"_s));
        QVERIFY(!QFile::exists(m_vault->absolutePath(u"Rozszerzenia/Licznik zadań.md"_s)));
        // Working code: checked, then written into Rozszerzenia/ with the user's yes.
        const QString page = u"---\ntags: [rozszerzenie]\nopis: Liczy otwarte zadania.\n---\nLiczy zadania.\n\n```luau\n"
                             "b.command(\"policz\", function()\n"
                             "    return `Otwarte: {b.query(\"type:tasks status:open\").total}`\n"
                             "end, { label = \"Policz zadania\" })\n```\n"_s;
        r = m_engine->resume(r.call, calling({call(u"c7"_s, u"write_extension"_s,
                                                   {{u"name"_s, u"Licznik zadań"_s}, {u"content"_s, page}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        const QString prompt = r.value.toMap().value(u"prompt"_s).toString();
        QVERIFY2(prompt.contains(u"rozszerzenie „Rozszerzenia/Licznik zadań”"_s) && prompt.contains(u"Uruchom"_s),
                 qPrintable(prompt));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        result = lastResults(r).value(0).toMap();
        QVERIFY(!result.value(u"isError"_s).toBool());
        QVERIFY2(result.value(u"content"_s).toString().startsWith(u"Wrote the extension [[Rozszerzenia/Licznik zadań]]."_s)
                         && result.value(u"content"_s).toString().contains(u"command policz \"Policz zadania\""_s),
                 qPrintable(result.value(u"content"_s).toString()));
        QCOMPARE(file(u"Rozszerzenia/Licznik zadań.md"_s), page);
        r = m_engine->resume(r.call, answering(u"Napisałem [[Licznik zadań]]: naciśnij Uruchom na jego stronie."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(file(u"journals/2026-11-11.md"_s).contains(u"✏️ Zmienione: [[Rozszerzenia/Licznik zadań]]"_s),
                 qPrintable(file(u"journals/2026-11-11.md"_s)));
    }

    void unknownToolsAndTooManyTurns()
    {
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Zrób coś"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"usun_wszystko"_s, {}),
                                              call(u"c2"_s, u"read_page"_s, {{u"name"_s, u"Brak"_s}})}));
        const QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 2);
        QVERIFY(results[0].toMap().value(u"content"_s).toString().contains(u"No tool named"_s));
        QVERIFY(results[0].toMap().value(u"isError"_s).toBool());
        QVERIFY(results[1].toMap().value(u"isError"_s).toBool());
        // A model that never stops calling tools: sixteen turns, then the question ends.
        for (int turn = 2; turn <= 16 && r.status == Status::Suspended; ++turn)
            r = m_engine->resume(r.call, calling({call(u"c%1"_s.arg(turn), u"list_pages"_s, {})}));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(r.value.toString().contains(u"16"_s), qPrintable(r.value.toString()));
        QVERIFY(file(u"journals/2026-11-11.md"_s).contains(u"- 🤖 Zrób coś\n  - …"_s));
    }

    void summaryWithoutTools()
    {
        QVERIFY(open(sampleNotes()));
        Result r = m_engine->runCommand(u"asystent"_s, u"summarize"_s, {{u"page"_s, u"Dom"_s}});
        QCOMPARE(r.status, Status::Suspended);
        const QVariantMap request = r.value.toMap().value(u"request"_s).toMap();
        QVERIFY(!request.contains(u"tools"_s));
        QVERIFY(request.value(u"prompt"_s).toString().contains(u"Zapłacić czynsz"_s));
        r = m_engine->resume(r.call, answering(u"Dom: czynsz do zapłacenia."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(file(u"journals/2026-11-11.md"_s)
                        .contains(u"- 🤖 Streszczenie [[Dom]]\n  - Dom: czynsz do zapłacenia."_s));
        // No page on screen: said, nothing asked.
        QCOMPARE(m_engine->runCommand(u"asystent"_s, u"summarize"_s, {}).value.toString(),
                 u"Otwórz stronę, którą streścić."_s);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsAssistant)
#include "tst_extensions_assistant.moc"
