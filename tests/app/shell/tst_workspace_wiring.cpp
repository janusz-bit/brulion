// The Workspace wired to the real core against a QTemporaryDir vault: background indexing with
// progress and `ready`, quick capture into the journal (the phrases themselves:
// tests/core/notebook tst_notebook_capture), index queries for
// the views, the QueryHost (```query blocks, checking tasks), rename with link rewriting and the
// device-local state that follows it (fold state, favourites), the trash, version history,
// reminders through a fake notification sink (scheduling, firing late, missed reminders after a
// restart, snooze written into the note, switching off, the test notification), conflict copies
// and their resolution, and the AppController integration (settings -> reminders/sync, capture).
#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "index/query.h"
#include "index/vaultindex.h"
#include "notebook/json.h"
#include "reminders/calendar.h"
#include "reminders/reminders.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ScheduledNotification;
using brulion::shell::AppController;
using brulion::shell::Workspace;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

/// A notification sink recording what it is given (the platform notifiers' stand-in).
class FakeSink : public brulion::NotificationSink {
public:
    bool closedFiring = false;
    QList<QList<ScheduledNotification>> scheduled;
    QList<ScheduledNotification> shown;

    bool firesWhenClosed() const override { return closedFiring; }
    void schedule(const QList<ScheduledNotification> &list) override { scheduled.append(list); }
    void showNow(const ScheduledNotification &n) override { shown.append(n); }

    QStringList lastScheduledTitles() const
    {
        QStringList out;
        if (!scheduled.isEmpty()) {
            for (const ScheduledNotification &n : scheduled.last())
                out.append(n.title);
        }
        return out;
    }
};

QStringList titles(const QVariantList &list, const QString &key = u"title"_s)
{
    QStringList out;
    for (const QVariant &v : list)
        out.append(v.toMap().value(key).toString());
    return out;
}

} // namespace

class TestWorkspaceWiring : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{ QDate(2026, 10, 14), QTime(10, 30) }; // a Wednesday

    QString newRoot() { return m_tmp.filePath(u"vault-%1"_s.arg(++m_n)); }

    std::unique_ptr<Workspace> open(const QHash<QString, QString> &files = {}, QString *rootOut = nullptr,
                                    FakeSink *sink = nullptr)
    {
        const QString root = newRoot();
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeRaw(root + u'/' + it.key(), it.value().toUtf8());
        auto ws = reopen(root, sink);
        if (rootOut)
            *rootOut = root;
        return ws;
    }

    std::unique_ptr<Workspace> reopen(const QString &root, FakeSink *sink = nullptr)
    {
        auto ws = std::make_unique<Workspace>();
        ws->setClock([this] { return m_now; });
        ws->sync()->setSyncthingDiscovery(false);
        if (sink)
            ws->reminders()->setSink(sink);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init() { m_now = QDateTime(QDate(2026, 10, 14), QTime(10, 30)); }

    // ---- Indexing -------------------------------------------------------------------------------

    void indexingProgressAndReady()
    {
        const QString root = newRoot();
        for (int i = 0; i < 400; ++i)
            writeRaw(root + u"/Notatki/Strona %1.md"_s.arg(i), "- [[Strona 0]] #źródło zażółć gęślą jaźń\n");
        writeRaw(root + u"/assets/zdjęcie.png"_s, "\x89PNG");
        Workspace ws;
        ws.setClock([this] { return m_now; });
        QSignalSpy ready(&ws, &Workspace::readyChanged);
        QSignalSpy progress(&ws, &Workspace::indexProgressChanged);
        QVERIFY(!ws.ready());
        QCOMPARE(ws.indexProgress(), 1.0);
        QVERIFY(ws.open(root, QString()));
        QVERIFY(ws.indexing());
        QVERIFY(!ws.ready());
        QVERIFY(ws.indexProgress() < 0.01);
        QVERIFY(!ws.reminders()->isRunning()); // never on a half-built index
        QTRY_VERIFY_WITH_TIMEOUT(ws.ready(), 20000);
        QCOMPARE(ws.indexProgress(), 1.0);
        QVERIFY(progress.size() >= 2);
        QCOMPARE(ready.size(), 1);
        QVERIFY(ws.reminders()->isRunning());
        QCOMPARE(ws.tagList().size(), 1);
        QCOMPARE(ws.resolveLink(u"zdjęcie.png"_s, QString()), u"assets/zdjęcie.png"_s); // attachments too
        QCOMPARE(ws.search(u"zazolc"_s).size(), 50); // diacritic-insensitive, limited
        ws.close();
        QVERIFY(!ws.ready());
        QCOMPARE(ready.size(), 2);
        QVERIFY(!ws.reminders()->isRunning());
    }

    // ---- Quick capture ----------------------------------------------------------------------------

    void quickCaptureWritesTheJournal()
    {
        auto ws = open({ { u"templates/journal.md"_s, u"## Plan dnia\n- \n"_s } });
        const QVariantMap preview = ws->quickCapturePreview(u"Kupić mleko jutro o 18"_s);
        QCOMPARE(preview.value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(preview.value(u"text"_s).toString(), u"Kupić mleko"_s);
        QCOMPARE(preview.value(u"date"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(preview.value(u"time"_s).toString(), u"18:00"_s);
        QCOMPARE(preview.value(u"line"_s).toString(), u"- [ ] Kupić mleko ⏰ 2026-10-15 18:00"_s);
        QVERIFY(preview.value(u"when"_s).toString().contains(u"18:00"_s));
        QVERIFY(!ws->exists(u"journals/2026-10-14.md"_s)); // a preview writes nothing

        const QString path = ws->quickCapture(u"Kupić mleko jutro o 18"_s);
        QCOMPARE(path, u"journals/2026-10-14.md"_s);
        ws->quickCapture(u"Pomysł na prezent dla Żanety"_s);
        QCOMPARE(ws->readFile(path),
                 u"## Plan dnia\n- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n- Pomysł na prezent dla Żanety\n"_s);
        QVERIFY(ws->quickCapture(u"   "_s).isEmpty());
        // The reminder is in the index at once (the reminders view, notifications).
        const QVariantMap result = ws->runQuery(u"type:reminders"_s);
        QCOMPARE(titles(result.value(u"items"_s).toList(), u"text"_s), QStringList{ u"Kupić mleko"_s });
    }

    // ---- Index queries ----------------------------------------------------------------------------

    void indexQueries()
    {
        auto ws = open({
                { u"Projekty/Dom.md"_s, u"# Dom\n- Remont łazienki #remont\n- [ ] Kupić farbę 📅 2026-10-16\n"_s },
                { u"journals/2026-10-13.md"_s, u"- Rozmowa o [[Dom|domu]] z ekipą #remont\n- Dom wymaga pracy\n"_s },
                { u"journals/2026-10-14.md"_s, u"- Zadzwonić w sprawie [[Dom]] ⏰ 2026-10-14 17:00\n"_s },
                { u"Żółw.md"_s, u"- Żółw je sałatę\n"_s },
        });
        const QVariantList hits = ws->search(u"zolw"_s);
        QCOMPARE(titles(hits), QStringList{ u"Żółw"_s });
        QCOMPARE(hits.first().toMap().value(u"line"_s).toInt(), 0);
        QVERIFY(ws->search(u"   "_s).isEmpty());

        const QVariantList back = ws->backlinks(u"Projekty/Dom.md"_s);
        QCOMPARE(titles(back, u"path"_s), (QStringList{ u"journals/2026-10-14.md"_s, u"journals/2026-10-13.md"_s }));
        QCOMPARE(back.last().toMap().value(u"journal"_s).toString(), u"2026-10-13"_s);
        QVERIFY(back.last().toMap().value(u"text"_s).toString().contains(u"Rozmowa"_s));
        const QVariantList unlinked = ws->unlinkedReferences(u"Projekty/Dom.md"_s);
        QCOMPARE(unlinked.size(), 1);
        QCOMPARE(unlinked.first().toMap().value(u"line"_s).toInt(), 1);

        const QVariantList tags = ws->tagList();
        QCOMPARE(tags.size(), 1);
        QCOMPARE(tags.first().toMap().value(u"tag"_s).toString(), u"remont"_s);
        QCOMPARE(tags.first().toMap().value(u"count"_s).toInt(), 2);

        QCOMPARE(ws->journalCount(), 2);
        QCOMPARE(titles(ws->journals(), u"date"_s), (QStringList{ u"2026-10-14"_s, u"2026-10-13"_s }));
        QCOMPARE(ws->journals(1, 5).size(), 1);
        QVERIFY(ws->journals(5, 5).isEmpty());
        QCOMPARE(ws->journalDates(u"2026-10-01"_s, u"2026-10-31"_s), (QStringList{ u"2026-10-13"_s, u"2026-10-14"_s }));
        QVERIFY(ws->journalDates(u"2026-10-31"_s, u"2026-10-01"_s).isEmpty());

        const QVariantList calendar = ws->calendarItems(u"2026-10-14"_s, u"2026-10-16"_s);
        QStringList kinds;
        for (const QVariant &v : calendar)
            kinds.append(v.toMap().value(u"date"_s).toString() + u' ' + v.toMap().value(u"kind"_s).toString() + u' '
                         + v.toMap().value(u"time"_s).toString());
        QCOMPARE(kinds, (QStringList{ u"2026-10-14 reminder 17:00"_s, u"2026-10-14 journal "_s, u"2026-10-16 due "_s }));
        QVERIFY(ws->calendarItems(u"2026-01-01"_s, u"2028-01-01"_s).isEmpty()); // too long a range
    }

    // brulion-cli prints query results with notebook::queryResultJson(): the same keys and values
    // as the app's Workspace::runQuery maps (plus the CLI's `ref`, added outside).
    void queryJsonMatchesTheApp()
    {
        auto ws = open({{u"Praca.md"_s, u"- [ ] Raport #praca ⏫ 📅 2026-10-15 ⏳ 2026-10-14 🔁 co tydzień\n"
                                        u"  - notatka ⏰ 2026-10-15 09:00\n- [x] Oferta ✅ 2026-10-13\n"
                                        u"- [ ] Zgłoszenie 🔽 ➕ 2026-10-01 🛫 2026-10-12\n- [-] Wyjazd ❌ 2026-10-12\n"
                                        u"- leki ⏰! 2026-10-16 08:00 🔁 every day\n"_s},
                        {u"journals/2026-10-14.md"_s, u"- Żółw #praca ^b1\n"_s}});
        for (const QString &source :
             {u"type:tasks status:all"_s, u"type:reminders"_s, u"type:blocks tag:praca"_s, u"type:pages"_s,
              u"type:tasks status:all group:status"_s, u"type:tasks status:all group:priority sort:-priority"_s}) {
            const QVariantMap app = ws->runQuery(source);
            QVERIFY2(app.value(u"ok"_s).toBool(), qPrintable(source));
            const auto parsed = brulion::parseQuery(source);
            QVERIFY(std::holds_alternative<brulion::Query>(parsed));
            brulion::QueryOptions options;
            options.now = QTime(10, 30);
            const QJsonObject cli = brulion::notebook::queryResultJson(
                    *ws->index(), brulion::runQuery(std::get<brulion::Query>(parsed), *ws->index(), m_now.date(),
                                                    Qt::Monday, options));
            QVariantMap expected = app;
            for (const char *key : {"ok", "error", "complete"})
                expected.remove(QString::fromLatin1(key));
            // QML gets ints, JSON doubles: compare as JSON.
            QCOMPARE(QJsonDocument(cli).toJson(QJsonDocument::Compact),
                     QJsonDocument(QJsonObject::fromVariantMap(expected)).toJson(QJsonDocument::Compact));
        }
        // The keys QML reads stay (the created / cancelled dates are there too).
        const QVariantList tasks = ws->runQuery(u"type:tasks status:all"_s).value(u"items"_s).toList();
        QCOMPARE(tasks.size(), 4);
        const QVariantMap raport = tasks.at(0).toMap();
        for (const char *key : {"path", "title", "folder", "line", "depth", "text", "raw", "task"})
            QVERIFY2(raport.contains(QString::fromLatin1(key)), key);
        const QVariantMap task = raport.value(u"task"_s).toMap();
        QCOMPARE(task.value(u"status"_s).toString(), u"todo"_s);
        QCOMPARE(task.value(u"priority"_s).toString(), u"high"_s);
        QCOMPARE(task.value(u"due"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(raport.value(u"line"_s).toInt(), 0);
        QCOMPARE(tasks.at(2).toMap().value(u"task"_s).toMap().value(u"created"_s).toString(), u"2026-10-01"_s);
        QCOMPARE(tasks.at(3).toMap().value(u"task"_s).toMap().value(u"cancelled"_s).toString(), u"2026-10-12"_s);
        QCOMPARE(tasks.at(3).toMap().value(u"line"_s).toInt(), 4);

        // Search hits, calendar items and the notebook's settings: the command line's JSON too.
        const brulion::VaultConfig config = ws->vault()->config();
        const QList<brulion::SearchHit> hits = ws->index()->search(u"żółw"_s, 10);
        QVERIFY(!hits.isEmpty());
        QVariantList cliHits;
        for (const brulion::SearchHit &h : hits)
            cliHits.append(brulion::notebook::searchHitJson(h, config).toVariantMap());
        QCOMPARE(ws->search(u"żółw"_s, 10), cliHits);
        QCOMPARE(ws->search(u"żółw"_s, 10).first().toMap().value(u"journal"_s).toString(), u"2026-10-14"_s);
        QVariantList cliCalendar;
        for (const brulion::CalendarItem &item :
             brulion::calendarItems(*ws->index(), QDate(2026, 10, 12), QDate(2026, 10, 16)))
            cliCalendar.append(brulion::notebook::calendarItemJson(item).toVariantMap());
        QCOMPARE(ws->calendarItems(u"2026-10-12"_s, u"2026-10-16"_s), cliCalendar);
        QVERIFY(cliCalendar.size() >= 4);
        QCOMPARE(QJsonObject::fromVariantMap(ws->vaultConfig()), brulion::notebook::vaultConfigJson(config));
        QCOMPARE(ws->vaultConfig().value(u"weekStartsOn"_s).toInt(), 1);
        QCOMPARE(ws->vaultConfig().value(u"defaultReminderTime"_s).toString(), u"09:00"_s);
    }

    void queryHost()
    {
        auto ws = open({
                { u"Praca.md"_s, u"- [ ] Raport kwartalny #praca 📅 2026-10-15\n- [x] Wysłać ofertę #praca\n"
                                 u"- [ ] Prywatne #dom\n"_s },
                { u"Strona z zapytaniem.md"_s, u"- notatka\n"_s },
        });
        // The editor reaches it through the EditorHost pointer.
        brulion::EditorHost *host = ws.get();
        auto *queries = qobject_cast<brulion::QueryHost *>(host);
        QVERIFY(queries);

        QVariantMap r = queries->runQuery(u"type:tasks tag:praca status:open"_s, u"Strona z zapytaniem.md"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QVERIFY(r.value(u"complete"_s).toBool());
        QCOMPARE(r.value(u"type"_s).toString(), u"tasks"_s);
        QCOMPARE(r.value(u"total"_s).toInt(), 1);
        const QVariantMap item = r.value(u"items"_s).toList().first().toMap();
        QCOMPARE(item.value(u"path"_s).toString(), u"Praca.md"_s);
        QCOMPARE(item.value(u"title"_s).toString(), u"Praca"_s);
        QCOMPARE(item.value(u"line"_s).toInt(), 0);
        const QVariantMap task = item.value(u"task"_s).toMap();
        QCOMPARE(task.value(u"status"_s).toString(), u"todo"_s);
        QCOMPARE(task.value(u"due"_s).toString(), u"2026-10-15"_s);
        QVERIFY(item.value(u"raw"_s).toString().startsWith(u"- [ ] Raport"_s));

        // Grouping, Polish keywords.
        r = ws->runQuery(u"typ:zadania tag:praca status:all group:status"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"groups"_s).toList().size(), 2);

        // A mistake gives a helpful message instead of results.
        r = ws->runQuery(u"status:zrobione-może"_s);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QVERIFY(!r.value(u"error"_s).toString().isEmpty());

        // Checking a task in the results writes the note; the query follows the index.
        QVERIFY(queries->setTaskStatus(u"Praca.md"_s, 0, u"done"_s));
        QVERIFY(ws->readFile(u"Praca.md"_s).startsWith(u"- [x] Raport kwartalny #praca 📅 2026-10-15 ✅ 2026-10-14\n"_s));
        QCOMPARE(ws->runQuery(u"type:tasks tag:praca status:open"_s).value(u"total"_s).toInt(), 0);
        QVERIFY(!ws->setTaskStatus(u"Praca.md"_s, 0, u"done"_s)); // nothing to change
        QVERIFY(!ws->setTaskStatus(u"Praca.md"_s, 0, u"zrobione"_s));
        QVERIFY(!ws->setTaskStatus(u"brak.md"_s, 0, u"todo"_s));
        // The status menu's "No status": the checkbox and its date go, the text stays.
        QVERIFY(queries->setTaskStatus(u"Praca.md"_s, 0, QString()));
        QVERIFY(ws->readFile(u"Praca.md"_s).startsWith(u"- Raport kwartalny #praca 📅 2026-10-15\n"_s));
        QCOMPARE(ws->runQuery(u"type:tasks tag:praca status:all"_s).value(u"total"_s).toInt(), 1); // the offer
        QVERIFY(!ws->setTaskStatus(u"Praca.md"_s, 0, QString()));                                  // no task left
        QVERIFY(ws->setTaskStatus(u"Praca.md"_s, 0, u"deferred"_s));                               // a task again
        QVERIFY(ws->readFile(u"Praca.md"_s).startsWith(u"- [>] Raport kwartalny"_s));
    }

    // ---- Pages, trash, history, device-local state ---------------------------------------------------

    void renameFollowsLinksAndLocalState()
    {
        QString root;
        auto ws = open({ { u"Zakupy.md"_s, u"- mleko\n  - pełne\n- chleb\n  - razowy\n"_s },
                         { u"Dziennik.md"_s, u"- lista: [[Zakupy]], [[zakupy|sklep]] i ![[Zakupy#Lista]]\n"_s } },
                       &root);
        ws->setFoldState(u"Zakupy.md"_s, { 2, 0, 2, -1 });
        QCOMPARE(ws->foldState(u"Zakupy.md"_s), (QVariantList{ 0, 2 }));
        ws->setFavorite(u"Zakupy.md"_s, true);

        QCOMPARE(ws->renamePage(u"Zakupy.md"_s, u"Sprawunki w Łodzi"_s), u"Sprawunki w Łodzi.md"_s);
        QCOMPARE(ws->readFile(u"Dziennik.md"_s),
                 u"- lista: [[Sprawunki w Łodzi]], [[Sprawunki w Łodzi|sklep]] i ![[Sprawunki w Łodzi#Lista]]\n"_s);
        QCOMPARE(ws->foldState(u"Sprawunki w Łodzi.md"_s), (QVariantList{ 0, 2 }));
        QVERIFY(ws->foldState(u"Zakupy.md"_s).isEmpty());
        QVERIFY(ws->isFavorite(u"Sprawunki w Łodzi.md"_s));

        // Fold state is device-local and survives a restart.
        ws.reset();
        ws = reopen(root);
        QCOMPARE(ws->foldState(u"Sprawunki w Łodzi.md"_s), (QVariantList{ 0, 2 }));
        ws->setFoldState(u"Sprawunki w Łodzi.md"_s, {});
        QVERIFY(ws->foldState(u"Sprawunki w Łodzi.md"_s).isEmpty());
        ws->setFoldState(u"Sprawunki w Łodzi.md"_s, { 0 });
        QVERIFY(!ws->deletePage(u"Sprawunki w Łodzi.md"_s).isEmpty());
        QVERIFY(ws->foldState(u"Sprawunki w Łodzi.md"_s).isEmpty()); // forgotten with the page
    }

    void trash()
    {
        auto ws = open({ { u"Notatka.md"_s, u"- a\n"_s },
                         { u"Projekty/Dom.md"_s, u"- dom\n"_s },
                         { u"Projekty/Ogród.md"_s, u"- ogród\n"_s } });
        QSignalSpy changed(ws.get(), &Workspace::trashChanged);
        QCOMPARE(ws->trashCount(), 0);
        QVERIFY(ws->trashItems().isEmpty());

        const QString note = ws->deletePage(u"Notatka.md"_s);
        QCOMPARE(note, u".trash/Notatka.md"_s);
        m_now = m_now.addSecs(60);
        QVERIFY(!ws->deletePage(u"Projekty"_s).isEmpty()); // a folder is one item
        QVERIFY(changed.size() >= 2);
        const QVariantList items = ws->trashItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(ws->trashCount(), 2);
        const QVariantMap folder = items.first().toMap(); // newest first
        QCOMPARE(folder.value(u"trashPath"_s).toString(), u".trash/Projekty"_s);
        QCOMPARE(folder.value(u"original"_s).toString(), u"Projekty"_s);
        QCOMPARE(folder.value(u"folder"_s).toBool(), true);
        QCOMPARE(folder.value(u"files"_s).toInt(), 2);
        QCOMPARE(folder.value(u"deletedAt"_s).toString(), u"2026-10-14T10:31:00"_s);
        const QVariantMap page = items.last().toMap();
        QCOMPARE(page.value(u"title"_s).toString(), u"Notatka"_s);
        QCOMPARE(page.value(u"folder"_s).toBool(), false);
        QVERIFY(page.value(u"when"_s).toString().contains(u"10:30"_s));

        QCOMPARE(ws->restore(u".trash/Projekty"_s), u"Projekty"_s);
        QVERIFY(ws->exists(u"Projekty/Ogród.md"_s));
        QCOMPARE(ws->trashCount(), 1);
        QVERIFY(!ws->deleteForever(u"Notatka.md"_s)); // only inside the trash
        QCOMPARE(ws->trashCount(), 1);
        QVERIFY(ws->deleteForever(u".trash/Notatka.md"_s));
        QCOMPARE(ws->trashCount(), 0);

        ws->deletePage(u"Projekty/Dom.md"_s);
        QCOMPARE(ws->trashCount(), 1);
        QVERIFY(ws->emptyTrash());
        QCOMPARE(ws->trashCount(), 0);
        QVERIFY(ws->trashItems().isEmpty());
    }

    void historyRestore()
    {
        auto ws = open({ { u"Notatka.md"_s, u"- pierwsza wersja\n"_s } });
        QVERIFY(ws->write(u"Notatka.md"_s, u"- druga wersja\n"_s));
        m_now = m_now.addSecs(30);
        QVERIFY(ws->write(u"Notatka.md"_s, u"- trzecia wersja\n"_s)); // throttled: no new snapshot
        QVariantList versions = ws->history(u"Notatka.md"_s);
        QCOMPARE(versions.size(), 1);
        const QVariantMap first = versions.first().toMap();
        QCOMPARE(ws->historyText(first.value(u"id"_s).toString()), u"- pierwsza wersja\n"_s);
        QCOMPARE(first.value(u"reason"_s).toString(), u"user"_s);
        QVERIFY(!first.value(u"time"_s).toString().isEmpty());

        // Restoring keeps the replaced text in the history, even inside the throttle window.
        QVERIFY(ws->restoreVersion(u"Notatka.md"_s, first.value(u"id"_s).toString()));
        QCOMPARE(ws->readFile(u"Notatka.md"_s), u"- pierwsza wersja\n"_s);
        versions = ws->history(u"Notatka.md"_s);
        QCOMPARE(versions.size(), 2);
        QCOMPARE(ws->historyText(versions.first().toMap().value(u"id"_s).toString()), u"- trzecia wersja\n"_s);
        QVERIFY(!ws->restoreVersion(u"Notatka.md"_s, u"nie-ma"_s));
        QVERIFY(ws->historyText(u"nie-ma"_s).isEmpty());

        // A deleted page can come back from its history.
        ws->deletePage(u"Notatka.md"_s);
        QVERIFY(ws->historyPaths().contains(u"Notatka.md"_s));
        QVERIFY(ws->restoreVersion(u"Notatka.md"_s, versions.first().toMap().value(u"id"_s).toString()));
        QCOMPARE(ws->readFile(u"Notatka.md"_s), u"- trzecia wersja\n"_s);
    }

    // ---- Reminders ----------------------------------------------------------------------------------

    void snoozeFollowsTheReminderNotItsOldLine()
    {
        FakeSink sink;
        m_now = QDateTime(QDate(2026, 10, 14), QTime(13, 0));
        auto ws = open({ { u"Dom.md"_s, u"- [ ] Podlać kwiaty ⏰ 2026-10-14 14:00\n- [ ] Wynieść śmieci ⏰ 2026-10-14 18:00\n"_s } },
                       nullptr, &sink);
        QVERIFY(ws->reminders()->isRunning());
        const ScheduledNotification shown = ws->reminders()->scheduler()->upcoming().first();
        QCOMPARE(shown.title, u"Podlać kwiaty"_s);
        QCOMPARE(shown.line, 0);

        // A line inserted above: the same reminder is found on its new line.
        QVERIFY(ws->write(u"Dom.md"_s, u"- nowa linia\n- [ ] Podlać kwiaty ⏰ 2026-10-14 14:00\n"
                                       u"- [ ] Wynieść śmieci ⏰ 2026-10-14 18:00\n"_s));
        QVERIFY(ws->reminders()->snoozeNotification(shown, 10));
        QCOMPARE(ws->readFile(u"Dom.md"_s), u"- nowa linia\n- [ ] Podlać kwiaty ⏰ 2026-10-14 13:10\n"
                                            u"- [ ] Wynieść śmieci ⏰ 2026-10-14 18:00\n"_s);

        // The reminder's text was edited and another reminder now sits on its old line: nothing
        // is moved (the other one must not be snoozed instead).
        const ScheduledNotification later = ws->reminders()->scheduler()->upcoming().first();
        QVERIFY(ws->write(u"Dom.md"_s, u"- [ ] Wynieść śmieci ⏰ 2026-10-14 18:00\n- [ ] Podlać storczyki ⏰ 2026-10-14 13:10\n"_s));
        QSignalSpy errors(ws->reminders(), &brulion::shell::WorkspaceReminders::errorOccurred);
        QVERIFY(!ws->reminders()->snoozeNotification(later, 10));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(ws->readFile(u"Dom.md"_s), u"- [ ] Wynieść śmieci ⏰ 2026-10-14 18:00\n- [ ] Podlać storczyki ⏰ 2026-10-14 13:10\n"_s);
    }

    void remindersThroughTheSink()
    {
        const QString note = u"- [ ] Odebrać paczkę ⏰ 2026-10-14 07:00\n"
                             u"- [ ] Zadzwonić do Łukasza ⏰ 2026-10-14 14:00\n"
                             u"- ⏰! 2026-10-14 16:00 Pobudka po drzemce\n"
                             u"- [ ] Zapłacić rachunki ⏰ 2026-10-15 09:00\n"_s;
        FakeSink sink;
        m_now = QDateTime(QDate(2026, 10, 14), QTime(6, 0));
        QString root;
        auto ws = open({ { u"journals/2026-10-14.md"_s, note } }, &root, &sink);
        QVERIFY(ws->reminders()->isRunning());
        QCOMPARE(sink.lastScheduledTitles(), (QStringList{ u"Odebrać paczkę"_s, u"Zadzwonić do Łukasza"_s,
                                                           u"Pobudka po drzemce"_s, u"Zapłacić rachunki"_s }));
        QVERIFY(sink.shown.isEmpty());
        QCOMPARE(ws->reminders()->next().value(u"title"_s).toString(), u"Odebrać paczkę"_s);
        QCOMPARE(ws->reminders()->next().value(u"when"_s).toString(), u"dziś 07:00"_s);
        QCOMPARE(ws->reminders()->upcoming().size(), 4);
        QCOMPARE(ws->reminders()->backend(), u"custom"_s);
        QVERIFY(ws->reminders()->canShow());

        // Brulion was closed from 6:00 to 15:00: 14:00 is shown late, 7:00 is too late (missed).
        ws.reset();
        m_now = QDateTime(QDate(2026, 10, 14), QTime(15, 0));
        sink.shown.clear();
        ws = reopen(root, &sink);
        QCOMPARE(sink.shown.size(), 1);
        QCOMPARE(sink.shown.first().title, u"Zadzwonić do Łukasza"_s);
        QCOMPARE(ws->reminders()->missedCount(), 1);
        const QVariantMap missed = ws->reminders()->missed().first().toMap();
        QCOMPARE(missed.value(u"title"_s).toString(), u"Odebrać paczkę"_s);
        QCOMPARE(missed.value(u"at"_s).toString(), u"2026-10-14 07:00"_s);
        QCOMPARE(missed.value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(missed.value(u"line"_s).toInt(), 0);

        // "Snooze" from the notification: the new time goes into the note.
        QVERIFY(ws->reminders()->snoozeNotification(sink.shown.first(), 10));
        QVERIFY(ws->readFile(u"journals/2026-10-14.md"_s).contains(u"- [ ] Zadzwonić do Łukasza ⏰ 2026-10-14 15:10\n"_s));
        // Snoozing a missed one from the banner (default length).
        ws->reminders()->setSnoozeMinutes(30);
        QVERIFY(ws->reminders()->snooze(missed.value(u"id"_s).toString()));
        QVERIFY(ws->readFile(u"journals/2026-10-14.md"_s).startsWith(u"- [ ] Odebrać paczkę ⏰ 2026-10-14 15:30\n"_s));
        QCOMPARE(ws->reminders()->missedCount(), 0);
        QVERIFY(!ws->reminders()->snooze(u"nie-ma-takiego"_s));

        // Switched off: everything armed is cancelled.
        QSignalSpy running(ws->reminders(), &brulion::shell::WorkspaceReminders::runningChanged);
        ws->reminders()->setEnabled(false);
        QVERIFY(!ws->reminders()->isRunning());
        QVERIFY(sink.scheduled.last().isEmpty());
        ws->reminders()->setEnabled(true);
        QVERIFY(ws->reminders()->isRunning());
        QCOMPARE(running.size(), 2);
        QVERIFY(!sink.scheduled.last().isEmpty());

        // The settings page's test notification.
        QVERIFY(ws->reminders()->sendTestNotification(u"Brulion"_s, u"Przypomnienia działają."_s));
        QCOMPARE(sink.shown.last().body, u"Przypomnienia działają."_s);
        ws->reminders()->cancelAll();
        QVERIFY(sink.scheduled.last().isEmpty());
    }

    void missedRemindersCanBeHidden()
    {
        FakeSink sink;
        m_now = QDateTime(QDate(2026, 10, 14), QTime(6, 0));
        QString root;
        auto ws = open({ { u"a.md"_s, u"- Rano ⏰ 2026-10-14 06:30\n- Śniadanie ⏰ 2026-10-14 06:45\n"_s } }, &root, &sink);
        ws.reset();
        m_now = QDateTime(QDate(2026, 10, 14), QTime(20, 0));
        ws = reopen(root, &sink);
        QCOMPARE(ws->reminders()->missedCount(), 2);
        QCOMPARE(titles(ws->reminders()->missed()), (QStringList{ u"Rano"_s, u"Śniadanie"_s }));

        // The banner opens the note at the reminder's line (and forgets it).
        QSignalSpy navigation(ws.get(), &Workspace::navigationRequested);
        const QVariantMap second = ws->reminders()->missed().at(1).toMap();
        ws->reminders()->openMissed(second.value(u"id"_s).toString());
        QCOMPARE(navigation.size(), 1);
        const QVariantMap route = navigation.first().first().toMap();
        QCOMPARE(route.value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(route.value(u"path"_s).toString(), u"a.md"_s);
        QCOMPARE(route.value(u"line"_s).toInt(), 1);
        QCOMPARE(ws->reminders()->missedCount(), 1);
        ws->reminders()->openMissed(u"x"_s); // unknown: nothing happens
        QCOMPARE(navigation.size(), 1);

        // "Show missed reminders" off.
        QSignalSpy missed(ws->reminders(), &brulion::shell::WorkspaceReminders::missedChanged);
        ws->reminders()->setNotifyMissed(false);
        QCOMPARE(ws->reminders()->missedCount(), 0);
        QVERIFY(ws->reminders()->missed().isEmpty());
        QVERIFY(!missed.isEmpty());
    }

    void firesInAppAtItsTime()
    {
        FakeSink sink;
        m_now = QDateTime(QDate(2026, 10, 14), QTime(11, 59));
        auto ws = open({ { u"Dom.md"_s, u"- [ ] Wyjąć pranie ⏰ 2026-10-14 12:00\n"_s } }, nullptr, &sink);
        ws->reminders()->setTestIntervals(20, 10);
        QSignalSpy fired(ws->reminders(), &brulion::shell::WorkspaceReminders::fired);
        QTest::qWait(60);
        QVERIFY(sink.shown.isEmpty());
        m_now = QDateTime(QDate(2026, 10, 14), QTime(12, 0, 5)); // the clock reaches it
        QTRY_COMPARE(sink.shown.size(), 1);
        QCOMPARE(sink.shown.first().title, u"Wyjąć pranie"_s);
        QCOMPARE(sink.shown.first().body, u"Dom"_s);
        QTRY_COMPARE(fired.size(), 1);
        QCOMPARE(fired.first().first().toMap().value(u"when"_s).toString(), u"dziś 12:00"_s);
        QTest::qWait(60);
        QCOMPARE(sink.shown.size(), 1); // once
    }

    void remindersWithoutSink()
    {
        auto ws = open({ { u"a.md"_s, u"- Coś ⏰ 2026-10-14 12:00\n"_s } });
        QVERIFY(ws->reminders()->isRunning()); // timed in-app, shown nowhere
        QVERIFY(!ws->reminders()->canShow());
        QVERIFY(!ws->reminders()->sendTestNotification(u"t"_s, u"b"_s));
        QCOMPARE(ws->reminders()->backend(), QString());
    }

    // ---- Conflicts ----------------------------------------------------------------------------------

    void conflictCopies()
    {
        auto ws = open({ { u"Notatka.md"_s, u"- moja wersja\n"_s },
                         { u"Notatka.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"- ich wersja\n"_s },
                         { u"Plan.md"_s, u"- plan\n"_s },
                         { u"Plan.conflict-Laptop-20261014-093000.md"_s, u"- plan z laptopa\n"_s },
                         { u"Inna.md"_s, u"- inna\n"_s } });
        auto *sync = ws->sync();
        QCOMPARE(sync->conflictCount(), 2);
        const QVariantMap c = sync->conflicts().first().toMap();
        QCOMPARE(c.value(u"original"_s).toString(), u"Notatka.md"_s);
        QCOMPARE(c.value(u"source"_s).toString(), u"syncthing"_s);
        QCOMPARE(c.value(u"device"_s).toString(), u"ABCDEFG"_s);
        QCOMPARE(c.value(u"time"_s).toString(), u"2026-10-14T10:10:10"_s);
        QVERIFY(c.value(u"text"_s).toBool());
        const QString copy = c.value(u"copy"_s).toString();
        const QVariantMap details = sync->conflictDetails(copy);
        QCOMPARE(details.value(u"ours"_s).toString(), u"- moja wersja\n"_s);
        QCOMPARE(details.value(u"theirs"_s).toString(), u"- ich wersja\n"_s);
        QVERIFY(sync->conflictDetails(u"Inna.md"_s).isEmpty());

        // Never deletes ordinary notes, never mixes up originals.
        QVERIFY(!sync->resolveConflict(u"Notatka.md"_s, u"Inna.md"_s, u"x"_s));
        QVERIFY(!sync->resolveConflict(u"Plan.md"_s, copy, u"x"_s));
        QCOMPARE(ws->readFile(u"Inna.md"_s), u"- inna\n"_s);

        QSignalSpy changed(sync, &brulion::shell::WorkspaceSync::conflictsChanged);
        QVERIFY(sync->resolveConflict(u"Notatka.md"_s, copy, u"- moja wersja\n- ich wersja\n"_s));
        QCOMPARE(ws->readFile(u"Notatka.md"_s), u"- moja wersja\n- ich wersja\n"_s);
        QVERIFY(!ws->exists(copy));
        QCOMPARE(ws->trashCount(), 1); // the copy is recoverable
        QCOMPARE(sync->conflictCount(), 1);
        QVERIFY(!changed.isEmpty());
        // The replaced text is in the history.
        QCOMPARE(ws->historyText(ws->history(u"Notatka.md"_s).first().toMap().value(u"id"_s).toString()),
                 u"- moja wersja\n"_s);

        // "Keep mine": the copy goes, the original stays as it is.
        const QString plan = sync->conflicts().first().toMap().value(u"copy"_s).toString();
        QCOMPARE(sync->conflicts().first().toMap().value(u"source"_s).toString(), u"brulion"_s);
        QVERIFY(sync->discardConflictCopy(plan));
        QCOMPARE(ws->readFile(u"Plan.md"_s), u"- plan\n"_s);
        QCOMPARE(sync->conflictCount(), 0);

        // A copy arriving later (Syncthing) is noticed.
        QVERIFY(writeRaw(ws->rootPath() + u"/Inna.sync-conflict-20261014-120000-XYZ1234.md"_s, "- inna z telefonu\n"));
        ws->vault()->rescan();
        QCOMPARE(sync->conflictCount(), 1);
    }

    // ---- The app's controller ------------------------------------------------------------------------

    void appControllerIntegration()
    {
        AppController::Options options;
        options.settingsFile = m_tmp.filePath(u"app/settings.ini"_s);
        options.dataDir = m_tmp.filePath(u"app/data"_s);
        AppController app(options);
        app.setClock([this] { return m_now; });
        Workspace *ws = app.workspace();
        ws->sync()->setSyncthingDiscovery(false);
        brulion::shell::AppSettings *settings = app.settings();

        // The platform's notifier is the reminders' sink; settings reach the reminders.
        QVERIFY(!ws->reminders()->backend().isEmpty());
        settings->setSnoozeMinutes(25);
        QCOMPARE(ws->reminders()->snoozeMinutes(), 25);
        settings->setRemindersEnabled(false);
        QVERIFY(!ws->reminders()->enabled());
        settings->setRemindersEnabled(true);

        const QString root = newRoot();
        writeRaw(root + u"/Start.md"_s, "- start\n");
        app.openVault(root);
        QTRY_COMPARE(app.status(), u"ready"_s);
        QTRY_VERIFY(ws->ready());
        QVERIFY(ws->reminders()->isRunning());

        // Quick capture from the shell turns dates into reminders.
        QVERIFY(app.capture(u"Kupić mleko jutro o 18"_s));
        QCOMPARE(ws->readFile(u"journals/2026-10-14.md"_s), u"- [ ] Kupić mleko ⏰ 2026-10-15 18:00\n"_s);

        // brulion-cli `app open` (ipc/appmessage.h): a route of the notebook that is open is shown,
        // one of another notebook is refused with a notice.
        QSignalSpy toasts(&app, &AppController::toastRequested);
        app.openRoute({{u"view"_s, u"page"_s}, {u"path"_s, u"Start.md"_s}, {u"line"_s, 0}}, root);
        QCOMPARE(app.currentRoute().value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(app.currentRoute().value(u"path"_s).toString(), u"Start.md"_s);
        app.openRoute({{u"view"_s, u"journal"_s}, {u"date"_s, u"2026-10-14"_s}}, newRoot());
        QCOMPARE(app.currentRoute().value(u"path"_s).toString(), u"Start.md"_s);
        QCOMPARE(toasts.size(), 1);
        app.openRoute({{u"view"_s, u"nonsense"_s}});
        QCOMPARE(app.currentRoute().value(u"path"_s).toString(), u"Start.md"_s);

        // The sync seam: off until Nextcloud is set up completely.
        QVERIFY(!app.syncAvailable());
        QCOMPARE(app.syncStatus().value(u"state"_s).toString(), u"disabled"_s);
        settings->setNextcloudUrl(u"https://127.0.0.1:9"_s);
        settings->setNextcloudUser(u"jan"_s);
        settings->setNextcloudEnabled(true);
        QVERIFY(!ws->sync()->configured()); // no password yet
        QVERIFY(!app.syncAvailable());
        QVERIFY(settings->setNextcloudPassword(u"hasło-aplikacji"_s));
        QVERIFY(ws->sync()->configured());
        QVERIFY(ws->sync()->active());
        QVERIFY(app.syncAvailable());
        QCOMPARE(ws->sync()->currentDavUrl(), u"https://127.0.0.1:9/remote.php/dav/files/jan/Brulion"_s);
        settings->setNextcloudEnabled(false);
        QVERIFY(!app.syncAvailable());

        app.closeVault();
        QTRY_VERIFY(!ws->isOpen());
        QVERIFY(!ws->reminders()->isRunning());
    }

    // brulion-cli `app open` and a second launch (ipc/appmessage.h) as main() hands them over: the
    // payload as it comes off the socket (the line a JSON number), a route asked for while the
    // notebook is still opening (shown once it is ready — or, for another notebook, a notice), and
    // verbs of newer senders that change nothing.
    void instanceMessages()
    {
        AppController::Options options;
        options.settingsFile = m_tmp.filePath(u"app-messages/settings.ini"_s);
        options.dataDir = m_tmp.filePath(u"app-messages/data"_s);
        AppController app(options);
        app.setClock([this] { return m_now; });
        app.workspace()->sync()->setSyncthingDiscovery(false);
        QSignalSpy shown(&app, &AppController::showWindowRequested);
        QSignalSpy toasts(&app, &AppController::toastRequested);
        // The payload as main() gets it: written by brulion-cli, parsed from the socket's line.
        const auto payload = [](const QJsonObject &sent) {
            return QJsonDocument::fromJson(QJsonDocument(sent).toJson(QJsonDocument::Compact)).object();
        };
        const QString root = newRoot();
        writeRaw(root + u"/Projekty/Dom.md"_s, "- a\n- b\n- c\n");

        // While it opens: the route waits for the notebook.
        app.openVault(root);
        QCOMPARE(app.status(), u"loading"_s);
        const QJsonObject open{
                {u"route"_s, QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"Projekty/Dom.md"_s}, {u"line"_s, 2}}},
                {u"vault"_s, root}};
        QVERIFY(app.handleInstanceMessage("open", payload(open)));
        QCOMPARE(shown.size(), 1);
        QTRY_COMPARE(app.status(), u"ready"_s);
        QCOMPARE(app.currentRoute().value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(app.currentRoute().value(u"path"_s).toString(), u"Projekty/Dom.md"_s);
        QCOMPARE(app.currentRoute().value(u"line"_s).toInt(), 2);
        QCOMPARE(toasts.size(), 0);

        // Ready: today's journal, then a page by the same payload shape.
        QVERIFY(app.handleInstanceMessage("today", {}));
        QCOMPARE(app.currentRoute().value(u"view"_s).toString(), u"journal"_s);
        QVERIFY(app.handleInstanceMessage("open", payload(open)));
        QCOMPARE(app.currentRoute().value(u"line"_s).toInt(), 2);
        QVERIFY(app.handleInstanceMessage("activate", {}));
        QCOMPARE(shown.size(), 4);

        // Unknown verbs (wave 4's remote control on an older app): not even the window.
        QVERIFY(!app.handleInstanceMessage("screenshot", payload({{u"path"_s, u"/tmp/x.png"_s}})));
        QVERIFY(!app.handleInstanceMessage("navigate", payload(open)));
        QCOMPARE(shown.size(), 4);
        QCOMPARE(app.currentRoute().value(u"path"_s).toString(), u"Projekty/Dom.md"_s);

        // A route of another notebook asked for while this one opens: a notice, no navigation.
        app.openPage(u"Projekty/Dom.md"_s);
        const QString other = newRoot();
        writeRaw(other + u"/Start.md"_s, "- start\n");
        app.openVault(other);
        QCOMPARE(app.status(), u"loading"_s);
        QVERIFY(app.handleInstanceMessage("open", payload(open)));
        QTRY_COMPARE(app.status(), u"ready"_s);
        QCOMPARE(toasts.size(), 1);
        QVERIFY(toasts.first().first().toString().contains(QDir::toNativeSeparators(root)));
        QVERIFY(app.currentRoute().value(u"path"_s).toString() != u"Projekty/Dom.md"_s);

        app.closeVault();
        QTRY_VERIFY(!app.workspace()->isOpen());
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceWiring)
#include "tst_workspace_wiring.moc"
