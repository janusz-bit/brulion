// The new import sources in the app: the CSV mapping of ImportController (analysis, a column's type
// and name, a title template, one table page, back to what was detected, written into a real
// notebook), TokenImport for Todoist and Airtable (token → download → preview → import; the
// token in no file), MsTodoImport's OneNote service (sign-in with Notes.Read → notebooks to choose
// → download) against FakeMicrosoft, and ImportAttachments (the files of attachment cells join the
// input and are embedded). No network: fake servers answer from the event loop.
#include "appsettings.h"
#include "importcontroller.h"
#include "mstodoimport.h"
#include "tokenimport.h"
#include "workspace.h"

#include "fakemicrosoft.h"
#include "io/csvimport.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using brulion::ioviews::ImportAttachments;
using brulion::ioviews::ImportController;
using brulion::ioviews::MsTodoImport;
using brulion::ioviews::TokenImport;
using brulion::shell::AppSettings;
namespace io = brulion::io;
// No `namespace sync = brulion::sync`: POSIX declares a function ::sync() (unistd.h, which the
// precompiled headers bring in on some systems), and the two names clash.

namespace {

template<typename Object>
bool waitState(const Object &o, const QString &state, int ms = 10000)
{
    QElapsedTimer t;
    t.start();
    while (o.state() != state && t.elapsed() < ms * BRULION_TEST_SLOWDOWN)
        QTest::qWait(5);
    if (o.state() != state)
        qWarning() << "state" << o.state();
    return o.state() == state;
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : u"<missing "_s + path + u'>';
}

QByteArray readAll(const QString &dir)
{
    QByteArray out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.open(QIODevice::ReadOnly))
            out += f.readAll();
    }
    return out;
}

/// Answers by host + path + query from memory, asynchronously.
class FakeServer : public brulion::sync::HttpTransport {
public:
    QHash<QString, QByteArray> answers;
    QHash<QString, int> statuses;
    QList<brulion::sync::HttpRequest> requests;

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override
    {
        requests.append(request);
        const QString key = request.url.host() + request.url.path(QUrl::FullyEncoded)
                            + (request.url.hasQuery() ? u'?' + request.url.query(QUrl::FullyEncoded) : QString());
        brulion::sync::HttpResponse r;
        r.status = statuses.value(key, answers.contains(key) ? 200 : 404);
        r.body = answers.value(key);
        QTimer::singleShot(0, [done, r] { done(r); });
    }
};

const QString Books = u"Tytuł;Autor;Rok;Okładka\n"
                      "Pan Tadeusz;Adam Mickiewicz;1834;okladka.jpg (https://dl.example.com/a/okladka.jpg)\n"
                      "Lalka;Bolesław Prus;1890;\n"_s;

} // namespace

class TestAppIoCloudImport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open(QString *rootOut)
    {
        const QString root = m_tmp.filePath(u"notatnik-%1"_s.arg(++m_n));
        QDir().mkpath(root);
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        *rootOut = root;
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void csvMapping()
    {
        QString root;
        auto ws = open(&root);
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput({io::InputFile{u"Książki.csv"_s, Books.toUtf8()}}, u"Książki.csv"_s);
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.effectiveId(), u"csv"_s);
        QVariantMap m = c.csvMapping();
        QVERIFY(m.value(u"available"_s).toBool());
        QCOMPARE(m.value(u"attachments"_s).toInt(), 1);
        QVariantMap file = m.value(u"file"_s).toMap();
        QCOMPARE(file.value(u"titleColumn"_s).toString(), u"Tytuł"_s);
        QCOMPARE(file.value(u"separator"_s).toString(), u";"_s);
        const QVariantList columns = file.value(u"columns"_s).toList();
        QCOMPARE(columns.size(), 4);
        QCOMPARE(columns[2].toMap().value(u"typeText"_s).toString(), u"Number"_s);
        QVERIFY(columns[0].toMap().value(u"isTitle"_s).toBool());
        QCOMPARE(c.csvTitleChoices().size(), 5);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 2);

        // A column as text under another name, a title template.
        c.setCsvColumn(u"Rok"_s, u"Rok wydania"_s, u"text"_s);
        c.setCsvOption(u"titleTemplate"_s, u"{{Autor}} – {{Tytuł}}"_s);
        QVERIFY(c.csvMapping().value(u"customized"_s).toBool());
        QTest::qWait(10);
        QVERIFY(waitState(c, u"ready"_s));
        QVERIFY(waitState(c, u"ready"_s));
        QElapsedTimer t;
        t.start();
        while (!c.csvMapping().value(u"file"_s).toMap().value(u"samplePage"_s).toString().contains(u"Rok wydania"_s)
               && t.elapsed() < 5000 * BRULION_TEST_SLOWDOWN)
            QTest::qWait(10);
        file = c.csvMapping().value(u"file"_s).toMap();
        QVERIFY2(file.value(u"samplePage"_s).toString().contains(u"Rok wydania: \"1834\""_s),
                 qPrintable(file.value(u"samplePage"_s).toString()));
        QCOMPARE(file.value(u"titles"_s).toStringList().value(0), u"Adam Mickiewicz – Pan Tadeusz"_s);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        const QString page = readText(root + u"/Import/CSV/Adam Mickiewicz – Pan Tadeusz.md"_s);
        QVERIFY2(page.contains(u"Rok wydania: \"1834\""_s), qPrintable(page));
        QVERIFY(page.contains(u"https://dl.example.com/a/okladka.jpg"_s));
        QVERIFY(QFile::exists(root + u"/Import/CSV/Książki.base"_s));

        // New input forgets the mapping.
        c.setInput({io::InputFile{u"x.csv"_s, "a,b\n1,2\n"}}, u"x.csv"_s);
        QVERIFY(c.csvOptions().isEmpty());
    }

    void csvTablePage()
    {
        QString root;
        auto ws = open(&root);
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput({io::InputFile{u"Książki.csv"_s, Books.toUtf8()}}, u"Książki.csv"_s);
        QVERIFY(waitState(c, u"ready"_s));
        c.setCsvOption(u"mode"_s, u"table"_s);
        QElapsedTimer t;
        t.start();
        while (c.preview().value(u"pages"_s).toInt() != 1 && t.elapsed() < 5000 * BRULION_TEST_SLOWDOWN)
            QTest::qWait(10);
        QCOMPARE(c.preview().value(u"pages"_s).toInt(), 1);
        c.resetCsvMapping();
        QVERIFY(c.csvOptions().isEmpty());
    }

    void attachmentsDownloaded()
    {
        QString root;
        auto ws = open(&root);
        ImportController c;
        c.setWorkspace(ws.get());
        c.setInput({io::InputFile{u"Książki.csv"_s, Books.toUtf8()}}, u"Książki.csv"_s);
        QVERIFY(waitState(c, u"ready"_s));
        FakeServer s;
        s.answers.insert(u"dl.example.com/a/okladka.jpg"_s, "JPEG");
        ImportAttachments a;
        a.setTransport(&s);
        a.setImporter(&c);
        QSignalSpy downloaded(&a, &ImportAttachments::downloaded);
        a.download();
        QVERIFY(downloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QCOMPARE(downloaded.first().at(0).toInt(), 1);
        QVERIFY(s.requests.first().headers.isEmpty()
                || !s.requests.first().headers.first().first.contains("Authorization"));
        QVERIFY(waitState(c, u"ready"_s));
        QElapsedTimer t;
        t.start();
        while (c.csvMapping().value(u"downloadedAttachments"_s).toInt() != 1
               && t.elapsed() < 5000 * BRULION_TEST_SLOWDOWN)
            QTest::qWait(10);
        QCOMPARE(c.csvMapping().value(u"downloadedAttachments"_s).toInt(), 1);
        QVERIFY(c.pendingAttachments().isEmpty());
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY2(readText(root + u"/Import/CSV/Pan Tadeusz.md"_s).contains(u"![[assets/CSV/okladka.jpg]]"_s),
                 qPrintable(readText(root + u"/Import/CSV/Pan Tadeusz.md"_s)));
        QVERIFY(QFile::exists(root + u"/assets/CSV/okladka.jpg"_s));
    }

    void todoist()
    {
        QString root;
        auto ws = open(&root);
        ImportController c;
        c.setWorkspace(ws.get());
        FakeServer s;
        const QJsonObject sync{
                {u"sync_token"_s, u"x"_s},
                {u"projects"_s, QJsonArray{QJsonObject{{u"id"_s, u"p"_s}, {u"name"_s, u"Dom"_s}}}},
                {u"items"_s, QJsonArray{QJsonObject{{u"id"_s, u"i"_s},
                                                    {u"project_id"_s, u"p"_s},
                                                    {u"content"_s, u"Wynieść śmieci"_s},
                                                    {u"due"_s, QJsonObject{{u"date"_s, u"2026-11-21"_s},
                                                                           {u"string"_s, u"every saturday"_s},
                                                                           {u"is_recurring"_s, true}}}}}}};
        s.answers.insert(u"api.todoist.com/api/v1/sync"_s, QJsonDocument(sync).toJson());
        TokenImport t;
        t.setTransport(&s);
        t.setTimeUnitMs(1);
        t.setImporter(&c);
        t.setService(u"todoist"_s);
        const QString token = u"0123456789abcdef0123456789abcdef01234567"_s;
        t.setToken(token);
        QSignalSpy downloaded(&t, &TokenImport::downloaded);
        t.connectService();
        QCOMPARE(t.state(), u"downloading"_s);
        QVERIFY(downloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QCOMPARE(t.token(), QString()); // forgotten after the download
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.effectiveId(), u"todoist"_s);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QCOMPARE(readText(root + u"/Import/Todoist/Dom.md"_s),
                 u"---\nsource: Todoist\n---\n- [ ] Wynieść śmieci 📅 2026-11-21 🔁 every week on Saturday\n"_s);
        QVERIFY(!readAll(root).contains(token.toUtf8()));
        QVERIFY(!readAll(root + u"-state"_s).contains(token.toUtf8()));
    }

    void airtable()
    {
        QString root;
        auto ws = open(&root);
        ImportController c;
        c.setWorkspace(ws.get());
        FakeServer s;
        s.answers.insert(u"api.airtable.com/v0/meta/bases"_s,
                         QJsonDocument(QJsonObject{{u"bases"_s, QJsonArray{QJsonObject{{u"id"_s, u"appA"_s},
                                                                                       {u"name"_s, u"Biuro"_s}}}}})
                                 .toJson());
        const QJsonObject table{
                {u"id"_s, u"tbl1"_s},
                {u"name"_s, u"Klienci"_s},
                {u"primaryFieldId"_s, u"f1"_s},
                {u"fields"_s,
                 QJsonArray{QJsonObject{{u"id"_s, u"f1"_s}, {u"name"_s, u"Firma"_s}, {u"type"_s, u"singleLineText"_s}},
                            QJsonObject{{u"id"_s, u"f2"_s}, {u"name"_s, u"Aktywny"_s}, {u"type"_s, u"checkbox"_s}}}}};
        s.answers.insert(
                u"api.airtable.com/v0/meta/bases/appA/tables"_s,
                QJsonDocument(QJsonObject{{u"tables"_s, QJsonArray{table, QJsonObject{{u"id"_s, u"tbl2"_s},
                                                                                      {u"name"_s, u"Inne"_s}}}}})
                        .toJson());
        s.answers.insert(
                u"api.airtable.com/v0/appA/tbl1?pageSize=100"_s,
                QJsonDocument(QJsonObject{{u"records"_s,
                                           QJsonArray{QJsonObject{
                                                   {u"id"_s, u"r1"_s},
                                                   {u"fields"_s, QJsonObject{{u"Firma"_s, u"Żabka sp. z o.o."_s},
                                                                             {u"Aktywny"_s, true}}}}}}})
                        .toJson());
        TokenImport t;
        t.setTransport(&s);
        t.setTimeUnitMs(1);
        t.setImporter(&c);
        t.setService(u"airtable"_s);
        t.setToken(u"patABC.secret"_s);
        t.connectService();
        QElapsedTimer timer;
        timer.start();
        while (t.tables().size() != 2 && timer.elapsed() < 5000 * BRULION_TEST_SLOWDOWN)
            QTest::qWait(5);
        QCOMPARE(t.state(), u"choosing"_s);
        QCOMPARE(t.baseId(), u"appA"_s); // the only base is chosen by itself
        QCOMPARE(t.checkedTables(), 2);
        t.setTableChecked(u"tbl2"_s, false);
        QCOMPARE(t.checkedTables(), 1);
        QSignalSpy downloaded(&t, &TokenImport::downloaded);
        t.download();
        QVERIFY(downloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QVERIFY(std::ranges::none_of(
                s.requests, [](const brulion::sync::HttpRequest &r) { return r.url.path().contains(u"tbl2"_s); }));
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.effectiveId(), u"airtable"_s);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QVERIFY2(readText(root + u"/Import/Airtable/Żabka sp. z o.o.md"_s).contains(u"Aktywny: true"_s),
                 qPrintable(readText(root + u"/Import/Airtable/Żabka sp. z o.o.md"_s)));
    }

    void tokenFailureShown()
    {
        ImportController c;
        FakeServer s;
        s.statuses.insert(u"api.todoist.com/api/v1/sync"_s, 401);
        TokenImport t;
        t.setTransport(&s);
        t.setImporter(&c);
        t.connectService(); // no token yet
        QCOMPARE(t.errorCode(), u"no-token"_s);
        t.setToken(u"zly"_s);
        t.connectService();
        QVERIFY(waitState(t, u"error"_s));
        QCOMPARE(t.errorCode(), u"unauthorized"_s);
        QVERIFY(!t.errorText().contains(u"zly"_s));
    }

    void oneNote()
    {
        QString root;
        auto ws = open(&root);
        AppSettings settings(m_tmp.filePath(u"settings.ini"_s), m_tmp.filePath(u"data"_s));
        mstodotest::FakeMicrosoft ms;
        ms.scope = u"Notes.Read"_s;
        const auto json = [](const QJsonObject &o) { return QJsonDocument(o).toJson(); };
        ms.otherGraph.insert(
                u"/v1.0/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s,
                json(QJsonObject{{u"value"_s,
                                  QJsonArray{QJsonObject{{u"id"_s, u"nb1"_s}, {u"displayName"_s, u"Studia"_s}},
                                             QJsonObject{{u"id"_s, u"nb2"_s}, {u"displayName"_s, u"Prywatne"_s}}}}}));
        ms.otherGraph.insert(u"/v1.0/me/onenote/notebooks/nb1/sections?%24select=id,displayName"_s,
                             json(QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"s1"_s},
                                                                                  {u"displayName"_s, u"Fizyka"_s}}}}}));
        ms.otherGraph.insert(u"/v1.0/me/onenote/notebooks/nb1/sectionGroups?%24select=id,displayName"_s,
                             json(QJsonObject{{u"value"_s, QJsonArray{}}}));
        ms.otherGraph.insert(
                u"/v1.0/me/onenote/sections/s1/pages?%24select=id,title,createdDateTime,lastModifiedDateTime,level,order,links&pagelevel=true&%24top=100"_s,
                json(QJsonObject{{u"value"_s, QJsonArray{QJsonObject{{u"id"_s, u"p1"_s},
                                                                     {u"title"_s, u"Wykład 1: kinematyka"_s},
                                                                     {u"level"_s, 0},
                                                                     {u"order"_s, 0}}}}}));
        ms.otherGraph.insert(u"/v1.0/me/onenote/pages/p1/content"_s,
                             "<html><body><p data-tag=\"to-do\">Powtórzyć wzory</p><p>v = s / t</p></body></html>");
        ImportController c;
        c.setWorkspace(ws.get());
        MsTodoImport m;
        m.setSettings(&settings);
        m.setTransport(&ms);
        m.setTimeUnitMs(1);
        m.setImporter(&c);
        m.setService(u"onenote"_s);
        m.setClientId(ms.clientId);
        m.signIn();
        QVERIFY(waitState(m, u"choosing"_s));
        QCOMPARE(m.notebooks().size(), 2);
        QCOMPARE(m.checkedNotebooks(), 2);
        m.setNotebookChecked(u"nb2"_s, false);
        QCOMPARE(m.checkedNotebooks(), 1);
        QSignalSpy downloaded(&m, &MsTodoImport::downloaded);
        m.downloadNotebooks();
        QVERIFY(downloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.effectiveId(), u"onenote"_s);
        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        const QString page = readText(root + u"/Import/OneNote/Studia/Fizyka/Wykład 1 kinematyka.md"_s);
        QVERIFY2(page.contains(u"- [ ] Powtórzyć wzory"_s), qPrintable(page));
        QVERIFY(!readAll(root).contains(ms.accessToken.toUtf8()));
        QVERIFY(!readAll(m_tmp.path()).contains(ms.accessToken.toUtf8()));
    }
};

QTEST_GUILESS_MAIN(TestAppIoCloudImport)
#include "tst_appio_cloudimport.moc"
