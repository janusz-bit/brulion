// The built-in AI assistant (src/core/extensions/builtin/asystent.luau) as an agent on a real
// notebook: a vault, its index and the NotebookHost, the model played by the test, a turn of a
// conversation ("chat": ctx.messages, ctx.page; its answer the command's value). The host's
// full-text search and queries (b.search, b.query), the tools' requests (valid for ai::requestFrom
// and both providers), a question answered from a query's results, a conversation that goes on
// (its history in the request, the page it is about before the first message), the day with its
// weekday (no calendar of the days after it), more of a search hit's page than its line, a query
// as a small model writes it (a tag as a page, a full stop, a page that is not there), what it
// says it does (b.status), a change made with the user's yes, one declined, "yes to every change"
// asked once, an append to a page that is not there refused, a change made not made again (on the
// line's new ref too), one that changes nothing refused, turns that only repeat a change made
// ending the question, a reminder asked for and written as a due date written as a reminder, an
// answer that says a change was made when none was asked for again, a task changed not written
// into the journal again, an extension
// written (the reference read, the page checked, refused while it does not load, written with the
// user's yes), a picture looked at (a request of its own with the picture, its answer the tool's
// result), an unknown tool, too many turns, the command the palette does not offer and its run by
// hand.

#include "ai/ai.h"
#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
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

/// The first result's text.
QString firstResult(const Result &r)
{
    return lastResults(r).value(0).toMap().value(u"content"_s).toString();
}

/// What the first message starts with on 2026-11-11 (a Wednesday): the day with its weekday.
const QString todayPl = u"Dziś jest środa, 2026-11-11.\n\n"_s;

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

    bool open(const QHash<QString, QString> &files, QDate today = QDate(2026, 11, 11), const QString &language = u"pl"_s)
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
        options.now = [today] { return QDateTime(today, QTime(18, 30)); };
        options.language = language;
        options.uiLanguage = language;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_statuses.clear();
        m_host->onStatus = [this](const QString &, const QString &text) { m_statuses.append(text); };
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{test::kRoomyMs, 64ll << 20}))
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
        QCOMPARE(request.value(u"messages"_s).toList().value(0).toMap().value(u"content"_s).toString(), todayPl + question);
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
        // What it asked and each task's status in words: a small model reads no checkboxes.
        QVERIFY2(found.contains(u"of \"type:tasks status:open due:<=+7d\":\n"_s) && found.contains(u"open task"_s)
                         && !found.contains(u"[ ]"_s),
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

    void theDayGoesWithItsWeekday_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QDate>("today");
        QTest::addColumn<QString>("line"); // the first message's beginning
        // The day with its weekday, in the notes' language — and no calendar of the days after it:
        // a small model took those dates for what the user said.
        QTest::newRow("pl") << u"pl"_s << QDate(2026, 11, 11) << todayPl;
        QTest::newRow("en, the year's last days") << u"en"_s << QDate(2026, 12, 30) << u"Today is Wednesday, 2026-12-30.\n\n"_s;
        QTest::newRow("pl, a leap day") << u"pl"_s << QDate(2028, 2, 29) << u"Dziś jest wtorek, 2028-02-29.\n\n"_s;
        // (The clocks go back that night: the day stays a Sunday.)
        QTest::newRow("pl, the end of summer time") << u"pl"_s << QDate(2026, 10, 25)
                                                    << u"Dziś jest niedziela, 2026-10-25.\n\n"_s;
    }
    void theDayGoesWithItsWeekday()
    {
        QFETCH(QString, language);
        QFETCH(QDate, today);
        QFETCH(QString, line);
        QVERIFY(open(sampleNotes(), today, language));
        const QString question = u"Co mam w piątek?"_s;
        const Result r = ask(question);
        QCOMPARE(r.status, Status::Suspended);
        const QVariantMap request = r.value.toMap().value(u"request"_s).toMap();
        QCOMPARE(request.value(u"messages"_s).toList().value(0).toMap().value(u"content"_s).toString(), line + question);
        // Not in the system text: it stays the same from day to day.
        QVERIFY(!request.value(u"system"_s).toString().contains(today.toString(Qt::ISODate)));
        m_engine->cancel(r.call);
    }

    void aHitShowsMoreOfItsPage()
    {
        // b.search gives one line of a page, its best: a person's page found by "weterynarz" is
        // the line "Weterynarz", not the phone under it — a small model then made a number up.
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"Ludzie/Anna Kowalska.md"_s, u"# Anna Kowalska\n\n- Weterynarz\n- Telefon: 601 234 567\n- Dzwonić po 16\n"_s);
        notes.insert(u"journals/2026-11-09.md"_s,
                     u"- Biegłem 8 km w 50 minut.\n- Sprzątanie szafy.\n- Wieczorem jeszcze 4 km biegu.\n"_s);
        QString contacts = u"# Kontakty\n\n"_s;
        for (int i = 1; i <= 15; ++i)
            contacts += u"- Sąsiad spod %1: pan Żółkiewski\n"_s.arg(i);
        contacts += u"\n## Hydraulik\n\n- Jan Nowak\n- tel. 500 100 200\n- tylko rano\n- nie w soboty\n\n## Elektryk\n\n"
                    "- Ewa Lis\n\n- Hydraulika polecił sąsiad spod 4.\n"_s;
        notes.insert(u"Kontakty.md"_s, contacts);
        for (int i = 1; i <= 6; ++i)
            notes.insert(u"Ludzie/Osoba %1.md"_s.arg(i), u"- Fryzjerka nr %1\n- Telefon: 60%1 000 000\n"_s.arg(i));
        QVERIFY(open(notes));
        Result r = ask(u"Jaki jest numer do weterynarza?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"weterynarz"_s}}),
                                              call(u"c2"_s, u"search_notes"_s, {{u"query"_s, u"biegłem km"_s}}),
                                              call(u"c3"_s, u"search_notes"_s, {{u"query"_s, u"hydraulik"_s}}),
                                              call(u"c4"_s, u"search_notes"_s, {{u"query"_s, u"telefon"_s}})}));
        const QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 4);
        // A short page: whole under its hit, without its title.
        const QString vet = results[0].toMap().value(u"content"_s).toString();
        QVERIFY2(vet.startsWith(u"- [[Anna Kowalska]], line 3: Weterynarz (ref Ludzie/Anna Kowalska.md#L3@"_s), qPrintable(vet));
        QVERIFY2(vet.endsWith(u")\n  The whole page:\n    - Weterynarz\n    - Telefon: 601 234 567\n    - Dzwonić po 16"_s),
                 qPrintable(vet));
        QVERIFY(!vet.contains(u"# Anna Kowalska"_s));
        // A day: both runs, not only the line that matched best.
        const QString runs = results[1].toMap().value(u"content"_s).toString();
        QVERIFY2(runs.contains(u"[[2026-11-09]], line 1: Biegłem 8 km w 50 minut."_s)
                         && runs.contains(u"\n  The whole page:\n    - Biegłem 8 km w 50 minut.\n    - Sprzątanie szafy.\n"
                                          "    - Wieczorem jeszcze 4 km biegu."_s),
                 qPrintable(runs));
        // A long page: the lines after the hit's in its section (three at most), then the other
        // lines with the search's words, in any form.
        const QString plumber = results[2].toMap().value(u"content"_s).toString();
        QVERIFY2(plumber.startsWith(u"- [[Kontakty]], line 19: ## Hydraulik (ref Kontakty.md#L19@"_s), qPrintable(plumber));
        QVERIFY2(plumber.endsWith(u")\n  More of the page:\n    line 21: - Jan Nowak\n    line 22: - tel. 500 100 200\n"
                                  "    line 23: - tylko rano\n    line 30: - Hydraulika polecił sąsiad spod 4."_s),
                 qPrintable(plumber));
        // Only the first five hits: a search stays short (the model on the device holds 16 384
        // tokens).
        const QString phones = results[3].toMap().value(u"content"_s).toString();
        QCOMPARE(phones.count(u"\n- [["_s) + 1, 7);
        QCOMPARE(phones.count(u"The whole page:"_s), 5);
        m_engine->cancel(r.call);
    }

    void whatMoreOfAPageShows()
    {
        // A short page without its frontmatter and title, tasks in words as a query's result has
        // them, a child under its parent.
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"Ludzie/Piotr Zając.md"_s, u"---\ntags: [osoba]\n---\n# Piotr Zając\n\n- Mechanik\n  - Telefon: 512 345 678\n"
                                                 "- [ ] Oddać mu klucz\n- [x] Zapłacić za naprawę\n"_s);
        // A long page, a list item hit: the children after it, indented as they are; another line
        // with the word, a child, with its parent; a long line cut in whole letters.
        QString garage = u"# Warsztat\n\n"_s;
        for (int i = 1; i <= 14; ++i)
            garage += u"- Wpis numer %1 o niczym\n"_s.arg(i);
        garage += u"- Wulkanizacja u pana Lisa\n  - tel. 600 700 800\n  - [ ] umówić na wtorek\n  - płatne gotówką\n"
                  "  - nie w soboty\n- Myjnia na rogu\n- Garaż\n  - wulkanizacja opon zimowych w listopadzie\n- Strych\n"
                  "  - gęśl po dziadku\n    - "_s
                  + u"zażółć gęślą jaźń "_s.repeated(15) + u"\n"_s;
        notes.insert(u"Warsztat.md"_s, garage);
        QVERIFY(open(notes));
        Result r = ask(u"Do kogo z autem?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"mechanik"_s}}),
                                              call(u"c2"_s, u"search_notes"_s, {{u"query"_s, u"wulkanizacja"_s}}),
                                              call(u"c3"_s, u"search_notes"_s, {{u"query"_s, u"gęśl"_s}})}));
        const QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 3);
        const QString mechanic = results[0].toMap().value(u"content"_s).toString();
        QVERIFY2(mechanic.endsWith(u")\n  The whole page:\n    - Mechanik\n      - Telefon: 512 345 678\n"
                                   "    - open task: Oddać mu klucz\n    - done task: Zapłacić za naprawę"_s),
                 qPrintable(mechanic));
        QVERIFY(!mechanic.contains(u"tags:"_s) && !mechanic.contains(u"---"_s) && !mechanic.contains(u"# Piotr"_s));
        const QString tyres = results[1].toMap().value(u"content"_s).toString();
        QVERIFY2(tyres.startsWith(u"- [[Warsztat]], line 17: Wulkanizacja u pana Lisa (ref Warsztat.md#L17@"_s), qPrintable(tyres));
        QVERIFY2(tyres.endsWith(u")\n  More of the page:\n    line 18:   - tel. 600 700 800\n    line 19:   - open task: umówić na "
                                "wtorek\n    line 20:   - płatne gotówką\n    line 23: - Garaż\n    line 24:   - wulkanizacja "
                                "opon zimowych w listopadzie"_s),
                 qPrintable(tyres));
        const QString attic = results[2].toMap().value(u"content"_s).toString();
        const QString cutLine = attic.section(u'\n', -1);
        QVERIFY2(cutLine.startsWith(u"    line 27:     - zażółć gęślą jaźń"_s) && cutLine.endsWith(u"…"_s), qPrintable(attic));
        QVERIFY(!cutLine.contains(QChar::ReplacementCharacter));
        QVERIFY(cutLine.toUtf8().size() <= int(u"    line 27: "_s.size()) + 200 + int(u"…"_s.toUtf8().size()));
        m_engine->cancel(r.call);
    }

    void aLongPageIsNotReadWhole()
    {
        // A page of 40 000 lines, the hit near its end: what follows the hit, found without
        // splitting the whole page or folding every line of it (the time limit of a turn).
        QString big;
        for (int i = 1; i <= 40000; ++i)
            big += i == 39990 ? u"- igła w stogu siana\n  - pod spodem nitka\n"_s : u"- wiersz %1 całkiem zwyczajny\n"_s.arg(i);
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"Wielka.md"_s, big);
        QVERIFY(open(notes));
        Result r = ask(u"Gdzie igła?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"igła"_s}})}));
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY2(firstResult(r).endsWith(u"\n  More of the page:\n    line 39991:   - pod spodem nitka\n    line 39992: - wiersz "
                                         "39991 całkiem zwyczajny\n    line 39993: - wiersz 39992 całkiem zwyczajny"_s),
                 qPrintable(firstResult(r)));
        m_engine->cancel(r.call);
    }

    void moreOfThePagesFitsTheBudget()
    {
        // Six short pages of almost 600 bytes each: as many of them shown whole as fit in 3000 bytes.
        QHash<QString, QString> notes = sampleNotes();
        for (int page = 1; page <= 6; ++page) {
            QString text;
            for (int line = 1; line <= 10; ++line)
                text += u"- Budżet %1, pozycja %2: zakupy na cały miesiąc w domu\n"_s.arg(page).arg(line);
            QVERIFY(text.toUtf8().size() <= 600);
            notes.insert(u"Budżet %1.md"_s.arg(page), text);
        }
        QVERIFY(open(notes));
        Result r = ask(u"Jaki mam budżet?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"budżet"_s}})}));
        const QString found = firstResult(r);
        QCOMPARE(found.count(u"\n- [["_s) + 1, 6);
        qsizetype more = 0; // the bytes of what is shown of the pages
        for (const QString &hit : found.split(u"\n- [["_s))
            if (hit.contains(u"\n  The whole page:"_s))
                more += hit.section(u'\n', 1).toUtf8().size() + 1;
        QVERIFY2(more > 2000 && more <= 3000, qPrintable(QString::number(more)));
        QVERIFY(found.count(u"The whole page:"_s) < 5);
        m_engine->cancel(r.call);
    }

    void aQueryAsASmallModelWritesIt_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("result"); // its beginning
        // A page's name as it is: nothing read otherwise. Open is unfinished: in progress too.
        QTest::newRow("a page") << u"type:tasks status:open page:\"Dom\""_s
                                << u"2 matches of \"type:tasks status:open page:\"Dom\"\":\n- [[Dom]], open task, due "
                                   "2026-11-13: Zapłacić czynsz"_s;
        // The user's full stop after the page's name.
        QTest::newRow("a full stop") << u"type:tasks status:open page:\"Dom.\""_s
                                     << u"(page:\"Dom.\" read as page:\"Dom\".)\n2 matches of \"type:tasks status:open page:\"Dom\"\":"_s;
        QTest::newRow("a full stop, no quotes") << u"type:tasks status:open page:Dom."_s
                                                << u"(page:Dom. read as page:\"Dom\".)\n2 matches of"_s;
        // A tag asked for as a page.
        QTest::newRow("a tag as a page") << u"type:tasks status:open page:\"#pilne\""_s
                                         << u"(page:\"#pilne\" read as tag:pilne, a tag.)\n1 match of \"type:tasks status:open tag:pilne\":"_s;
        QTest::newRow("not that tag") << u"-page:#pilne type:tasks status:open"_s
                                      << u"(page:#pilne read as tag:pilne, a tag.)\n2 matches of \"-tag:pilne type:tasks status:open\":"_s;
        QTest::newRow("a tag's name as a page")
                << u"type:tasks status:open strona:\"Pilne\""_s
                << u"(strona:\"Pilne\" read as tag:Pilne: no page has that name, a tag has.)\n1 match of \"type:tasks status:open tag:Pilne\":"_s;
        // A page that is not there: said so, with the pages of a close name, else the notebook's.
        QTest::newRow("no such page, a close one")
                << u"type:tasks page:\"Domy\""_s
                << u"No page named \"Domy\" (pages with a close name: Dom). Nothing matches \"type:tasks page:\"Domy\"\"."_s;
        QTest::newRow("no such page")
                << u"type:tasks page:\"Kuchnia\""_s
                << u"No page named \"Kuchnia\" (pages: Dom, Zakupy). Nothing matches \"type:tasks page:\"Kuchnia\"\"."_s;
        // Nothing found on a page that is there: nothing else to say.
        QTest::newRow("nothing on a page") << u"type:tasks status:cancelled page:Zakupy"_s
                                           << u"Nothing matches \"type:tasks status:cancelled page:Zakupy\"."_s;
        // A conversation with the AI holds the words, but is no page to offer; nor is a day.
        QTest::newRow("not a conversation")
                << u"type:tasks page:\"Co w domu\""_s
                << u"No page named \"Co w domu\" (pages with a close name: Dom). Nothing matches \"type:tasks page:\"Co w domu\"\"."_s;
        QTest::newRow("a day not there")
                << u"type:tasks page:\"2026-11-12\""_s
                << u"No page named \"2026-11-12\" (the journal has no note of that day). Nothing matches \"type:tasks "
                   "page:\"2026-11-12\"\"."_s;
        // A page left out that does not exist leaves nothing out: no word about it.
        QTest::newRow("not a page not there") << u"type:tasks status:cancelled -page:\"Kuchnia\""_s
                                              << u"Nothing matches \"type:tasks status:cancelled -page:\"Kuchnia\"\"."_s;
        // A phrase is words to find, a filter's name in it too.
        QTest::newRow("a phrase") << u"type:tasks \"page:pilne\""_s << u"Nothing matches \"type:tasks \"page:pilne\"\"."_s;
        // A tag with its "#".
        QTest::newRow("a tag with its #") << u"type:tasks status:open tag:#pilne"_s
                                          << u"(tag:#pilne read as tag:pilne.)\n1 match of \"type:tasks status:open tag:pilne\":"_s;
        // A query that fails or finds nothing as written, read once more as a small model meant it:
        // a filter the language does not have is its words…
        QTest::newRow("task:") << u"type:tasks task:\"czynsz\""_s
                               << u"(task:\"czynsz\" read as \"czynsz\".)\n1 match of \"type:tasks \"czynsz\"\":"_s;
        QTest::newRow("word:, no quotes") << u"type:tasks status:open word:płot"_s
                                          << u"(word:płot read as płot.)\n1 match of \"type:tasks status:open płot\":"_s;
        QTest::newRow("słowa:") << u"type:tasks -słowa:\"czynsz\" status:open"_s
                                << u"(-słowa:\"czynsz\" read as -\"czynsz\".)\n2 matches of \"type:tasks -\"czynsz\" status:open\":"_s;
        QTest::newRow("task:tasks") << u"task:tasks status:open \"czynsz\""_s
                                    << u"(task:tasks read as type:tasks.)\n1 match of \"type:tasks status:open \"czynsz\"\":"_s;
        QTest::newRow("a bare task:") << u"type:tasks task: czynsz"_s
                                      << u"(task: left out.)\n1 match of \"type:tasks czynsz\":"_s;
        // … the label of the tool description's example copied before the query is left out…
        QTest::newRow("a label") << u"in progress: type:tasks status:doing"_s
                                 << u"(in progress: left out, a label.)\n1 match of \"type:tasks status:doing\":"_s;
        // … so are OR between its terms, a filter given twice and the description's placeholder…
        QTest::newRow("OR") << u"type:tasks status:open \"czynsz\" OR words:\"czynsz\""_s
                            << u"(OR left out: every filter and word must hold; words:\"czynsz\" read as \"czynsz\".)\n1 match of "
                               "\"type:tasks status:open \"czynsz\"\":"_s;
        QTest::newRow("twice") << u"type:tasks status:doing or type:tasks status:doing"_s
                               << u"(or left out: every filter and word must hold; type:tasks given once; status:doing given "
                                  "once.)\n1 match of \"type:tasks status:doing\":"_s;
        QTest::newRow("tag:name") << u"open tasks with a tag: type:tasks status:open tag:name"_s
                                  << u"(open tasks with a tag: left out, a label; tag:name left out: no tag has that name.)\n3 "
                                     "matches of \"type:tasks status:open\":"_s;
        // … a tag of joined words no note has is those words, a "?" or brackets stuck to a token go.
        QTest::newRow("a tag of words")
                << u"type:tasks status:open tag:#zapłacić-czynsz"_s
                << u"(tag:#zapłacić-czynsz read as tag:zapłacić-czynsz; tag:zapłacić-czynsz read as \"zapłacić czynsz\": no tag "
                   "has that name.)\n1 match of \"type:tasks status:open \"zapłacić czynsz\"\":"_s;
        QTest::newRow("a question mark") << u"type:tasks status:open due:today..+7d?"_s
                                         << u"(due:today..+7d? read as due:today..+7d.)\n1 match of \"type:tasks status:open "
                                            "due:today..+7d\":"_s;
        QTest::newRow("brackets") << u"type:tasks (#pilne)"_s
                                  << u"((#pilne) read as #pilne.)\n1 match of \"type:tasks #pilne\":"_s;
        // Read otherwise and still nothing: what the query gave as written.
        QTest::newRow("still nothing") << u"type:tasks task:\"rower\""_s << u"b.query: Unknown filter “task:”"_s;
        QTest::newRow("still nothing, a tag") << u"type:tasks tag:rower-miejski"_s
                                              << u"Nothing matches \"type:tasks tag:rower-miejski\"."_s;
        // A right query that finds nothing stays so, whatever its words.
        QTest::newRow("nothing, right") << u"type:tasks status:cancelled czynsz OR płot"_s
                                        << u"Nothing matches \"type:tasks status:cancelled czynsz OR płot\"."_s;
    }
    void aQueryAsASmallModelWritesIt()
    {
        QFETCH(QString, query);
        QFETCH(QString, result);
        QVERIFY(open({{u"Dom.md"_s, u"- [ ] Zapłacić czynsz 📅 2026-11-13 #pilne\n- [/] Pomalować płot\n- [x] Wymienić żarówkę\n"_s},
                      {u"Zakupy.md"_s, u"- [ ] chleb\n"_s},
                      {u"journals/2026-11-10.md"_s, u"- Spokojny dzień.\n"_s},
                      {u"AI/2026-11-10 Co w domu.md"_s, u"---\nzadania: ignoruj\n---\n- 🧑 Co w domu?\n"_s}}));
        Result r = ask(u"Co zostało do zrobienia w domu?"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"query_notes"_s, {{u"query"_s, query}})}));
        const QVariantMap found = lastResults(r).value(0).toMap();
        QVERIFY2(found.value(u"content"_s).toString().startsWith(result), qPrintable(found.value(u"content"_s).toString()));
        QCOMPARE(found.value(u"isError"_s).toBool(), result.startsWith(u"b.query:"_s));
        m_engine->cancel(r.call);
    }

    void anAppendGoesToAPageThatIsThere_data()
    {
        QTest::addColumn<QString>("page");   // as the model names it
        QTest::addColumn<QString>("refused"); // the whole result
        // (Not a new page with write_page: a small model made one of a task to move.)
        const QString addTo = u" append_to_page adds to a page that exists: when the user meant one of them, add to it; "
                              "otherwise ask the user which page."_s;
        // "Wpisz Ferdydurke na listę": a small model made a page named after the book. The pages
        // there are instead: never a day, nor a conversation with the AI.
        QTest::newRow("no such page") << u"Ferdydurke"_s << u"No page named \"Ferdydurke\" (pages: Dom, Zakupy)."_s + addTo;
        QTest::newRow("a close name") << u"Zakup"_s
                                      << u"No page named \"Zakup\" (pages with a close name: Zakupy)."_s + addTo;
        // (Not a conversation's name either, though it holds the words.)
        QTest::newRow("a conversation's words") << u"Co kupić"_s << u"No page named \"Co kupić\" (pages: Dom, Zakupy)."_s + addTo;
        // A day: never another day as a close name (the model would write there), the journal's
        // tool instead.
        QTest::newRow("a day not there")
                << u"2026-11-12"_s
                << u"No page named \"2026-11-12\" (the journal has no note of that day). A day's journal is "
                   "append_to_journal's, with its date."_s;
        QTest::newRow("a day there") << u"[[2026-11-10]]"_s << u"A day's journal is append_to_journal's, with its date."_s;
        // A conversation with the AI: the chat's page, not one to add to.
        QTest::newRow("a conversation")
                << u"2026-11-10 Co kupić"_s
                << u"[[2026-11-10 Co kupić]] is a conversation with the AI, not a page to add to (pages: Dom, Zakupy)."_s;
        QTest::newRow("a conversation's path")
                << u"AI/2026-11-10 Co kupić.md"_s
                << u"[[AI/2026-11-10 Co kupić]] is a conversation with the AI, not a page to add to (pages: Dom, Zakupy)."_s;
        // The page itself, named as a link or a file: asked for and added to.
        QTest::newRow("a link") << u"[[Zakupy]]"_s << QString();
        QTest::newRow("a file") << u"Zakupy.md"_s << QString();
        QTest::newRow("another case") << u" zakupy "_s << QString();
    }
    void anAppendGoesToAPageThatIsThere()
    {
        QFETCH(QString, page);
        QFETCH(QString, refused);
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"AI/2026-11-10 Co kupić.md"_s, u"---\nzadania: ignoruj\n---\n- 🧑 Co kupić?\n"_s);
        QVERIFY(open(notes));
        Result r = ask(u"Wpisz to na listę"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"append_to_page"_s, {{u"name"_s, page}, {u"markdown"_s, u"- mleko"_s}})}));
        if (!refused.isEmpty()) {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s); // nothing asked
            QCOMPARE(firstResult(r), refused);
            QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
            QVERIFY(!m_vault->exists(u"Ferdydurke.md"_s) && !m_vault->exists(u"journals/2026-11-12.md"_s));
            QCOMPARE(file(u"journals/2026-11-10.md"_s), notes.value(u"journals/2026-11-10.md"_s));
            QCOMPARE(file(u"AI/2026-11-10 Co kupić.md"_s), notes.value(u"AI/2026-11-10 Co kupić.md"_s));
        } else {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
            r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
            QVERIFY2(firstResult(r).startsWith(u"Added to [["_s), qPrintable(firstResult(r)));
            QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        }
        m_engine->cancel(r.call);
    }

    void aChangeMadeIsNotMadeAgain()
    {
        // A small model makes the change again and again (the same reminder sixteen times): the
        // same call, once made, is not made again — its result says so.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Dopisz mleko do zakupów i przypomnij mi o nim jutro"_s);
        const QVariantMap add = call(u"c1"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}});
        r = m_engine->resume(r.call, calling({add}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        QCOMPARE(firstResult(r), u"Added to [[Zakupy]]."_s);
        r = m_engine->resume(r.call, calling({add}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s); // not asked
        QCOMPARE(firstResult(r), u"This change was made already, by the same call above (Added to [[Zakupy]].). Do not "
                                 "make it again: tell the user what you did."_s);
        QVERIFY(!lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
        QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        // Another change is asked for, and made.
        const QVariantMap remind = call(u"c2"_s, u"append_to_journal"_s,
                                        {{u"markdown"_s, u"- [ ] Kupić mleko ⏰ 2026-11-12 09:00"_s}, {u"date"_s, u"2026-11-12"_s}});
        r = m_engine->resume(r.call, calling({remind}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        // A change declined is no change made: the same call is asked for again (the system text
        // tells the model not to; the bench counts it against the model).
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(2)); // no
        QCOMPARE(firstResult(r), u"The user declined this change."_s);
        r = m_engine->resume(r.call, calling({remind}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QCOMPARE(firstResult(r), u"Added to the journal of 2026-11-12."_s);
        r = m_engine->resume(r.call, calling({remind}));
        QVERIFY2(firstResult(r).startsWith(u"This change was made already"_s), qPrintable(firstResult(r)));
        QCOMPARE(file(u"journals/2026-11-12.md"_s).count(u"Kupić mleko"_s), 1);
        r = m_engine->resume(r.call, answering(u"Dopisałam mleko i przypomnienie."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Dopisałam mleko i przypomnienie.\n\n✏️ Zmienione: [[Zakupy]], [[2026-11-12]]"_s);
    }

    void aChangeWrittenOtherwiseIsTheSameChange()
    {
        // After "yes to every change" nothing is asked: the same change written a little otherwise
        // would be written twice.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Dopisz mleko do zakupów i do dziennika"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(1)); // yes to every change
        QCOMPARE(firstResult(r), u"Added to [[Zakupy]]."_s);
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"append_to_page"_s, {{u"name"_s, u"[[Zakupy]]"_s}, {u"markdown"_s, u"- mleko\r\n"_s}}),
                                              call(u"c3"_s, u"append_to_page"_s, {{u"name"_s, u"zakupy.md"_s}, {u"markdown"_s, u" - mleko "_s}})}));
        for (const QVariant &result : lastResults(r))
            QVERIFY2(result.toMap().value(u"content"_s).toString().startsWith(u"This change was made already"_s),
                     qPrintable(result.toMap().value(u"content"_s).toString()));
        QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
        // Today's journal, with its date given or not.
        r = m_engine->resume(r.call, calling({call(u"c4"_s, u"append_to_journal"_s, {{u"markdown"_s, u"- Kupiłam mleko"_s}})}));
        QCOMPARE(firstResult(r), u"Added to the journal of 2026-11-11."_s);
        r = m_engine->resume(r.call, calling({call(u"c5"_s, u"append_to_journal"_s,
                                                   {{u"markdown"_s, u"- Kupiłam mleko\n"_s}, {u"date"_s, u"2026-11-11"_s}})}));
        QVERIFY2(firstResult(r).startsWith(u"This change was made already"_s), qPrintable(firstResult(r)));
        QCOMPARE(file(u"journals/2026-11-11.md"_s).count(u"Kupiłam mleko"_s), 1);
        // Something else on the same page is another change.
        r = m_engine->resume(r.call, calling({call(u"c6"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- masło"_s}})}));
        QCOMPARE(firstResult(r), u"Added to [[Zakupy]]."_s);
        m_engine->cancel(r.call);
    }

    void aChangeToThePageMakesItNewAgain()
    {
        // A page written, added to, then written as at first: that last write is a change again.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Zrób listę prezentów"_s);
        const QVariantMap write = call(u"c1"_s, u"write_page"_s, {{u"name"_s, u"Prezenty"_s}, {u"content"_s, u"- szalik\n"_s}});
        r = m_engine->resume(r.call, calling({write}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QCOMPARE(firstResult(r), u"Created [[Prezenty]]."_s);
        r = m_engine->resume(r.call, calling({write}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QCOMPARE(firstResult(r), u"This change was made already, by the same call above (Created [[Prezenty]].). Do not make it "
                                 "again: tell the user what you did."_s);
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"append_to_page"_s, {{u"name"_s, u"Prezenty"_s}, {u"markdown"_s, u"- książka"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QCOMPARE(file(u"Prezenty.md"_s), u"- szalik\n- książka\n"_s);
        r = m_engine->resume(r.call, calling({write}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s); // asked again
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QCOMPARE(firstResult(r), u"Rewrote [[Prezenty]]."_s);
        QCOMPARE(file(u"Prezenty.md"_s), u"- szalik\n"_s);

        // A task's line changed, then the same call again: made already (not "stale").
        QString error;
        const QString ref = m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        QVERIFY(ref.startsWith(u"Dom.md#L1@"_s));
        const QVariantMap tick = call(u"c3"_s, u"update_block"_s, {{u"ref"_s, ref}, {u"status"_s, u"done"_s}});
        r = m_engine->resume(r.call, calling({tick}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QVERIFY2(firstResult(r).startsWith(u"Changed; its ref is now Dom.md#L1@"_s), qPrintable(firstResult(r)));
        r = m_engine->resume(r.call, calling({tick, call(u"c4"_s, u"update_block"_s, {{u"ref"_s, ref}, {u"text"_s, u" [x]  "_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        for (const QVariant &result : lastResults(r))
            QVERIFY2(result.toMap().value(u"content"_s).toString().startsWith(u"This change was made already"_s),
                     qPrintable(result.toMap().value(u"content"_s).toString()));
        QCOMPARE(file(u"Dom.md"_s).count(u"✅"_s), 1);
        m_engine->cancel(r.call);
    }

    void aChangeThatChangesNothingIsRefused_data()
    {
        QTest::addColumn<int>("task");         // Dom.md's line, from 0
        QTest::addColumn<QVariantMap>("input"); // besides the ref
        QTest::addColumn<QString>("refused");   // "": asked and made
        // A task ticked off again: so it is already.
        const QString ticked = u"Not changed: the line already reads \"[x] Wymienić żarówkę\" (done task), the status given. "
                               "Tell the user it is so already."_s;
        QTest::newRow("done again") << 1 << QVariantMap{{u"status"_s, u"done"_s}} << ticked;
        QTest::newRow("done again, as a line") << 1 << QVariantMap{{u"text"_s, u"- [x] Wymienić żarówkę"_s}} << ticked;
        // Its own text again and no status: the date stays where none is given, so it reads the
        // same — a small model left out what changes, and is told what would (not that it is so).
        const QString left = u"Not changed: the call gives no status, and its text is the line's own: the line already reads "
                             "\"[ ] Zapłacić czynsz 📅 2026-11-13\" (open task). To change the task, give what changes: its "
                             "status (todo, doing, done, cancelled), or its text with the new date (📅 YYYY-MM-DD)."_s;
        QTest::newRow("its text without the date") << 0 << QVariantMap{{u"text"_s, u"Zapłacić czynsz"_s}} << left;
        QTest::newRow("its text with the date") << 0 << QVariantMap{{u"text"_s, u" Zapłacić  czynsz 📅 2026-11-13"_s}}
                                                << left;
        // Open again: maybe all that was asked, maybe a move without its date.
        QTest::newRow("open again") << 0 << QVariantMap{{u"text"_s, u"[ ] Zapłacić czynsz"_s}, {u"status"_s, u"todo"_s}}
                                    << u"Not changed: the line already reads \"[ ] Zapłacić czynsz 📅 2026-11-13\" (open task), "
                                       "the status given, and the text is the line's own. If that status is all the user "
                                       "asked for, tell them it is so already; to move the task, give its text with the new "
                                       "date (📅 YYYY-MM-DD)."_s;
        // A line that is no task, its own text again.
        QTest::newRow("no task") << 2 << QVariantMap{{u"text"_s, u"- Żółw je sałatę w ogrodzie"_s}}
                                 << u"Not changed: the call's text is the line's own: the line already reads \"Żółw je sałatę w "
                                    "ogrodzie\". To change it, give its new text."_s;
        // A change after all: another status, another date, other words.
        QTest::newRow("reopened") << 1 << QVariantMap{{u"status"_s, u"todo"_s}} << QString();
        QTest::newRow("another date") << 0 << QVariantMap{{u"text"_s, u"Zapłacić czynsz 📅 2026-11-20"_s}} << QString();
        QTest::newRow("other words") << 0 << QVariantMap{{u"text"_s, u"Zapłacić czynsz w banku"_s}} << QString();
        QTest::newRow("done") << 0 << QVariantMap{{u"text"_s, u"Zapłacić czynsz"_s}, {u"status"_s, u"done"_s}} << QString();
    }
    void aChangeThatChangesNothingIsRefused()
    {
        QFETCH(int, task);
        QFETCH(QVariantMap, input);
        QFETCH(QString, refused);
        QVERIFY(open(sampleNotes()));
        QString error;
        QString ref;
        for (const QString &query : {u"type:tasks status:all"_s, u"type:blocks"_s})
            for (const QVariant &item : m_host->query(query, 20, &error).value(u"items"_s).toList())
                if (item.toMap().value(u"ref"_s).toString().startsWith(u"Dom.md#L%1@"_s.arg(task + 1)))
                    ref = item.toMap().value(u"ref"_s).toString();
        QVERIFY2(!ref.isEmpty(), qPrintable(error));
        input.insert(u"ref"_s, ref);
        Result r = ask(u"Odhacz to"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, input)}));
        if (refused.isEmpty()) {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
            r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
            QVERIFY2(firstResult(r).startsWith(u"Changed; its ref is now"_s), qPrintable(firstResult(r)));
            QVERIFY(file(u"Dom.md"_s) != sampleNotes().value(u"Dom.md"_s));
        } else {
            // Refused before the user is asked: nothing to approve, nothing written.
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
            QCOMPARE(firstResult(r), refused);
            QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
            QCOMPARE(file(u"Dom.md"_s), sampleNotes().value(u"Dom.md"_s));
        }
        m_engine->cancel(r.call);
    }

    void aLineChangedIsChangedOnItsNewRefToo_data()
    {
        QTest::addColumn<QVariantMap>("first"); // update_block's input besides the ref (Dom.md's first task)
        QTest::addColumn<QVariantMap>("again"); // the next call's, on the line's new ref
        QTest::addColumn<QString>("said");      // "made": the same change made; "line": the line changed; "": asked
        QTest::addColumn<QString>("reads");     // the line then, for "line"
        const QVariantMap done{{u"status"_s, u"done"_s}};
        const QVariantMap moved{{u"text"_s, u"Zapłacić czynsz 📅 2026-11-20"_s}};
        const QString ticked = u"[x] Zapłacić czynsz 📅 2026-11-13 ✅ 2026-11-11"_s;
        const QString later = u"[ ] Zapłacić czynsz 📅 2026-11-20"_s;
        // The change again on the ref its result gave: made already, by that call (not "it is so
        // already": a small model then told the user nothing needed changing).
        QTest::newRow("ticked, ticked again") << done << done << u"made"_s << QString();
        QTest::newRow("moved, moved again") << moved << moved << u"made"_s << QString();
        // Something else that changes nothing on that line: changed already, as it reads now.
        QTest::newRow("ticked, its line with the box") << done << QVariantMap{{u"text"_s, u"- [x] Zapłacić czynsz"_s}}
                                                       << u"line"_s << ticked;
        QTest::newRow("ticked, its text again") << done << QVariantMap{{u"text"_s, u"Zapłacić czynsz"_s}} << u"line"_s
                                                << ticked;
        QTest::newRow("moved, its status as it is") << moved << QVariantMap{{u"status"_s, u"todo"_s}} << u"line"_s << later;
        QTest::newRow("moved, its text without the date") << moved << QVariantMap{{u"text"_s, u"Zapłacić czynsz"_s}}
                                                          << u"line"_s << later;
        // A change after all: asked as ever.
        QTest::newRow("ticked, opened again") << done << QVariantMap{{u"status"_s, u"todo"_s}} << QString() << QString();
        QTest::newRow("moved, moved further") << moved << QVariantMap{{u"text"_s, u"Zapłacić czynsz 📅 2026-11-27"_s}}
                                              << QString() << QString();
    }
    void aLineChangedIsChangedOnItsNewRefToo()
    {
        QFETCH(QVariantMap, first);
        QFETCH(QVariantMap, again);
        QFETCH(QString, said);
        QFETCH(QString, reads);
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref =
                m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        QVERIFY2(ref.startsWith(u"Dom.md#L1@"_s), qPrintable(error));
        first.insert(u"ref"_s, ref);
        Result r = ask(u"Odhacz czynsz"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, first)}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        const QString changed = firstResult(r);
        QVERIFY2(changed.startsWith(u"Changed; its ref is now Dom.md#L1@"_s), qPrintable(changed));
        const QString newRef = changed.mid(24).chopped(1);
        QVERIFY(newRef != ref);
        const QString written = file(u"Dom.md"_s);
        again.insert(u"ref"_s, newRef);
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"update_block"_s, again)}));
        if (said.isEmpty()) {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        } else {
            // Not asked, nothing written, no error: the change is made.
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
            QCOMPARE(firstResult(r),
                     said == u"made"
                             ? u"This change was made already, by the same call above (%1). Do not make it again: tell the user "
                               "what you did."_s.arg(changed)
                             : u"This line was changed already, by your call above (%1): it reads \"%2\" now. Do not change it "
                               "again: tell the user what you did."_s.arg(changed, reads));
            QVERIFY(!lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
            QCOMPARE(file(u"Dom.md"_s), written);
        }
        m_engine->cancel(r.call);
    }

    void anotherLineAsItIsIsSoAlready()
    {
        // A line this question did not change, ticked off again after another change: so it is
        // already, as ever.
        QVERIFY(open(sampleNotes()));
        QString error;
        QString rent, bulb;
        for (const QVariant &item : m_host->query(u"type:tasks status:all"_s, 5, &error).value(u"items"_s).toList()) {
            const QString ref = item.toMap().value(u"ref"_s).toString();
            (ref.startsWith(u"Dom.md#L1@"_s) ? rent : bulb) = ref;
        }
        QVERIFY2(bulb.startsWith(u"Dom.md#L2@"_s), qPrintable(error));
        Result r = ask(u"Odhacz czynsz i żarówkę"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, {{u"ref"_s, rent}, {u"status"_s, u"done"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"update_block"_s, {{u"ref"_s, bulb}, {u"status"_s, u"done"_s}})}));
        QCOMPARE(firstResult(r), u"Not changed: the line already reads \"[x] Wymienić żarówkę\" (done task), the status given. "
                                 "Tell the user it is so already."_s);
        m_engine->cancel(r.call);
    }

    void aLoopOfRepeatsEnds_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QVariantList>("rounds"); // the model's turns, each its calls (every change approved)
        QTest::addColumn<int>("ends");            // after which turn the question ends with "done"; 0: it goes on
        const QVariant add = QVariantList{call(u"c"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}})};
        const QVariant read = QVariantList{call(u"r"_s, u"read_page"_s, {{u"name"_s, u"Zakupy"_s}})};
        const QVariant search = QVariantList{call(u"s"_s, u"search_notes"_s, {{u"query"_s, u"żółw"_s}})};
        const QVariant both = QVariantList{add.toList().value(0), read.toList().value(0)};
        // A change made, then two turns that only repeat: the change again, the same read again.
        QTest::newRow("the same change twice more") << u"pl"_s << QVariantList{add, add, add} << 3;
        QTest::newRow("in English") << u"en"_s << QVariantList{add, add, add} << 3;
        QTest::newRow("a read, then it again twice") << u"pl"_s << QVariantList{add, read, read, read} << 4;
        QTest::newRow("both again") << u"pl"_s << QVariantList{both, both, both} << 3;
        // One such turn, then something new: it goes on.
        QTest::newRow("a repeat, something new, a repeat") << u"pl"_s << QVariantList{add, add, search, add} << 0;
        QTest::newRow("a repeat with something new") << u"pl"_s
                                                      << QVariantList{add, add, QVariantList{add.toList().value(0), search.toList().value(0)}, add}
                                                      << 0;
        // No change made: the same reads go on as before (to the last step).
        QTest::newRow("reads only") << u"pl"_s << QVariantList{read, read, read, read} << 0;
    }
    void aLoopOfRepeatsEnds()
    {
        QFETCH(QString, language);
        QFETCH(QVariantList, rounds);
        QFETCH(int, ends);
        QVERIFY(open(sampleNotes(), QDate(2026, 11, 11), language));
        Result r = ask(u"Dopisz mleko do zakupów"_s);
        for (int turn = 1; turn <= rounds.size(); ++turn) {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
            r = m_engine->resume(r.call, calling(rounds[turn - 1].toList()));
            while (r.status == Status::Suspended && r.value.toMap().value(u"kind"_s).toString() == u"choose")
                r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
            if (turn == ends) {
                QVERIFY2(r.ok(), qPrintable(r.error));
                QCOMPARE(r.value.toString(), language == u"pl" ? u"Gotowe.\n\n✏️ Zmienione: [[Zakupy]]"_s
                                                               : u"Done.\n\n✏️ Changed: [[Zakupy]]"_s);
                QCOMPARE(file(u"Zakupy.md"_s), u"- chleb\n- mleko\n"_s);
                return;
            }
            QCOMPARE(r.status, Status::Suspended);
        }
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s); // asked on
        m_engine->cancel(r.call);
    }

    void aLoopOnTheChangedLineEnds()
    {
        // The task ticked off, then ticked off again on its new ref and written again with its box:
        // each made already — the question ends, and the model's answer after a turn of it is its own.
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref =
                m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        Result r = ask(u"Możesz odhaczyć „zapłacić czynsz”? Już to zrobiłem."_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, {{u"ref"_s, ref}, {u"status"_s, u"done"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        const QString newRef = firstResult(r).mid(24).chopped(1);
        const QVariantList tick{call(u"c2"_s, u"update_block"_s, {{u"ref"_s, newRef}, {u"status"_s, u"done"_s}})};
        r = m_engine->resume(r.call, calling(tick));
        QVERIFY2(firstResult(r).startsWith(u"This change was made already"_s), qPrintable(firstResult(r)));
        // One such turn, then an answer: the model's.
        const Result answered = m_engine->resume(r.call, answering(u"Odhaczyłam „Zapłacić czynsz” w [[Dom]]."_s));
        QCOMPARE(answered.value.toString(), u"Odhaczyłam „Zapłacić czynsz” w [[Dom]].\n\n✏️ Zmienione: [[Dom]]"_s);
        // Two: done here.
        QVERIFY(open(sampleNotes()));
        r = ask(u"Możesz odhaczyć „zapłacić czynsz”? Już to zrobiłem."_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, {{u"ref"_s, ref}, {u"status"_s, u"done"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        r = m_engine->resume(r.call, calling(tick));
        QCOMPARE(r.status, Status::Suspended);
        r = m_engine->resume(r.call, calling({call(u"c3"_s, u"update_block"_s, {{u"ref"_s, newRef}, {u"text"_s, u"[x] Zapłacić czynsz"_s}})}));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Gotowe.\n\n✏️ Zmienione: [[Dom]]"_s);
        QCOMPARE(file(u"Dom.md"_s).split(u'\n').value(0), u"- [x] Zapłacić czynsz 📅 2026-11-13 ✅ 2026-11-11"_s);
    }

    void aReminderAskedForIsWrittenAsOne_data()
    {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QString>("tool");    // append_to_journal (today's), append_to_page (Zakupy), update_block (Dom's first task)
        QTest::addColumn<QString>("text");    // its markdown, or the line's new text
        QTest::addColumn<QString>("shown");   // what the user approves
        QTest::addColumn<QString>("written"); // the line the note then has
        QTest::addColumn<QString>("note");    // what the result adds ("": nothing)
        const QString bank = u"Jutro o 8:30 muszę zadzwonić do banku — przypomnij mi."_s;
        const auto noteOf = [](const QString &reminder) {
            return u" The time went in as a reminder, %1: 📅 is a due date and rings no alarm."_s.arg(reminder);
        };
        // A reminder asked for, a due date with a time written: a reminder (the hour as HH:MM).
        const QString reminded = u"- [ ] Zadzwonić do banku ⏰ 2026-11-12 08:30"_s;
        QTest::newRow("przypomnij") << bank << u"append_to_journal"_s << u"- [ ] Zadzwonić do banku 📅 2026-11-12 8:30"_s
                                    << reminded << reminded << noteOf(u"⏰ 2026-11-12 08:30"_s);
        QTest::newRow("remind me") << u"Remind me to call the bank tomorrow at 8:30."_s << u"append_to_journal"_s
                                   << u"- [ ] Call the bank 📅 2026-11-12 08:30"_s << u"- [ ] Call the bank ⏰ 2026-11-12 08:30"_s
                                   << u"- [ ] Call the bank ⏰ 2026-11-12 08:30"_s << noteOf(u"⏰ 2026-11-12 08:30"_s);
        QTest::newRow("przypomnienie, on a page") << u"Dodaj do zakupów przypomnienie: mleko, jutro o 18:00."_s
                                                  << u"append_to_page"_s << u"- [ ] Kupić mleko 📅 2026-11-12 18:00"_s
                                                  << u"- [ ] Kupić mleko ⏰ 2026-11-12 18:00"_s
                                                  << u"- [ ] Kupić mleko ⏰ 2026-11-12 18:00"_s << noteOf(u"⏰ 2026-11-12 18:00"_s);
        // A task's line: the reminder goes on it, its due date stays.
        QTest::newRow("a task's line") << u"Przypomnij mi o czynszu 12 listopada o 9:00."_s << u"update_block"_s
                                       << u"Zapłacić czynsz 📅 2026-11-12 09:00"_s << u"Zapłacić czynsz ⏰ 2026-11-12 09:00"_s
                                       << u"- [ ] Zapłacić czynsz ⏰ 2026-11-12 09:00 📅 2026-11-13"_s
                                       << noteOf(u"⏰ 2026-11-12 09:00"_s);
        // Written as it is: a reminder of its own, a day without a time, no reminder asked for, a
        // due date named, a reminder not wanted, no time of day.
        const QString both = u"- [ ] Zadzwonić do banku 📅 2026-11-13 ⏰ 2026-11-12 08:30"_s;
        QTest::newRow("its own reminder") << bank << u"append_to_journal"_s << both << both << both << QString();
        const QString day = u"- [ ] Zadzwonić do banku 📅 2026-11-12"_s;
        QTest::newRow("no time") << bank << u"append_to_journal"_s << day << day << day << QString();
        const QString due = u"- [ ] Zadzwonić do banku 📅 2026-11-12 08:30"_s;
        QTest::newRow("no reminder asked") << u"Dodaj zadanie: zadzwonić do banku jutro o 8:30."_s << u"append_to_journal"_s
                                           << due << due << due << QString();
        QTest::newRow("a due date named") << u"Przypomnij mi o banku, termin jutro o 8:30."_s << u"append_to_journal"_s << due
                                          << due << due << QString();
        QTest::newRow("no reminder wanted") << u"Nie przypominaj mi, tylko zapisz: bank jutro o 8:30."_s << u"append_to_journal"_s
                                            << due << due << due << QString();
        const QString late = u"- [ ] Zadzwonić do banku 📅 2026-11-12 25:00"_s;
        QTest::newRow("no time of day") << bank << u"append_to_journal"_s << late << late << late << QString();
    }
    void aReminderAskedForIsWrittenAsOne()
    {
        QFETCH(QString, question);
        QFETCH(QString, tool);
        QFETCH(QString, text);
        QFETCH(QString, shown);
        QFETCH(QString, written);
        QFETCH(QString, note);
        QVERIFY(open(sampleNotes()));
        QVariantMap input;
        QString path = u"journals/2026-11-11.md"_s;
        QString done = u"Added to the journal of 2026-11-11."_s;
        if (tool == u"append_to_journal") {
            input = {{u"markdown"_s, text}};
        } else if (tool == u"append_to_page") {
            input = {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, text}};
            path = u"Zakupy.md"_s;
            done = u"Added to [[Zakupy]]."_s;
        } else {
            QString error;
            input = {{u"ref"_s, m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0)
                                        .toMap().value(u"ref"_s).toString()},
                     {u"text"_s, text}};
            path = u"Dom.md"_s;
            done = u"Changed; its ref is now "_s;
        }
        Result r = ask(question);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, tool, input)}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        QVERIFY2(r.value.toMap().value(u"prompt"_s).toString().endsWith(u"\n\n"_s + shown),
                 qPrintable(r.value.toMap().value(u"prompt"_s).toString()));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        QVERIFY2(firstResult(r).startsWith(done), qPrintable(firstResult(r)));
        if (note.isEmpty())
            QVERIFY2(!firstResult(r).contains(u"reminder"_s), qPrintable(firstResult(r)));
        else
            QVERIFY2(firstResult(r).endsWith(note), qPrintable(firstResult(r)));
        QVERIFY2(file(path).split(u'\n').contains(written), qPrintable(file(path)));
        m_engine->cancel(r.call);
    }

    void aReminderWrittenAgainIsMadeAlready()
    {
        // The reminder went in as one (asReminders); the model writes it again, now as a reminder:
        // the same change, not a second line.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Jutro o 8:30 muszę zadzwonić do banku — przypomnij mi."_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"append_to_journal"_s,
                                                   {{u"markdown"_s, u"- [ ] Zadzwonić do banku 📅 2026-11-12 8:30"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        QVERIFY2(firstResult(r).startsWith(u"Added to the journal of 2026-11-11."_s), qPrintable(firstResult(r)));
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"append_to_journal"_s,
                                                   {{u"markdown"_s, u"- [ ] Zadzwonić do banku ⏰ 2026-11-12 08:30"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s); // not asked
        QVERIFY2(firstResult(r).startsWith(u"This change was made already"_s), qPrintable(firstResult(r)));
        QCOMPARE(file(u"journals/2026-11-11.md"_s).count(u"Zadzwonić do banku"_s), 1);
        m_engine->cancel(r.call);
    }

    void anAnswerThatSaysAChangeIsMadeIsAskedAgain_data()
    {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QVariantList>("earlier"); // the conversation before
        QTest::addColumn<QVariantList>("calls");   // a turn of calls before the answer ("{rent}", "{bulb}": Dom's refs)
        QTest::addColumn<int>("choice");           // the user's choice for a change among them (0 yes, 2 no)
        QTest::addColumn<QString>("answer");
        QTest::addColumn<bool>("again");           // asked for once more
        const QString tickOff = u"Możesz odhaczyć „zapłacić czynsz”? Już to zrobiłem."_s;
        const QVariantList query{call(u"q"_s, u"query_notes"_s, {{u"query"_s, u"type:tasks \"Zapłacić czynsz\""_s}})};
        const QVariantList add{call(u"a"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}})};
        const QVariantList none;
        // A change said to be made, none made: once more, with what was answered saying so.
        QTest::newRow("ticked off in words") << tickOff << none << query << 0 << u"Odhaczyłam „Zapłacić czynsz” w [[Dom]]. ✅"_s
                                             << true;
        QTest::newRow("no tool at all") << u"Zanotuj, że byłem dziś na basenie."_s << none << none << 0
                                        << u"Zanotowałam: byłeś dziś na basenie."_s << true;
        QTest::newRow("after a word") << u"Dopisz mleko do zakupów."_s << none << none << 0
                                      << u"Jasne, dopisałam mleko do [[Zakupy]]."_s << true;
        QTest::newRow("emphasis and a sign") << u"Dopisz mleko do zakupów."_s << none << none << 0
                                             << u"✅ **Dodałem** mleko do [[Zakupy]]."_s << true;
        QTest::newRow("a page refused") << u"Chcę przeczytać „Ferdydurke”, wpisz to na listę."_s << none
                                        << QVariantList{call(u"a"_s, u"append_to_page"_s,
                                                             {{u"name"_s, u"Zadania"_s}, {u"markdown"_s, u"- [ ] Ferdydurke"_s}})}
                                        << 0 << u"Dodałem „Ferdydurke” do listy **Zadania**."_s << true;
        QTest::newRow("in English") << u"Add milk to my shopping list."_s << none << none << 0
                                    << u"I've added milk to [[Zakupy]]."_s << true;
        QTest::newRow("a request as a question") << u"Czy możesz dopisać mleko do zakupów?"_s << none << none << 0
                                                 << u"Dopisałam mleko."_s << true;
        // Not: a change made, declined or so already; a question; no such words first; a quote or
        // an item of the notes; a change of the answer before.
        QTest::newRow("a change made") << u"Dopisz mleko do zakupów."_s << none << add << 0
                                       << u"Dopisałam mleko do [[Zakupy]]."_s << false;
        QTest::newRow("a change declined") << u"Dopisz mleko do zakupów."_s << none << add << 2 << u"Dodałam mleko."_s
                                           << false;
        QTest::newRow("so already") << u"Odhacz „wymienić żarówkę”."_s << none
                                    << QVariantList{call(u"u"_s, u"update_block"_s, {{u"ref"_s, u"{bulb}"_s}, {u"status"_s, u"done"_s}})}
                                    << 0 << u"Odhaczyłam „Wymienić żarówkę”."_s << false;
        QTest::newRow("a question") << u"Co mam na liście zakupów?"_s << none << none << 0
                                    << u"Zapisałam tam tylko chleb ([[Zakupy]])."_s << false;
        QTest::newRow("a question, in English") << u"Did I note the plumber's number?"_s << none << none << 0
                                                << u"I noted nothing like that: no note has it."_s << false;
        QTest::newRow("not first") << u"Dopisz mleko do zakupów."_s << none << none << 0
                                   << u"Na liście jest chleb; mleka nie dodałam."_s << false;
        QTest::newRow("not done") << u"Dopisz mleko do zakupów."_s << none << none << 0
                                  << u"Nie dodałam mleka: na którą listę?"_s << false;
        QTest::newRow("a quote") << u"Pokaż, co pisałem 10 listopada."_s << none << none << 0
                                 << u"„Zapisałem się na basen” — tak piszesz 10 listopada."_s << false;
        QTest::newRow("an item of the notes") << u"Pokaż wpisy z 10 listopada."_s << none << none << 0
                                              << u"- Dodałem zdjęcia z wakacji\n- Spotkanie z Anią"_s << false;
        QTest::newRow("a draft") << u"Przygotuj krótką informację, że się spóźnię."_s << none << none << 0
                                 << u"Spóźnię się około 15 minut — przepraszam!"_s << false;
        QTest::newRow("the change before")
                << u"Dzięki!"_s
                << QVariantList{QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, u"Dopisz mleko do zakupów."_s}},
                                QVariantMap{{u"role"_s, u"assistant"_s}, {u"content"_s, u"Dopisałam mleko.\n\n✏️ Zmienione: [[Zakupy]]"_s}}}
                << none << 0 << u"Dopisałam mleko — nie ma za co."_s << false;
    }
    void anAnswerThatSaysAChangeIsMadeIsAskedAgain()
    {
        QFETCH(QString, question);
        QFETCH(QVariantList, earlier);
        QFETCH(QVariantList, calls);
        QFETCH(int, choice);
        QFETCH(QString, answer);
        QFETCH(bool, again);
        QVERIFY(open(sampleNotes()));
        QString error;
        QHash<QString, QString> refs;
        for (const QVariant &item : m_host->query(u"type:tasks status:all"_s, 5, &error).value(u"items"_s).toList()) {
            const QString ref = item.toMap().value(u"ref"_s).toString();
            refs.insert(ref.startsWith(u"Dom.md#L1@"_s) ? u"{rent}"_s : u"{bulb}"_s, ref);
        }
        for (QVariant &c : calls) {
            QVariantMap one = c.toMap();
            QVariantMap input = one.value(u"input"_s).toMap();
            if (refs.contains(input.value(u"ref"_s).toString()))
                input.insert(u"ref"_s, refs.value(input.value(u"ref"_s).toString()));
            one.insert(u"input"_s, input);
            c = one;
        }
        const QString note = u"\n\n(Nothing is changed yet: no tool made a change for this question. If the user asked for "
                             "one, make it now with its tool; otherwise answer without saying that you changed anything.)"_s;
        Result r = ask(question, QString(), earlier);
        if (!calls.isEmpty()) {
            r = m_engine->resume(r.call, calling(calls));
            while (r.status == Status::Suspended && r.value.toMap().value(u"kind"_s).toString() == u"choose")
                r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(choice));
        }
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        const qsizetype before = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList().size();
        r = m_engine->resume(r.call, answering(answer));
        if (!again) {
            QVERIFY2(r.ok(), qPrintable(r.error));
            QVERIFY2(r.value.toString().startsWith(answer), qPrintable(r.value.toString()));
            return;
        }
        // Asked again, without that answer: the message it answered says nothing is changed yet —
        // the last result of the turn, or with no tool called the user's own.
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        const QVariantList messages = r.value.toMap().value(u"request"_s).toMap().value(u"messages"_s).toList();
        QCOMPARE(messages.size(), before);
        if (calls.isEmpty()) {
            QCOMPARE(messages.last().toMap().value(u"content"_s).toString(), (earlier.isEmpty() ? todayPl : QString()) + question + note);
        } else {
            QVERIFY2(lastResults(r).last().toMap().value(u"content"_s).toString().endsWith(note), qPrintable(lastResults(r).last().toMap().value(u"content"_s).toString()));
            QCOMPARE(lastResults(r).size(), calls.size());
        }
        // Once only: the same answer again is the answer.
        r = m_engine->resume(r.call, answering(answer));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), answer);
        // The change made instead: the model's answer as ever.
        r = ask(question, QString(), earlier);
        r = m_engine->resume(r.call, answering(answer));
        r = m_engine->resume(r.call, calling({call(u"a"_s, u"append_to_page"_s, {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- mleko"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        r = m_engine->resume(r.call, answering(u"Dopisałam mleko."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Dopisałam mleko.\n\n✏️ Zmienione: [[Zakupy]]"_s);
    }

    void aTaskChangedIsNoJournalNote_data()
    {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QVariantMap>("change"); // update_block's input besides the ref (Dom.md's first task)
        QTest::addColumn<QString>("markdown");   // append_to_journal's then
        QTest::addColumn<bool>("refused");
        const QString tickOff = u"Możesz odhaczyć „zapłacić czynsz”? Już to zrobiłem."_s;
        const QVariantMap done{{u"status"_s, u"done"_s}};
        // The task ticked off, then written into the day's journal too: the copy is refused.
        QTest::newRow("ticked, its line again") << tickOff << done << u"- [x] Zapłacić czynsz 📅 2026-11-13"_s << true;
        QTest::newRow("its words and a remark") << tickOff << done << u"- [ ] Zapłacić czynsz ✅ (odhaczono)"_s << true;
        QTest::newRow("its words, folded") << tickOff << done << u"ZAPLACIC  czynsz — zrobione"_s << true;
        QTest::newRow("among other lines") << tickOff << done << u"- Kupić mleko\n- [x] Zapłacić czynsz"_s << true;
        // Moved: neither its old words nor its new ones go into the journal again.
        QTest::newRow("moved") << u"Przełóż czynsz na 20 listopada"_s
                               << QVariantMap{{u"text"_s, u"Zapłacić czynsz 📅 2026-11-20"_s}}
                               << u"- [ ] Zapłacić czynsz 📅 2026-11-20"_s << true;
        QTest::newRow("moved, other words") << u"Przełóż czynsz na 20 listopada"_s
                                            << QVariantMap{{u"text"_s, u"Zapłacić czynsz w banku 📅 2026-11-20"_s}}
                                            << u"- [ ] Zapłacić czynsz w banku"_s << true;
        // Other words, something else, or a journal note the user asks for: asked as ever.
        QTest::newRow("other words") << tickOff << done << u"- Zapłaciłem czynsz za listopad"_s << false;
        QTest::newRow("something else") << tickOff << done << u"- Kupić mleko"_s << false;
        QTest::newRow("its words inside a line") << tickOff << done << u"- Rano: zapłacić czynsz"_s << false;
        QTest::newRow("the journal asked for") << u"Odhacz „zapłacić czynsz” i zapisz to w dzienniku."_s << done
                                               << u"- [x] Zapłacić czynsz"_s << false;
        QTest::newRow("the journal asked for, en") << u"Tick off the rent and note it in my journal."_s << done
                                                   << u"- [x] Zapłacić czynsz"_s << false;
    }
    void aTaskChangedIsNoJournalNote()
    {
        QFETCH(QString, question);
        QFETCH(QVariantMap, change);
        QFETCH(QString, markdown);
        QFETCH(bool, refused);
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref =
                m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        QVERIFY2(ref.startsWith(u"Dom.md#L1@"_s), qPrintable(error));
        change.insert(u"ref"_s, ref);
        Result r = ask(question);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, change)}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        QVERIFY2(firstResult(r).startsWith(u"Changed; its ref is now"_s), qPrintable(firstResult(r)));
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"append_to_journal"_s, {{u"markdown"_s, markdown}})}));
        if (refused) {
            // Refused before the user is asked: nothing to approve, no journal written.
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
            QVERIFY2(firstResult(r).startsWith(u"Not added: this is the task changed above in [[Dom]] (\"Zapłacić czynsz"_s),
                     qPrintable(firstResult(r)));
            QVERIFY(firstResult(r).endsWith(u"\"), whose line says it already. Tell the user what you changed."_s));
            QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
            QVERIFY(!QFile::exists(m_vault->rootPath() + u"/journals/2026-11-11.md"_s));
            r = m_engine->resume(r.call, answering(u"Odhaczyłam."_s));
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(r.value.toString(), u"Odhaczyłam.\n\n✏️ Zmienione: [[Dom]]"_s);
        } else {
            QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
            m_engine->cancel(r.call);
        }
    }

    void aPlainLineChangedMayGoIntoTheJournal()
    {
        // Not a task: a line of the journal changed, then the same words on another day — no tick
        // to repeat, the user is asked.
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref = m_host->query(u"type:blocks path:journals/"_s, 5, &error).value(u"items"_s).toList().value(0)
                                    .toMap().value(u"ref"_s).toString();
        QVERIFY2(ref.startsWith(u"journals/2026-11-10.md#L1@"_s), qPrintable(error));
        Result r = ask(u"Przenieś spotkanie z Anią na piątek"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s,
                                                   {{u"ref"_s, ref}, {u"text"_s, u"Spotkanie z Anią ⏰ 2026-11-13 18:00"_s}})}));
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        QVERIFY2(firstResult(r).startsWith(u"Changed; its ref is now"_s), qPrintable(firstResult(r)));
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"append_to_journal"_s,
                                                   {{u"markdown"_s, u"- Spotkanie z Anią ⏰ 2026-11-13 18:00"_s},
                                                    {u"date"_s, u"2026-11-13"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        m_engine->cancel(r.call);
    }

    void aCallMadeBeforeIsNotRunAgain()
    {
        // A small model asks the same again and again until its steps are up: the same call says
        // so instead of running again — until a change, after which a page may read otherwise.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Co z żółwiem?"_s);
        QCOMPARE(r.status, Status::Suspended);
        const QVariantMap search = call(u"c1"_s, u"search_notes"_s, {{u"query"_s, u"żółw"_s}});
        r = m_engine->resume(r.call, calling({search}));
        QVERIFY(lastResults(r).value(0).toMap().value(u"content"_s).toString().contains(u"Żółw je sałatę"_s));
        m_statuses.clear();
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"search_notes"_s, {{u"query"_s, u"żółw"_s}}),
                                              call(u"c3"_s, u"search_notes"_s, {{u"query"_s, u"sałata"_s}})}));
        QVariantList results = lastResults(r);
        QCOMPARE(results.size(), 2);
        QVERIFY2(results[0].toMap().value(u"content"_s).toString().startsWith(u"The same call as before"_s),
                 qPrintable(results[0].toMap().value(u"content"_s).toString()));
        QVERIFY(!results[0].toMap().value(u"isError"_s).toBool());
        QVERIFY(!results[1].toMap().value(u"content"_s).toString().startsWith(u"The same call"_s));
        QCOMPARE(m_statuses, (QStringList{u"Szukam w notatkach: „sałata”…"_s, u""_s})); // the first one did not run

        // After a change the same read runs again.
        r = m_engine->resume(r.call, calling({call(u"c4"_s, u"read_page"_s, {{u"name"_s, u"Zakupy"_s}})}));
        r = m_engine->resume(r.call, calling({call(u"c5"_s, u"append_to_page"_s,
                                                   {{u"name"_s, u"Zakupy"_s}, {u"markdown"_s, u"- masło"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0)); // yes
        r = m_engine->resume(r.call, calling({call(u"c6"_s, u"read_page"_s, {{u"name"_s, u"Zakupy"_s}})}));
        QVERIFY2(lastResults(r).value(0).toMap().value(u"content"_s).toString().contains(u"masło"_s),
                 qPrintable(lastResults(r).value(0).toMap().value(u"content"_s).toString()));
    }

    void lateStepsAreToldToAnswer()
    {
        // From half its sixteen steps on, the last result of a turn says which step it is.
        QVERIFY(open(sampleNotes()));
        Result r = ask(u"Co z żółwiem?"_s);
        for (int step = 1; step <= 8; ++step) {
            QCOMPARE(r.status, Status::Suspended);
            r = m_engine->resume(r.call, calling({call(u"c%1"_s.arg(step), u"search_notes"_s,
                                                       {{u"query"_s, u"słowo%1"_s.arg(step)}})}));
            const QString content = lastResults(r).value(0).toMap().value(u"content"_s).toString();
            QCOMPARE(content.contains(u"(Step "_s), step >= 8);
            if (step == 8)
                QVERIFY2(content.endsWith(u"(Step 8 of 16: answer soon with what you have found.)"_s), qPrintable(content));
        }
        m_engine->cancel(r.call);
    }

    void theConversationIsNoFind()
    {
        // The conversation's own page matches its question word for word: a small model read it
        // instead of the notes. Its tools leave it out — the search, the list, a query —, and
        // the day's line that only links to it; reading it is refused.
        QHash<QString, QString> notes = sampleNotes();
        const QString conversation = u"AI/2026-11-11 Co z żółwiem"_s;
        notes.insert(conversation + u".md"_s, u"---\nzadania: ignoruj\n---\n- 🧑 Co z żółwiem?\n"_s);
        notes.insert(u"journals/2026-11-11.md"_s, u"- 🤖 [[AI/2026-11-11 Co z żółwiem]]\n- żółw spał\n- żółw jadł sałatę\n"_s);
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
        // The day's line that links to the conversation holds the question's words, so the search
        // finds it first: the day is found by its own notes instead, the link left out.
        QVERIFY2(found.contains(u"- [[2026-11-11]], line 2: żółw spał (ref journals/2026-11-11.md#L2@"_s)
                         && found.contains(u")\n  The whole page:\n    - żółw spał\n    - żółw jadł sałatę"_s),
                 qPrintable(found));
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

    void aTasksStatusAloneKeepsItsText_data()
    {
        QTest::addColumn<QVariantMap>("input"); // besides the ref
        QTest::addColumn<QString>("line");      // Dom.md's first line afterwards
        // The status alone: the words, the date and the tag stay.
        QTest::newRow("status") << QVariantMap{{u"status"_s, u"done"_s}}
                                << u"- [x] Zapłacić czynsz 📅 2026-11-13 #rachunki ✅ 2026-11-11"_s;
        QTest::newRow("empty text") << QVariantMap{{u"text"_s, u"  "_s}, {u"status"_s, u"doing"_s}}
                                    << u"- [/] Zapłacić czynsz 📅 2026-11-13 #rachunki"_s;
        // The line as a small model writes it: its checkbox is the status.
        QTest::newRow("checkbox in the text") << QVariantMap{{u"text"_s, u"- [x] Zapłacić czynsz #rachunki"_s}}
                                              << u"- [x] Zapłacić czynsz #rachunki 📅 2026-11-13 ✅ 2026-11-11"_s;
        QTest::newRow("a status given wins") << QVariantMap{{u"text"_s, u"[x] Zapłacić czynsz #rachunki"_s},
                                                            {u"status"_s, u"doing"_s}}
                                             << u"- [/] Zapłacić czynsz #rachunki 📅 2026-11-13"_s;
        QTest::newRow("a marker in the text") << QVariantMap{{u"text"_s, u"- Zapłacić czynsz w banku"_s}}
                                              << u"- [ ] Zapłacić czynsz w banku 📅 2026-11-13"_s;
    }

    void aTasksStatusAloneKeepsItsText()
    {
        QFETCH(QVariantMap, input);
        QFETCH(QString, line);
        QHash<QString, QString> notes = sampleNotes();
        notes[u"Dom.md"_s].replace(u"📅 2026-11-13"_s, u"📅 2026-11-13 #rachunki"_s);
        QVERIFY(open(notes));
        QString error;
        const QString ref =
                m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        QVERIFY(!ref.isEmpty());
        input.insert(u"ref"_s, ref);
        Result r = ask(u"Czynsz zapłacony"_s);
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, input)}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"choose"_s);
        r = m_engine->resume(r.call, r.value.toMap().value(u"options"_s).toStringList().value(0));
        QVERIFY2(!lastResults(r).value(0).toMap().value(u"isError"_s).toBool(),
                 qPrintable(lastResults(r).value(0).toMap().value(u"content"_s).toString()));
        QCOMPARE(file(u"Dom.md"_s).split(u'\n').value(0), line);
    }

    void aChangeOfNothingIsRefused()
    {
        QVERIFY(open(sampleNotes()));
        QString error;
        const QString ref =
                m_host->query(u"type:tasks status:open"_s, 5, &error).value(u"items"_s).toList().value(0).toMap().value(u"ref"_s).toString();
        Result r = ask(u"Zmień to"_s);
        // Neither text nor status: an error for the model, no question to the user.
        r = m_engine->resume(r.call, calling({call(u"c1"_s, u"update_block"_s, {{u"ref"_s, ref}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVERIFY(lastResults(r).value(0).toMap().value(u"isError"_s).toBool());
        QVERIFY2(lastResults(r).value(0).toMap().value(u"content"_s).toString().contains(u"Nothing to change"_s),
                 qPrintable(lastResults(r).value(0).toMap().value(u"content"_s).toString()));
        // The status alone with a ref gone stale: refused before asking, nothing written.
        QString stale = ref;
        stale.replace(QRegularExpression(u"@[0-9a-f]+$"_s), u"@00000000"_s);
        r = m_engine->resume(r.call, calling({call(u"c2"_s, u"update_block"_s, {{u"ref"_s, stale}, {u"status"_s, u"done"_s}})}));
        QCOMPARE(r.value.toMap().value(u"kind"_s).toString(), u"ai"_s);
        QVERIFY2(lastResults(r).value(0).toMap().value(u"content"_s).toString().startsWith(u"stale"_s),
                 qPrintable(lastResults(r).value(0).toMap().value(u"content"_s).toString()));
        QCOMPARE(file(u"Dom.md"_s), sampleNotes().value(u"Dom.md"_s));
        r = m_engine->resume(r.call, answering(u"Nic nie zmieniłam."_s));
        QVERIFY2(r.ok(), qPrintable(r.error));
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
        QVERIFY2(first.startsWith(todayPl + u"Rozmowa dotyczy strony [[Dom]]:\n\n- [ ] Zapłacić czynsz"_s), qPrintable(first));
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
