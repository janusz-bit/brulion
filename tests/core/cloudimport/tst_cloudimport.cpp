// The cloudimport module's downloads against fake servers (no network): OneNote through Microsoft
// Graph (notebooks, nested section groups, paged pages with levels, page HTML, resources), Todoist's
// Sync API (with comment files), Airtable's REST API (bases, tables, paged records, attachments)
// and the files of CSV cells — each result fed to the io module's importer. Also: the token only
// to the service's hosts, other hosts refused, 429 with Retry-After retried, failures explained,
// size budgets, cancel. Timing: the fakes answer from the event loop; waits are for signals.
#include "cloudimport/cloudimport.h"
#include "io/csvimport.h"
#include "io/importer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

struct Answer {
    int status = 200;
    QByteArray body;
    QList<std::pair<QByteArray, QByteArray>> headers;
};

/// Answers requests by URL (path + query without the host's scheme), asynchronously.
class FakeServer : public sync::HttpTransport {
public:
    QHash<QString, QList<Answer>> answers; ///< "host/path?query" -> answers in order (the last repeats)
    QList<sync::HttpRequest> requests;

    void json(const QString &key, const QJsonObject &o, int status = 200)
    {
        answers[key].append(Answer{status, QJsonDocument(o).toJson(QJsonDocument::Compact), {}});
    }
    void bytes(const QString &key, const QByteArray &data, const QByteArray &type = "application/octet-stream")
    {
        answers[key].append(Answer{200, data, {{"content-type", type}}});
    }

    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        requests.append(request);
        const QString key = request.url.host() + request.url.path(QUrl::FullyEncoded)
                            + (request.url.hasQuery() ? u'?' + request.url.query(QUrl::FullyEncoded) : QString());
        sync::HttpResponse response;
        auto it = answers.find(key);
        if (it == answers.end() || it->isEmpty()) {
            response.status = 404;
            response.body = "{\"error\":{\"code\":\"itemNotFound\"}}";
        } else {
            const Answer a = it->size() > 1 ? it->takeFirst() : it->first();
            response.status = a.status;
            response.body = a.body;
            for (const auto &[name, value] : a.headers)
                response.headers.insert(name.toLower(), value);
        }
        QTimer::singleShot(0, [done, response] { done(response); });
    }

    QByteArray authorization(qsizetype i) const
    {
        for (const auto &[name, value] : requests.at(i).headers) {
            if (name.compare("Authorization", Qt::CaseInsensitive) == 0)
                return value;
        }
        return {};
    }
};

/// Run a download to its end; the files (empty on failure: `failure` gets the code).
QList<io::FileEntry> wait(cloudimport::Download &d, QString *failure = nullptr, QStringList *warnings = nullptr)
{
    QSignalSpy finished(&d, &cloudimport::Download::finished);
    QSignalSpy failed(&d, &cloudimport::Download::failed);
    for (int i = 0; i < 500 && finished.isEmpty() && failed.isEmpty(); ++i)
        QTest::qWait(10);
    if (!failed.isEmpty()) {
        if (failure)
            *failure = failed.first().at(0).toString() + u": "_s + failed.first().at(1).toString();
        return {};
    }
    if (finished.isEmpty())
        return {};
    if (warnings)
        *warnings = finished.first().at(1).toStringList();
    return finished.first().at(0).value<QList<io::FileEntry>>();
}

io::ImportResult importAs(const QString &id, const QList<io::FileEntry> &files, const QString &folder)
{
    io::ImportOptions o;
    o.targetFolder = folder;
    o.timeZone = QTimeZone("Europe/Warsaw");
    return io::runImport(*io::importerById(id), files, o);
}

QString textAt(const io::ImportResult &r, const QString &path)
{
    for (const auto &f : r.files) {
        if (f.path == path)
            return QString::fromUtf8(f.data);
    }
    QStringList paths;
    for (const auto &f : r.files)
        paths.append(f.path);
    return u"<missing " + path + u"> in "_s + paths.join(u", "_s);
}

const QString Graph = u"graph.microsoft.com/v1.0"_s;

void addOneNote(FakeServer &s)
{
    s.json(Graph + u"/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"nb1"_s}, {u"displayName"_s, u"Praca"_s}},
                                               QJsonObject{{u"id"_s, u"nb2"_s}, {u"displayName"_s, u"Dom"_s}}}}});
    s.json(Graph + u"/me/onenote/notebooks/nb1/sections?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"s1"_s}, {u"displayName"_s, u"Spotkania"_s}}}}});
    s.json(Graph + u"/me/onenote/notebooks/nb1/sectionGroups?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"g1"_s}, {u"displayName"_s, u"Projekty"_s}}}}});
    s.json(Graph + u"/me/onenote/sectionGroups/g1/sections?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"s2"_s}, {u"displayName"_s, u"Żagle"_s}}}}});
    s.json(Graph + u"/me/onenote/sectionGroups/g1/sectionGroups?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{}}});
    s.json(Graph + u"/me/onenote/notebooks/nb2/sections?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{}}});
    s.json(Graph + u"/me/onenote/notebooks/nb2/sectionGroups?%24select=id,displayName"_s,
           QJsonObject{{u"value"_s, QJsonArray{}}});
    const QString pages =
            u"/pages?%24select=id,title,createdDateTime,lastModifiedDateTime,level,order,links&pagelevel=true&%24top=100"_s;
    const auto page = [](const QString &id, const QString &title, int level, int order) {
        return QJsonObject{
                {u"id"_s, id},
                {u"title"_s, title},
                {u"level"_s, level},
                {u"order"_s, order},
                {u"createdDateTime"_s, u"2026-11-02T08:15:00Z"_s},
                {u"lastModifiedDateTime"_s, u"2026-11-03T08:15:00Z"_s},
                {u"links"_s,
                 QJsonObject{{u"oneNoteClientUrl"_s,
                              QJsonObject{{u"href"_s, QString(u"onenote:https://x/Spotkania.one#"_s + title
                                                              + u"&section-id={a}&page-id={"_s + id
                                                              + u"0000-1111-2222-3333-444444444444}&end"_s)}}}}}};
    };
    // Two pages of results (paging).
    s.json(Graph + u"/me/onenote/sections/s1"_s + pages,
           QJsonObject{
                   {u"value"_s, QJsonArray{page(u"aaaa"_s, u"Zarząd"_s, 0, 0)}},
                   {u"@odata.nextLink"_s, u"https://graph.microsoft.com/v1.0/me/onenote/sections/s1/pages?skip=1"_s}});
    s.json(Graph + u"/me/onenote/sections/s1/pages?skip=1"_s,
           QJsonObject{{u"value"_s, QJsonArray{page(u"bbbb"_s, u"Notatki ąę"_s, 1, 1)}}});
    s.json(Graph + u"/me/onenote/sections/s2"_s + pages,
           QJsonObject{{u"value"_s, QJsonArray{page(u"cccc"_s, u"Regaty"_s, 0, 0)}}});
    const QString res = u"https://graph.microsoft.com/v1.0/users('u')/onenote/resources/"_s;
    s.bytes(Graph + u"/me/onenote/pages/aaaa/content"_s,
            (u"<html><body><div><p data-tag=\"to-do\">Budżet</p><p>Zob. <a "
             u"href=\"onenote:https://x/Spotkania.one#Notatki&amp;"
             "page-id={bbbb0000-1111-2222-3333-444444444444}&amp;end\">notatki</a></p>"
             "<img src=\""_s
             + res + u"r1/$value\" data-src-type=\"image/png\" data-fullres-src=\""_s + res
             + u"r2/$value\" data-fullres-src-type=\"image/png\" alt=\"OCR\" />"
               "<object data-attachment=\"Umowa.pdf\" type=\"application/pdf\" data=\""_s
             + res
             + u"r3/$value\" />"
               "<img src=\"https://evil.example.com/x.png\" /></div></body></html>"_s)
                    .toUtf8(),
            "text/html");
    s.bytes(Graph + u"/me/onenote/pages/bbbb/content"_s, "<html><body><p>Ważne</p></body></html>", "text/html");
    s.bytes(Graph + u"/me/onenote/pages/cccc/content"_s, "<html><body><p>Start o 10</p></body></html>", "text/html");
    s.bytes(u"graph.microsoft.com/v1.0/users('u')/onenote/resources/r2/$value"_s, "PNG-FULL", "image/png");
    s.bytes(u"graph.microsoft.com/v1.0/users('u')/onenote/resources/r3/$value"_s, "%PDF", "application/pdf");
}

} // namespace

class TestCloudImport : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { qRegisterMetaType<QList<io::FileEntry>>(); }

    void oneNote()
    {
        FakeServer s;
        addOneNote(s);
        cloudimport::OneNoteDownload d(&s);
        d.client()->setTimeUnitMs(1);
        QSignalSpy listed(&d, &cloudimport::OneNoteDownload::notebooksListed);
        d.listNotebooks(u"tok-123"_s);
        QVERIFY(listed.wait(2000));
        QCOMPARE(listed.first().at(0).value<QList<cloudimport::OneNoteNotebook>>().size(), 2);

        d.start(u"tok-123"_s, {u"nb1"_s});
        QString failure;
        const QList<io::FileEntry> files = wait(d, &failure);
        QVERIFY2(!files.isEmpty(), qPrintable(failure));
        // Only Graph was asked, always with the token; the other host's picture was not fetched.
        for (qsizetype i = 0; i < s.requests.size(); ++i) {
            QCOMPARE(s.requests[i].url.host(), u"graph.microsoft.com"_s);
            QCOMPARE(s.authorization(i), QByteArray("Bearer tok-123"));
        }
        // The small picture is not downloaded when there is a full-resolution one.
        QVERIFY(std::ranges::none_of(s.requests,
                                     [](const sync::HttpRequest &r) { return r.url.path().contains(u"/r1/"_s); }));
        const io::ImportResult r = importAs(u"onenote"_s, files, u"Import/OneNote"_s);
        const QString board = textAt(r, u"Import/OneNote/Praca/Spotkania/Zarząd.md"_s);
        QVERIFY2(board.contains(u"- [ ] Budżet"_s), qPrintable(board));
        QVERIFY2(board.contains(u"[[Import/OneNote/Praca/Spotkania/Zarząd/Notatki ąę|notatki]]"_s), qPrintable(board));
        QVERIFY(board.contains(u"![[assets/OneNote/Zarząd.png]]"_s));
        QVERIFY(board.contains(u"[[assets/OneNote/Umowa.pdf]]"_s));
        QVERIFY(board.contains(u"![](https://evil.example.com/x.png)"_s));
        QVERIFY(textAt(r, u"Import/OneNote/Praca/Spotkania/Zarząd/Notatki ąę.md"_s).contains(u"Ważne"_s));
        QVERIFY(textAt(r, u"Import/OneNote/Praca/Projekty/Żagle/Regaty.md"_s).contains(u"Start o 10"_s));
    }

    void oneNoteFailures_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::newRow("expired sign-in") << 401 << u"unauthorized"_s;
        QTest::newRow("no permission") << 403 << u"forbidden"_s;
        QTest::newRow("server") << 500 << u"server"_s;
        QTest::newRow("throttled after retries") << 429 << u"throttled"_s;
    }
    void oneNoteFailures()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        FakeServer s;
        s.json(Graph + u"/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s,
               QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, u"x"_s}}}}, status);
        cloudimport::OneNoteDownload d(&s);
        d.client()->setTimeUnitMs(1);
        d.start(u"secret-token"_s);
        QString failure;
        QVERIFY(wait(d, &failure).isEmpty());
        QVERIFY2(failure.startsWith(code + u':'), qPrintable(failure));
        QVERIFY(!failure.contains(u"secret-token"_s));
    }

    void retryAfter()
    {
        FakeServer s;
        addOneNote(s);
        const QString key = Graph + u"/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s;
        s.answers[key].prepend(Answer{429, "{}", {{"retry-after", "2"}}});
        cloudimport::OneNoteDownload d(&s);
        d.client()->setTimeUnitMs(1);
        d.start(u"t"_s, {u"nb2"_s});
        QString failure;
        // nb2 ("Dom") has no sections: the download ends with only the manifest.
        const QList<io::FileEntry> files = wait(d, &failure);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().path, u"OneNote/onenote.json"_s);
        QCOMPARE(std::ranges::count_if(
                         s.requests, [](const sync::HttpRequest &r) { return r.url.path().endsWith(u"/notebooks"_s); }),
                 2);
    }

    void cancel()
    {
        FakeServer s;
        addOneNote(s);
        cloudimport::OneNoteDownload d(&s);
        QSignalSpy finished(&d, &cloudimport::Download::finished);
        QSignalSpy failed(&d, &cloudimport::Download::failed);
        d.start(u"t"_s);
        d.cancel();
        QTest::qWait(50);
        QVERIFY(finished.isEmpty() && failed.isEmpty());
    }

    void todoist()
    {
        FakeServer s;
        const QJsonObject sync{
                {u"sync_token"_s, u"tok"_s},
                {u"full_sync"_s, true},
                {u"user"_s, QJsonObject{{u"email"_s, u"jan@example.com"_s}}},
                {u"projects"_s, QJsonArray{QJsonObject{{u"id"_s, u"p1"_s}, {u"name"_s, u"Zakupy"_s}}}},
                {u"items"_s, QJsonArray{QJsonObject{{u"id"_s, u"i1"_s},
                                                    {u"project_id"_s, u"p1"_s},
                                                    {u"content"_s, u"Mleko"_s},
                                                    {u"priority"_s, 3}}}},
                {u"notes"_s,
                 QJsonArray{QJsonObject{{u"id"_s, u"n1"_s},
                                        {u"item_id"_s, u"i1"_s},
                                        {u"content"_s, u"lista"_s},
                                        {u"file_attachment"_s,
                                         QJsonObject{{u"file_name"_s, u"lista.txt"_s},
                                                     {u"file_url"_s, u"https://files.todoist.com/a/lista.txt"_s}}}},
                            QJsonObject{{u"id"_s, u"n2"_s},
                                        {u"item_id"_s, u"i1"_s},
                                        {u"content"_s, u"obce"_s},
                                        {u"file_attachment"_s,
                                         QJsonObject{{u"file_name"_s, u"x.png"_s},
                                                     {u"file_url"_s, u"https://dropbox.example.com/x.png"_s}}}}}}};
        s.json(u"api.todoist.com/api/v1/sync"_s, sync);
        s.bytes(u"files.todoist.com/a/lista.txt"_s, "mleko\nchleb");
        cloudimport::TodoistDownload d(&s);
        d.client()->setTimeUnitMs(1);
        d.start(u" 0123abc "_s);
        QString failure;
        QStringList warnings;
        const QList<io::FileEntry> files = wait(d, &failure, &warnings);
        QVERIFY2(!files.isEmpty(), qPrintable(failure));
        QCOMPARE(s.requests.first().method, QByteArray("POST"));
        const QUrlQuery form(QString::fromUtf8(s.requests.first().body));
        QCOMPARE(form.queryItemValue(u"sync_token"_s), u"*"_s);
        QCOMPARE(s.authorization(0), QByteArray("Bearer 0123abc"));
        QVERIFY(std::ranges::none_of(s.requests,
                                     [](const sync::HttpRequest &r) { return r.url.host().contains(u"dropbox"_s); }));
        QVERIFY(!files.first().data.contains("jan@example.com")); // the account's details are left out
        QVERIFY(warnings.join(u'\n').contains(u"outside Todoist"_s));
        const io::ImportResult r = importAs(u"todoist"_s, files, u"Import/Todoist"_s);
        const QString page = textAt(r, u"Import/Todoist/Zakupy.md"_s);
        QVERIFY2(
                page.contains(
                        u"- [ ] Mleko ⏫\n  - lista [[assets/Todoist/lista.txt|lista.txt]]\n  - obce [x.png](https://dropbox.example.com/x.png)"_s),
                qPrintable(page));
    }

    void todoistBadToken()
    {
        FakeServer s;
        s.answers[u"api.todoist.com/api/v1/sync"_s].append(Answer{401, "Unauthorized", {}});
        cloudimport::TodoistDownload d(&s);
        d.start(u"bad"_s);
        QString failure;
        QVERIFY(wait(d, &failure).isEmpty());
        QVERIFY2(failure.startsWith(u"unauthorized:"_s), qPrintable(failure));
    }

    void airtable()
    {
        FakeServer s;
        s.json(u"api.airtable.com/v0/meta/bases"_s,
               QJsonObject{{u"bases"_s, QJsonArray{QJsonObject{{u"id"_s, u"appA"_s}, {u"name"_s, u"Biuro"_s}}}},
                           {u"offset"_s, u"o1"_s}});
        s.json(u"api.airtable.com/v0/meta/bases?offset=o1"_s,
               QJsonObject{{u"bases"_s, QJsonArray{QJsonObject{{u"id"_s, u"appB"_s}, {u"name"_s, u"Dom"_s}}}}});
        const QJsonObject tables{
                {u"tables"_s, QJsonArray{QJsonObject{
                                      {u"id"_s, u"tblK"_s},
                                      {u"name"_s, u"Kontakty"_s},
                                      {u"primaryFieldId"_s, u"f1"_s},
                                      {u"fields"_s, QJsonArray{QJsonObject{{u"id"_s, u"f1"_s},
                                                                           {u"name"_s, u"Nazwisko"_s},
                                                                           {u"type"_s, u"singleLineText"_s}},
                                                               QJsonObject{{u"id"_s, u"f2"_s},
                                                                           {u"name"_s, u"Zdjęcie"_s},
                                                                           {u"type"_s, u"multipleAttachments"_s}}}}}}}};
        s.json(u"api.airtable.com/v0/meta/bases/appA/tables"_s, tables);
        const auto record = [](const QString &id, const QString &name, const QJsonArray &photos) {
            return QJsonObject{{u"id"_s, id},
                               {u"fields"_s, QJsonObject{{u"Nazwisko"_s, name}, {u"Zdjęcie"_s, photos}}}};
        };
        s.json(u"api.airtable.com/v0/appA/tblK?pageSize=100"_s,
               QJsonObject{{u"records"_s,
                            QJsonArray{record(
                                    u"r1"_s, u"Kowalska"_s,
                                    QJsonArray{QJsonObject{{u"url"_s, u"https://v5.airtableusercontent.com/a/b/c"_s},
                                                           {u"filename"_s, u"portret.jpg"_s}}})}},
                           {u"offset"_s, u"itr2"_s}});
        s.json(u"api.airtable.com/v0/appA/tblK?pageSize=100&offset=itr2"_s,
               QJsonObject{{u"records"_s, QJsonArray{record(u"r2"_s, u"Nowak"_s, {})}}});
        s.bytes(u"v5.airtableusercontent.com/a/b/c"_s, "JPEG");
        cloudimport::AirtableDownload d(&s);
        d.client()->setTimeUnitMs(1);
        QSignalSpy bases(&d, &cloudimport::AirtableDownload::basesListed);
        d.listBases(u"patX.secret"_s);
        QVERIFY(bases.wait(2000));
        QCOMPARE(bases.first().at(0).value<QList<cloudimport::AirtableBase>>().size(), 2);
        QSignalSpy listed(&d, &cloudimport::AirtableDownload::tablesListed);
        d.listTables(u"patX.secret"_s, u"appA"_s);
        QVERIFY(listed.wait(2000));
        s.requests.clear();
        d.start(u"patX.secret"_s, u"appA"_s, u"Biuro"_s);
        QString failure;
        const QList<io::FileEntry> files = wait(d, &failure);
        QVERIFY2(!files.isEmpty(), qPrintable(failure));
        // The token goes to the API, never to the attachments' host.
        for (qsizetype i = 0; i < s.requests.size(); ++i)
            QCOMPARE(s.authorization(i).isEmpty(), s.requests[i].url.host() != u"api.airtable.com"_s);
        const io::ImportResult r = importAs(u"airtable"_s, files, u"Import/Airtable"_s);
        QVERIFY2(textAt(r, u"Import/Airtable/Kowalska.md"_s).contains(u"![[assets/Airtable/portret.jpg]]"_s),
                 qPrintable(textAt(r, u"Import/Airtable/Kowalska.md"_s)));
        QVERIFY(textAt(r, u"Import/Airtable/Nowak.md"_s).contains(u"source: Airtable"_s));
    }

    void urlFiles()
    {
        FakeServer s;
        s.bytes(u"dl.example.com/a.png"_s, "PNG");
        s.bytes(u"dl.example.com/big.bin"_s, QByteArray(2048, 'x'));
        cloudimport::UrlFilesDownload d(&s);
        cloudimport::FileBudget budget;
        budget.maxFileBytes = 1024;
        d.setBudget(budget);
        d.start({{u"https://dl.example.com/a.png"_s, u"a.png"_s},
                 {u"http://dl.example.com/a.png"_s, u"plain.png"_s},
                 {u"https://dl.example.com/big.bin"_s, u"big.bin"_s},
                 {u"https://dl.example.com/missing.png"_s, u"missing.png"_s}});
        QStringList warnings;
        const QList<io::FileEntry> files = wait(d, nullptr, &warnings);
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().path, io::csvAttachmentInputPath(u"https://dl.example.com/a.png"_s, u"a.png"_s));
        QCOMPARE(warnings.size(), 3);
        for (qsizetype i = 0; i < s.requests.size(); ++i)
            QVERIFY(s.authorization(i).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCloudImport)
#include "tst_cloudimport.moc"
