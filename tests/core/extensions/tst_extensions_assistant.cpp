// The built-in AI assistant (src/core/extensions/builtin/asystent.luau) as an agent on a real
// notebook: a vault, its index and the NotebookHost, the model played by the test, a turn of a
// conversation ("chat": ctx.messages, ctx.page; its answer the command's value). The host's
// full-text search and queries (b.search, b.query), the tools' requests (valid for ai::requestFrom
// and both providers), a question answered from a query's results, a conversation that goes on
// (its history in the request, the page it is about before the first message), what it says it
// does (b.status), a change made with the user's yes, one declined, "yes to every change" asked
// once, an extension written (the reference read, the page checked, refused while it does not
// load, written with the user's yes), a picture looked at (a request of its own with the picture,
// its answer the tool's result), an unknown tool, too many turns, the command the palette does not
// offer and its run by hand.

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
    QStringList m_statuses; // b.status, in order

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
        m_statuses.clear();
        m_host->onStatus = [this](const QString &, const QString &text) { m_statuses.append(text); };
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return false;
        const Result loaded = m_engine->load(builtinExtension(u"asystent"_s));
        if (!loaded.ok())
            qWarning("%s", qPrintable(loaded.error));
        return loaded.ok();
    }

    QString file(const QString &path) const { return m_vault->readText(path); }

    /// A turn of a conversation (`earlier`: its messages before, {role, content}) that ends with
    /// `question`, about `page`: the call waiting for the model's first turn.
    Result ask(const QString &question, const QString &page = QString(), const QVariantList &earlier = {},
               const QString &conversation = QString())
    {
        QVariantList messages = earlier;
        messages.append(QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, question}});
        return m_engine->runCommand(u"asystent"_s, u"chat"_s,
                                    {{u"page"_s, page}, {u"messages"_s, messages}, {u"conversation"_s, conversation}});
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
        QCOMPARE(request.value(u"tools"_s).toList().size(), 12);
        // The day is in the first message: the system text is the same every day (a model on the
        // device keeps what it read of it on disk).
        QVERIFY(!request.value(u"system"_s).toString().contains(u"2026-11-11"_s));
        QCOMPARE(request.value(u"messages"_s).toList().value(0).toMap().value(u"content"_s).toString(),
                 u"Dziś jest 2026-11-11.\n\n"_s + question);
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
        // The answer is the command's value, as the model wrote it: the app writes it into the
        // conversation. Nothing else was written.
        r = m_engine->resume(r.call, answering(u"  W tym tygodniu:\n\n- [[Dom]]: zapłacić czynsz do piątku.\n"_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"W tym tygodniu:\n\n- [[Dom]]: zapłacić czynsz do piątku."_s);
        QVERIFY(!m_vault->exists(u"journals/2026-11-11.md"_s));
        QCOMPARE(file(u"Dom.md"_s), sampleNotes().value(u"Dom.md"_s));
        // What it did, said as it did it; nothing while the model thinks.
        QCOMPARE(m_statuses, (QStringList{u""_s, u"Przeglądam notatki: type:tasks status:open due:<=+7d…"_s, u""_s,
                                          u"Szukam w notatkach: „żółw”…"_s, u"Czytam „Zakupy”…"_s, u""_s, u""_s}));
    }

    void theConversationIsNoFind()
    {
        // The conversation's own page matches its question word for word: a small model read it
        // instead of the notes. Its tools leave it out — the search, the list, a query —, and
        // the day's line that only links to it; reading it is refused.
        QHash<QString, QString> notes = sampleNotes();
        const QString conversation = u"AI/2026-11-11 Co z żółwiem"_s;
        notes.insert(conversation + u".md"_s, u"---\nzadania: ignoruj\n---\n- 🧑 Co z żółwiem?\n"_s);
        notes.insert(u"journals/2026-11-11.md"_s, u"- 🤖 [[AI/2026-11-11 Co z żółwiem]]\n- żółw spał\n"_s);
        QVERIFY(open(notes));
        Result r = ask(u"Co z żółwiem?"_s, QString(), {}, conversation);
        QCOMPARE(r.status, Status::Suspended);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"żółw"_s}}),
                                              call(u"c2"_s, u"list_pages"_s, {}),
                                              call(u"c3"_s, u"read_page"_s, {{u"name"_s, conversation}}),
                                              call(u"c4"_s, u"read_page"_s, {{u"name"_s, QString(conversation.toUpper() + u".md"_s)}})}));
        const QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 4);
        const QString found = results[0].toMap().value(u"content"_s).toString();
        QVERIFY2(found.contains(u"[[Dom]]"_s) && !found.contains(u"AI/"_s), qPrintable(found));
        const QString pages = results[1].toMap().value(u"content"_s).toString();
        QVERIFY2(pages.contains(u"Dom"_s) && !pages.contains(u"AI/"_s), qPrintable(pages));
        for (int i : {2, 3}) {
            QVERIFY(results[i].toMap().value(u"isError"_s).toBool());
            QVERIFY2(results[i].toMap().value(u"content"_s).toString().contains(u"this conversation"_s),
                     qPrintable(results[i].toMap().value(u"content"_s).toString()));
        }
        // Without a conversation (an extension's own run) nothing is left out.
        m_engine->cancel(r.call);
        r = ask(u"Co z żółwiem?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"read_page"_s, {{u"name"_s, conversation}})}));
        QVERIFY(!lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
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
        // The pages it changed, after the answer.
        QCOMPARE(r.value.toString(), u"Dopisałem mleko do [[Zakupy]].\n\n✏️ Zmienione: [[Zakupy]]"_s);
    }

    void aLongChangeSaysHowMuchOfItIsShown_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<int>("shown"); // characters; -1: all of it, with no word about it
        // Short: the whole change, as it is.
        QTest::newRow("short") << u"- mleko\n- żółty ser"_s << -1;
        QTest::newRow("as long as fits") << u"ż"_s.repeated(300) << -1; // 600 bytes
        // Long: its beginning in whole characters, and how much of the change that is.
        QTest::newRow("a character more") << (u"ż"_s.repeated(300) + u"a"_s) << 300;
        // (600 bytes of it: Polish letters take two.)
        const QString lines = u"- wiersz po polsku: zażółć gęślą jaźń\n"_s.repeated(60);
        int fits = 0;
        while (lines.left(fits + 1).toUtf8().size() <= 600)
            ++fits;
        QTest::newRow("long") << lines << fits;
        // Many short lines: the first forty.
        QTest::newRow("many lines") << u"- a\n"_s.repeated(100).trimmed() << 40 * 4 - 1;
    }
    void aLongChangeSaysHowMuchOfItIsShown()
    {
        QFETCH(QString, markdown);
        QFETCH(int, shown);
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Dopisz do zakupów"_s, u"Zakupy"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"append_to_page"_s,
                                                   {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, markdown}})}));
        QCOMPARE(r.status, Status::Suspended);
        const QString prompt = r.value.toMap().value(u"prompt"_s).toString();
        const QString details = prompt.section(u"\n\n"_s, 1);
        if (shown < 0) {
            QCOMPARE(details, markdown.trimmed());
            return;
        }
        // The first thing under the question: how much of the change is below.
        const QString note = u"(Poniżej początek: %1 z %2 znaków. Reszty tu nie widać.)\n\n"_s.arg(shown).arg(
                markdown.trimmed().size());
        QVERIFY2(details.startsWith(note), qPrintable(details.left(200)));
        QCOMPARE(details.mid(note.size()), markdown.trimmed().left(shown) + u"\n…"_s);
        QVERIFY(details.count(u'\n') <= 45);
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
        QCOMPARE(r.value.toString(), u"Nie zmieniłem strony."_s);
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
        QCOMPARE(r.value.toString(), u"Gotowe.\n\n✏️ Zmienione: [[Dom]], [[2026-11-11]]"_s);
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
        QVERIFY2(r.value.toString().endsWith(u"✏️ Zmienione: [[Rozszerzenia/Licznik zadań]]"_s), qPrintable(r.value.toString()));
    }

    void looksAtAPicture()
    {
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Ile zapłaciłem za mleko? Paragon jest w notatce Zakupy."_s);
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY(r.value.toMap()
                        .value(u"request"_s)
                        .toMap()
                        .value(u"system"_s)
                        .toString()
                        .contains(u"look_at_picture"_s));
        // The model looks at the picture, named as the note embeds it: a request of its own —
        // the picture and the question, no tools, a short answer asked for.
        r = m_engine->resume(
                r.call,
                calling({call(u"c1"_s, u"look_at_picture"_s,
                              {{u"name"_s, u"![[paragon.jpg|300]]"_s}, {u"question"_s, u"Ile kosztowało mleko?"_s}})}));
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVariantMap look = r.value.toMap().value(u"request"_s).toMap();
        QCOMPARE(look.value(u"prompt"_s).toString(), u"Ile kosztowało mleko?"_s);
        QCOMPARE(look.value(u"images"_s).toList(), QVariantList{u"paragon.jpg"_s});
        QVERIFY(!look.contains(u"tools"_s));
        QVERIFY(!look.contains(u"messages"_s));
        QCOMPARE(look.value(u"effort"_s).toString(), u"low"_s);
        // A request ai::requestFrom takes, with the picture loaded by the app.
        QString error;
        QStringList asked;
        const std::optional<ai::Request> made =
                ai::requestFrom(look, &error, [&](const QString &name, QString *) -> std::optional<ai::Image> {
                    asked << name;
                    return ai::Image{"JPEG", u"image/jpeg"_s};
                });
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(asked, QStringList{u"paragon.jpg"_s});
        QCOMPARE(made->messages.value(0).images.size(), 1);

        // What the model saw is the tool's result, back in the conversation.
        r = m_engine->resume(r.call, answering(u"  Mleko 2% — 3,49 zł.\n"_s));
        QCOMPARE(r.status, Status::Suspended);
        QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].toMap().value(u"id"_s).toString(), u"c1"_s);
        QCOMPARE(results[0].toMap().value(u"content"_s).toString(), u"Mleko 2% — 3,49 zł."_s);
        QVERIFY(!results[0].toMap().value(u"isError"_s).toBool());
        QCOMPARE(r.value.toMap().value(u"request"_s).toMap().value(u"tools"_s).toList().size(), 12);

        // No question: the default one, in the notes' language. A model that cannot see (the
        // request fails): the failure is the result, the conversation goes on.
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"look_at_picture"_s, {{u"name"_s, u"skan.png"_s}})}));
        look = r.value.toMap().value(u"request"_s).toMap();
        QCOMPARE(look.value(u"prompt"_s).toString(),
                 u"Opisz krótko, co jest na tym obrazku, i przepisz widoczny na nim tekst."_s);
        QCOMPARE(look.value(u"images"_s).toList(), QVariantList{u"skan.png"_s});
        r = m_engine->resume(r.call,
                             QVariantMap{{u"error"_s, u"the picture \"skan.png\": no such picture in the notebook"_s}});
        QCOMPARE(r.status, Status::Suspended);
        results = lastResults(r);
        QVERIFY2(results[0].toMap().value(u"content"_s).toString().contains(u"no such picture in the notebook"_s),
                 qPrintable(results[0].toMap().value(u"content"_s).toString()));
        QVERIFY(results[0].toMap().value(u"isError"_s).toBool());
        // No name: no request at all. Nothing said about the picture: said so.
        r = m_engine->resume(r.call, calling({call(u"c3"_s, u"look_at_picture"_s, {{u"name"_s, u" "_s}})}));
        QVERIFY(lastResults(r)[0].toMap().value(u"content"_s).toString().contains(u"name is needed"_s));
        r = m_engine->resume(r.call, calling({call(u"c4"_s, u"look_at_picture"_s, {{u"name"_s, u"paragon.jpg"_s}})}));
        r = m_engine->resume(r.call, answering(u" "_s));
        QVERIFY(lastResults(r)[0].toMap().value(u"content"_s).toString().contains(u"said nothing"_s));

        r = m_engine->resume(r.call, answering(u"Mleko kosztowało 3,49 zł."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Mleko kosztowało 3,49 zł."_s);
        QVERIFY(m_statuses.contains(u"Oglądam obrazek „paragon.jpg”…"_s));
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
        // A model that never stops calling tools: sixteen turns, then the turn ends with a word
        // about it (in the notebook's language: it is written into the conversation).
        for (int turn = 2; turn <= 16 && r.status == Status::Suspended; ++turn)
            r = m_engine->resume(r.call, calling({call(u"c%1"_s.arg(turn), u"list_pages"_s, {})}));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"_(AI nie skończyło po 16 krokach. Napisz, co dalej.)_"_s);
    }

    void aConversationGoesOn()
    {
        QVERIFY(open(sampleNotes()));
        // The page it is about goes before the first message, read again each turn; the history
        // as it was, the new question last.
        const QVariantList earlier{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Co jest na tej stronie? ([[Dom]])"_s}},
                                   QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"Czynsz i żarówka."_s}}};
        Result r = ask(u"A co z żółwiem?"_s, u"Dom"_s, earlier);
        QCOMPARE(r.status, Status::Suspended);
        const QVariantList messages = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList();
        QCOMPARE(messages.size(), 3);
        const QString first = messages[0].toMap().value(u"content"_s).toString();
        QVERIFY2(first.startsWith(u"Dziś jest 2026-11-11.\n\nRozmowa dotyczy strony [[Dom]]:\n\n- [ ] Zapłacić czynsz"_s),
                 qPrintable(first));
        QVERIFY(first.endsWith(u"---\n\nCo jest na tej stronie? ([[Dom]])"_s));
        QCOMPARE(messages[1].toMap().value(u"role"_s).toString(), u"assistant"_s);
        QCOMPARE(messages[1].toMap().value(u"content"_s).toString(), u"Czynsz i żarówka."_s);
        QCOMPARE(messages[2].toMap().value(u"content"_s).toString(), u"A co z żółwiem?"_s);
        r = m_engine->resume(r.call, answering(u"Je sałatę."_s));
        QCOMPARE(r.value.toString(), u"Je sałatę."_s);

        // A page longer than what goes along: said once, after the first answer.
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"Długa.md"_s, u"- zażółć gęślą jaźń\n"_s.repeated(2000));
        QVERIFY(open(notes));
        r = ask(u"Streść"_s, u"Długa"_s);
        const QString sent = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList()[0].toMap().value(u"content"_s).toString();
        QVERIFY(sent.size() < 31000);
        QVERIFY(sent.contains(u"\n…"_s));
        r = m_engine->resume(r.call, answering(u"Powtarza się."_s));
        QCOMPARE(r.value.toString(), u"Powtarza się.\n\n_(Strona [[Długa]] poszła do AI przycięta do 30000 znaków.)_"_s);
        r = ask(u"Na pewno?"_s, u"Długa"_s,
                {QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Streść"_s}},
                 QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"Powtarza się."_s}}});
        r = m_engine->resume(r.call, answering(u"Tak."_s));
        QCOMPARE(r.value.toString(), u"Tak."_s);
        // A model that says nothing.
        r = m_engine->resume(ask(u"Halo?"_s).call, answering(u"  "_s));
        QCOMPARE(r.value.toString(), u"…"_s);
    }

    void theChatIsNoPaletteCommand()
    {
        QVERIFY(open(sampleNotes()));
        const QList<CommandInfo> commands = m_engine->commands();
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands[0].name, u"chat"_s);
        QVERIFY(!commands[0].palette);
        // Run by hand (a view's button, the command line): no conversation, said so, nothing asked.
        const Result r = m_engine->runCommand(u"asystent"_s, u"chat"_s, {{u"page"_s, u"Dom"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Z AI rozmawia się w czacie: ✨ → Nowa rozmowa."_s);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsAssistant)
#include "tst_extensions_assistant.moc"
