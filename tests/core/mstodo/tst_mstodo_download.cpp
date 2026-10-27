// The Graph download against FakeMicrosoft: lists and tasks with @odata.nextLink paging, steps
// and links by $expand or (when Graph refuses it) task by task, attachments within the size
// budget, Retry-After on 429, the ways it fails (expired sign-in, no permission, no network),
// paging links to other hosts, cancel — and the bundle imported by the io module's importer.
#include "fakemicrosoft.h"

#include "io/importer.h"
#include "mstodo/mstodo.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::mstodo::TodoDownload;
using mstodotest::FakeMicrosoft;

namespace {

struct Result {
    QJsonObject bundle;
    QByteArray raw;
    QStringList warnings;
    QString failedCode;
    QString failedMessage;
};

Result download(FakeMicrosoft &ms, const QString &token = {}, const brulion::mstodo::DownloadLimits &limits = {})
{
    TodoDownload d(&ms);
    d.setTimeUnitMs(1);
    d.setLimits(limits);
    QSignalSpy finished(&d, &TodoDownload::finished);
    QSignalSpy failed(&d, &TodoDownload::failed);
    d.start(token.isEmpty() ? ms.accessToken : token);
    Result r;
    QElapsedTimer t;
    t.start();
    while (finished.isEmpty() && failed.isEmpty() && t.elapsed() < 10000)
        QTest::qWait(5);
    if (!finished.isEmpty()) {
        r.raw = finished.first().at(0).toByteArray();
        r.bundle = QJsonDocument::fromJson(r.raw).object();
        r.warnings = finished.first().at(1).toStringList();
    }
    if (!failed.isEmpty()) {
        r.failedCode = failed.first().at(0).toString();
        r.failedMessage = failed.first().at(1).toString();
    }
    return r;
}

QJsonObject listNamed(const QJsonObject &bundle, const QString &name)
{
    for (const QJsonValue &v : bundle.value(u"lists"_s).toArray()) {
        if (v.toObject().value(u"displayName"_s).toString() == name)
            return v.toObject();
    }
    return {};
}

QJsonObject taskTitled(const QJsonObject &list, const QString &title)
{
    for (const QJsonValue &v : list.value(u"tasks"_s).toArray()) {
        if (v.toObject().value(u"title"_s).toString() == title)
            return v.toObject();
    }
    return {};
}

int requestsMatching(const FakeMicrosoft &ms, const QString &part)
{
    int n = 0;
    for (const auto &r : ms.requests)
        n += QUrl::fromPercentEncoding(r.url.toEncoded()).contains(part);
    return n;
}

} // namespace

class TestMsTodoDownload : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void everything()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        TodoDownload d(&ms);
        QSignalSpy progress(&d, &TodoDownload::progress);
        QSignalSpy finished(&d, &TodoDownload::finished);
        d.start(ms.accessToken);
        QVERIFY(d.active());
        QVERIFY(finished.wait(10000));
        QVERIFY(!d.active());
        const QJsonObject bundle = QJsonDocument::fromJson(finished.first().at(0).toByteArray()).object();
        QCOMPARE(finished.first().at(1).toStringList(), QStringList());
        QCOMPARE(bundle.value(u"brulion"_s).toString(), u"mstodo-export"_s);
        QCOMPARE(bundle.value(u"version"_s).toInt(), 1);
        QCOMPARE(bundle.value(u"lists"_s).toArray().size(), 2); // two pages of lists
        const QJsonObject zadania = listNamed(bundle, u"Zadania"_s);
        QCOMPARE(zadania.value(u"wellknownListName"_s).toString(), u"defaultList"_s);
        QCOMPARE(zadania.value(u"tasks"_s).toArray().size(), 3); // two pages of tasks
        QVERIFY(!zadania.contains(u"tasks@odata.nextLink"_s));
        const QJsonObject t1 = taskTitled(zadania, u"Zadzwonić do pani Józefy"_s);
        QCOMPARE(t1.value(u"checklistItems"_s).toArray().size(), 2);
        QCOMPARE(t1.value(u"linkedResources"_s).toArray().size(), 1);
        const QJsonArray attachments = t1.value(u"attachments"_s).toArray();
        QCOMPARE(attachments.size(), 1);
        QCOMPARE(QByteArray::fromBase64(attachments.first().toObject().value(u"contentBytes"_s).toString().toLatin1()),
                 QByteArray("źdźbło"));
        QCOMPARE(listNamed(bundle, u"Zakupy ąę"_s).value(u"tasks"_s).toArray().size(), 1);

        // Progress: list by list, the tasks so far.
        QVERIFY(progress.size() >= 3);
        QCOMPARE(progress.last().at(0).toInt(), 2);
        QCOMPARE(progress.last().at(1).toInt(), 2);
        QCOMPARE(progress.last().at(2).toInt(), 4);

        // The requests: Graph only, the token as a Bearer header, the ids percent-encoded, steps
        // and links in one go, attachments of the task that has some.
        for (const auto &r : std::as_const(ms.requests)) {
            QCOMPARE(r.url.scheme(), u"https"_s);
            QCOMPARE(r.url.host(), u"graph.microsoft.com"_s);
            QCOMPARE(r.method, QByteArray("GET"));
            QCOMPARE(r.header("Authorization"), "Bearer " + ms.accessToken.toUtf8());
            QVERIFY(r.timeoutMs > 0);
        }
        QCOMPARE(ms.requests.first().url, QUrl(u"https://graph.microsoft.com/v1.0/me/todo/lists"_s));
        QVERIFY(ms.requests.at(2).url.toString(QUrl::FullyEncoded)
                        .startsWith(u"https://graph.microsoft.com/v1.0/me/todo/lists/AAMkADZhZG%3D%3D/tasks?"_s));
        QCOMPARE(requestsMatching(ms, u"$expand=checklistItems,linkedResources"_s), 3);
        QCOMPARE(requestsMatching(ms, u"/checklistItems"_s), 0);
        QCOMPARE(requestsMatching(ms, u"/attachments"_s), 2); // the list, then the file
        QCOMPARE(ms.graphRequests(), 7);
        // The bundle carries no token.
        QVERIFY(!finished.first().at(0).toByteArray().contains(ms.accessToken.toUtf8()));
    }

    void importsTheBundle()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        const Result r = download(ms);
        brulion::io::ImportOptions options;
        options.timeZone = QTimeZone("Europe/Warsaw");
        const auto *importer = brulion::io::detectImporter({{u"Microsoft To Do.json"_s, r.raw}});
        QVERIFY(importer);
        QCOMPARE(importer->id(), u"mstodo"_s);
        const brulion::io::ImportResult imported =
                brulion::io::runImport(*importer, {{u"Microsoft To Do.json"_s, r.raw}}, options);
        QVERIFY2(imported.warnings.isEmpty(), qPrintable(imported.warnings.join(u" | "_s)));
        QStringList paths;
        QString zadania;
        for (const auto &f : imported.files) {
            paths.append(f.path);
            if (f.path == u"Zadania.md")
                zadania = QString::fromUtf8(f.data);
        }
        paths.sort();
        QCOMPARE(paths, QStringList({u"Zadania.md"_s, u"Zakupy ąę.md"_s, u"assets/Microsoft To Do/notatka.txt"_s}));
        QCOMPARE(zadania, u"---\nsource: Microsoft To Do\n---\n"
                          u"- [ ] Zadzwonić do pani Józefy ⏫ ➕ 2026-10-20 📅 2026-10-27 ⏰ 2026-10-27 08:00\n"
                          u"  Numer w notesie — źródło: ąćęłńóśźż\n"
                          u"  - [x] Znaleźć numer\n  - [ ] Umówić się\n  - [Kontakt](https://example.com/kontakt)\n"
                          u"  - [[assets/Microsoft To Do/notatka.txt|notatka.txt]]\n"
                          u"- [/] Podlać kwiaty 📅 2026-10-31 🔁 every week on Saturday\n"
                          u"- [x] Oddać książkę ✅ 2026-10-25\n"_s);
    }

    void expandRefused()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.refuseExpand = true;
        const Result r = download(ms);
        QVERIFY2(r.failedCode.isEmpty(), qPrintable(r.failedMessage));
        const QJsonObject t1 = taskTitled(listNamed(r.bundle, u"Zadania"_s), u"Zadzwonić do pani Józefy"_s);
        QCOMPARE(t1.value(u"checklistItems"_s).toArray().size(), 2);
        QCOMPARE(t1.value(u"linkedResources"_s).toArray().size(), 1);
        // Asked once with $expand, then without for every list; steps and links task by task.
        QCOMPARE(requestsMatching(ms, u"$expand"_s), 1);
        QCOMPARE(requestsMatching(ms, u"/checklistItems"_s), 4);
        QCOMPARE(requestsMatching(ms, u"/linkedResources"_s), 4);
        QCOMPARE(taskTitled(listNamed(r.bundle, u"Zakupy ąę"_s), u"Chleb"_s).value(u"checklistItems"_s).toArray().size(),
                 0);
    }

    void throttled()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.throttle = 3;
        const Result r = download(ms);
        QVERIFY2(r.failedCode.isEmpty(), qPrintable(r.failedMessage));
        QCOMPARE(r.bundle.value(u"lists"_s).toArray().size(), 2);
        QCOMPARE(ms.graphRequests(), 7 + 3);
    }

    void failures_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("throttle");
        QTest::addColumn<bool>("wrongToken");
        QTest::addColumn<QString>("code");
        QTest::newRow("sign-in expired") << 0 << 0 << true << u"unauthorized"_s;
        QTest::newRow("no permission") << 403 << 0 << false << u"forbidden"_s;
        QTest::newRow("server error") << 500 << 0 << false << u"server"_s;
        QTest::newRow("no network") << -1 << 0 << false << u"network"_s;
        QTest::newRow("throttled for good") << 0 << 1000 << false << u"throttled"_s;
    }

    void failures()
    {
        QFETCH(int, status);
        QFETCH(int, throttle);
        QFETCH(bool, wrongToken);
        QFETCH(QString, code);
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.graphStatus = status;
        ms.throttle = throttle;
        const QString token = wrongToken ? u"EwBwA8l6BAAUexpiredTokenWithSecretBits0987654321"_s : ms.accessToken;
        const Result r = download(ms, token);
        QCOMPARE(r.failedCode, code);
        QVERIFY(!r.failedMessage.isEmpty());
        QVERIFY(!r.failedMessage.contains(token));
        QVERIFY(r.raw.isEmpty());
        QVERIFY(ms.graphRequests() <= 5); // retries are bounded
    }

    void attachmentsWithinBudget()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        brulion::mstodo::DownloadLimits limits;
        limits.maxAttachmentBytes = 5;
        const Result r = download(ms, {}, limits);
        const QJsonObject t1 = taskTitled(listNamed(r.bundle, u"Zadania"_s), u"Zadzwonić do pani Józefy"_s);
        QCOMPARE(t1.value(u"attachments"_s).toArray().size(), 0);
        QCOMPARE(requestsMatching(ms, u"/attachments/att-1"_s), 0); // never downloaded
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().contains(u"25 MB"_s));

        FakeMicrosoft ms2;
        ms2.addSampleData();
        limits = {};
        limits.maxTotalAttachmentBytes = 4;
        const Result r2 = download(ms2, {}, limits);
        QCOMPARE(taskTitled(listNamed(r2.bundle, u"Zadania"_s), u"Zadzwonić do pani Józefy"_s)
                         .value(u"attachments"_s)
                         .toArray()
                         .size(),
                 0);
        QCOMPARE(r2.warnings.size(), 1);
        QVERIFY(r2.warnings.first().contains(u"too large"_s));
    }

    void attachmentContentInTheList()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.attachmentListHasContent = true;
        const Result r = download(ms);
        QCOMPARE(taskTitled(listNamed(r.bundle, u"Zadania"_s), u"Zadzwonić do pani Józefy"_s)
                         .value(u"attachments"_s)
                         .toArray()
                         .size(),
                 1);
        QCOMPARE(requestsMatching(ms, u"/attachments/att-1"_s), 0);
    }

    void pagingToAnotherHost()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.foreignNextLinkList = u"AAMkADZhZG=="_s;
        const Result r = download(ms);
        for (const auto &req : std::as_const(ms.requests))
            QCOMPARE(req.url.host(), u"graph.microsoft.com"_s); // the token never leaves Graph
        const QJsonObject zadania = listNamed(r.bundle, u"Zadania"_s);
        QCOMPARE(zadania.value(u"tasks"_s).toArray().size(), 2);
        QVERIFY(zadania.contains(u"tasks@odata.nextLink"_s)); // the importer says it is incomplete
    }

    void taskLimit()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        brulion::mstodo::DownloadLimits limits;
        limits.maxTasks = 2;
        const Result r = download(ms, {}, limits);
        QCOMPARE(listNamed(r.bundle, u"Zadania"_s).value(u"tasks"_s).toArray().size(), 2);
        QVERIFY(listNamed(r.bundle, u"Zakupy ąę"_s).contains(u"tasks@odata.nextLink"_s));
    }

    void noLists()
    {
        FakeMicrosoft ms;
        const Result r = download(ms);
        QVERIFY(r.failedCode.isEmpty());
        QCOMPARE(r.bundle.value(u"lists"_s).toArray().size(), 0);
    }

    void cancel()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        TodoDownload d(&ms);
        QSignalSpy finished(&d, &TodoDownload::finished);
        QSignalSpy failed(&d, &TodoDownload::failed);
        // Cancelled once the lists are there.
        connect(&d, &TodoDownload::progress, &d, [&d] { d.cancel(); });
        d.start(ms.accessToken);
        QTRY_VERIFY(!d.active());
        const auto sent = ms.requests.size();
        QTest::qWait(50);
        QCOMPARE(ms.requests.size(), sent);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(failed.size(), 0);
    }

    void destroyedWhileRunning()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        {
            TodoDownload d(&ms);
            d.start(ms.accessToken);
        }
        QTest::qWait(30); // the pending answer finds no download and does nothing
        QCOMPARE(ms.requests.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMsTodoDownload)
#include "tst_mstodo_download.moc"
